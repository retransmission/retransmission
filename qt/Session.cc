// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#include <algorithm>
#include <array>
#include <cassert>
#include <ranges>
#include <string_view>
#include <utility>

#include <QApplication>
#include <QByteArray>
#include <QClipboard>
#include <QCoreApplication>
#include <QDebug>
#include <QDesktopServices>
#include <QFile>
#include <QFileInfo>
#include <QMessageBox>
#include <QStyle>
#include <QTextStream>
#include <QtDebug>

#include <small/vector.hpp>

#include <libtransmission/transmission.h>

#include <libtransmission/quark.h>
#include <libtransmission/serializer.h>
#include <libtransmission/session-id.h>
#include <libtransmission/utils.h>
#include <libtransmission/variant.h>

#include "Session.h"

#include "AddData.h"
#include "Filters.h"
#include "Prefs.h"
#include "SessionDialog.h"
#include "Torrent.h"
#include "UserMetaType.h"
#include "Utils.h"
#include "VariantHelpers.h"

using namespace std::literals;

using ::trqt::variant_helpers::dictFind;

/***
****
***/

void Session::sessionSet(tr_quark const key, tr_variant val)
{
    auto params = tr_variant::Map{ 1U };
    params.insert_or_assign(key, std::move(val));
    exec(TR_KEY_session_set, std::move(params));
}

void Session::portTest(Session::PortTestIpProtocol const ip_protocol)
{
    static auto constexpr IpStr = std::array{ "ipv4"sv, "ipv6"sv };

    if (portTestPending(ip_protocol)) {
        return;
    }
    port_test_pending_[ip_protocol] = true;

    auto args = tr_variant::make_map(1U);
    tr_variantDictAddStrView(&args, TR_KEY_ip_protocol, IpStr[ip_protocol]);

    auto const response_func = [this, ip_protocol](RpcResponse const& r) {
        port_test_pending_[ip_protocol] = false;

        // If for whatever reason the status optional is empty here,
        // then something must have gone wrong with the port test,
        // so the UI should show the "error" state
        emit portTested(dictFind<bool>(r.args.get(), TR_KEY_port_is_open), ip_protocol);
    };

    auto q = tr::app::RpcQueue::make();

    q->add([this, &args](RpcClient::ResponseFunc done) { exec(TR_KEY_port_test, &args, std::move(done)); }, response_func);

    q->add(response_func);

    q->run();
}

bool Session::portTestPending(Session::PortTestIpProtocol const ip_protocol) const noexcept
{
    return ip_protocol < NUM_PORT_TEST_IP_PROTOCOL && port_test_pending_[ip_protocol];
}

void Session::copyMagnetLinkToClipboard(int torrent_id)
{
    auto params = makeParams(TR_KEY_ids, torrent_ids_t{ torrent_id });
    auto fields = tr_variant::Vector{};
    fields.reserve(1U);
    fields.emplace_back(tr_variant::unmanaged_string(TR_KEY_magnet_link));
    params.insert_or_assign(TR_KEY_fields, std::move(fields));

    auto q = tr::app::RpcQueue::make();

    q->add([this, params = std::move(params)](RpcClient::ResponseFunc done) mutable {
        exec(TR_KEY_torrent_get, std::move(params), std::move(done));
    });

    q->add([](RpcResponse const& r) {
        tr_variant* torrents = nullptr;
        if (!tr_variantDictFindList(r.args.get(), TR_KEY_torrents, &torrents)) {
            return;
        }

        if (tr_variant* const child = tr_variantListChild(torrents, 0)) {
            if (auto const link = dictFind<QString>(child, TR_KEY_magnet_link)) {
                QApplication::clipboard()->setText(*link);
            }
        }
    });

    q->run();
}

