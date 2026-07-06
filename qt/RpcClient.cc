// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#include <functional>
#include <optional>
#include <string>
#include <utility>

#include "RpcClient.h"

#include <QCoreApplication>
#include <QTimer>

#include "Utils.h"

namespace
{

// Marshaler that tells tr::app::RpcClient how to hop work to the Qt event loop.
[[nodiscard]] tr::app::RpcClient::UiThreadFunc makeUiMarshaler()
{
    return [](std::function<void()> fn) {
        if (qApp != nullptr) {
            QTimer::singleShot(0, qApp, std::move(fn));
            return;
        }

        fn();
    };
}

} // namespace

RpcClient::RpcClient(QObject* parent)
    : QObject{ parent }
    , impl_{ makeUiMarshaler() }
{
    connections_[0] = impl_.network_response.connect_scoped([this](long const status, std::string const& message) {
        auto const code = status == 200 ? QNetworkReply::NoError : QNetworkReply::UnknownNetworkError;
        emit networkResponse(code, Utils::qstringFromUtf8(message));
    });
    connections_[1] = impl_.auth_required.connect_scoped([this]() { emit httpAuthenticationRequired(); });
    connections_[2] = impl_.data_read_progress.connect_scoped([this]() { emit dataReadProgress(); });
    connections_[3] = impl_.data_send_progress.connect_scoped([this]() { emit dataSendProgress(); });
}

RpcClient::~RpcClient() = default;

void RpcClient::stop()
{
    impl_.stop();
    url_.clear();
}

void RpcClient::start(tr_session* session)
{
    impl_.start(session);
}

void RpcClient::start(QUrl const& url)
{
    // keep the full URL (with any userinfo) for url(); tr::app::RpcClient
    // receives the credentials separately and a URL without userinfo.
    url_ = url;

    auto username = std::optional<std::string>{};
    auto password = std::optional<std::string>{};
    if (!url.userName().isEmpty()) {
        username = url.userName().toStdString();
        password = url.password().toStdString();
    }

    auto clean_url = url;
    clean_url.setUserInfo(QString{});
    impl_.start(clean_url.toString().toStdString(), std::move(username), std::move(password));
}

void RpcClient::exec(tr_quark const method, tr_variant::Map args, ResponseFunc on_done)
{
    impl_.exec(method, std::move(args), std::move(on_done));
}

void RpcClient::exec(tr_quark const method, tr_variant* args, ResponseFunc on_done)
{
    impl_.exec(method, args, std::move(on_done));
}
