// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <sigslot/signal.hpp>

#include <QtCore/QObject>
#include <QtCore/QString>
#include <QtCore/QUrl>

#include <QtWidgets/QMessageBox>

#include <libtransmission-app/session.h>

#include "Prefs.h"
#include "RpcClient.h"

class Session
    : public QObject
    , public tr::app::Session
{
    Q_OBJECT

public:
    Session(QString const& config_dir, Prefs& prefs, RpcClient& rpc);
    Session(Session&&) = delete;
    Session(Session const&) = delete;
    Session& operator=(Session&&) = delete;
    Session& operator=(Session const&) = delete;
    ~Session() override;

    [[nodiscard]] QUrl getRemoteUrl() const;

    // From here on, a failure shows no message box: the app has left the screen,
    // and a reply that `stop()` cut short fails only because the session went away.
    void setShuttingDown() noexcept
    {
        is_shutting_down_ = true;
    }

    [[nodiscard]] constexpr bool isShuttingDown() const noexcept
    {
        return is_shutting_down_;
    }

    void copyMagnetLinkToClipboard(int torrent_id);

public slots:
    void launchWebInterface() const;

signals:
    void dataReadProgress();
    void dataSendProgress();
    void networkResponse(bool is_network_error, QString const& message);
    void httpAuthenticationRequired();

private:
    void onRenameFailed(RenameError const& error) const;
    void onAddReport(AddReport const& report) const;
    void showMessage(QMessageBox::Icon icon, QString const& title, QString const& text, QString const& detail = {}) const;

    bool is_shutting_down_ = false;

    sigslot::scoped_connection rename_failed_connection_;
    sigslot::scoped_connection add_report_connection_;
};