void Session::updatePref(tr_quark key)
{
    if (Prefs::isCore(key)) {
        switch (key) {
        case TR_KEY_alt_speed_down:
        case TR_KEY_alt_speed_enabled:
        case TR_KEY_alt_speed_time_begin:
        case TR_KEY_alt_speed_time_day:
        case TR_KEY_alt_speed_time_enabled:
        case TR_KEY_alt_speed_time_end:
        case TR_KEY_alt_speed_up:
        case TR_KEY_blocklist_date:
        case TR_KEY_blocklist_enabled:
        case TR_KEY_blocklist_url:
        case TR_KEY_default_trackers:
        case TR_KEY_dht_enabled:
        case TR_KEY_download_queue_enabled:
        case TR_KEY_download_queue_size:
        case TR_KEY_speed_limit_down:
        case TR_KEY_speed_limit_down_enabled:
        case TR_KEY_encryption:
        case TR_KEY_idle_seeding_limit:
        case TR_KEY_idle_seeding_limit_enabled:
        case TR_KEY_incomplete_dir:
        case TR_KEY_incomplete_dir_enabled:
        case TR_KEY_lpd_enabled:
        case TR_KEY_peer_limit_global:
        case TR_KEY_peer_limit_per_torrent:
        case TR_KEY_peer_port:
        case TR_KEY_peer_port_random_on_start:
        case TR_KEY_queue_stalled_minutes:
        case TR_KEY_pex_enabled:
        case TR_KEY_port_forwarding_enabled:
        case TR_KEY_rename_partial_files:
        case TR_KEY_script_torrent_done_enabled:
        case TR_KEY_script_torrent_done_filename:
        case TR_KEY_script_torrent_done_seeding_enabled:
        case TR_KEY_script_torrent_done_seeding_filename:
        case TR_KEY_start_added_torrents:
        case TR_KEY_trash_original_torrent_files:
        case TR_KEY_seed_ratio_limit:
        case TR_KEY_seed_ratio_limited:
        case TR_KEY_speed_limit_up:
        case TR_KEY_speed_limit_up_enabled:
        case TR_KEY_utp_enabled:
            {
                auto [pref_key, pref_val] = prefs_.keyval(key);
                sessionSet(pref_key, std::move(pref_val));
            }
            break;

        case TR_KEY_download_dir:
            {
                auto [pref_key, pref_val] = prefs_.keyval(key);
                sessionSet(pref_key, std::move(pref_val));
            }
            /* this will change the 'freespace' argument, so refresh */
            refreshSessionInfo();
            break;

        case TR_KEY_rpc_authentication_required:
            if (session_ != nullptr) {
                tr_sessionSetRPCPasswordEnabled(session_, prefs_.get<bool>(key));
            }

            break;

        case TR_KEY_rpc_enabled:
            if (session_ != nullptr) {
                tr_sessionSetRPCEnabled(session_, prefs_.get<bool>(key));
            }

            break;

        case TR_KEY_rpc_password:
            if (session_ != nullptr) {
                tr_sessionSetRPCPassword(session_, prefs_.get<QString>(key).toStdString());
            }

            break;

        case TR_KEY_rpc_port:
            if (session_ != nullptr) {
                tr_sessionSetRPCPort(session_, static_cast<uint16_t>(prefs_.get<int>(key)));
            }

            break;

        case TR_KEY_rpc_username:
            if (session_ != nullptr) {
                tr_sessionSetRPCUsername(session_, prefs_.get<QString>(key).toStdString());
            }

            break;

        case TR_KEY_rpc_whitelist_enabled:
            if (session_ != nullptr) {
                tr_sessionSetRPCWhitelistEnabled(session_, prefs_.get<bool>(key));
            }

            break;

        case TR_KEY_rpc_whitelist:
            if (session_ != nullptr) {
                tr_sessionSetRPCWhitelist(session_, prefs_.get<QString>(key).toStdString());
            }

            break;

        default:
            qWarning() << "unhandled pref:" << static_cast<int>(key);
        }
    }
}

/***
****
***/

