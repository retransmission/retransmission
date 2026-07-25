// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <chrono>
#include <cstddef> // size_t
#include <cstdint> // uint64_t
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

class tr_web
{
public:
    // The response struct passed to the user's FetchDoneFunc callback
    // when a fetch() finishes.
    struct FetchResponse {
        long status = 0; // http server response, e.g. 200
        std::map<std::string, std::string> headers; // keys are lowercased
        std::string body;
        std::string primary_ip;
        bool did_connect = false;
        bool did_timeout = false;

        void* user_data = nullptr; // from FetchOptions::done_func_user_data

        // Case-insensitive lookup of a response header value.
        [[nodiscard]] std::optional<std::string_view> header(std::string_view name) const;
    };

    // Callback to invoke when fetch() is done
    using FetchDoneFunc = std::function<void(FetchResponse const&)>;

    class FetchOptions
    {
    public:
        enum class IPProtocol : uint8_t {
            ANY,
            V4,
            V6,
        };

        enum class AuthScheme : uint8_t {
            Basic, // send credentials preemptively (curl's default)
            Any, // let curl negotiate any scheme the server offers
        };

        FetchOptions(
            std::string_view url_in,
            FetchDoneFunc&& done_func_in,
            void* done_func_user_data_in,
            std::chrono::seconds timeout_secs_in = DefaultTimeoutSecs)
            : url{ url_in }
            , done_func{ std::move(done_func_in) }
            , done_func_user_data{ done_func_user_data_in }
            , timeout_secs{ timeout_secs_in }
        {
        }

        // the URL to fetch
        std::string url;

        // Callback to invoke with a FetchResponse when done
        FetchDoneFunc done_func = nullptr;
        void* done_func_user_data = nullptr;

        // If you need to set multiple cookies, set them all using a single
        // option concatenated like this: "name1=content1; name2=content2;"
        std::optional<std::string> cookies;

        // If set, the request is sent as an HTTP POST with this request body.
        // If unset, the request is a GET.
        std::optional<std::string> body;

        // Extra HTTP request headers to send, keyed by header name,
        // e.g. headers["Content-Type"] = "application/json".
        std::map<std::string, std::string> headers;

        // HTTP Basic authentication credentials.
        // If username is set, it and the password are sent to the server.
        std::optional<std::string> username;
        std::optional<std::string> password;

        // Authentication scheme to use when credentials are set.
        // Basic sends them preemptively; Any lets curl negotiate the server's
        // scheme (Basic, Digest, NTLM, Negotiate) in a challenge round-trip.
        AuthScheme auth_scheme = AuthScheme::Basic;

        // If set, credentials are also looked up in a .netrc file (using curl's
        // CURL_NETRC_OPTIONAL mode). An empty string uses curl's default file
        // (~/.netrc); a non-empty string names the file to read instead.
        std::optional<std::string> netrc_file;

        // If set, connect through this Unix domain socket instead of over TCP.
        std::optional<std::string> unix_socket_path;

        // If set, bytes [range->first...range->second] are requested.
        // https://developer.mozilla.org/en-US/docs/Web/HTTP/Range_requests
        std::optional<std::pair<uint64_t, uint64_t>> range;

        // Tag used by tr_web::Mediator to limit some transfers' bandwidth
        std::optional<int> speed_limit_tag;

        // Optionally set the underlying sockets' send/receive buffers' size.
        // Can be used to conserve resources for scrapes and announces, where
        // the payload is known to be small.
        std::optional<int> sndbuf;
        std::optional<int> rcvbuf;

        // Maximum time to wait before timeout
        std::chrono::seconds timeout_secs = DefaultTimeoutSecs;

        // If set, overrides the default TLS certificate verification (which is
        // otherwise controlled by the TR_CURL_SSL_NO_VERIFY env var).
        // Set to false to accept self-signed certs, e.g. a local RPC server's.
        std::optional<bool> ssl_verify;

        // If set, overrides the default curl verbose logging (which is
        // otherwise controlled by the TR_CURL_VERBOSE env var).
        std::optional<bool> verbose;

        // Called periodically by the web internals when data is received.
        // Used by webseeds to report to tr_bandwidth for data xfer stats
        std::function<void(size_t /*n_bytes*/)> on_data_received;

        // IP protocol to use when making the request
        IPProtocol ip_proto = IPProtocol::ANY;

        static auto constexpr DefaultTimeoutSecs = std::chrono::seconds{ 120 };
    };

    void fetch(FetchOptions&& options);

    // Notify tr_web that it's going to be destroyed soon.
    // New fetch() tasks will be rejected, but already-running tasks
    // are left alone so that they can finish.
    void startShutdown(std::chrono::milliseconds /*deadline*/);

    [[nodiscard]] bool is_idle() const noexcept;

    // If you want to give running tasks a chance to finish,
    // call startShutdown() before destroying the tr_web object.
    // Deleting the object will cancel all of its tasks.
    ~tr_web();

    tr_web(tr_web const&) = delete;
    tr_web(tr_web&&) = delete;
    tr_web& operator=(tr_web const&) = delete;
    tr_web& operator=(tr_web&&) = delete;

    /**
     * Mediates between `tr_web` and its clients.
     *
     * NB: Note that `tr_web` calls all these methods from its own thread.
     * Overridden methods should take care to be threadsafe.
     */
    class Mediator
    {
    public:
        virtual ~Mediator() = default;

        // Return the location of the cookie file, or nullopt to not use one
        [[nodiscard]] virtual std::optional<std::string> cookieFile() const
        {
            return std::nullopt;
        }

        // Return IPv4 user public address string, or nullopt to not use one
        [[nodiscard]] virtual std::optional<std::string> bind_address_V4() const
        {
            return std::nullopt;
        }

        // Return IPv6 user public address string, or nullopt to not use one
        [[nodiscard]] virtual std::optional<std::string> bind_address_V6() const
        {
            return std::nullopt;
        }

        // Return the preferred user aagent, or nullopt to not use one
        [[nodiscard]] virtual std::optional<std::string_view> userAgent() const
        {
            return std::nullopt;
        }

        // Return the number of bytes that should be allowed. See tr_bandwidth::clamp()
        [[nodiscard]] virtual size_t clamp([[maybe_unused]] int bandwidth_tag, size_t byte_count) const
        {
            return byte_count;
        }

        // Return the preferred proxy url
        [[nodiscard]] virtual std::optional<std::string> proxyUrl() const
        {
            return std::nullopt;
        }

        // Invoke the user-provided fetch callback
        // NOLINTNEXTLINE(cppcoreguidelines-rvalue-reference-param-not-moved)
        virtual void run(FetchDoneFunc&& func, FetchResponse&& response) const
        {
            func(response);
        }

        [[nodiscard]] virtual std::chrono::steady_clock::time_point now() const
        {
            return std::chrono::steady_clock::now();
        }
    };

    // Note that tr_web does no management of the `mediator` reference.
    // The caller must ensure `mediator` is valid for tr_web's lifespan.
    [[nodiscard]] static std::unique_ptr<tr_web> create(Mediator& mediator);

private:
    class Impl;
    std::unique_ptr<Impl> const impl_;
    explicit tr_web(Mediator& mediator);
};

void tr_sessionFetch(struct tr_session* session, tr_web::FetchOptions&& options);
