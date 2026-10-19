// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#include "PrefsDialog.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <functional>
#include <optional>
#include <utility>
#include <vector>

#include <QtCore/QCoreApplication>
#include <QtCore/QDateTime>
#include <QtCore/QSignalBlocker>
#include <QtCore/QTime>
#include <QtCore/QTimer>

#include <QtWidgets/QCheckBox>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QDialogButtonBox>
#include <QtWidgets/QDoubleSpinBox>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QMessageBox>
#include <QtWidgets/QPlainTextEdit>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QSpinBox>
#include <QtWidgets/QStackedWidget>
#include <QtWidgets/QTimeEdit>

#include <libtransmission/quark.h>
#include <libtransmission/types.h>

#include "ColumnResizer.h"
#include "FreeSpaceLabel.h"
#include "Prefs.h"
#include "Session.h"
#include "Speed.h"
#include "TrFormat.h"
#include "Utils.h"

using namespace tr;

namespace
{
auto constexpr BlocklistInfoRefreshInterval = std::chrono::seconds{ 15 };
} // namespace

// ---

// Runs `setter` with the pref's value now and whenever `key` is refreshed.
template<typename T, typename W, typename Setter>
void PrefsDialog::addUpdater(W* const w, tr_quark const key, Setter const setter)
{
    auto updater = [this, w, key, setter]() {
        auto const blocker = QSignalBlocker{ w };
        std::invoke(setter, w, prefs_.get<T>(key));
    };
    updater();
    updaters_.emplace(key, std::move(updater));
}

// Like addUpdater(), and also saves the pref whenever `w` emits `signal`.
template<typename T, typename W, typename Setter, typename Signal>
void PrefsDialog::bind(W* const w, tr_quark const key, Setter const setter, Signal const signal)
{
    addUpdater<T>(w, key, setter);
    connect(w, signal, this, [this, key](T const& val) { set(key, val); });
}

// Enables `widgets` iff every pref in `keys` is true.
void PrefsDialog::bindEnabled(QWidgetList const& widgets, std::initializer_list<tr_quark> const keys)
{
    auto updater = [this, widgets, keys = std::vector<tr_quark>{ keys }]() {
        auto const enabled = std::ranges::all_of(keys, [this](tr_quark const key) { return prefs_.get<bool>(key); });
        for (auto* const w : widgets) {
            w->setEnabled(enabled);
        }
    };
    updater();
    for (auto const key : keys) {
        updaters_.emplace(key, updater);
    }
}

// Binds a combo box whose items hold their pref values as item data.
template<typename T>
void PrefsDialog::initCombo(QComboBox* const w, tr_quark const key)
{
    auto const select = [](QComboBox* const combo, T const val) {
        if (auto const idx = combo->findData(static_cast<int>(val)); idx >= 0) {
            combo->setCurrentIndex(idx);
        }
    };
    addUpdater<T>(w, key, select);

    auto on_activated = [this, key, w](int const idx) {
        set(key, static_cast<T>(w->itemData(idx).toInt()));
    };
    connect(w, qOverload<int>(&QComboBox::activated), this, std::move(on_activated));
}

void PrefsDialog::initWidget(QPlainTextEdit* const w, tr_quark const key)
{
    addUpdater<QString>(w, key, &QPlainTextEdit::setPlainText);

    // We don't want to change the preference every time there's a keystroke
    // in a QPlainTextEdit, so instead of connecting to the textChanged signal,
    // only update the pref when the text changed AND focus was lost.
    auto on_focus_changed = [this, key, w](QWidget* old, QWidget* /*cur*/) {
        if (w == old && w->document()->isModified()) {
            set(key, w->toPlainText());
        }
    };
    connect(qApp, &QApplication::focusChanged, w, std::move(on_focus_changed));
}

void PrefsDialog::initWidget(FreeSpaceLabel* const w, tr_quark const key)
{
    addUpdater<QString>(w, key, &FreeSpaceLabel::setPath);
}

void PrefsDialog::initWidget(PathButton* const w, tr_quark const key)
{
    bind<QString>(w, key, &PathButton::setPath, &PathButton::pathChanged);
}

void PrefsDialog::initWidget(QCheckBox* const w, tr_quark const key)
{
    bind<bool>(w, key, &QCheckBox::setChecked, &QCheckBox::toggled);
}

