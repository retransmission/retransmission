// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#include "DetailsDialog.h"

#include <algorithm>
#include <cassert>
#include <ctime>
#include <functional>
#include <initializer_list>
#include <iterator> // std::back_inserter()
#include <map>
#include <memory>
#include <ranges>
#include <set>
#include <span>
#include <type_traits>
#include <utility>

#include <fmt/format.h>
#include <fmt/ranges.h> // fmt::join()

#include <QtCore/QDateTime>
#include <QtCore/QFile>
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

#include <libtransmission/announce-list.h>
#include <libtransmission/quark.h>
#include <libtransmission/shared-string.h>
#include <libtransmission/types.h>
#include <libtransmission/values.h>

#include <libtransmission-app/peer-list.h>
#include <libtransmission-app/torrent.h>
#include <libtransmission-app/uniform-value.h>

#include "BaseDialog.h"
#include "ColumnResizer.h"
#include "Formatter.h"
#include "NativeIcon.h"
#include "Prefs.h"
#include "Session.h"
#include "Speed.h"
#include "SqueezeLabel.h"
#include "TorrentDelegate.h"
#include "TorrentModel.h"
#include "TrackerDelegate.h"
#include "TrackerModel.h"
#include "TrackerModelFilter.h"
#include "TrFormat.h"
#include "ui_TrackersDialog.h"
#include "Utils.h"

using tr::app::PeerColumn;
using tr::app::PeerFlag;
using tr::app::PeerListItem;
using tr::app::Torrent;
using tr::app::uniform_value;

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

// the peers view's index for `column`
[[nodiscard]] constexpr int col(PeerColumn const column) noexcept
{
    return static_cast<int>(column);
}

[[nodiscard]] int measureViewItem(QTreeWidget const* view, int column, QString const& text)
{
    auto const item_width = Utils::measureViewItem(view, text);
    auto const header_width = Utils::measureHeaderItem(view->header(), view->headerItem()->text(column));

    return std::max(item_width, header_width);
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

[[nodiscard]] QString peerFlagDescription(PeerFlag const flag)
{
    switch (flag) {
    case PeerFlag::OptimisticUnchoke:
        return DetailsDialog::tr("Optimistic unchoke");

    case PeerFlag::DownloadingFrom:
        return DetailsDialog::tr("Downloading from this peer");

    case PeerFlag::WouldDownloadFrom:
        return DetailsDialog::tr("We would download from this peer if they would let us");

    case PeerFlag::UploadingTo:
        return DetailsDialog::tr("Uploading to peer");

    case PeerFlag::WouldUploadTo:
        return DetailsDialog::tr("This peer would download from us if we would let them");

    case PeerFlag::UnchokedUsUninterested:
        return DetailsDialog::tr("Peer has unchoked us, but we're not interested");

    case PeerFlag::UnchokedPeerUninterested:
        return DetailsDialog::tr("We unchoked this peer, but they're not interested");

    case PeerFlag::Encrypted:
        return DetailsDialog::tr("Encrypted connection");

    case PeerFlag::FromDht:
        return DetailsDialog::tr("Peer was found through DHT");

    case PeerFlag::FromPex:
        return DetailsDialog::tr("Peer was found through Peer Exchange (PEX)");

    case PeerFlag::Incoming:
        return DetailsDialog::tr("Peer is an incoming connection");

    case PeerFlag::Utp:
        return DetailsDialog::tr("Peer is connected over µTP");

    case PeerFlag::SupportsHolepunch:
        return DetailsDialog::tr("Peer supports holepunch (BEP 55)");
    }

    return {};
}

// One line per known flag, e.g. "D: Downloading from this peer"
[[nodiscard]] QString peerFlagsTooltip(std::string_view const flag_str)
{
    auto const flags = tr::app::PeerFlags::from_string(flag_str);
    auto lines = QStringList{};

    for (auto const& [flag, letter] : tr::app::PeerFlagLetters) {
        if (flags.contains(flag)) {
            lines << QStringLiteral("%1: %2").arg(QLatin1Char{ letter }).arg(peerFlagDescription(flag));
        }
    }

    return lines.join(QLatin1Char('\n'));
}

} // namespace

// ---

