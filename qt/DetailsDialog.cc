// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#include "DetailsDialog.h"

#include <algorithm>
#include <bit>
#include <cassert>
#include <ctime>
#include <functional>
#include <initializer_list>
#include <map>
#include <optional>
#include <set>
#include <type_traits>
#include <utility>

#include <QtCore/QDateTime>
#include <QtCore/QItemSelectionModel>
#include <QtCore/QList>
#include <QtCore/QRegularExpression>
#include <QtCore/QSignalBlocker>
#include <QtCore/QString>
#include <QtCore/QStringList>

#include <QtGui/QDesktopServices>
#include <QtGui/QFont>
#include <QtGui/QFontMetrics>
#include <QtGui/QResizeEvent>

#include <QtWidgets/QHeaderView>
#include <QtWidgets/QInputDialog>
#include <QtWidgets/QLabel>
#include <QtWidgets/QMessageBox>
#include <QtWidgets/QStyle>
#include <QtWidgets/QTreeWidgetItem>

#include <QtNetwork/QHostAddress>

#include <libtransmission/announce-list.h>
#include <libtransmission/quark.h>
#include <libtransmission/types.h>
#include <libtransmission/utils.h> // tr_getRatio()

#include "BaseDialog.h"
#include "ColumnResizer.h"
#include "Formatter.h"
#include "NativeIcon.h"
#include "Prefs.h"
#include "Session.h"
#include "SqueezeLabel.h"
#include "Torrent.h"
#include "TorrentModel.h"
#include "TrackerDelegate.h"
#include "TrackerModel.h"
#include "TrackerModelFilter.h"
#include "TrFormat.h"
#include "ui_TrackersDialog.h"
#include "Utils.h"

// ---

namespace
{

class TrackersDialog : public BaseDialog
{
    Q_OBJECT

public:
    explicit TrackersDialog(QString const& tracker_list, QWidget* parent = nullptr)
        : BaseDialog{ parent }
    {
        ui_.setupUi(this);
        ui_.trackerList->setPlainText(tracker_list);
        connect(ui_.dialogButtons, &QDialogButtonBox::clicked, this, &TrackersDialog::onButtonBoxClicked);
    }

signals:
    void trackerListEdited(QString /*tracker_list*/);

private slots:
    void onButtonBoxClicked(QAbstractButton* button)
    {
        if (ui_.dialogButtons->standardButton(button) == QDialogButtonBox::Ok) {
            emit trackerListEdited(ui_.trackerList->toPlainText());
        }

        close();
    }

private:
    Ui::TrackersDialog ui_{};
};

[[nodiscard]] constexpr tr_quark priorityKey(int priority)
{
    switch (priority) {
    case TR_PRI_LOW:
        return TR_KEY_priority_low;

    case TR_PRI_HIGH:
        return TR_KEY_priority_high;

    default:
        return TR_KEY_priority_normal;
    }
}

int constexpr DebounceIntervalMSec = 100;
int constexpr RefreshIntervalMSec = 4000;

// NOLINTNEXTLINE(performance-enum-size)
enum // peer columns
{
    COL_LOCK,
    COL_UP,
    COL_UP_REQS,
    COL_DOWN,
    COL_DOWN_REQS,
    COL_PERCENT,
    COL_STATUS,
    COL_ADDRESS,
    COL_CLIENT,
    N_COLUMNS
};

int measureViewItem(QTreeWidget const* view, int column, QString const& text)
{
    auto const item_width = Utils::measureViewItem(view, text);
    auto const header_width = Utils::measureHeaderItem(view->header(), view->headerItem()->text(column));

    return std::max(item_width, header_width);
}

// The value that `getter` yields for every one of `torrents`,
// or nullopt when they disagree or there are no torrents.
template<typename Getter>
[[nodiscard]] std::optional<std::decay_t<std::invoke_result_t<Getter, Torrent const*>>> uniformValue(
    QList<Torrent const*> const& torrents,
    Getter const getter)
{
    if (!torrents.empty() && std::ranges::adjacent_find(torrents, std::ranges::not_equal_to{}, getter) == torrents.end()) {
        return std::invoke(getter, torrents.front());
    }
    return {};
}

// Updates a widget from the model unless the user is editing it, and
// without echoing the new value back out through the widget's signals.
void setIfIdle(QComboBox* const box, int const index)
{
    if (!box->hasFocus()) {
        auto const blocker = QSignalBlocker{ box };
        box->setCurrentIndex(index);
    }
}

template<typename SpinBox, typename Value>
void setIfIdle(SpinBox* const spin, Value const value)
{
    if (!spin->hasFocus()) {
        auto const blocker = QSignalBlocker{ spin };
        spin->setValue(value);
    }
}

[[nodiscard]] QString peerFlagDescription(QChar const flag)
{
    switch (flag.unicode()) {
    case 'O':
        return DetailsDialog::tr("Optimistic unchoke");

    case 'D':
        return DetailsDialog::tr("Downloading from this peer");

    case 'd':
        return DetailsDialog::tr("We would download from this peer if they would let us");

    case 'U':
        return DetailsDialog::tr("Uploading to peer");

    case 'u':
        return DetailsDialog::tr("This peer would download from us if we would let them");

    case 'K':
        return DetailsDialog::tr("Peer has unchoked us, but we're not interested");

    case '?':
        return DetailsDialog::tr("We unchoked this peer, but they're not interested");

    case 'E':
        return DetailsDialog::tr("Encrypted connection");

    case 'H':
        return DetailsDialog::tr("Peer was found through DHT");

    case 'X':
        return DetailsDialog::tr("Peer was found through Peer Exchange (PEX)");

    case 'I':
        return DetailsDialog::tr("Peer is an incoming connection");

    case 'T':
        return DetailsDialog::tr("Peer is connected over µTP");

    case 'h':
        return DetailsDialog::tr("Peer supports holepunch (BEP 55)");

    default:
        return {};
    }
}

// One line per known flag, e.g. "D: Downloading from this peer"
[[nodiscard]] QString peerFlagsTooltip(QString const& flags)
{
    auto lines = QStringList{};

    for (QChar const flag : flags) {
        if (auto const description = peerFlagDescription(flag); !description.isEmpty()) {
            lines << QStringLiteral("%1: %2").arg(flag).arg(description);
        }
    }

    return lines.join(QLatin1Char('\n'));
}

} // namespace

