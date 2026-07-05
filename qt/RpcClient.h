// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <array>

#include <QObject>
#include <QNetworkReply> // QNetworkReply::NetworkError
#include <QString>
#include <QUrl>

#include <sigslot/signal.hpp>

#include <libtransmission-app/rpc-client.h>

#include <libtransmission/quark.h>
#include <libtransmission/variant.h>

struct tr_session;

using RpcResponse = tr::app::RpcResponse;

// Qt wrapper for tr::app::RpcClient
class RpcClient : public QObject
{
    Q_OBJECT

public:
    using ResponseFunc = tr::app::RpcClient::ResponseFunc;

    explicit RpcClient(QObject* parent = nullptr);
    ~RpcClient() override;
    RpcClient(RpcClient&&) = delete;
    RpcClient(RpcClient const&) = delete;
    RpcClient& operator=(RpcClient&&) = delete;
    RpcClient& operator=(RpcClient const&) = delete;

    [[nodiscard]] constexpr auto const& url() const noexcept
    {
        return url_;
    }

    [[nodiscard]] auto isLocal() const noexcept
    {
        return impl_.is_local();
    }

    void stop();
    void start(tr_session* session);
    void start(QUrl const& url);

    void exec(tr_quark method, tr_variant::Map args, ResponseFunc on_done = {});
    void exec(tr_quark method, tr_variant* args, ResponseFunc on_done = {});

signals:
    void httpAuthenticationRequired();
    void dataReadProgress();
    void dataSendProgress();
    void networkResponse(QNetworkReply::NetworkError code, QString const& message);

private:
    QUrl url_;
    tr::app::RpcClient impl_;
    std::array<sigslot::scoped_connection, 4> connections_;
};