// A row in the peers table
class DetailsDialog::PeerItem : public QTreeWidgetItem
{
    PeerListItem item_;

public:
    PeerItem(PeerListItem item, QIcon const& encrypted_icon)
        : item_{ std::move(item) }
    {
        for (
            auto const column : { PeerColumn::UploadSpeed,
                                  PeerColumn::UploadRequests,
                                  PeerColumn::DownloadSpeed,
                                  PeerColumn::DownloadRequests,
                                  PeerColumn::Progress }) {
            setTextAlignment(col(column), Qt::AlignRight | Qt::AlignVCenter);
        }

        auto const& peer = item_.peer();
        setIcon(col(PeerColumn::Encrypted), peer.is_encrypted ? encrypted_icon : QIcon{});
        setToolTip(col(PeerColumn::Encrypted), peer.is_encrypted ? DetailsDialog::tr("Encrypted connection") : QString{});
        setText(col(PeerColumn::Address), Utils::qstringFromUtf8(peer.address));
        setText(col(PeerColumn::Client), Utils::qstringFromUtf8(peer.client_name.sv()));
        setToolTip(col(PeerColumn::Flags), peerFlagsTooltip(peer.flags));
        refreshCells();
    }

    // `item` has this row's key
    void refresh(PeerListItem item)
    {
        if (item.peer().flags != item_.peer().flags) {
            setToolTip(col(PeerColumn::Flags), peerFlagsTooltip(item.peer().flags));
        }

        item_ = std::move(item);
        refreshCells();
    }