Session::Session(QString config_dir, Prefs& prefs, RpcClient& rpc)
    : config_dir_{ std::move(config_dir) }
    , prefs_{ prefs }
    , rpc_{ rpc }
{
    connect(&prefs_, qOverload<tr_quark>(&Prefs::changed), this, &Session::updatePref);
    connect(&rpc_, &RpcClient::httpAuthenticationRequired, this, &Session::httpAuthenticationRequired);
    connect(&rpc_, &RpcClient::dataReadProgress, this, &Session::dataReadProgress);
    connect(&rpc_, &RpcClient::dataSendProgress, this, &Session::dataSendProgress);
    connect(&rpc_, &RpcClient::networkResponse, this, &Session::networkResponse);

    duplicates_timer_.setSingleShot(true);
    connect(&duplicates_timer_, &QTimer::timeout, this, &Session::onDuplicatesTimer);
}

Session::~Session()
{
    stop();
}

/***
****
***/

void Session::stop()
{
    rpc_.stop();

    if (session_ != nullptr) {
        tr_sessionClose(session_);
        session_ = nullptr;
    }
}

void Session::restart()
{
    stop();
    start();
}

void Session::start()
{
    if (prefs_.get<bool>(TR_KEY_remote_session_enabled)) {
        QUrl url;
        if (prefs_.get<bool>(TR_KEY_remote_session_https)) {
            url.setScheme(QStringLiteral("https"));
        } else {
            url.setScheme(QStringLiteral("http"));
        }
        url.setHost(prefs_.get<QString>(TR_KEY_remote_session_host));
        url.setPort(prefs_.get<int>(TR_KEY_remote_session_port));

        auto const root_path = prefs_.get<QString>(TR_KEY_remote_session_url_base_path);
        auto const relative_path = TrHttpServerRpcRelativePath;
        url.setPath(root_path + Utils::qstringFromUtf8(relative_path));

        if (prefs_.get<bool>(TR_KEY_remote_session_requires_authentication)) {
            url.setUserName(prefs_.get<QString>(TR_KEY_remote_session_username));
            url.setPassword(prefs_.get<QString>(TR_KEY_remote_session_password));
        }

        rpc_.start(url);
    } else {
        auto config_dir = config_dir_.toStdString();
        auto const settings = tr_sessionLoadSettings(config_dir);
        session_ = tr_sessionInit(config_dir, true, settings);

        rpc_.start(session_);

        auto* const ctor = tr_ctorNew(session_);
        tr_sessionLoadTorrents(session_, ctor);
        tr_ctorFree(ctor);
    }

    emit sourceChanged();
}

// ---

// static
tr_variant Session::getTorrentIdsVariant(torrent_ids_t const& torrent_ids)
{
    if (&torrent_ids == &RecentlyActiveIDs) {
        return tr_variant::unmanaged_string(TR_KEY_recently_active);
    }

    if (!std::empty(torrent_ids)) {
        return tr::serializer::to_variant(torrent_ids);
    }

    return {};
}

Session::Tag Session::torrentSetImpl(tr_variant::Map params)
{
    auto const q = tr::app::RpcQueue::make();
    auto const tag = q->tag();

    q->add([this, params = std::move(params)](RpcClient::ResponseFunc done) mutable {
        rpc_.exec(TR_KEY_torrent_set, std::move(params), std::move(done));
    });
    q->add([this, tag]() { emit sessionCalled(tag); });
    q->set_tolerate_errors();
    q->run();

    return tag;
}

void Session::torrentSetLocation(torrent_ids_t const& torrent_ids, QString const& path, bool do_move)
{
    if (!torrent_ids.empty()) {
        exec(TR_KEY_torrent_set_location, makeParams(TR_KEY_ids, torrent_ids, TR_KEY_location, path, TR_KEY_move, do_move));
    }
}