void PrefsDialog::initWidget(QDoubleSpinBox* const w, tr_quark const key)
{
    bind<double>(w, key, &QDoubleSpinBox::setValue, qOverload<double>(&QDoubleSpinBox::valueChanged));
}

void PrefsDialog::initWidget(QLineEdit* const w, tr_quark const key)
{
    addUpdater<QString>(w, key, &QLineEdit::setText);

    auto on_editing_finished = [this, key, w]() {
        if (w->isModified()) {
            set(key, w->text());
        }
    };
    connect(w, &QLineEdit::editingFinished, this, std::move(on_editing_finished));
}

void PrefsDialog::initWidget(QSpinBox* const w, tr_quark const key)
{
    bind<int>(w, key, &QSpinBox::setValue, qOverload<int>(&QSpinBox::valueChanged));
}

void PrefsDialog::initWidget(QTimeEdit* const w, tr_quark const key)
{
    auto const set_minutes = [](QTimeEdit* const edit, int const minutes) {
        edit->setTime(QTime{ 0, 0 }.addSecs(minutes * 60));
    };
    addUpdater<int>(w, key, set_minutes);

    auto on_editing_finished = [this, key, w]() {
        auto const minutes = w->time().msecsSinceStartOfDay() / (1000 * 60);
        set(key, minutes);
    };
    connect(w, &QAbstractSpinBox::editingFinished, this, std::move(on_editing_finished));
}

// ---

void PrefsDialog::initRemoteTab()
{
    initWidget(ui_.enableRpcCheck, TR_KEY_rpc_enabled);
    initWidget(ui_.rpcPortSpin, TR_KEY_rpc_port);
    initWidget(ui_.requireRpcAuthCheck, TR_KEY_rpc_authentication_required);
    initWidget(ui_.rpcUsernameEdit, TR_KEY_rpc_username);
    initWidget(ui_.rpcPasswordEdit, TR_KEY_rpc_password);
    initWidget(ui_.enableRpcWhitelistCheck, TR_KEY_rpc_whitelist_enabled);
    initWidget(ui_.rpcWhitelistEdit, TR_KEY_rpc_whitelist);

    connect(ui_.openWebClientButton, &QAbstractButton::clicked, &session_, &Session::launchWebInterface);

    auto const web_widgets = QWidgetList{ ui_.rpcPortLabel,
                                          ui_.rpcPortSpin,
                                          ui_.requireRpcAuthCheck,
                                          ui_.enableRpcWhitelistCheck };
    auto const auth_widgets = QWidgetList{ ui_.rpcUsernameLabel,
                                           ui_.rpcUsernameEdit,
                                           ui_.rpcPasswordLabel,
                                           ui_.rpcPasswordEdit };
    auto const whitelist_widgets = QWidgetList{ ui_.rpcWhitelistLabel, ui_.rpcWhitelistEdit };

    if (session_.is_embedded()) {
        bindEnabled(web_widgets, { TR_KEY_rpc_enabled });
        bindEnabled(auth_widgets, { TR_KEY_rpc_enabled, TR_KEY_rpc_authentication_required });
        bindEnabled(whitelist_widgets, { TR_KEY_rpc_enabled, TR_KEY_rpc_whitelist_enabled });
        return;
    }

    // a remote session doesn't expose its RPC server settings
    for (auto* const w : QWidgetList{ ui_.enableRpcCheck } + web_widgets + auth_widgets + whitelist_widgets) {
        w->setToolTip(tr("Not supported by remote sessions"));
        w->setEnabled(false);
    }
}

// ---