    bool operator<(QTreeWidgetItem const& other) const override
    {
        auto const* i = dynamic_cast<PeerItem const*>(&other);
        auto const* tw = treeWidget();
        auto const column = static_cast<PeerColumn>(tw != nullptr ? tw->sortColumn() : col(tr::app::DefaultPeerSortColumn));

        assert(i != nullptr);

        return item_.compare(i->item_, column) < 0;
    }

private:
    void refreshCells()
    {
        auto const& peer = item_.peer();
        auto const progress = item_.progress();

        setText(col(PeerColumn::UploadSpeed), peer.rate_to_peer.is_zero() ? QString{} : Speed{ peer.rate_to_peer }.toQstring());
        setText(
            col(PeerColumn::UploadRequests),
            peer.active_reqs_to_client > 0 ? QString::number(peer.active_reqs_to_client) : QString{});
        setText(
            col(PeerColumn::DownloadSpeed),
            peer.rate_to_client.is_zero() ? QString{} : Speed{ peer.rate_to_client }.toQstring());
        setText(
            col(PeerColumn::DownloadRequests),
            peer.active_reqs_to_peer > 0 ? QString::number(peer.active_reqs_to_peer) : QString{});
        setText(
            col(PeerColumn::Progress),
            progress != tr::Values::Percent{} ? Formatter::percentToString(progress) : QString{});
        setText(col(PeerColumn::Flags), Utils::qstringFromUtf8(peer.flags));
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
    set_done_connection_ = session_.torrent_set_done.connect_scoped(&DetailsDialog::onTorrentSetDone, this);

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

void DetailsDialog::setIds(tr::app::TorrentList::Ids ids)
{
    if (ids != ids_) {
        ui_.filesView->clear();

        ids_ = std::move(ids);
        session_.refresh_torrents(ids_, tr::app::TorrentKeySet::DetailAll);
        labels_need_refresh_ = true;

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
        for (auto const column : tr::app::PeerColumns) {
            if (tr::app::is_extra_peer_column(column)) {
                ui_.peersView->setColumnHidden(col(column), !show);
            }
        }
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
    session_.refresh_torrents(ids_, tr::app::TorrentKeySet::DetailStats);
}

void DetailsDialog::onTorrentsEdited(std::span<tr_torrent_id_t const> const ids)
{
    // refresh the edited torrents that are on display here
    auto interesting_ids = tr::app::TorrentList::Ids{};
    std::ranges::set_intersection(ids_, ids, std::back_inserter(interesting_ids));
    session_.refresh_torrents(std::move(interesting_ids), tr::app::TorrentKeySet::DetailInfo);
}

void DetailsDialog::scheduleRefreshUI()
{
    if (!ui_debounce_timer_.isActive()) {
        ui_debounce_timer_.start(DebounceIntervalMSec);
    }
}

void DetailsDialog::onTorrentsChanged(std::span<tr_torrent_id_t const> const ids, tr::app::TorrentFieldSet const& /*fields*/)
{
    if (std::ranges::any_of(ids_, [&ids](auto const id) { return std::ranges::binary_search(ids, id); })) {
        scheduleRefreshUI();
    }
}

void DetailsDialog::onTorrentSetDone(tr::app::Session::Tag const tag, tr::app::Session::SetResult const& /*result*/)
{
    // The Session re-fetched before firing. canEdit() held refreshUI() back, so start it here:
    // a set that changed nothing fires no `torrentsChanged` to do it.
    if (pending_changes_tags_.erase(tag) > 0 && canEdit()) {
        scheduleRefreshUI();
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
}

void DetailsDialog::refreshUI()
{
    auto const none = tr("None");
    auto const mixed = tr("Mixed");
    auto const now = time(nullptr);

    // build a list of torrents
    auto torrents = QList<Torrent const*>{};
    for (auto const id : ids_) {
        if (auto const* const tor = model_.list().find(id); tor != nullptr) {
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
    } else if (auto const activity = uniform_value(torrents, &Torrent::activity); !activity) {
        string = mixed;
    } else if (*activity != TR_STATUS_STOPPED) {
        string = TorrentDelegate::activityString(*torrents.front());
    } else if (std::ranges::all_of(torrents, &Torrent::is_finished)) {
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
            if (tor->has_metadata()) {
                have_unverified += tor->have_unchecked().base_quantity();
                have_verified += tor->have_valid().base_quantity();
                size_when_done += tor->size_when_done().base_quantity();
                left_until_done += tor->left_until_done().base_quantity();
                available += tor->size_when_done().base_quantity() - tor->left_until_done().base_quantity() +
                    tor->desired_available().base_quantity();
            }
        }

        auto const pct = tr::Values::Percent{ size_when_done - left_until_done, size_when_done }.to_string();
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
        string = TR_FORMAT("{percent}%", fmt::arg("percent", tr::Values::Percent{ available, size_when_done }.to_string()));
    }

    ui_.availabilityValueLabel->setText(string);

    // downloadedValueLabel
    if (torrents.empty()) {
        string = none;
    } else {
        auto const downloaded_str = Formatter::storageToString(sum(&Torrent::downloaded_ever).base_quantity());

        if (auto const failed = sum(&Torrent::corrupt_ever); !failed.is_zero()) {
            string = TR_FORMAT(
                "{downloaded_size} (+{discarded_size} discarded after failed checksum)",
                fmt::arg("downloaded_size", downloaded_str),
                fmt::arg("discarded_size", Formatter::storageToString(failed.base_quantity())));
        } else {
            string = downloaded_str;
        }
    }

    ui_.downloadedValueLabel->setText(string);

    // uploadedValueLabel
    if (torrents.empty()) {
        string = none;
    } else {
        auto const uploaded = sum(&Torrent::uploaded_ever).base_quantity();
        string = TR_FORMAT(
            "{uploaded_size} (Ratio: {ratio})",
            fmt::arg("uploaded_size", Formatter::storageToString(uploaded)),
            fmt::arg("ratio", tr::Values::Ratio{ uploaded, sum(&Torrent::size_when_done).base_quantity() }.to_string()));
    }

    ui_.uploadedValueLabel->setText(string);

    // runningTimeValueLabel
    if (torrents.empty()) {
        string = none;
    } else if (std::ranges::all_of(torrents, &Torrent::is_paused)) {
        string = state_string; // paused || finished
    } else if (auto const started = uniform_value(torrents, &Torrent::start_date); started && *started != 0) {
        string = Formatter::timeToString(static_cast<int>(std::difftime(now, *started)));
    } else {
        string = mixed;
    }

    ui_.runningTimeValueLabel->setText(string);

    // remainingTimeValueLabel
    if (torrents.empty()) {
        string = none;
    } else if (auto const eta = uniform_value(torrents, &Torrent::eta); !eta) {
        string = mixed;
    } else if (*eta < 0) {
        string = tr("Unknown");
    } else {
        string = Formatter::timeToString(static_cast<int>(*eta));
    }

    ui_.remainingTimeValueLabel->setText(string);

    // lastActivityValueLabel
    if (torrents.empty()) {
        string = none;
    } else {
        auto const latest = std::ranges::max(torrents, {}, &Torrent::activity_date)->activity_date();
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
    } else if (auto const error = uniform_value(torrents, &TorrentDelegate::errorString); !error) {
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
        auto const pieces = sum(&Torrent::piece_count);
        auto const size = sum(&Torrent::total_size).base_quantity();
        auto const piece_size = uniform_value(torrents, &Torrent::piece_size).value_or(0U);

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
    } else if (auto const hash = uniform_value(torrents, &Torrent::hash_string)) {
        string = Utils::qstringFromUtf8(*hash);
    } else {
        string = mixed;
    }

    ui_.hashValueLabel->setText(string);

    // privacyValueLabel
    if (torrents.empty()) {
        string = none;
    } else if (auto const is_private = uniform_value(torrents, &Torrent::is_private)) {
        string = *is_private ? tr("Private to this tracker -- DHT and PEX disabled") : tr("Public torrent");
    } else {
        string = mixed;
    }

    ui_.privacyValueLabel->setText(string);

    // labelsTextEdit
    if (labels_need_refresh_) {
        labels_need_refresh_ = false;

        auto const labels = uniform_value(torrents, &Torrent::labels);
        bool const editable = labels.has_value();
        labels_baseline_ = editable ?
            Utils::qstringFromUtf8(
                fmt::format("{}", fmt::join(*labels | std::views::transform(&tr::shared_string::sv), ", "))) :
            QString{};
        ui_.labelsTextEdit->setPlainText(labels_baseline_);
        ui_.labelsTextEdit->setPlaceholderText(editable || torrents.empty() ? none : mixed);
        ui_.labelsTextEdit->setReadOnly(!editable);
        ui_.labelsTextEdit->setEnabled(editable || torrents.empty());
    }

    // commentTextEdit
    auto const comment = uniform_value(torrents, &Torrent::comment);
    bool const is_comment_mixed = !torrents.empty() && !comment.has_value();
    if (torrents.empty()) {
        string = none;
    } else if (is_comment_mixed) {
        string = mixed;
    } else {
        string = Utils::qstringFromUtf8(comment->sv());
    }

    if (ui_.commentTextEdit->toPlainText() != string) {
        ui_.commentTextEdit->setPlainText(string);
    }

    ui_.commentTextEdit->setEnabled(!is_comment_mixed && !string.isEmpty());

    // originValueLabel
    auto const creator = uniform_value(torrents, &Torrent::creator);
    auto const created = uniform_value(torrents, &Torrent::date_created);

    if (torrents.empty()) {
        string = none;
    } else if (!creator || !created) {
        string = mixed;
    } else if (creator->empty() && *created <= 0) {
        string = tr("N/A");
    } else if (*created <= 0) {
        string = TR_FORMAT("Created by {creator}", fmt::arg("creator", creator->sv()));
    } else if (creator->empty()) {
        auto const date_str = QDateTime::fromSecsSinceEpoch(*created).toString();
        string = TR_FORMAT("Created on {date}", fmt::arg("date", date_str));
    } else {
        auto const date_str = QDateTime::fromSecsSinceEpoch(*created).toString();
        string = TR_FORMAT("Created by {creator} on {date}", fmt::arg("creator", creator->sv()), fmt::arg("date", date_str));
    }

    ui_.originValueLabel->setText(string);

    // locationValueLabel
    if (torrents.empty()) {
        string = none;
    } else if (auto const path = uniform_value(torrents, &Torrent::download_dir)) {
        string = Utils::qstringFromUtf8(path->sv());
    } else {
        string = mixed;
    }

    ui_.locationValueLabel->setText(string);

    // addedValueLabel
    if (torrents.empty()) {
        string = none;
    } else if (auto const added = uniform_value(torrents, &Torrent::added_date); !added) {
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

        ui_.sessionLimitCheck->setChecked(uniform_value(torrents, &Torrent::honors_session_limits).value_or(false));
        ui_.singleDownCheck->setChecked(uniform_value(torrents, &Torrent::download_limited).value_or(false));
        ui_.singleUpCheck->setChecked(uniform_value(torrents, &Torrent::upload_limited).value_or(false));

        auto const priority = uniform_value(torrents, &Torrent::bandwidth_priority);
        setIfIdle(ui_.bandwidthPriorityCombo, priority ? ui_.bandwidthPriorityCombo->findData(*priority) : -1);
        setIfIdle(ui_.singleDownSpin, static_cast<int>(baseline.download_limit().count(tr::Values::Speed::Units::KByps)));
        setIfIdle(ui_.singleUpSpin, static_cast<int>(baseline.upload_limit().count(tr::Values::Speed::Units::KByps)));
        setIfIdle(ui_.peerLimitSpin, baseline.peer_limit());
    }

    if (!torrents.empty()) {
        Torrent const& baseline = *torrents.front();

        auto const ratio_mode = uniform_value(torrents, &Torrent::seed_ratio_mode);
        setIfIdle(ui_.ratioCombo, ratio_mode ? ui_.ratioCombo->findData(*ratio_mode) : -1);
        ui_.ratioSpin->setVisible(ratio_mode == TR_RATIOLIMIT_SINGLE);
        setIfIdle(ui_.ratioSpin, baseline.seed_ratio_limit());

        auto const idle_mode = uniform_value(torrents, &Torrent::seed_idle_mode);
        setIfIdle(ui_.idleCombo, idle_mode ? ui_.idleCombo->findData(*idle_mode) : -1);
        ui_.idleSpin->setVisible(idle_mode == TR_IDLELIMIT_SINGLE);
        setIfIdle(ui_.idleSpin, baseline.seed_idle_limit());
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

    auto new_items = QList<QTreeWidgetItem*>{};

    for (Torrent const* const tor : torrents) {
        for (auto const& peer : tor->peers()) {
            auto item = PeerListItem{ *tor, peer };
            auto const key = item.key();
            peers_.add_or_update(
                key,
                [&] {
                    auto* const row = new PeerItem{ std::move(item), icon_encrypted_ };
                    new_items << row;
                    return row;
                },
                [&](PeerItem* const row) { row->refresh(std::move(item)); });
        }
    }

    ui_.peersView->addTopLevelItems(new_items);
    peers_.remove_unseen([](PeerItem* const row) { delete row; }); // ~QTreeWidgetItem removes it from the view

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
            torrentSet(tr::app::TorrentList::Ids{ std::begin(ids), std::end(ids) }, TR_KEY_tracker_add, urls);
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

    auto const* const tor = model_.list().find(*std::begin(ids_));
    if (tor == nullptr) {
        return;
    }

    auto* dialog = new TrackersDialog{ Utils::qstringFromUtf8(tor->tracker_list().sv()), this };
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
        torrent_id_to_tracker_ids[inf.torrent_id].insert(static_cast<int>(inf.st.id));
    }

    // batch all of a tracker's torrents into one command
    for (auto const& [torrent_id, tracker_ids] : torrent_id_to_tracker_ids) {
        torrentSet({ torrent_id }, TR_KEY_tracker_remove, tracker_ids);
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
    // `editingFinished` also fires when focus only passes through the spin,
    // so send a value only if the user changed it: with several torrents
    // shown, the spin holds the first one's value, not every torrent's
    auto const bind_spin = [this](auto* const spin, tr_quark const key) {
        using Spin = std::remove_pointer_t<decltype(spin)>;
        auto const edited = std::make_shared<bool>(false);
        connect(spin, qOverload<decltype(spin->value())>(&Spin::valueChanged), this, [edited] { *edited = true; });
        connect(spin, &QAbstractSpinBox::editingFinished, this, [this, spin, key, edited] {
            if (std::exchange(*edited, false)) {
                torrentSet(key, spin->value());
            }
        });
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
    ui_.peersView->sortByColumn(col(tr::app::DefaultPeerSortColumn), Qt::AscendingOrder);

    auto const set_width = [this](PeerColumn const column, QString const& sample) {
        ui_.peersView->setColumnWidth(col(column), measureViewItem(ui_.peersView, col(column), sample));
    };

    ui_.peersView->setColumnWidth(col(PeerColumn::Encrypted), 20);
    set_width(PeerColumn::UploadSpeed, speed_width_str);
    set_width(PeerColumn::UploadRequests, QStringLiteral("888"));
    set_width(PeerColumn::DownloadSpeed, speed_width_str);
    set_width(PeerColumn::DownloadRequests, QStringLiteral("888"));
    set_width(PeerColumn::Progress, QStringLiteral("100%"));
    set_width(PeerColumn::Flags, QStringLiteral("ODUK?EXI"));
    set_width(PeerColumn::Address, QStringLiteral("888.888.888.888"));

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
    session_.rename_torrent_path(ids_, old_path.toStdString(), new_name.toStdString());
}

void DetailsDialog::onOpenRequested(QString const& path) const
{
    if (!session_.is_local_filesystem()) {
        return;
    }

    for (int const id : ids_) {
        auto const* const tor = model_.list().find(id);

        if (tor == nullptr) {
            continue;
        }

        auto const local_file_path = Utils::qstringFromUtf8(tor->download_dir().sv()) + QLatin1Char('/') + path;

        if (!QFile::exists(local_file_path)) {
            continue;
        }

        if (QDesktopServices::openUrl(QUrl::fromLocalFile(local_file_path))) {
            break;
        }
    }
}

#include "DetailsDialog.moc"