// ---

// A row in the peers table
class DetailsDialog::PeerItem : public QTreeWidgetItem
{
    Peer peer_;
    QString collated_address_;

public:
    PeerItem(Peer p, QIcon const& encrypted_icon)
        : peer_{ std::move(p) }
        , collated_address_{ collateAddress(peer_.address) }
    {
        for (auto const column : { COL_UP, COL_UP_REQS, COL_DOWN, COL_DOWN_REQS, COL_PERCENT }) {
            setTextAlignment(column, Qt::AlignRight | Qt::AlignVCenter);
        }

        setIcon(COL_LOCK, peer_.is_encrypted ? encrypted_icon : QIcon{});
        setToolTip(COL_LOCK, peer_.is_encrypted ? DetailsDialog::tr("Encrypted connection") : QString{});
        setText(COL_ADDRESS, peer_.address);
        setText(COL_CLIENT, peer_.client_name);
        setToolTip(COL_STATUS, peerFlagsTooltip(peer_.flags));
        refreshCells();
    }

    // `p` has this item's address: the address is part of the dialog's lookup key
    void refresh(Peer const& p)
    {
        if (p.flags != peer_.flags) {
            setToolTip(COL_STATUS, peerFlagsTooltip(p.flags));
        }

        peer_ = p;
        refreshCells();
    }

    bool operator<(QTreeWidgetItem const& other) const override
    {
        auto const* i = dynamic_cast<PeerItem const*>(&other);
        auto const* tw = treeWidget();
        int const column = tw != nullptr ? tw->sortColumn() : 0;

        assert(i != nullptr);

        switch (column) {
        case COL_UP:
            return peer_.rate_to_peer < i->peer_.rate_to_peer;

        case COL_UP_REQS:
            return peer_.active_reqs_to_client < i->peer_.active_reqs_to_client;

        case COL_DOWN:
            return peer_.rate_to_client < i->peer_.rate_to_client;

        case COL_DOWN_REQS:
            return peer_.active_reqs_to_peer < i->peer_.active_reqs_to_peer;

        case COL_PERCENT:
            return peer_.progress < i->peer_.progress;

        case COL_STATUS:
            return peer_.flags < i->peer_.flags;

        case COL_CLIENT:
            return peer_.client_name < i->peer_.client_name;

        case COL_LOCK:
            return peer_.is_encrypted && !i->peer_.is_encrypted;

        default:
            return collated_address_ < i->collated_address_;
        }
    }

private:
    void refreshCells()
    {
        setText(COL_UP, peer_.rate_to_peer.is_zero() ? QString{} : peer_.rate_to_peer.toQstring());
        setText(COL_UP_REQS, peer_.active_reqs_to_client > 0 ? QString::number(peer_.active_reqs_to_client) : QString{});
        setText(COL_DOWN, peer_.rate_to_client.is_zero() ? QString{} : peer_.rate_to_client.toQstring());
        setText(COL_DOWN_REQS, peer_.active_reqs_to_peer > 0 ? QString::number(peer_.active_reqs_to_peer) : QString{});
        setText(
            COL_PERCENT,
            peer_.progress > 0 ? QStringLiteral("%1%").arg(static_cast<int>(peer_.progress * 100.0)) : QString{});
        setText(COL_STATUS, peer_.flags);
    }

    [[nodiscard]] static QString collateAddress(QString const& address)
    {
        auto collated = QString{};

        if (auto ip_address = QHostAddress{}; ip_address.setAddress(address)) {
            if (ip_address.protocol() == QAbstractSocket::IPv4Protocol) {
                quint32 const ipv4_address = ip_address.toIPv4Address();
                collated = QStringLiteral("1-") +
                    QString::fromUtf8(QByteArray::number(ipv4_address, 16).rightJustified(8, '0'));
            } else if (ip_address.protocol() == QAbstractSocket::IPv6Protocol) {
                Q_IPV6ADDR const ipv6_address = ip_address.toIPv6Address();
                QByteArray tmp(16, '\0');

                for (int i = 0; i < 16; ++i) {
                    tmp[i] = std::bit_cast<char>(ipv6_address[i]);
                }

                collated = QStringLiteral("2-") + QString::fromUtf8(tmp.toHex());
            }
        }

        if (collated.isEmpty()) {
            collated = QStringLiteral("3-") + address.toLower();
        }

        return collated;
    }
};

// ---

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
int DetailsDialog::prev_tab_index = 0;