void Session::torrentRenamePath(torrent_ids_t const& torrent_ids, QString const& oldpath, QString const& newname)
{
    if (torrent_ids.empty()) {
        return;
    }

    auto q = tr::app::RpcQueue::make();
    q->add(
        [this, params = makeParams(TR_KEY_ids, torrent_ids, TR_KEY_path, oldpath, TR_KEY_name, newname)](
            RpcClient::ResponseFunc done) mutable { exec(TR_KEY_torrent_rename_path, std::move(params), std::move(done)); },
        [](RpcResponse const& r) {
            auto const path = dictFind<QString>(r.args.get(), TR_KEY_path).value_or(QStringLiteral("(unknown)"));
            auto const name = dictFind<QString>(r.args.get(), TR_KEY_name).value_or(QStringLiteral("(unknown)"));

            auto* d = new QMessageBox{
                QMessageBox::Information,
                tr("Error Renaming Path"),
                tr(R"(<p><b>Unable to rename "%1" as "%2": %3.</b></p><p>Please correct the errors and try again.</p>)")
                    .arg(path)
                    .arg(name)
                    .arg(Utils::qstringFromUtf8(r.errmsg)),
                QMessageBox::Close,
                QApplication::activeWindow()
            };
            QObject::connect(d, &QMessageBox::rejected, d, &QMessageBox::deleteLater);
            d->show();
        });

    q->add([this, torrent_ids](RpcResponse const& /*r*/) { refreshTorrents(torrent_ids, TorrentProperties::Rename); });

    q->run();
}

namespace
{
using TorrentProperties = Session::TorrentProperties;

[[nodiscard]] small::max_size_vector<tr_quark, 64U> getKeys(TorrentProperties const props)
{
    // IMPORTANT when changing these key sets:
    // 1. `TR_KEY_id` must be in every set
    // 2. When changing `MainInfo` or `MainStats`, update their union in `MainAll`

    // clang-format off: one line per key
    switch (props)
    {
        // unchanging fields needed by the details dialog
        case TorrentProperties::DetailInfo:
            return {
                TR_KEY_comment,
                TR_KEY_creator,
                TR_KEY_date_created,
                TR_KEY_files,
                TR_KEY_id,
                TR_KEY_is_private,
                TR_KEY_labels,
                TR_KEY_piece_count,
                TR_KEY_piece_size,
                TR_KEY_tracker_list,
                TR_KEY_trackers,
            };

        // changing fields needed by the details dialog
        case TorrentProperties::DetailStat:
            return {
                TR_KEY_activity_date,
                TR_KEY_bandwidth_priority,
                TR_KEY_corrupt_ever,
                TR_KEY_desired_available,
                TR_KEY_download_limit,
                TR_KEY_download_limited,
                TR_KEY_downloaded_ever,
                TR_KEY_file_stats,
                TR_KEY_have_unchecked,
                TR_KEY_honors_session_limits,
                TR_KEY_id,
                TR_KEY_peer_limit,
                TR_KEY_peers,
                TR_KEY_seed_idle_limit,
                TR_KEY_seed_idle_mode,
                TR_KEY_start_date,
                TR_KEY_tracker_stats,
                TR_KEY_upload_limit,
                TR_KEY_upload_limited,
            };

        // union of MainInfoKeys + MainStatKeys
        case TorrentProperties::MainAll:
            return {
                TR_KEY_added_date,
                TR_KEY_download_dir,
                TR_KEY_downloaded_ever,
                TR_KEY_edit_date,
                TR_KEY_error,
                TR_KEY_error_string,
                TR_KEY_eta,
                TR_KEY_file_count,
                TR_KEY_hash_string,
                TR_KEY_have_unchecked,
                TR_KEY_have_valid,
                TR_KEY_id,
                TR_KEY_is_finished,
                TR_KEY_labels,
                TR_KEY_left_until_done,
                TR_KEY_manual_announce_time,
                TR_KEY_metadata_percent_complete,
                TR_KEY_name,
                TR_KEY_peers_connected,
                TR_KEY_peers_getting_from_us,
                TR_KEY_peers_sending_to_us,
                TR_KEY_percent_complete,
                TR_KEY_percent_done,
                TR_KEY_primary_mime_type,
                TR_KEY_queue_position,
                TR_KEY_rate_download,
                TR_KEY_rate_upload,
                TR_KEY_recheck_progress,
                TR_KEY_seed_ratio_limit,
                TR_KEY_seed_ratio_mode,
                TR_KEY_size_when_done,
                TR_KEY_status,
                TR_KEY_total_size,
                TR_KEY_trackers,
                TR_KEY_upload_ratio,
                TR_KEY_uploaded_ever,
                TR_KEY_webseeds_sending_to_us,
            };

        // unchanging fields needed by the main window
        case TorrentProperties::MainInfo:
            return {
                TR_KEY_added_date,
                TR_KEY_download_dir,
                TR_KEY_file_count,
                TR_KEY_hash_string,
                TR_KEY_id,
                TR_KEY_labels,
                TR_KEY_name,
                TR_KEY_primary_mime_type,
                TR_KEY_total_size,
                TR_KEY_trackers,
            };

        // changing fields needed by the main window
        case TorrentProperties::MainStats:
            return {
                TR_KEY_downloaded_ever,
                TR_KEY_edit_date,
                TR_KEY_error,
                TR_KEY_error_string,
                TR_KEY_eta,
                TR_KEY_have_unchecked,
                TR_KEY_have_valid,
                TR_KEY_id,
                TR_KEY_is_finished,
                TR_KEY_left_until_done,
                TR_KEY_manual_announce_time,
                TR_KEY_metadata_percent_complete,
                TR_KEY_peers_connected,
                TR_KEY_peers_getting_from_us,
                TR_KEY_peers_sending_to_us,
                TR_KEY_percent_complete,
                TR_KEY_percent_done,
                TR_KEY_queue_position,
                TR_KEY_rate_download,
                TR_KEY_rate_upload,
                TR_KEY_recheck_progress,
                TR_KEY_seed_ratio_limit,
                TR_KEY_seed_ratio_mode,
                TR_KEY_size_when_done,
                TR_KEY_status,
                TR_KEY_upload_ratio,
                TR_KEY_uploaded_ever,
                TR_KEY_webseeds_sending_to_us,
            };

        // keys needed after renaming a torrent
        case TorrentProperties::Rename:
            return {
                TR_KEY_file_stats,
                TR_KEY_files,
                TR_KEY_id,
                TR_KEY_name,
            };
    }
    // clang-format on

    return {};
}
} // namespace

