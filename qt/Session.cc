// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#include "Session.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <string_view>
#include <utility>

#include <QtCore/QByteArray>
#include <QtCore/QCoreApplication>
#include <QtCore/QDebug>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QtDebug>
#include <QtCore/QTextStream>

#include <QtGui/QClipboard>
#include <QtGui/QDesktopServices>

#include <QtWidgets/QApplication>
#include <QtWidgets/QMessageBox>
#include <QtWidgets/QStyle>

#include <libtransmission/log.h>
#include <libtransmission/quark.h>
#include <libtransmission/serializer.h>
#include <libtransmission/torrent-builder.h>
#include <libtransmission/transmission.h>
#include <libtransmission/variant.h>

#include <libtransmission-app/rpc-queue.h>

#include "AddData.h"
#include "Prefs.h"
#include "SessionDialog.h"
#include "TrFormat.h"
#include "Utils.h"
#include "VariantHelpers.h"

using namespace std::literals;

using tr::app::RpcQueue;
using ::trqt::variant_helpers::dictFind;

/***
****
***/

void Session::portTest(tr_address_type const ip_protocol)
{
    static auto constexpr IpStr = std::array{ "ipv4"sv, "ipv6"sv };

    if (portTestPending(ip_protocol)) {
        return;
    }
    port_test_pending_[ip_protocol] = true;

    auto args = tr_variant::Map{ 1U };
    args.insert_or_assign(TR_KEY_ip_protocol, tr_variant::unmanaged_string(IpStr[ip_protocol]));

    auto const response_func = [this, ip_protocol](RpcResponse const& r) {
        port_test_pending_[ip_protocol] = false;

        // The status optional can be empty here, e.g. on network errors,
        // and the UI should show the "error" state
        emit portTested(dictFind<bool>(r.args.get(), TR_KEY_port_is_open), ip_protocol);
    };

    RpcQueue::create()
        .add(
            [this, args = std::move(args)](RpcClient::ResponseFunc done) mutable {
                exec(TR_KEY_port_test, std::move(args), std::move(done));
            },
            response_func)
        .add(response_func)
        .run();
}

bool Session::portTestPending(tr_address_type const ip_protocol) const noexcept
{
    return port_test_pending_[ip_protocol];
}

void Session::copyMagnetLinkToClipboard(int torrent_id)
{
    auto params = tr_variant::Map{ 2U };
    params.insert_or_assign(TR_KEY_ids, tr::serializer::to_variant(Ids{ torrent_id }));
    params.insert_or_assign(TR_KEY_fields, tr::app::make_fields({ TR_KEY_magnet_link }));

    RpcQueue::create()
        .add([this, params = std::move(params)](RpcClient::ResponseFunc done) mutable {
            exec(TR_KEY_torrent_get, std::move(params), std::move(done));
        })
        .add([](RpcResponse const& r) {
            auto const* const args = r.args->get_if<tr_variant::Map>();
            auto const* const torrents = args != nullptr ? args->find_if<tr_variant::Vector>(TR_KEY_torrents) : nullptr;
            if (torrents == nullptr || std::empty(*torrents)) {
                return;
            }

            if (auto const link = dictFind<QString>(&torrents->front(), TR_KEY_magnet_link)) {
                QApplication::clipboard()->setText(*link);
            }
        })
        .run();
}

/***
****
***/

Session::Session(QString config_dir, Prefs& prefs, RpcClient& rpc)
    : tr::app::Session{ prefs, rpc.impl() }
    , config_dir_{ std::move(config_dir) }
    , prefs_{ prefs }
    , rpc_{ rpc }
{
    session_refresh_tag_ = session_refresh_needed.connect_scoped([this] { refresh_session_info(); });
    rename_failed_connection_ = torrent_rename_failed.connect_scoped(&Session::onRenameFailed, this);
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
        set_embedded_session(nullptr);
        tr_sessionClose(session_);
        session_ = nullptr;
    }

    clear_session_info();
    set_session_type({});
    emit stopped();
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
        session_ = tr_sessionInit(config_dir, true, settings, Utils::trashOrRemove);
        set_embedded_session(session_);

        if (tr_sessionConfigDirIsContended(session_)) {
            emit configDirContended();
            return;
        }

        rpc_.start(session_);

        auto builder = tr_torrent_builder{ session_ };
        tr_sessionLoadTorrents(session_, &builder);
    }

    emit sourceChanged();
}

// ---

void Session::onRenameFailed(RenameError const& error) const
{
    auto const title = tr("Error Renaming Path");
    auto const summary = TR_FORMAT(
        R"(Unable to rename "{old_path}" as "{path}": {error}.)",
        fmt::arg("old_path", Utils::qstringFromUtf8(error.old_path)),
        fmt::arg("path", Utils::qstringFromUtf8(error.new_name)),
        fmt::arg("error", Utils::qstringFromUtf8(error.errmsg)));
    auto const advice = error.network_error ? QString{} :
                                              QStringLiteral("<p>%1</p>").arg(tr("Please correct the errors and try again."));
    auto const text = QStringLiteral("<p><b>%1</b></p>%2").arg(summary, advice);
    auto* d = new QMessageBox{ QMessageBox::Information, title, text, QMessageBox::Close, QApplication::activeWindow() };
    QObject::connect(d, &QMessageBox::rejected, d, &QMessageBox::deleteLater);
    d->show();
}