DetailsDialog::DetailsDialog(Session& session, Prefs& prefs, TorrentModel const& model, QWidget* parent)
    : BaseDialog{ parent }
    , session_{ session }
    , prefs_{ prefs }
    , model_{ model }
{
    ui_.setupUi(this);

    initInfoTab();
    initPeersTab();
    initTrackerTab();
    initFilesTab();
    initOptionsTab();

    ui_.tabs->setCurrentIndex(prev_tab_index);

    resize(prefs_.get<int>(TR_KEY_details_window_width), prefs_.get<int>(TR_KEY_details_window_height));

    for (tr_quark const key : { TR_KEY_show_tracker_scrapes, TR_KEY_show_backup_trackers, TR_KEY_show_extra_peer_details }) {
        refreshPref(key);
    }

    connect(&model_, &TorrentModel::torrentsChanged, this, &DetailsDialog::onTorrentsChanged);
    connect(&model_, &TorrentModel::torrentsEdited, this, &DetailsDialog::onTorrentsEdited);
    connect(&prefs_, qOverload<tr_quark>(&Prefs::changed), this, &DetailsDialog::refreshPref);
    connect(&session_, &Session::sessionCalled, this, &DetailsDialog::onSessionCalled);

    // call refreshModel periodically
    connect(&model_timer_, &QTimer::timeout, this, &DetailsDialog::refreshModel);
    model_timer_.start(RefreshIntervalMSec);
    refreshModel();

    // set up the debounce timer
    connect(&ui_debounce_timer_, &QTimer::timeout, this, &DetailsDialog::refreshUI);
    ui_debounce_timer_.setSingleShot(true);

    // set labels
    connect(ui_.dialogButtons, &QDialogButtonBox::clicked, this, &DetailsDialog::onButtonBoxClicked);
}

DetailsDialog::~DetailsDialog()
{
    prev_tab_index = ui_.tabs->currentIndex();
}

void DetailsDialog::resizeEvent(QResizeEvent* event)
{
    BaseDialog::resizeEvent(event);

    auto const rect = geometry();
    prefs_.set(TR_KEY_details_window_width, rect.width());
    prefs_.set(TR_KEY_details_window_height, rect.height());
}

void DetailsDialog::setIds(torrent_ids_t const& ids)
{
    if (ids != ids_) {
        ui_.filesView->clear();

        ids_ = ids;
        session_.refreshDetailInfo(ids_);
        labels_need_refresh_ = true;

        refreshModel();
        refreshUI();
    }
}

void DetailsDialog::refreshPref(tr_quark const key)
{
    if (key == TR_KEY_show_tracker_scrapes) {
        auto* selection_model = ui_.trackersView->selectionModel();
        tracker_delegate_->setShowMore(prefs_.get<bool>(key));
        selection_model->clear();
        ui_.trackersView->reset();
        selection_model->select(selection_model->selection(), QItemSelectionModel::Select);
        selection_model->setCurrentIndex(selection_model->currentIndex(), QItemSelectionModel::NoUpdate);
    } else if (key == TR_KEY_show_backup_trackers) {
        tracker_filter_->setShowBackupTrackers(prefs_.get<bool>(key));
    } else if (key == TR_KEY_show_extra_peer_details) {
        bool const show = prefs_.get<bool>(key);
        ui_.peersView->setColumnHidden(COL_UP_REQS, !show);
        ui_.peersView->setColumnHidden(COL_DOWN_REQS, !show);
    }
}

void DetailsDialog::bindPrefCheckBox(QAbstractButton* const check, tr_quark const key)
{
    check->setChecked(prefs_.get<bool>(key));
    connect(check, &QAbstractButton::clicked, this, [this, key](bool const val) { prefs_.set(key, val); });
}

// ---

void DetailsDialog::refreshModel()
{
    if (!ids_.empty()) {
        session_.refreshExtraStats(ids_);
    }
}

void DetailsDialog::onTorrentsEdited(torrent_ids_t const& ids)
{
    // refresh the edited torrents that are on display here
    auto interesting_ids = torrent_ids_t{};

    for (auto const id : ids) {
        if (ids_.contains(id)) {
            interesting_ids.insert(id);
        }
    }

    if (!interesting_ids.empty()) {
        session_.refreshDetailInfo(interesting_ids);
    }
}

void DetailsDialog::onTorrentsChanged(torrent_ids_t const& ids, Torrent::fields_t const& /*fields*/)
{
    if (ui_debounce_timer_.isActive()) {
        return;
    }

    if (!std::ranges::any_of(ids_, [&ids](auto const id) { return ids.contains(id); })) {
        return;
    }

    ui_debounce_timer_.start(DebounceIntervalMSec);
}

void DetailsDialog::onSessionCalled(Session::Tag const tag)
{
    if (pending_changes_tags_.erase(tag) > 0 && canEdit()) {
        refreshModel();
    }
}

void DetailsDialog::onButtonBoxClicked(QAbstractButton* const button)
{
    if (ui_.dialogButtons->standardButton(button) != QDialogButtonBox::Close) {
        return;
    }

    if (ui_.labelsTextEdit->isReadOnly()) { // no edits could have been made
        return;
    }

    auto const labels_text = ui_.labelsTextEdit->toPlainText().trimmed();

    if (labels_text == labels_baseline_) { // no edits have been made
        return;
    }

    static auto const Separators = QRegularExpression{ QStringLiteral("[,;]\\s*") };
    torrentSet(TR_KEY_labels, labels_text.split(Separators, Qt::SkipEmptyParts));

    if (!ids_.empty()) {
        session_.refreshDetailInfo(ids_);
    }
}