void Session::refreshTorrents(torrent_ids_t const& torrent_ids, TorrentProperties const props)
{
    auto fields = tr_variant::Vector{};
    auto const keys = getKeys(props);
    fields.reserve(std::size(keys));
    std::ranges::transform(keys, std::back_inserter(fields), [](auto key) { return tr_variant::unmanaged_string(key); });

    auto map = tr_variant::Map{ 3U };
    map.try_emplace(TR_KEY_format, tr_variant::unmanaged_string("table"sv));
    map.try_emplace(TR_KEY_fields, std::move(fields));
    addParamPair(map, TR_KEY_ids, torrent_ids);

    auto q = tr::app::RpcQueue::make();

    auto args = tr_variant{ std::move(map) };
    q->add([this, &args](RpcClient::ResponseFunc done) { exec(TR_KEY_torrent_get, &args, std::move(done)); });

    bool const all_torrents = std::empty(torrent_ids);

    q->add([this, all_torrents](RpcResponse const& r) {
        tr_variant* torrents = nullptr;

        if (tr_variantDictFindList(r.args.get(), TR_KEY_torrents, &torrents)) {
            emit torrentsUpdated(torrents, all_torrents);
        }

        if (tr_variantDictFindList(r.args.get(), TR_KEY_removed, &torrents)) {
            emit torrentsRemoved(torrents);
        }
    });

    q->run();
}

void Session::refreshDetailInfo(torrent_ids_t const& ids)
{
    refreshTorrents(ids, TorrentProperties::DetailInfo);
}

void Session::refreshExtraStats(torrent_ids_t const& ids)
{
    refreshTorrents(ids, TorrentProperties::DetailStat);
}

void Session::sendTorrentRequest(tr_quark const method, torrent_ids_t const& torrent_ids)
{
    auto q = tr::app::RpcQueue::make();

    q->add([this, method, params = makeParams(TR_KEY_ids, torrent_ids)](RpcClient::ResponseFunc done) mutable {
        exec(method, std::move(params), std::move(done));
    });

    q->add([this, torrent_ids]() { refreshTorrents(torrent_ids, TorrentProperties::MainStats); });

    q->run();
}

void Session::pauseTorrents(torrent_ids_t const& ids)
{
    sendTorrentRequest(TR_KEY_torrent_stop, ids);
}