void Session::refreshActiveTorrents()
{
    refresh_active_torrents();
}

void Session::refreshAllTorrents()
{
    refresh_all_torrents();
}

void Session::initTorrents()
{
    refresh_all_torrents(tr::app::TorrentKeySet::MainAll);
}

// Asks for the blocklist fields only.
// A full session_get reply could overwrite prefs that the user changed while it was in flight.
void Session::refreshBlocklistInfo()
{
    auto fields = tr_variant::Vector{};
    fields.reserve(2U);
    fields.emplace_back(tr_variant::unmanaged_string(TR_KEY_blocklist_date));
    fields.emplace_back(tr_variant::unmanaged_string(TR_KEY_blocklist_size));
    auto params = tr_variant::Map{ 1U };
    params.try_emplace(TR_KEY_fields, std::move(fields));

    RpcQueue::create()
        .add([this, params = std::move(params)](RpcClient::ResponseFunc done) mutable {
            exec(TR_KEY_session_get, std::move(params), std::move(done));
        })
        .add([this](RpcResponse const& r) {
            // not import_session_info(), which reads the session type from the session id this reply lacks
            if (auto const* const map = r.args->get_if<tr_variant::Map>()) {
                import_session_settings(*map);
                session_updated();
            }
        })
        .run();
}

void Session::updateBlocklist()
{
    RpcQueue::create()
        .add(
            [this](RpcClient::ResponseFunc done) { exec(TR_KEY_blocklist_update, nullptr, std::move(done)); },
            // A JSON-RPC or network error aborts the queue before the step below, so
            // report the failure here or the progress dialog is left with no result.
            [this](RpcResponse const& r) { emit blocklistUpdateFailed(QString::fromStdString(r.errmsg)); })
        .add([this](RpcResponse const& r) {
            // session_get reports the new size and date with the rest of the session info
            refresh_session_info();

            // Only this manual update announces success, not every session_get refresh.
            if (auto const* const map = r.args->get_if<tr_variant::Map>()) {
                emit blocklistUpdated(map->value_if<int64_t>(TR_KEY_blocklist_size).value_or(0));
            }
        })
        .run();
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

    RpcQueue::create()
        .add(
            [this, args_dict = std::move(args_dict)](RpcClient::ResponseFunc done) mutable {
                exec(TR_KEY_torrent_add, std::move(args_dict), std::move(done));
            },
            [add_me](RpcResponse const& r) {
                auto const title = tr("Error Adding Torrent");
                auto const text = QStringLiteral("<p><b>%1</b></p><p>%2</p>")
                                      .arg(Utils::qstringFromUtf8(r.errmsg), add_me.readableName());
                auto* d = new QMessageBox{ QMessageBox::Warning,
                                           title,
                                           text,
                                           QMessageBox::Close,
                                           QApplication::activeWindow() };
                QObject::connect(d, &QMessageBox::rejected, d, &QMessageBox::deleteLater);
                d->show();
            })
        .add([this, add_me](RpcResponse const& r) {
            if (auto const* const args = r.args->get_if<tr_variant::Map>()) {
                if (args->contains(TR_KEY_torrent_added)) {
                    add_me.disposeSourceFile();
                } else if (auto const* const dup = args->find_if<tr_variant::Map>(TR_KEY_torrent_duplicate)) {
                    add_me.disposeSourceFile();

                    if (auto const iter = dup->find(TR_KEY_hash_string); iter != dup->end()) {
                        if (auto const hash = iter->second.value_if<std::string_view>()) {
                            duplicates_.try_emplace(add_me.readableShortName(), Utils::qstringFromUtf8(*hash));
                            duplicates_timer_.start(1000);
                        }
                    }
                }
            }
        })
        .run();
}

void Session::onDuplicatesTimer()
{
    decltype(duplicates_) duplicates;
    duplicates.swap(duplicates_);

    QStringList lines;
    for (auto const& [dupe, original] : duplicates) {
        lines.push_back(
            TR_FORMAT("{torrent_name} (copy of {hash})", fmt::arg("torrent_name", dupe), fmt::arg("hash", original.left(7))));
    }

    if (!lines.empty()) {
        lines.sort(Qt::CaseInsensitive);
        // NOLINTNEXTLINE(readability-redundant-casting): Remove this comment when we drop Qt5
        auto const count = static_cast<int>(lines.size());
        auto const title = tr("Duplicate Torrent(s)", "", count);
        auto const detail = lines.join(QStringLiteral("\n"));
        auto const detail_text = TR_FORMAT_N("Unable to add {count:L} duplicate torrent(s)", count, fmt::arg("count", count));
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
    auto params = tr_variant::Map{ 3U };
    params.insert_or_assign(TR_KEY_download_dir, local_path.toStdString());
    params.insert_or_assign(TR_KEY_paused, !prefs_.get<bool>(TR_KEY_start_added_torrents));
    params.insert_or_assign(TR_KEY_metainfo, AddData(filename).toBase64().toStdString());
    exec(TR_KEY_torrent_add, std::move(params));
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
    } else // embedded session
    {
        url.setScheme(QStringLiteral("http"));
        url.setHost(QStringLiteral("localhost"));
        url.setPort(prefs_.get<int>(TR_KEY_rpc_port));
    }

    QDesktopServices::openUrl(url);
}
