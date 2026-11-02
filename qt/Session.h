// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <array>
#include <map>
#include <optional>

#include <sigslot/signal.hpp>

#include <QtCore/QObject>
#include <QtCore/QString>
#include <QtCore/QTimer>

#include <libtransmission/converters.h>
#include <libtransmission/quark.h>
#include <libtransmission/types.h>
#include <libtransmission/variant.h>

#include <libtransmission-app/session.h>

#include "Prefs.h"
#include "RpcClient.h"

class AddData;

struct tr_variant;

class Session
    : public QObject
    , public tr::app::Session
{
    Q_OBJECT

public:
    Session(QString config_dir, Prefs& prefs, RpcClient& rpc);
    Session(Session&&) = delete;
    Session(Session const&) = delete;
    Session& operator=(Session&&) = delete;
    Session& operator=(Session const&) = delete;
    ~Session() override;

    void stop();
    void restart();

    [[nodiscard]] constexpr auto const& getRemoteUrl() const noexcept
    {
        return rpc_.url();
    }

    void updateBlocklist();
    void portTest(tr_address_type ip_protocol);
    void copyMagnetLinkToClipboard(int torrent_id);

    [[nodiscard]] bool portTestPending(tr_address_type ip_protocol) const noexcept;

    void exec(tr_quark method, tr_variant* args, RpcClient::ResponseFunc on_done = {});
    void exec(tr_quark method, tr_variant::Map params, RpcClient::ResponseFunc on_done = {});

    void addTorrent(AddData const& add_me, tr_variant::Map args_dict);
    void addNewlyCreatedTorrent(QString const& filename, QString const& local_path);

public slots:
    void addTorrent(AddData const& add_me);
    void launchWebInterface() const;

signals:
    void sourceChanged();

    // stop() disconnected from the session and forgot its info and torrents.
    void stopped();

    // The session found its config dir held by another and started nothing.
    // Emitted from every path that reaches start(), so a handler need not know which one ran.
    void configDirContended();

    void portTested(std::optional<bool> status, tr_address_type ip_protocol);
    void blocklistUpdated(qint64);
    void blocklistUpdateFailed(QString const& message);
    void dataReadProgress();
    void dataSendProgress();
    void networkResponse(bool is_network_error, QString const& message);
    void httpAuthenticationRequired();

private slots:
    void onDuplicatesTimer();

private:
    void start();

    void onRenameFailed(RenameError const& error) const;

    QString const config_dir_;
    Prefs& prefs_;

    std::array<bool, NUM_TR_AF_INET_TYPES> port_test_pending_ = {};
    tr_session* session_ = {};
    RpcClient& rpc_;

    sigslot::scoped_connection rename_failed_connection_;

    std::map<QString, QString> duplicates_;
    QTimer duplicates_timer_;
};