void Session::startTorrents(torrent_ids_t const& ids)
{
    sendTorrentRequest(TR_KEY_torrent_start, ids);
}

void Session::startTorrentsNow(torrent_ids_t const& ids)
{
    sendTorrentRequest(TR_KEY_torrent_start_now, ids);
}

void Session::queueMoveTop(torrent_ids_t const& ids)
{
    sendTorrentRequest(TR_KEY_queue_move_top, ids);
}

void Session::queueMoveUp(torrent_ids_t const& ids)
{
    sendTorrentRequest(TR_KEY_queue_move_up, ids);
}

void Session::queueMoveDown(torrent_ids_t const& ids)
{
    sendTorrentRequest(TR_KEY_queue_move_down, ids);
}

void Session::queueMoveBottom(torrent_ids_t const& ids)
{
    sendTorrentRequest(TR_KEY_queue_move_bottom, ids);
}

void Session::refreshActiveTorrents()
{
    // If this object is passed as "ids" (compared by address), then recently active torrents are queried.
    refreshTorrents(RecentlyActiveIDs, TorrentProperties::MainStats);
}

void Session::refreshAllTorrents()
{
    // if an empty ids object is used, all torrents are queried.
    torrent_ids_t const ids = {};
    refreshTorrents(ids, TorrentProperties::MainStats);
}

void Session::initTorrents(torrent_ids_t const& ids)
{
    refreshTorrents(ids, TorrentProperties::MainAll);
}

void Session::refreshSessionStats()
{
    auto q = tr::app::RpcQueue::make();

    q->add([this](RpcClient::ResponseFunc done) { exec(TR_KEY_session_stats, nullptr, std::move(done)); });

    q->add([this](RpcResponse const& r) { updateStats(r.args.get()); });

    q->run();
}

void Session::refreshSessionInfo()
{
    auto q = tr::app::RpcQueue::make();

    q->add([this](RpcClient::ResponseFunc done) { exec(TR_KEY_session_get, nullptr, std::move(done)); });

    q->add([this](RpcResponse const& r) { updateInfo(r.args.get()); });

    q->run();
}

void Session::updateBlocklist()
{
    auto q = tr::app::RpcQueue::make();

    q->add([this](RpcClient::ResponseFunc done) { exec(TR_KEY_blocklist_update, nullptr, std::move(done)); });

    q->add([this](RpcResponse const& r) {
        if (auto const size = dictFind<int>(r.args.get(), TR_KEY_blocklist_size); size) {
            setBlocklistSize(*size);
        }
    });

    q->run();
}

/***
****
***/

void Session::exec(tr_quark method, tr_variant* args, RpcClient::ResponseFunc on_done)
{
    rpc_.exec(method, args, std::move(on_done));
}

void Session::exec(tr_quark method, tr_variant::Map params, RpcClient::ResponseFunc on_done)
{
    rpc_.exec(method, std::move(params), std::move(on_done));
}

void Session::updateStats(tr_variant const& args_dict, tr_session_stats& stats)
{
    static constexpr auto Fields = std::tuple{
        tr::serializer::Field<&tr_session_stats::downloadedBytes>{ TR_KEY_downloaded_bytes },
        tr::serializer::Field<&tr_session_stats::filesAdded>{ TR_KEY_files_added },
        tr::serializer::Field<&tr_session_stats::secondsActive>{ TR_KEY_seconds_active },
        tr::serializer::Field<&tr_session_stats::sessionCount>{ TR_KEY_session_count },
        tr::serializer::Field<&tr_session_stats::uploadedBytes>{ TR_KEY_uploaded_bytes },
    };
    tr::serializer::load(args_dict, stats, Fields);

    stats.ratio = static_cast<float>(tr_getRatio(stats.uploadedBytes, stats.downloadedBytes));
}

void Session::updateStats(tr_variant* dict)
{
    if (tr_variant* var = nullptr; tr_variantDictFindDict(dict, TR_KEY_current_stats, &var)) {
        updateStats(*var, stats_);
    }

    if (tr_variant* var = nullptr; tr_variantDictFindDict(dict, TR_KEY_cumulative_stats, &var)) {
        updateStats(*var, cumulative_stats_);
    }

    emit statsUpdated();
}