void PrefsDialog::initSpeedTab()
{
    auto const suffix = QStringLiteral(" %1").arg(Speed::displayName(Speed::Units::KByps));

    ui_.uploadSpeedLimitSpin->setSuffix(suffix);
    ui_.downloadSpeedLimitSpin->setSuffix(suffix);
    ui_.altUploadSpeedLimitSpin->setSuffix(suffix);
    ui_.altDownloadSpeedLimitSpin->setSuffix(suffix);

    auto* const alt_description = ui_.altSpeedLimitsSectionDescriptionLabel;
    alt_description->setText(QStringLiteral("<small>%1</small>").arg(alt_description->text()));

    initWidget(ui_.uploadSpeedLimitCheck, TR_KEY_speed_limit_up_enabled);
    initWidget(ui_.uploadSpeedLimitSpin, TR_KEY_speed_limit_up);
    initWidget(ui_.downloadSpeedLimitCheck, TR_KEY_speed_limit_down_enabled);
    initWidget(ui_.downloadSpeedLimitSpin, TR_KEY_speed_limit_down);
    initWidget(ui_.altUploadSpeedLimitSpin, TR_KEY_alt_speed_up);
    initWidget(ui_.altDownloadSpeedLimitSpin, TR_KEY_alt_speed_down);
    initWidget(ui_.altSpeedLimitScheduleCheck, TR_KEY_alt_speed_time_enabled);
    initWidget(ui_.altSpeedLimitStartTimeEdit, TR_KEY_alt_speed_time_begin);
    initWidget(ui_.altSpeedLimitEndTimeEdit, TR_KEY_alt_speed_time_end);
    bindEnabled({ ui_.uploadSpeedLimitSpin }, { TR_KEY_speed_limit_up_enabled });
    bindEnabled({ ui_.downloadSpeedLimitSpin }, { TR_KEY_speed_limit_down_enabled });

    auto* const days_combo = ui_.altSpeedLimitDaysCombo;
    days_combo->addItem(tr("Every Day"), TR_SCHED_ALL);
    days_combo->addItem(tr("Weekdays"), TR_SCHED_WEEKDAY);
    days_combo->addItem(tr("Weekends"), TR_SCHED_WEEKEND);

    // list the days in the locale's week order
    auto const locale = QLocale{};
    auto days = std::array{
        std::pair{ Qt::Monday, TR_SCHED_MON },    std::pair{ Qt::Tuesday, TR_SCHED_TUES },
        std::pair{ Qt::Wednesday, TR_SCHED_WED }, std::pair{ Qt::Thursday, TR_SCHED_THURS },
        std::pair{ Qt::Friday, TR_SCHED_FRI },    std::pair{ Qt::Saturday, TR_SCHED_SAT },
        std::pair{ Qt::Sunday, TR_SCHED_SUN },
    };
    std::ranges::rotate(days, std::begin(days) + (locale.firstDayOfWeek() - Qt::Monday));
    for (auto const& [qt_day, tr_day] : days) {
        days_combo->addItem(locale.dayName(qt_day), tr_day);
    }
    initCombo<int>(days_combo, TR_KEY_alt_speed_time_day);

    bindEnabled(
        { ui_.altSpeedLimitStartTimeEdit,
          ui_.altSpeedLimitToLabel,
          ui_.altSpeedLimitEndTimeEdit,
          ui_.altSpeedLimitDaysLabel,
          days_combo },
        { TR_KEY_alt_speed_time_enabled });

    auto* cr = new ColumnResizer{ this };
    cr->addLayout(ui_.speedLimitsSectionLayout);
    cr->addLayout(ui_.altSpeedLimitsSectionLayout);
    cr->update();
}

// ---

void PrefsDialog::initDesktopTab()
{
    ui_.showTrayIconCheck->setText(Utils::withAppName(ui_.showTrayIconCheck->text()));
    initWidget(ui_.showTrayIconCheck, TR_KEY_show_notification_area_icon);
    initWidget(ui_.startMinimizedCheck, TR_KEY_start_minimized);
    initWidget(ui_.notifyOnTorrentAddedCheck, TR_KEY_torrent_added_notification_enabled);
    initWidget(ui_.notifyOnTorrentCompletedCheck, TR_KEY_torrent_complete_notification_enabled);
    initWidget(ui_.playSoundOnTorrentCompletedCheck, TR_KEY_torrent_complete_sound_enabled);
    initWidget(ui_.preventSleepCheck, TR_KEY_inhibit_desktop_hibernation);
}

// ---

QString PrefsDialog::getPortStatusText(PortTestStatus const status)
{
    switch (status) {
    case PortTestStatus::Unknown:
        return tr("unknown");
    case PortTestStatus::Checking:
        return tr("checking…");
    case PortTestStatus::Open:
        return tr("open");
    case PortTestStatus::Closed:
        return tr("closed");
    case PortTestStatus::Error:
        return tr("error");
    }

    return {};
}

