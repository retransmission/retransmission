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
    add_report_connection_ = torrent_add_report.connect_scoped(&Session::onAddReport, this);
    connect(&rpc, &RpcClient::httpAuthenticationRequired, this, &Session::httpAuthenticationRequired);
    connect(&rpc, &RpcClient::dataReadProgress, this, &Session::dataReadProgress);
    connect(&rpc, &RpcClient::dataSendProgress, this, &Session::dataSendProgress);
    connect(&rpc, &RpcClient::networkResponse, this, &Session::networkResponse);
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

void Session::onAddReport(AddReport const& report) const
{
    // One box per list. A lone entry is the box's text; several are a count, with the entries as its details.

    if (auto const& failed = report.failed; std::size(failed) == 1U) {
        auto const text = QStringLiteral("<p><b>%1</b></p><p>%2</p>")
                              .arg(Utils::qstringFromUtf8(failed.front().errmsg), Utils::qstringFromUtf8(failed.front().name));
        showMessage(QMessageBox::Warning, tr("Error Adding Torrent"), text);
    } else if (!std::empty(failed)) {
        auto lines = QStringList{};
        for (auto const& failure : failed) {
            lines.push_back(Utils::qstringFromUtf8(failure.name));
            lines.push_back(QStringLiteral("    ") + Utils::qstringFromUtf8(failure.errmsg));
        }

        auto const count = static_cast<int>(std::size(failed));
        auto const text = TR_FORMAT_N("Unable to add {count:L} torrent(s)", count, fmt::arg("count", count));
        showMessage(QMessageBox::Warning, tr("Error Adding Torrent"), text, lines.join(QStringLiteral("\n")));
    }

    auto lines = QStringList{};
    for (auto const& duplicate : report.duplicates) {
        lines.push_back(TR_FORMAT(
            "{torrent_name} (copy of {hash})",
            fmt::arg("torrent_name", duplicate.name),
            fmt::arg("hash", duplicate.short_hash())));
    }

    if (!lines.empty()) {
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