void DetailsDialog::refreshUI()
{
    auto const none = tr("None");
    auto const mixed = tr("Mixed");
    auto const now = time(nullptr);

    // build a list of torrents
    auto torrents = QList<Torrent const*>{};
    for (auto const id : ids_) {
        if (Torrent const* const tor = model_.getTorrentFromId(id); tor != nullptr) {
            torrents << tor;
        }
    }

    // `getter` totalled over every torrent
    auto const sum = [&torrents](auto const getter) {
        auto total = std::decay_t<std::invoke_result_t<decltype(getter), Torrent const&>>{};

        for (Torrent const* const tor : torrents) {
            total += std::invoke(getter, *tor);
        }

        return total;
    };

    ///
    ///  activity tab
    ///

    // stateValueLabel
    auto string = QString{};
    if (torrents.empty()) {
        string = none;
    } else if (auto const activity = uniformValue(torrents, &Torrent::getActivity); !activity) {
        string = mixed;
    } else if (*activity != TR_STATUS_STOPPED) {
        string = torrents.front()->activityString();
    } else if (std::ranges::all_of(torrents, &Torrent::isFinished)) {
        string = tr("Seeding complete");
    } else {
        string = tr("Paused");
    }

    ui_.stateValueLabel->setText(string);
    auto const state_string = string;

    // haveValueLabel
    uint64_t size_when_done = 0;
    uint64_t available = 0;

    if (torrents.empty()) {
        string = none;
    } else {
        uint64_t left_until_done = 0;
        uint64_t have_verified = 0;
        uint64_t have_unverified = 0;

        for (Torrent const* const tor : torrents) {
            if (tor->hasMetadata()) {
                have_unverified += tor->haveUnverified();
                have_verified += tor->haveVerified();
                size_when_done += tor->sizeWhenDone();
                left_until_done += tor->leftUntilDone();
                available += tor->sizeWhenDone() - tor->leftUntilDone() + tor->haveUnverified() + tor->desiredAvailable();
            }
        }

        double const d = size_when_done == 0 ?
            100.0 :
            100.0 * static_cast<double>(size_when_done - left_until_done) / static_cast<double>(size_when_done);
        auto const pct = Formatter::percentToString(d);
        auto const size_when_done_str = Formatter::storageToString(size_when_done);

        if (have_unverified == 0U && left_until_done == 0U) {
            //: Text following the "Have:" label in torrent properties dialog;
            //: {current_size} is amount of downloaded and verified data
            string = TR_FORMAT("{current_size} (100%)", fmt::arg("current_size", Formatter::storageToString(have_verified)));
        } else if (have_unverified == 0U) {
            //: Text following the "Have:" label in torrent properties dialog;
            //: {current_size} is amount of downloaded and verified data,
            //: {complete_size} is overall size of torrent data,
            //: {percent_done} is percentage ({current_size}/{complete_size}*100)
            string = TR_FORMAT(
                "{current_size} of {complete_size} ({percent_done}%)",
                fmt::arg("current_size", Formatter::storageToString(have_verified)),
                fmt::arg("complete_size", size_when_done_str),
                fmt::arg("percent_done", pct));
        } else {
            //: Text following the "Have:" label in torrent properties dialog;
            //: {current_size} is amount of downloaded data (both verified and unverified),
            //: {complete_size} is overall size of torrent data,
            //: {percent_done} is percentage ({current_size}/{complete_size}*100),
            //: {unverified_size} is amount of downloaded but not yet verified data
            string = TR_FORMAT(
                "{current_size} of {complete_size} ({percent_done}%), {unverified_size} Unverified",
                fmt::arg("current_size", Formatter::storageToString(have_verified + have_unverified)),
                fmt::arg("complete_size", size_when_done_str),
                fmt::arg("percent_done", pct),
                fmt::arg("unverified_size", Formatter::storageToString(have_unverified)));
        }
    }

    ui_.haveValueLabel->setText(string);

    // availabilityValueLabel
    if (torrents.empty() || size_when_done == 0) {
        string = none;
    } else {
        auto const percent = 100.0 * static_cast<double>(available) / static_cast<double>(size_when_done);
        string = QStringLiteral("%1%").arg(Formatter::percentToString(percent));
    }

    ui_.availabilityValueLabel->setText(string);

    // downloadedValueLabel
    if (torrents.empty()) {
        string = none;
    } else {
        auto const downloaded_str = Formatter::storageToString(sum(&Torrent::downloadedEver));

        if (auto const failed = sum(&Torrent::failedEver); failed != 0) {
            string = TR_FORMAT(
                "{downloaded_size} (+{discarded_size} discarded after failed checksum)",
                fmt::arg("downloaded_size", downloaded_str),
                fmt::arg("discarded_size", Formatter::storageToString(failed)));
        } else {
            string = downloaded_str;
        }
    }

    ui_.downloadedValueLabel->setText(string);

    // uploadedValueLabel
    if (torrents.empty()) {
        string = none;
    } else {
        auto const uploaded = sum(&Torrent::uploadedEver);
        string = TR_FORMAT(
            "{uploaded_size} (Ratio: {ratio})",
            fmt::arg("uploaded_size", Formatter::storageToString(uploaded)),
            fmt::arg("ratio", Formatter::ratioToString(tr_getRatio(uploaded, sum(&Torrent::sizeWhenDone)))));
    }

    ui_.uploadedValueLabel->setText(string);

    // runningTimeValueLabel
    if (torrents.empty()) {
        string = none;
    } else if (std::ranges::all_of(torrents, &Torrent::isPaused)) {
        string = state_string; // paused || finished
    } else if (auto const started = uniformValue(torrents, &Torrent::lastStarted); started && *started != 0) {
        string = Formatter::timeToString(static_cast<int>(std::difftime(now, *started)));
    } else {
        string = mixed;
    }

    ui_.runningTimeValueLabel->setText(string);

    // remainingTimeValueLabel
    if (torrents.empty()) {
        string = none;
    } else if (auto const eta = uniformValue(torrents, &Torrent::getETA); !eta) {
        string = mixed;
    } else if (*eta < 0) {
        string = tr("Unknown");
    } else {
        string = Formatter::timeToString(*eta);
    }

    ui_.remainingTimeValueLabel->setText(string);

    // lastActivityValueLabel
    if (torrents.empty()) {
        string = none;
    } else {
        auto const latest = std::ranges::max(torrents, {}, &Torrent::lastActivity)->lastActivity();
        auto const seconds = static_cast<int>(std::difftime(now, latest));

        if (latest == 0) {
            string = none;
        } else if (seconds < 5) {
            string = tr("Active now");
        } else {
            string = TR_FORMAT("{time_span} ago", fmt::arg("time_span", Formatter::timeToString(seconds)));
        }
    }

    ui_.lastActivityValueLabel->setText(string);

    // errorValueLabel
    if (torrents.empty()) {
        string = none;
    } else if (auto const error = uniformValue(torrents, &Torrent::getError); !error) {
        string = mixed;
    } else {
        string = error->isEmpty() ? none : *error;
    }

    ui_.errorValueLabel->setText(string);

    ///
    /// information tab
    ///

    // sizeValueLabel
    if (torrents.empty()) {
        string = none;
    } else {
        auto const pieces = sum(&Torrent::pieceCount);
        auto const size = sum(&Torrent::totalSize);
        auto const piece_size = uniformValue(torrents, &Torrent::pieceSize).value_or(0U);

        if (size == 0) {
            string = none;
        } else if (piece_size > 0U) {
            string = TR_FORMAT_N(
                "{total_size} ({piece_count:L} pieces @ {piece_size})",
                pieces,
                fmt::arg("total_size", Formatter::storageToString(size)),
                fmt::arg("piece_count", pieces),
                fmt::arg("piece_size", Formatter::memoryToString(piece_size)));
        } else {
            string = TR_FORMAT_N(
                "{total_size} ({piece_count:L} pieces)",
                pieces,
                fmt::arg("total_size", Formatter::storageToString(size)),
                fmt::arg("piece_count", pieces));
        }
    }

    ui_.sizeValueLabel->setText(string);

    // hashValueLabel
    if (torrents.empty()) {
        string = none;
    } else if (auto const hash = uniformValue(torrents, &Torrent::hash)) {
        string = hash->toString();
    } else {
        string = mixed;
    }

    ui_.hashValueLabel->setText(string);

    // privacyValueLabel
    if (torrents.empty()) {
        string = none;
    } else if (auto const is_private = uniformValue(torrents, &Torrent::isPrivate)) {
        string = *is_private ? tr("Private to this tracker -- DHT and PEX disabled") : tr("Public torrent");
    } else {
        string = mixed;
    }

    ui_.privacyValueLabel->setText(string);

    // labelsTextEdit
    if (labels_need_refresh_) {
        labels_need_refresh_ = false;

        auto const labels = uniformValue(torrents, &Torrent::labels);
        bool const editable = labels.has_value();
        labels_baseline_ = editable ? labels->join(QStringLiteral(", ")) : QString{};
        ui_.labelsTextEdit->setPlainText(labels_baseline_);
        ui_.labelsTextEdit->setPlaceholderText(editable || torrents.empty() ? none : mixed);
        ui_.labelsTextEdit->setReadOnly(!editable);
        ui_.labelsTextEdit->setEnabled(editable || torrents.empty());
    }

    // commentTextEdit
    auto const comment = uniformValue(torrents, &Torrent::comment);
    bool const is_comment_mixed = !torrents.empty() && !comment.has_value();
    string = torrents.empty() ? none : comment.value_or(mixed);

    if (ui_.commentTextEdit->toPlainText() != string) {
        ui_.commentTextEdit->setPlainText(string);
    }

    ui_.commentTextEdit->setEnabled(!is_comment_mixed && !string.isEmpty());

    // originValueLabel
    auto const creator = uniformValue(torrents, &Torrent::creator);
    auto const created = uniformValue(torrents, &Torrent::dateCreated);

    if (torrents.empty()) {
        string = none;
    } else if (!creator || !created) {
        string = mixed;
    } else if (creator->isEmpty() && *created <= 0) {
        string = tr("N/A");
    } else if (*created <= 0) {
        string = TR_FORMAT("Created by {creator}", fmt::arg("creator", *creator));
    } else if (creator->isEmpty()) {
        auto const date_str = QDateTime::fromSecsSinceEpoch(*created).toString();
        string = TR_FORMAT("Created on {date}", fmt::arg("date", date_str));
    } else {
        auto const date_str = QDateTime::fromSecsSinceEpoch(*created).toString();
        string = TR_FORMAT("Created by {creator} on {date}", fmt::arg("creator", *creator), fmt::arg("date", date_str));
    }

    ui_.originValueLabel->setText(string);

    // locationValueLabel
    if (torrents.empty()) {
        string = none;
    } else if (auto const path = uniformValue(torrents, &Torrent::getPath)) {
        string = *path;
    } else {
        string = mixed;
    }

    ui_.locationValueLabel->setText(string);

    // addedValueLabel
    if (torrents.empty()) {
        string = none;
    } else if (auto const added = uniformValue(torrents, &Torrent::dateAdded); !added) {
        string = mixed;
    } else if (*added <= 0) {
        string = tr("N/A");
    } else {
        string = QDateTime::fromSecsSinceEpoch(*added).toString();
    }

    ui_.addedValueLabel->setText(string);

    ///
    ///  Options Tab
    ///

    if (canEdit() && !torrents.empty()) {
        Torrent const& baseline = *torrents.front();

        ui_.sessionLimitCheck->setChecked(uniformValue(torrents, &Torrent::honorsSessionLimits).value_or(false));
        ui_.singleDownCheck->setChecked(uniformValue(torrents, &Torrent::downloadIsLimited).value_or(false));
        ui_.singleUpCheck->setChecked(uniformValue(torrents, &Torrent::uploadIsLimited).value_or(false));

        auto const priority = uniformValue(torrents, &Torrent::getBandwidthPriority);
        setIfIdle(ui_.bandwidthPriorityCombo, priority ? ui_.bandwidthPriorityCombo->findData(*priority) : -1);
        setIfIdle(ui_.singleDownSpin, static_cast<int>(baseline.downloadLimit().count(Speed::Units::KByps)));
        setIfIdle(ui_.singleUpSpin, static_cast<int>(baseline.uploadLimit().count(Speed::Units::KByps)));
        setIfIdle(ui_.peerLimitSpin, baseline.peerLimit());
    }

    if (!torrents.empty()) {
        Torrent const& baseline = *torrents.front();

        auto const ratio_mode = uniformValue(torrents, &Torrent::seedRatioMode);
        setIfIdle(ui_.ratioCombo, ratio_mode ? ui_.ratioCombo->findData(*ratio_mode) : -1);
        ui_.ratioSpin->setVisible(ratio_mode == TR_RATIOLIMIT_SINGLE);
        setIfIdle(ui_.ratioSpin, baseline.seedRatioLimit());

        auto const idle_mode = uniformValue(torrents, &Torrent::seedIdleMode);
        setIfIdle(ui_.idleCombo, idle_mode ? ui_.idleCombo->findData(*idle_mode) : -1);
        ui_.idleSpin->setVisible(idle_mode == TR_IDLELIMIT_SINGLE);
        setIfIdle(ui_.idleSpin, baseline.seedIdleLimit());
        onIdleLimitChanged();
    }

    ///
    ///  Tracker tab
    ///

    tracker_model_->refresh(model_, ids_);
    ui_.editTrackersButton->setEnabled(std::size(ids_) == 1);

    ///
    ///  Peers tab
    ///

    auto peers2 = decltype(peers_){};
    auto new_items = QList<QTreeWidgetItem*>{};

    for (Torrent const* const tor : torrents) {
        auto const id_str = QString::number(tor->id());

        for (Peer const& peer : tor->peers()) {
            auto const key = id_str + QLatin1Char(':') + peer.address;

            PeerItem* item = nullptr;
            if (auto const iter = peers_.find(key); iter != std::end(peers_)) {
                item = iter->second;
                item->refresh(peer);
            } else { // new peer has connected
                item = new PeerItem{ peer, icon_encrypted_ };
                new_items << item;
            }

            peers2.try_emplace(key, item);
        }
    }

    ui_.peersView->addTopLevelItems(new_items);

    for (auto const& [key, item] : peers_) {
        if (!peers2.contains(key)) { // old peer has disconnected
            delete item; // ~QTreeWidgetItem removes it from the view
        }
    }

    peers_ = std::move(peers2);

    if (std::size(ids_) == 1) {
        ui_.filesView->update(torrents[0]->files(), canEdit());
    } else {
        ui_.filesView->clear();
    }
}

