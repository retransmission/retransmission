// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#include "RpcClient.h"

#include <functional>
#include <utility>

#include <QtCore/QTimer>

#include "Utils.h"

namespace
{

// Marshaler that tells tr::app::RpcClient how to hop work to the Qt event loop.
[[nodiscard]] tr::app::RpcClient::UiThreadFunc makeUiMarshaler(QObject* context)
{
    return [context](std::function<void()> fn) {
        // queued even when called on the UI thread: a reply never runs inside exec()
        QTimer::singleShot(0, context, std::move(fn));
    };
}

} // namespace

RpcClient::RpcClient(QObject* parent)
    : QObject{ parent }
    , impl_{ makeUiMarshaler(this) }
{
    connections_[0] = impl_.network_response.connect_scoped(
        [this](bool const is_network_error, std::string_view const message) {
            emit networkResponse(is_network_error, Utils::qstringFromUtf8(message));
        });
    connections_[1] = impl_.auth_required.connect_scoped([this] { emit httpAuthenticationRequired(); });
    connections_[2] = impl_.data_read_progress.connect_scoped([this] { emit dataReadProgress(); });
    connections_[3] = impl_.data_send_progress.connect_scoped([this] { emit dataSendProgress(); });
}

RpcClient::~RpcClient() = default;
