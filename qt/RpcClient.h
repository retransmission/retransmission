// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <array>

#include <sigslot/signal.hpp>

#include <QtCore/QObject>
#include <QtCore/QString>

#include <libtransmission-app/rpc-client.h>

using tr::app::RpcResponse;

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

    [[nodiscard]] constexpr tr::app::RpcClient& impl() noexcept
    {
        return impl_;
    }

signals:
    void httpAuthenticationRequired();
    void dataReadProgress();
    void dataSendProgress();
    void networkResponse(bool is_network_error, QString const& message);

private:
    tr::app::RpcClient impl_;
    std::array<sigslot::scoped_connection, 4> connections_;
};