// ---

void DetailsDialog::initInfoTab()
{
    int const cbh = QFontMetrics{ ui_.commentTextEdit->font() }.lineSpacing() * 4;
    ui_.commentTextEdit->setMinimumHeight(cbh);

    int const lteh = QFontMetrics{ ui_.labelsTextEdit->font() }.lineSpacing() * 2;
    ui_.labelsTextEdit->setFixedHeight(lteh);

    auto* cr = new ColumnResizer{ this };
    cr->addLayout(ui_.activitySectionLayout);
    cr->addLayout(ui_.detailsSectionLayout);
    cr->update();
}

// ---

void DetailsDialog::onIdleLimitChanged()
{
    //: Spin box format, "Stop seeding if idle for: [ 5 minutes ]"
    auto const* const units_format = QT_TRANSLATE_N_NOOP("DetailsDialog", "{minutes:L} minute(s)");
    Utils::updateSpinBoxFormat(ui_.idleSpin, "DetailsDialog", units_format, "minutes");
}

void DetailsDialog::onTrackerSelectionChanged()
{
    ui_.removeTrackerButton->setEnabled(ui_.trackersView->selectionModel()->hasSelection());
}

void DetailsDialog::onAddTrackerClicked()
{
    bool ok = false;
    auto const text_qstr = QInputDialog::getMultiLineText(
        this,
        tr("Add URL(s)"),
        tr("Add tracker announce URLs, one per line:"),
        {},
        &ok);
    if (!ok) {
        return;
    }

    // for each URL entered by the user...
    auto announce_list = tr_announce_list{};
    announce_list.parse(text_qstr.toStdString());
    auto url_to_ids = std::map<QString, std::set<tr_torrent_id_t>>{};
    for (auto const& info : announce_list) {
        // for each selected torrent...
        auto const announce_url = Utils::qstringFromUtf8(info.announce.sv());
        for (auto const& id : ids_) {
            // make a note if the torrent doesn't already have the URL
            if (tracker_model_->find(id, announce_url) == -1) {
                url_to_ids[announce_url].insert(id);
            }
        }
    }

    // now reverse the map so that if we're adding identical trackers
    // to more than one torrent, that can be batched into a single call
    auto ids_to_urls = std::map<std::set<tr_torrent_id_t>, std::set<QString>>{};
    for (auto& [announce_url, ids] : url_to_ids) {
        ids_to_urls[ids].insert(announce_url);
    }

    if (std::empty(ids_to_urls)) {
        QMessageBox::warning(this, tr("Error"), tr("No new URLs found."));
    } else {
        for (auto const& [ids, urls] : ids_to_urls) {
            torrentSet(torrent_ids_t{ std::begin(ids), std::end(ids) }, TR_KEY_tracker_add, urls);
        }
    }
}

