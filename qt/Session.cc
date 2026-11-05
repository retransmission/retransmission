// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#include "Session.h"

#include <cassert>
#include <optional>
#include <string_view>
#include <utility>

#include <QtCore/QDebug>
#include <QtCore/QtDebug>

#include <QtGui/QClipboard>
#include <QtGui/QDesktopServices>

#include <QtWidgets/QApplication>
#include <QtWidgets/QMessageBox>
#include <QtWidgets/QStyle>

#include <libtransmission/quark.h>
#include <libtransmission/serializer.h>
#include <libtransmission/variant.h>

#include <libtransmission-app/rpc-queue.h>

#include "AddData.h"
#include "Prefs.h"
#include "TrFormat.h"
#include "Utils.h"

using tr::app::RpcQueue;

/***
****
***/

void Session::copyMagnetLinkToClipboard(int torrent_id)
{
    auto params = tr_variant::Map{ 2U };
    params.insert_or_assign(TR_KEY_ids, tr::serializer::to_variant(Ids{ torrent_id }));
    params.insert_or_assign(TR_KEY_fields, tr::app::make_fields({ TR_KEY_magnet_link }));

    RpcQueue::create()
        .add([this, params = std::move(params)](RpcClient::ResponseFunc done) mutable {
            send(TR_KEY_torrent_get, std::move(params), std::move(done));
        })
        .add([](RpcResponse const& r) {
            auto const* const args = r.args_map();
            auto const* const torrents = args != nullptr ? args->find_if<tr_variant::Vector>(TR_KEY_torrents) : nullptr;
            auto const* const row = torrents != nullptr && !std::empty(*torrents) ?
                torrents->front().get_if<tr_variant::Map>() :
                nullptr;
            auto const link = row != nullptr ? row->value_if<std::string_view>(TR_KEY_magnet_link) :
                                               std::optional<std::string_view>{};
            if (link) {
                QApplication::clipboard()->setText(Utils::qstringFromUtf8(*link));
            }
        })
        .run();
}

/***
****
***/

Session::Session(QString const& config_dir, Prefs& prefs, RpcClient& rpc)
    : tr::app::Session{ config_dir.toStdString(), prefs, rpc.impl(), Utils::trashOrRemove }
    , prefs_{ prefs }
{
    rename_failed_connection_ = torrent_rename_failed.connect_scoped(&Session::onRenameFailed, this);
    connect(&rpc, &RpcClient::httpAuthenticationRequired, this, &Session::httpAuthenticationRequired);
    connect(&rpc, &RpcClient::dataReadProgress, this, &Session::dataReadProgress);
    connect(&rpc, &RpcClient::dataSendProgress, this, &Session::dataSendProgress);
    connect(&rpc, &RpcClient::networkResponse, this, &Session::networkResponse);

    duplicates_timer_.setSingleShot(true);
    connect(&duplicates_timer_, &QTimer::timeout, this, &Session::onDuplicatesTimer);
}

Session::~Session() = default;

QUrl Session::getRemoteUrl() const
{
    return QUrl{ Utils::qstringFromUtf8(remote_url()) };
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
    showMessage(QMessageBox::Information, title, text);
}

void Session::showMessage(QMessageBox::Icon const icon, QString const& title, QString const& text, QString const& detail) const
{
    if (is_shutting_down_) {
        return;
    }

    auto* const d = new QMessageBox{ icon, title, text, QMessageBox::Close, QApplication::activeWindow() };
    d->setDetailedText(detail);
    QObject::connect(d, &QMessageBox::rejected, d, &QMessageBox::deleteLater);
    d->show();
}

/***
****
***/

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
                send(TR_KEY_torrent_add, std::move(args_dict), std::move(done));
            },
            [this, add_me](RpcResponse const& r) {
                auto const text = QStringLiteral("<p><b>%1</b></p><p>%2</p>")
                                      .arg(Utils::qstringFromUtf8(r.errmsg), add_me.readableName());
                showMessage(QMessageBox::Warning, tr("Error Adding Torrent"), text);
            })
        .add([this, add_me](RpcResponse const& r) {
            if (auto const* const args = r.args_map()) {
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

        showMessage(QMessageBox::Warning, title, text, use_detail ? detail : QString{});
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
    send(TR_KEY_torrent_add, std::move(params));
}

/***
****
***/

void Session::launchWebInterface() const
{
    QDesktopServices::openUrl(QUrl{ Utils::qstringFromUtf8(web_client_url()) });
}