void PrefsDialog::updatePortStatusLabel()
{
    auto const status_ipv4 = getPortStatusText(port_test_status_[TR_AF_INET]);
    auto const status_ipv6 = getPortStatusText(port_test_status_[TR_AF_INET6]);

    ui_.peerPortStatusLabel->setText(
        port_test_status_[TR_AF_INET] == port_test_status_[TR_AF_INET6] ?
            TR_FORMAT("Status: <b>{status}</b>", fmt::arg("status", status_ipv4)) :
            TR_FORMAT(
                "Status: <b>{status_ipv4}</b> (IPv4), <b>{status_ipv6}</b> (IPv6)",
                fmt::arg("status_ipv4", status_ipv4),
                fmt::arg("status_ipv6", status_ipv6)));
}

void PrefsDialog::portTestSetEnabled()
{
    // Depend on the RPC call status instead of the UI status, so that the widgets
    // won't be enabled even if the port peer port changed while we have port_test
    // RPC call(s) in-flight.
    auto const sensitive = !session_.portTestPending(TR_AF_INET) && !session_.portTestPending(TR_AF_INET6);
    ui_.testPeerPortButton->setEnabled(sensitive);
    ui_.peerPortSpin->setEnabled(sensitive);
}

void PrefsDialog::onPortTested(std::optional<bool> const result, tr_address_type const ip_protocol)
{
    // Only update the UI if the current status is "checking", so that
    // we won't show the port test results for the old peer port if it
    // changed while we have port_test RPC call(s) in-flight.
    if (auto& status = port_test_status_[ip_protocol]; status == PortTestStatus::Checking) {
        if (!result) {
            status = PortTestStatus::Error;
        } else {
            status = *result ? PortTestStatus::Open : PortTestStatus::Closed;
        }
        updatePortStatusLabel();
    }
    portTestSetEnabled();
}

void PrefsDialog::onPortTest()
{
    port_test_status_.fill(PortTestStatus::Checking);
    updatePortStatusLabel();

    session_.portTest(TR_AF_INET);
    session_.portTest(TR_AF_INET6);

    portTestSetEnabled();
}

void PrefsDialog::initNetworkTab()
{
    ui_.torrentPeerLimitSpin->setRange(1, INT_MAX);
    ui_.globalPeerLimitSpin->setRange(1, INT_MAX);

    initWidget(ui_.peerPortSpin, TR_KEY_peer_port);
    updaters_.emplace(TR_KEY_peer_port, [this]() {
        port_test_status_.fill(PortTestStatus::Unknown);
        updatePortStatusLabel();
        portTestSetEnabled();
    });
    initWidget(ui_.randomPeerPortCheck, TR_KEY_peer_port_random_on_start);
    initWidget(ui_.enablePortForwardingCheck, TR_KEY_port_forwarding_enabled);
    initWidget(ui_.torrentPeerLimitSpin, TR_KEY_peer_limit_per_torrent);
    initWidget(ui_.globalPeerLimitSpin, TR_KEY_peer_limit_global);
    initWidget(ui_.enableUtpCheck, TR_KEY_utp_enabled);
    initWidget(ui_.enablePexCheck, TR_KEY_pex_enabled);
    initWidget(ui_.enableDhtCheck, TR_KEY_dht_enabled);
    initWidget(ui_.enableLpdCheck, TR_KEY_lpd_enabled);
    initWidget(ui_.defaultTrackersPlainTextEdit, TR_KEY_default_trackers);

    auto* cr = new ColumnResizer{ this };
    cr->addLayout(ui_.incomingPeersSectionLayout);
    cr->addLayout(ui_.peerLimitsSectionLayout);
    cr->update();

    connect(ui_.testPeerPortButton, &QAbstractButton::clicked, this, &PrefsDialog::onPortTest);
    connect(&session_, &Session::portTested, this, &PrefsDialog::onPortTested);

    updatePortStatusLabel();
}

// ---