void DetailsDialog::onTrackerListEdited(QString const& tracker_list)
{
    torrentSet(TR_KEY_tracker_list, tracker_list);
}

void DetailsDialog::onEditTrackersClicked()
{
    if (std::size(ids_) != 1) {
        return;
    }

    auto const* const tor = model_.getTorrentFromId(*std::begin(ids_));
    if (tor == nullptr) {
        return;
    }

    auto* dialog = new TrackersDialog{ tor->trackerList(), this };
    dialog->setWindowTitle(TR_FORMAT("{torrent_name} - Edit Trackers", fmt::arg("torrent_name", tor->name())));
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    connect(dialog, &TrackersDialog::trackerListEdited, this, &DetailsDialog::onTrackerListEdited);
    dialog->open();
}

void DetailsDialog::onRemoveTrackerClicked()
{
    // make a map of torrentIds to announce URLs to remove
    QItemSelectionModel* selection_model = ui_.trackersView->selectionModel();
    QModelIndexList const selected_rows = selection_model->selectedRows();
    auto torrent_id_to_tracker_ids = std::map<int, std::set<int>>{};

    for (auto const& model_index : selected_rows) {
        auto const inf = ui_.trackersView->model()->data(model_index, TrackerModel::TrackerRole).value<TrackerInfo>();
        torrent_id_to_tracker_ids[inf.torrent_id].insert(inf.st.id);
    }

    // batch all of a tracker's torrents into one command
    for (auto const& [torrent_id, tracker_ids] : torrent_id_to_tracker_ids) {
        torrentSet(torrent_ids_t{ torrent_id }, TR_KEY_tracker_remove, tracker_ids);
    }

    selection_model->clearSelection();
}