void Session::updateInfo(tr_variant* args_dict)
{
    disconnect(&prefs_, qOverload<tr_quark>(&Prefs::changed), this, &Session::updatePref);

    if (auto const* const settings = args_dict->get_if<tr_variant::Map>(); settings != nullptr) {
        for (auto const& [key, value] : *settings) {
            if (!Prefs::isCore(key)) {
                continue;
            }

            prefs_.set(key, value);
        }
    }

    /* Use the C API to get settings that, for security reasons, aren't supported by RPC */
    if (session_ != nullptr) {
        prefs_.set(TR_KEY_rpc_enabled, tr_sessionIsRPCEnabled(session_));
        prefs_.set(TR_KEY_rpc_authentication_required, tr_sessionIsRPCPasswordEnabled(session_));
        prefs_.set(TR_KEY_rpc_password, QString::fromStdString(tr_sessionGetRPCPassword(session_)));
        prefs_.set(TR_KEY_rpc_port, tr_sessionGetRPCPort(session_));
        prefs_.set(TR_KEY_rpc_username, QString::fromStdString(tr_sessionGetRPCUsername(session_)));
        prefs_.set(TR_KEY_rpc_whitelist_enabled, tr_sessionGetRPCWhitelistEnabled(session_));
        prefs_.set(TR_KEY_rpc_whitelist, QString::fromStdString(tr_sessionGetRPCWhitelist(session_)));
    }

    if (auto const size = dictFind<int>(args_dict, TR_KEY_blocklist_size); size && *size != blocklistSize()) {
        setBlocklistSize(*size);
    }

    if (auto const str = dictFind<QString>(args_dict, TR_KEY_version); str) {
        session_version_ = *str;
    }

    if (auto const str = dictFind<QString>(args_dict, TR_KEY_session_id)) {
        session_id_ = *str;
        is_definitely_local_session_ = tr_session_id::is_local(session_id_.toUtf8().constData());
    } else {
        session_id_.clear();
    }

    connect(&prefs_, qOverload<tr_quark>(&Prefs::changed), this, &Session::updatePref);

    emit sessionUpdated();
}

void Session::setBlocklistSize(int64_t i)
{
    blocklist_size_ = i;

    emit blocklistUpdated(i);
}

void Session::addTorrent(AddData const& add_me, tr_variant::Map args_dict)
{
    assert(!args_dict.contains(TR_KEY_filename));
    assert(!args_dict.contains(TR_KEY_metainfo));

    args_dict.try_emplace(TR_KEY_paused, !prefs_.get<bool>(TR_KEY_start_added_torrents));

    switch (add_me.type) {
    case AddData::MAGNET:
        args_dict.insert_or_assign(TR_KEY_filename, add_me.magnet.toStdString());
        break;

    case AddData::URL:
        args_dict.insert_or_assign(TR_KEY_filename, add_me.url.toString().toStdString());
        break;

    case AddData::FILENAME:
        [[fallthrough]];
    case AddData::METAINFO:
        args_dict.insert_or_assign(TR_KEY_metainfo, add_me.toBase64().toStdString());
        break;

    default:
        qWarning() << "Unhandled AddData type: " << add_me.type;
        break;
    }

    auto q = tr::app::RpcQueue::make();

    q->add(
        [this, args_dict = std::move(args_dict)](RpcClient::ResponseFunc done) mutable {
            exec(TR_KEY_torrent_add, std::move(args_dict), std::move(done));
        },
        [add_me](RpcResponse const& r) {
            auto* d = new QMessageBox{
                QMessageBox::Warning,
                tr("Error Adding Torrent"),
                QStringLiteral("<p><b>%1</b></p><p>%2</p>").arg(Utils::qstringFromUtf8(r.errmsg)).arg(add_me.readableName()),
                QMessageBox::Close,
                QApplication::activeWindow()
            };
            QObject::connect(d, &QMessageBox::rejected, d, &QMessageBox::deleteLater);
            d->show();
        });

    q->add([this, add_me](RpcResponse const& r) {
        if (auto const* const args = r.args->get_if<tr_variant::Map>(); args != nullptr) {
            if (args->contains(TR_KEY_torrent_added)) {
                add_me.disposeSourceFile();
            } else if (auto const* const dup = args->find_if<tr_variant::Map>(TR_KEY_torrent_duplicate); dup != nullptr) {
                add_me.disposeSourceFile();

                if (auto const iter = dup->find(TR_KEY_hash_string); iter != dup->end()) {
                    if (auto const hash = iter->second.value_if<std::string_view>()) {
                        duplicates_.try_emplace(add_me.readableShortName(), Utils::qstringFromUtf8(*hash));
                        duplicates_timer_.start(1000);
                    }
                }
            }
        }
    });

    q->run();
}