void PrefsDialog::onUpdateBlocklistClicked()
{
    auto* const box = new QMessageBox{
        QMessageBox::Information,
        QString{},
        QStringLiteral("<b>%1</b><p>%2</p>").arg(tr("Update Blocklist"), tr("Getting new blocklist…")),
        QMessageBox::Close,
        this
    };
    box->setAttribute(Qt::WA_DeleteOnClose);
    box->setTextFormat(Qt::RichText);

    auto on_updated = [box](qint64 const n) {
        box->setText(TR_FORMAT_N(
            "<b>Update succeeded!</b><p>Blocklist now has {count:L} rule(s).</p>",
            static_cast<int>(n),
            fmt::arg("count", n)));
    };
    connect(&session_, &Session::blocklistUpdated, box, std::move(on_updated));

    auto on_failed = [box](QString const& message) {
        // Keep markup out of the translatable strings (assemble it here) and match the
        // macOS client's wording. The detail line is libtransmission's own error text
        // when present, or a generic fallback otherwise.
        //
        // SECURITY: `message` originates from the session -- a remote daemon can be hostile --
        // and this dialog renders RichText, so toHtmlEscaped() is load-bearing: it stops a
        // malicious daemon from injecting markup here. Don't drop it.
        auto const detail = message.isEmpty() ? tr("Couldn't update blocklist") : message.toHtmlEscaped();
        box->setText(QStringLiteral("<b>%1</b><p>%2</p>").arg(tr("Download of the blocklist failed."), detail));
    };
    connect(&session_, &Session::blocklistUpdateFailed, box, std::move(on_failed));

    box->show();
    session_.updateBlocklist();
}

void PrefsDialog::initPrivacyTab()
{
    ui_.encryptionModeCombo->addItem(tr("Allow encryption"), TR_CLEAR_PREFERRED);
    ui_.encryptionModeCombo->addItem(tr("Prefer encryption"), TR_ENCRYPTION_PREFERRED);
    ui_.encryptionModeCombo->addItem(tr("Require encryption"), TR_ENCRYPTION_REQUIRED);
    initCombo<tr_encryption_mode>(ui_.encryptionModeCombo, TR_KEY_encryption);

    initWidget(ui_.blocklistCheck, TR_KEY_blocklist_enabled);
    initWidget(ui_.blocklistEdit, TR_KEY_blocklist_url);
    initWidget(ui_.autoUpdateBlocklistCheck, TR_KEY_blocklist_updates_enabled);

    bindEnabled(
        { ui_.blocklistEdit,
          ui_.blocklistStatusLabel,
          ui_.blocklistDateLabel,
          ui_.updateBlocklistButton,
          ui_.autoUpdateBlocklistCheck },
        { TR_KEY_blocklist_enabled });

    auto* cr = new ColumnResizer{ this };
    cr->addLayout(ui_.encryptionSectionLayout);
    cr->addLayout(ui_.blocklistSectionLayout);
    cr->update();

    connect(ui_.updateBlocklistButton, &QAbstractButton::clicked, this, &PrefsDialog::onUpdateBlocklistClicked);

    updateBlocklistLabel();
}

// ---

void PrefsDialog::initSeedingTab()
{
    ui_.doneSeedingScriptButton->setMode(PathButton::FileMode);
    ui_.doneSeedingScriptButton->setTitle(tr("Select \"Torrent Done Seeding\" Script"));

    initWidget(ui_.ratioLimitCheck, TR_KEY_seed_ratio_limited);
    initWidget(ui_.ratioLimitSpin, TR_KEY_seed_ratio_limit);
    initWidget(ui_.idleLimitCheck, TR_KEY_idle_seeding_limit_enabled);
    initWidget(ui_.idleLimitSpin, TR_KEY_idle_seeding_limit);
    initWidget(ui_.doneSeedingScriptCheck, TR_KEY_script_torrent_done_seeding_enabled);
    initWidget(ui_.doneSeedingScriptButton, TR_KEY_script_torrent_done_seeding_filename);
    initWidget(ui_.doneSeedingScriptEdit, TR_KEY_script_torrent_done_seeding_filename);
    bindEnabled({ ui_.ratioLimitSpin }, { TR_KEY_seed_ratio_limited });
    bindEnabled({ ui_.idleLimitSpin }, { TR_KEY_idle_seeding_limit_enabled });
    bindEnabled({ ui_.doneSeedingScriptStack }, { TR_KEY_script_torrent_done_seeding_enabled });

    auto const update_idle_limit_format = [this]() {
        //: Spin box format, "Stop seeding if idle for: [ 5 minutes ]"
        auto const* const units_format = QT_TRANSLATE_N_NOOP("PrefsDialog", "{minutes:L} minute(s)");
        Utils::updateSpinBoxFormat(ui_.idleLimitSpin, "PrefsDialog", units_format, "minutes");
    };
    connect(ui_.idleLimitSpin, qOverload<int>(&QSpinBox::valueChanged), this, update_idle_limit_format);
    update_idle_limit_format();
}