void DetailsDialog::initOptionsTab()
{
    auto const speed_unit_suffix = QStringLiteral(" %1").arg(Speed::displayName(Speed::Units::KByps));
    ui_.singleDownSpin->setSuffix(speed_unit_suffix);
    ui_.singleUpSpin->setSuffix(speed_unit_suffix);

    ui_.bandwidthPriorityCombo->addItem(tr("High"), TR_PRI_HIGH);
    ui_.bandwidthPriorityCombo->addItem(tr("Normal"), TR_PRI_NORMAL);
    ui_.bandwidthPriorityCombo->addItem(tr("Low"), TR_PRI_LOW);

    ui_.ratioCombo->addItem(tr("Use Global Settings"), TR_RATIOLIMIT_GLOBAL);
    ui_.ratioCombo->addItem(tr("Seed regardless of ratio"), TR_RATIOLIMIT_UNLIMITED);
    ui_.ratioCombo->addItem(tr("Stop seeding at ratio:"), TR_RATIOLIMIT_SINGLE);

    ui_.idleCombo->addItem(tr("Use Global Settings"), TR_IDLELIMIT_GLOBAL);
    ui_.idleCombo->addItem(tr("Seed regardless of activity"), TR_IDLELIMIT_UNLIMITED);
    ui_.idleCombo->addItem(tr("Stop seeding if idle for:"), TR_IDLELIMIT_SINGLE);

    auto* cr = new ColumnResizer{ this };
    cr->addLayout(ui_.speedSectionLayout);
    cr->addLayout(ui_.seedingLimitsSectionLayout);
    cr->addLayout(ui_.peerConnectionsSectionLayout);
    cr->update();

    // each control edits one property of every displayed torrent
    auto const bind_check = [this](QCheckBox* const check, tr_quark const key) {
        connect(check, &QCheckBox::clicked, this, [this, key](bool const val) { torrentSet(key, val); });
    };
    auto const bind_combo = [this](QComboBox* const combo, tr_quark const key) {
        connect(combo, qOverload<int>(&QComboBox::currentIndexChanged), this, [this, combo, key](int const index) {
            if (index != -1) {
                torrentSet(key, combo->itemData(index).toInt());
            }
        });
    };
    auto const bind_spin = [this](auto* const spin, tr_quark const key) {
        connect(spin, &QAbstractSpinBox::editingFinished, this, [this, spin, key]() { torrentSet(key, spin->value()); });
    };

    bind_check(ui_.sessionLimitCheck, TR_KEY_honors_session_limits);
    bind_check(ui_.singleDownCheck, TR_KEY_download_limited);
    bind_check(ui_.singleUpCheck, TR_KEY_upload_limited);
    bind_combo(ui_.bandwidthPriorityCombo, TR_KEY_bandwidth_priority);
    bind_combo(ui_.ratioCombo, TR_KEY_seed_ratio_mode);
    bind_combo(ui_.idleCombo, TR_KEY_seed_idle_mode);
    bind_spin(ui_.singleDownSpin, TR_KEY_download_limit);
    bind_spin(ui_.singleUpSpin, TR_KEY_upload_limit);
    bind_spin(ui_.ratioSpin, TR_KEY_seed_ratio_limit);
    bind_spin(ui_.idleSpin, TR_KEY_seed_idle_limit);
    bind_spin(ui_.peerLimitSpin, TR_KEY_peer_limit);

    connect(ui_.idleSpin, qOverload<int>(&QSpinBox::valueChanged), this, &DetailsDialog::onIdleLimitChanged);
}

