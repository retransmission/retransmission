// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#include "Session.h"

#include <string_view>
#include <utility>

#include <QtGui/QClipboard>
#include <QtGui/QDesktopServices>

#include <QtWidgets/QApplication>
#include <QtWidgets/QMessageBox>
#include <QtWidgets/QStyle>

#include "Prefs.h"
#include "TrFormat.h"
#include "Utils.h"

/***
****
***/

void Session::copyMagnetLinkToClipboard(int const torrent_id)
{
    fetch_magnet_link(torrent_id, [](std::string_view const link) {
        QApplication::clipboard()->setText(Utils::qstringFromUtf8(link));
    });
}

/***
****
***/

Session::Session(QString const& config_dir, Prefs& prefs, RpcClient& rpc)
    : tr::app::Session{ config_dir.toStdString(), prefs, rpc.impl(), Utils::trashOrRemove }
{
    rename_failed_connection_ = torrent_rename_failed.connect_scoped(&Session::onRenameFailed, this);
    add_failed_connection_ = torrent_add_failed.connect_scoped(&Session::onAddFailed, this);
    add_duplicate_connection_ = torrent_add_duplicate.connect_scoped(&Session::onAddDuplicate, this);
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

void Session::onAddFailed(tr::app::AddData const& add, std::string_view const errmsg) const
{
    auto const text = QStringLiteral("<p><b>%1</b></p><p>%2</p>")
                          .arg(Utils::qstringFromUtf8(errmsg), Utils::qstringFromUtf8(add.readable_name()));
    showMessage(QMessageBox::Warning, tr("Error Adding Torrent"), text);
}

void Session::onAddDuplicate(tr::app::AddData const& add, std::string_view const hash_string)
{
    duplicates_.try_emplace(Utils::qstringFromUtf8(add.readable_short_name()), Utils::qstringFromUtf8(hash_string));
    duplicates_timer_.start(1000);
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

void Session::addNewlyCreatedTorrent(QString const& filename, QString const& local_path)
{
    auto options = tr::app::AddOptions{};
    options.download_dir = local_path.toStdString();
    add_torrent(tr::app::AddData{ filename.toStdString() }, std::move(options));
}

/***
****
***/

void Session::launchWebInterface() const
{
    QDesktopServices::openUrl(QUrl{ Utils::qstringFromUtf8(web_client_url()) });
}