void PrefsDialog::initDownloadingTab()
{
    ui_.watchDirButton->setMode(PathButton::DirectoryMode);
    ui_.downloadDirButton->setMode(PathButton::DirectoryMode);
    ui_.incompleteDirButton->setMode(PathButton::DirectoryMode);
    ui_.doneDownloadingScriptButton->setMode(PathButton::FileMode);

    ui_.watchDirButton->setTitle(tr("Select Watch Directory"));
    ui_.downloadDirButton->setTitle(tr("Select Destination Folder"));
    ui_.incompleteDirButton->setTitle(tr("Select Incomplete Directory"));
    ui_.doneDownloadingScriptButton->setTitle(tr("Select \"Torrent Done Downloading\" Script"));

    ui_.watchDirStack->setMinimumWidth(200);

    ui_.downloadDirFreeSpaceLabel->setSession(session_);

    initWidget(ui_.watchDirCheck, TR_KEY_watch_dir_enabled);
    initWidget(ui_.watchDirButton, TR_KEY_watch_dir);
    initWidget(ui_.watchDirEdit, TR_KEY_watch_dir);
    initWidget(ui_.showTorrentOptionsDialogCheck, TR_KEY_show_options_window);
    initWidget(ui_.startAddedTorrentsCheck, TR_KEY_start_added_torrents);
    initWidget(ui_.detectTorrentFromClipboard, TR_KEY_read_clipboard);
    initWidget(ui_.trashTorrentFileCheck, TR_KEY_trash_original_torrent_files);
    initWidget(ui_.downloadDirButton, TR_KEY_download_dir);
    initWidget(ui_.downloadDirEdit, TR_KEY_download_dir);
    initWidget(ui_.downloadDirFreeSpaceLabel, TR_KEY_download_dir);
    initWidget(ui_.downloadQueueSizeSpin, TR_KEY_download_queue_size);
    initWidget(ui_.queueStalledMinutesSpin, TR_KEY_queue_stalled_minutes);
    initWidget(ui_.renamePartialFilesCheck, TR_KEY_rename_partial_files);
    initWidget(ui_.incompleteDirCheck, TR_KEY_incomplete_dir_enabled);
    initWidget(ui_.incompleteDirButton, TR_KEY_incomplete_dir);
    initWidget(ui_.incompleteDirEdit, TR_KEY_incomplete_dir);
    initWidget(ui_.doneDownloadingScriptCheck, TR_KEY_script_torrent_done_enabled);
    initWidget(ui_.doneDownloadingScriptButton, TR_KEY_script_torrent_done_filename);
    initWidget(ui_.doneDownloadingScriptEdit, TR_KEY_script_torrent_done_filename);
    initWidget(ui_.downloadDoneVerifyCheck, TR_KEY_torrent_complete_verify_enabled);
    bindEnabled({ ui_.watchDirStack }, { TR_KEY_watch_dir_enabled });
    bindEnabled({ ui_.incompleteDirStack }, { TR_KEY_incomplete_dir_enabled });
    bindEnabled({ ui_.doneDownloadingScriptStack }, { TR_KEY_script_torrent_done_enabled });

    auto* cr = new ColumnResizer{ this };
    cr->addLayout(ui_.addingSectionLayout);
    cr->addLayout(ui_.downloadQueueSectionLayout);
    cr->addLayout(ui_.incompleteSectionLayout);
    cr->update();

    auto const update_queue_stalled_format = [this]() {
        //: Spin box format, "Download is inactive if data sharing stopped: [ 5 minutes ago ]"
        auto const* const units_format = QT_TRANSLATE_N_NOOP("PrefsDialog", "{minutes_ago:L} minute(s) ago");
        Utils::updateSpinBoxFormat(ui_.queueStalledMinutesSpin, "PrefsDialog", units_format, "minutes_ago");
    };
    connect(ui_.queueStalledMinutesSpin, qOverload<int>(&QSpinBox::valueChanged), this, update_queue_stalled_format);
    update_queue_stalled_format();
}