// ---

void DetailsDialog::initTrackerTab()
{
    tracker_model_ = new TrackerModel{ this };
    tracker_filter_ = new TrackerModelFilter{ this };
    tracker_filter_->setSourceModel(tracker_model_);
    tracker_delegate_ = new TrackerDelegate{ this };

    ui_.trackersView->setModel(tracker_filter_);
    ui_.trackersView->setItemDelegate(tracker_delegate_);

    ui_.addTrackerButton->setIcon(icons::icon(icons::Type::AddTracker));
    ui_.editTrackersButton->setIcon(icons::icon(icons::Type::EditTrackers));
    ui_.removeTrackerButton->setIcon(icons::icon(icons::Type::RemoveTracker));

    bindPrefCheckBox(ui_.showTrackerScrapesCheck, TR_KEY_show_tracker_scrapes);
    bindPrefCheckBox(ui_.showBackupTrackersCheck, TR_KEY_show_backup_trackers);

    connect(ui_.addTrackerButton, &QAbstractButton::clicked, this, &DetailsDialog::onAddTrackerClicked);
    connect(ui_.editTrackersButton, &QAbstractButton::clicked, this, &DetailsDialog::onEditTrackersClicked);
    connect(ui_.removeTrackerButton, &QAbstractButton::clicked, this, &DetailsDialog::onRemoveTrackerClicked);
    connect(
        ui_.trackersView->selectionModel(),
        &QItemSelectionModel::selectionChanged,
        this,
        &DetailsDialog::onTrackerSelectionChanged);

    onTrackerSelectionChanged();
}

// ---

void DetailsDialog::initPeersTab()
{
    auto const speed_width_str = Speed{ 1024U, Speed::Units::MByps }.toQstring();

    ui_.peersView->setHeaderLabels(
        { QString{}, tr("Up"), tr("Up Reqs"), tr("Down"), tr("Dn Reqs"), tr("%"), tr("Flags"), tr("Address"), tr("Client") });
    ui_.peersView->sortByColumn(COL_ADDRESS, Qt::AscendingOrder);

    ui_.peersView->setColumnWidth(COL_LOCK, 20);
    ui_.peersView->setColumnWidth(COL_UP, measureViewItem(ui_.peersView, COL_UP, speed_width_str));
    ui_.peersView->setColumnWidth(COL_UP_REQS, measureViewItem(ui_.peersView, COL_UP_REQS, QStringLiteral("888")));
    ui_.peersView->setColumnWidth(COL_DOWN, measureViewItem(ui_.peersView, COL_DOWN, speed_width_str));
    ui_.peersView->setColumnWidth(COL_DOWN_REQS, measureViewItem(ui_.peersView, COL_DOWN_REQS, QStringLiteral("888")));
    ui_.peersView->setColumnWidth(COL_PERCENT, measureViewItem(ui_.peersView, COL_PERCENT, QStringLiteral("100%")));
    ui_.peersView->setColumnWidth(COL_STATUS, measureViewItem(ui_.peersView, COL_STATUS, QStringLiteral("ODUK?EXI")));
    ui_.peersView->setColumnWidth(COL_ADDRESS, measureViewItem(ui_.peersView, COL_ADDRESS, QStringLiteral("888.888.888.888")));

    bindPrefCheckBox(ui_.showExtraPeerDetailsCheck, TR_KEY_show_extra_peer_details);
}

// ---

void DetailsDialog::initFilesTab() const
{
    connect(ui_.filesView, &FileTreeView::openRequested, this, &DetailsDialog::onOpenRequested);
    connect(ui_.filesView, &FileTreeView::pathEdited, this, &DetailsDialog::onPathEdited);
    connect(ui_.filesView, &FileTreeView::priorityChanged, this, &DetailsDialog::onFilePriorityChanged);
    connect(ui_.filesView, &FileTreeView::wantedChanged, this, &DetailsDialog::onFileWantedChanged);
}

void DetailsDialog::onFilePriorityChanged(file_indices_t const& indices, int const priority)
{
    torrentSet(priorityKey(priority), indices);
}

void DetailsDialog::onFileWantedChanged(file_indices_t const& indices, bool const wanted)
{
    torrentSet(wanted ? TR_KEY_files_wanted : TR_KEY_files_unwanted, indices);
}

void DetailsDialog::onPathEdited(QString const& old_path, QString const& new_name)
{
    session_.torrentRenamePath(ids_, old_path, new_name);
}

void DetailsDialog::onOpenRequested(QString const& path) const
{
    if (!session_.is_local_filesystem()) {
        return;
    }

    for (int const id : ids_) {
        Torrent const* const tor = model_.getTorrentFromId(id);

        if (tor == nullptr) {
            continue;
        }

        auto const local_file_path = tor->getPath() + QLatin1Char('/') + path;

        if (!QFile::exists(local_file_path)) {
            continue;
        }

        if (QDesktopServices::openUrl(QUrl::fromLocalFile(local_file_path))) {
            break;
        }
    }
}

#include "DetailsDialog.moc"