void Session::onDuplicatesTimer()
{
    decltype(duplicates_) duplicates;
    duplicates.swap(duplicates_);

    QStringList lines;
    for (auto const& [dupe, original] : duplicates) {
        lines.push_back(tr("%1 (copy of %2)").arg(dupe).arg(original.left(7)));
    }

    if (!lines.empty()) {
        lines.sort(Qt::CaseInsensitive);
        // NOLINTNEXTLINE(readability-redundant-casting): Remove this comment when we drop Qt5
        auto const title = tr("Duplicate Torrent(s)", "", static_cast<int>(lines.size()));
        auto const detail = lines.join(QStringLiteral("\n"));
        // NOLINTNEXTLINE(readability-redundant-casting): Remove this comment when we drop Qt5
        auto const detail_text = tr("Unable to add %n duplicate torrent(s)", "", static_cast<int>(lines.size()));
        auto const use_detail = lines.size() > 1;
        auto const text = use_detail ? detail_text : detail;

        auto* d = new QMessageBox{ QMessageBox::Warning, title, text, QMessageBox::Close, QApplication::activeWindow() };
        if (use_detail) {
            d->setDetailedText(detail);
        }

        QObject::connect(d, &QMessageBox::rejected, d, &QMessageBox::deleteLater);
        d->show();
    }
}

void Session::addTorrent(AddData const& add_me)
{
    addTorrent(add_me, tr_variant::Map{ 3U });
}

void Session::addNewlyCreatedTorrent(QString const& filename, QString const& local_path)
{
    QByteArray const b64 = AddData(filename).toBase64();

    exec(
        TR_KEY_torrent_add,
        makeParams(
            TR_KEY_download_dir,
            local_path,
            TR_KEY_paused,
            !prefs_.get<bool>(TR_KEY_start_added_torrents),
            TR_KEY_metainfo,
            b64.toStdString()));
}

void Session::removeTorrents(torrent_ids_t const& ids, bool delete_files)
{
    if (!ids.empty()) {
        exec(TR_KEY_torrent_remove, makeParams(TR_KEY_ids, ids, TR_KEY_delete_local_data, delete_files));
    }
}

void Session::verifyTorrents(torrent_ids_t const& ids)
{
    if (!ids.empty()) {
        exec(TR_KEY_torrent_verify, makeParams(TR_KEY_ids, ids));
    }
}

void Session::reannounceTorrents(torrent_ids_t const& ids)
{
    if (!ids.empty()) {
        exec(TR_KEY_torrent_reannounce, makeParams(TR_KEY_ids, ids));
    }
}

/***
****
***/

void Session::launchWebInterface() const
{
    QUrl url;

    if (session_ == nullptr) // remote session
    {
        url = rpc_.url();

        auto const root_path = prefs_.get<QString>(TR_KEY_remote_session_url_base_path);
        auto const relative_path = TrHttpServerWebRelativePath;
        url.setPath(root_path + Utils::qstringFromUtf8(relative_path));
    } else // local session
    {
        url.setScheme(QStringLiteral("http"));
        url.setHost(QStringLiteral("localhost"));
        url.setPort(prefs_.get<int>(TR_KEY_rpc_port));
    }

    QDesktopServices::openUrl(url);
}