void PrefsDialog::updateLocality()
{
    // Path pickers browse this machine, so a remote session's paths get text fields.
    // The watch dir gets one too, even though it is always on this machine.
    auto const show_editor =
        [is_local = is_local_fs_](QStackedWidget* const stack, QWidget* const button, QWidget* const edit) {
            stack->setCurrentWidget(is_local ? button : edit);
            stack->setFixedHeight(stack->currentWidget()->sizeHint().height());
        };
    show_editor(ui_.watchDirStack, ui_.watchDirButton, ui_.watchDirEdit);
    show_editor(ui_.downloadDirStack, ui_.downloadDirButton, ui_.downloadDirEdit);
    show_editor(ui_.incompleteDirStack, ui_.incompleteDirButton, ui_.incompleteDirEdit);
    show_editor(ui_.doneDownloadingScriptStack, ui_.doneDownloadingScriptButton, ui_.doneDownloadingScriptEdit);
    show_editor(ui_.doneSeedingScriptStack, ui_.doneSeedingScriptButton, ui_.doneSeedingScriptEdit);
    ui_.downloadDirLabel->setBuddy(ui_.downloadDirStack->currentWidget());

    // inhibiting sleep only makes sense when the session runs on this machine
    ui_.preventSleepCheck->setVisible(is_local_fs_);
}

// ---

PrefsDialog::PrefsDialog(Session& session, Prefs& prefs, QWidget* const parent)
    : BaseDialog{ parent }
    , session_{ session }
    , prefs_{ prefs }
    , is_local_fs_{ session_.is_local_filesystem() }
{
    ui_.setupUi(this);
    setWindowTitle(Utils::withAppName(windowTitle()));

    initSpeedTab();
    initDownloadingTab();
    initSeedingTab();
    initPrivacyTab();
    initNetworkTab();
    initDesktopTab();
    initRemoteTab();

    updateLocality();

    connect(&session_, &Session::sessionUpdated, this, &PrefsDialog::sessionUpdated);
    // stop() clears the blocklist info, which the next session reports anew.
    connect(&session_, &Session::stopped, this, &PrefsDialog::updateBlocklistLabel);
    // The session also updates the blocklist on its own schedule.
    // setVisible() runs this timer while the dialog is shown.
    blocklist_info_timer_.setInterval(BlocklistInfoRefreshInterval);
    connect(&blocklist_info_timer_, &QTimer::timeout, &session_, &Session::refreshBlocklistInfo);

    adjustSize();
}

PrefsDialog::~PrefsDialog()
{
    // Hide here, not in ~QDialog: hiding takes focus from the dialog,
    // which runs focus-out handlers that save prefs through this class's members.
    hide();
}

void PrefsDialog::setVisible(bool const visible)
{
    blocklist_info_timer_.stop();

    if (visible) {
        session_.refreshBlocklistInfo();
        blocklist_info_timer_.start();
    }

    BaseDialog::setVisible(visible);
}

// ---

void PrefsDialog::sessionUpdated()
{
    if (auto const is_local_fs = session_.is_local_filesystem(); is_local_fs_ != is_local_fs) {
        is_local_fs_ = is_local_fs;
        updateLocality();
    }

    updateBlocklistLabel();
}

void PrefsDialog::updateBlocklistLabel()
{
    // Each label stays hidden until the session reports its value.
    // PrefsDialog.ui italicizes both labels and sets them to plain text,
    // so their msgids carry no markup and other clients can share them.
    auto const rule_count = session_.blocklist_size();
    ui_.blocklistStatusLabel->setVisible(rule_count.has_value());
    if (rule_count) {
        ui_.blocklistStatusLabel->setText(
            TR_FORMAT_N("Blocklist contains {count:L} rule(s)", static_cast<int>(*rule_count), fmt::arg("count", *rule_count)));
    }

    auto const updated_at = session_.blocklist_date();
    ui_.blocklistDateLabel->setVisible(updated_at.has_value());
    if (updated_at) {
        auto const secs = updated_at->time_since_epoch().count();
        auto const date_text = secs == 0 ? tr("Blocklist has never been updated") :
                                           TR_FORMAT(
                                               "Blocklist last updated {date}",
                                               fmt::arg("date", QDateTime::fromSecsSinceEpoch(secs).toString(Qt::TextDate)));
        ui_.blocklistDateLabel->setText(date_text);
    }
}

void PrefsDialog::refreshPref(tr_quark const key)
{
    for (auto [iter, end] = updaters_.equal_range(key); iter != end; ++iter) {
        iter->second();
    }
}
