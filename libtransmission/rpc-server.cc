// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#include <algorithm>
#include <array>
#include <atomic> // [rpc-diag]
#include <chrono>
#include <cstdint>
#include <cstdio> // [rpc-diag]
#include <cstring> /* for strcspn() */
#include <ctime>
#include <memory>
#include <ranges>
#include <string>
#include <string_view>
#include <thread> // [rpc-diag]
#include <utility>
#include <vector>

#ifdef _WIN32
#include <ws2tcpip.h>
#else
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>
#include <signal.h> // [rpc-diag] sigaction()
#include <execinfo.h> // [rpc-diag] backtrace()
#include <pthread.h> // [rpc-diag] pthread_kill()
#include <sys/syscall.h> // [rpc-diag] SYS_gettid
#endif

#include <event2/buffer.h>
#include <event2/http.h>
#include <event2/listener.h>

#include <fmt/chrono.h>
#include <fmt/format.h>

#include <libdeflate.h>

#include "libtransmission/crypto-utils.h" /* tr_ssha1_matches() */
#include "libtransmission/error.h"
#include "libtransmission/file-utils.h"
#include "libtransmission/log.h"
#include "libtransmission/net.h"
#include "libtransmission/platform.h" /* tr_getWebClientDir() */
#include "libtransmission/quark.h"
#include "libtransmission/rpc-server.h"
#include "libtransmission/rpcimpl.h"
#include "libtransmission/session.h"
#include "libtransmission/string-utils.h"
#include "libtransmission/timer.h"
#include "libtransmission/tr-strbuf.h"
#include "libtransmission/types.h"
#include "libtransmission/variant.h"
#include "libtransmission/web-utils.h"

struct evbuffer;

/* session_id is used to make cross-site request forgery attacks difficult.
 * Don't disable this feature unless you really know what you're doing!
 * https://en.wikipedia.org/wiki/Cross-site_request_forgery
 * https://shiflett.org/articles/cross-site-request-forgeries
 * http://www.webappsec.org/lists/websecurity/archive/2008-04/msg00037.html */
#define REQUIRE_SESSION_ID

#define MY_REALM "Transmission"

using namespace std::literals;

namespace
{
auto constexpr TrUnixSocketPrefix = "unix:"sv;

/* The maximum size of a unix socket path is defined per-platform based on sockaddr_un.sun_path.
 * On Windows the fallback is the length of an ipv6 address. Subtracting one at the end is for
 * double counting null terminators from sun_path and TrUnixSocketPrefix. */
#ifdef _WIN32
auto inline constexpr TrUnixAddrStrLen = size_t{ INET6_ADDRSTRLEN };
#else
auto inline constexpr TrUnixAddrStrLen = size_t{ sizeof(std::declval<struct sockaddr_un>().sun_path) +
                                                 std::size(TrUnixSocketPrefix) };
#endif

enum tr_rpc_address_type : uint8_t { TR_RPC_INET_ADDR, TR_RPC_UNIX_ADDR };

class tr_unix_addr
{
public:
    [[nodiscard]] std::string to_string() const
    {
        return std::empty(unix_socket_path_) ? std::string(TrUnixSocketPrefix) : unix_socket_path_;
    }

    [[nodiscard]] bool from_string(std::string_view src)
    {
        if (!tr_strv_starts_with(src, TrUnixSocketPrefix)) {
            return false;
        }

        if (std::size(src) >= TrUnixAddrStrLen) {
            tr_logAddError(
                fmt::format(
                    fmt::runtime(_("Unix socket path must be fewer than {count} characters (including '{prefix}' prefix)")),
                    fmt::arg("count", TrUnixAddrStrLen - 1),
                    fmt::arg("prefix", TrUnixSocketPrefix)));
            return false;
        }
        unix_socket_path_ = src;
        return true;
    }

private:
    std::string unix_socket_path_;
};
} // namespace

class tr_rpc_address
{
public:
    tr_rpc_address()
        : inet_addr_{ tr_address::any(TR_AF_INET) }
    {
    }

    [[nodiscard]] constexpr auto is_unix_addr() const noexcept
    {
        return type_ == TR_RPC_UNIX_ADDR;
    }

    [[nodiscard]] constexpr auto is_inet_addr() const noexcept
    {
        return type_ == TR_RPC_INET_ADDR;
    }

    bool from_string(std::string_view src)
    {
        if (auto address = tr_address::from_string(src); address.has_value()) {
            type_ = TR_RPC_INET_ADDR;
            inet_addr_ = address.value();
            return true;
        }

        if (unix_addr_.from_string(src)) {
            type_ = TR_RPC_UNIX_ADDR;
            return true;
        }

        return false;
    }

    [[nodiscard]] std::string to_string(tr_port port = {}) const
    {
        if (type_ == TR_RPC_UNIX_ADDR) {
            return unix_addr_.to_string();
        }

        if (std::empty(port)) {
            return inet_addr_.display_name();
        }
        return tr_socket_address::display_name(inet_addr_, port);
    }

private:
    tr_rpc_address_type type_ = TR_RPC_INET_ADDR;
    struct tr_address inet_addr_;
    class tr_unix_addr unix_addr_;
};

namespace
{
int constexpr DeflateLevel = 6; // medium / default

// ---
// [rpc-diag] TEMPORARY instrumentation to triage slow / stalled RPC responses on
// the `chore/investigate-rpc-lag` branch. Every line is prefixed with "[rpc-diag]"
// and (where possible) tagged with a per-request "#N" id so lines can be grepped
// and correlated. Emitted at INFO level, so run the daemon with:
//     transmission-daemon --foreground --log-level=info
// (or set "message-level": 4 in settings.json). Remove this whole block, the
// call sites, and the `heartbeat_timer` member once the regression is understood.
//
// The tracers deliberately test several theories at once:
//   * ENTER / "#N COMPLETE" bracket each request with wall-clock + monotonic timing
//     so we can line them up against the client's HTTP `Date` headers and tell
//     whether the multi-second gaps are actually spent inside the server.
//   * The event-loop heartbeat fires every 500ms; if a beat arrives late, the
//     single libevent/session thread was blocked (starved) during that window --
//     which would explain why even empty 401/409 replies are slow.
//   * make_response / handle_rpc timers separate JSON serialization from gzip
//     from the actual socket flush, so a slow *send* is distinguishable from slow
//     *processing*.
namespace rpc_diag
{
using clock = std::chrono::steady_clock;

auto constexpr HeartbeatIntervalMs = 500;
auto constexpr HeartbeatStallThresholdMs = 900; // report if a "beat" is at least this late
auto constexpr StepWarnMs = 50; // only log an in-handler step if it took at least this long

// handle_request() and the timers all run on the single libevent thread, so the
// plain statics below need no atomics/locks.
[[nodiscard]] std::uint64_t next_id() noexcept
{
    static std::uint64_t counter = 0U;
    return ++counter;
}

// Wall-clock UTC HH:MM:SS.mmm so lines line up with the client's HTTP `Date`
// headers (which are in GMT).
[[nodiscard]] std::string now_str()
{
    auto const now = std::chrono::system_clock::now();
    auto const secs = std::chrono::time_point_cast<std::chrono::seconds>(now);
    auto const ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - secs).count();
    return fmt::format("{:%H:%M:%S}.{:03d} UTC", fmt::gmtime(std::chrono::system_clock::to_time_t(now)), ms);
}

[[nodiscard]] std::int64_t ms_since(clock::time_point start) noexcept
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(clock::now() - start).count();
}

[[nodiscard]] constexpr std::string_view method_name(evhttp_cmd_type cmd) noexcept
{
    switch (cmd) {
    case EVHTTP_REQ_GET:
        return "GET"sv;
    case EVHTTP_REQ_POST:
        return "POST"sv;
    case EVHTTP_REQ_HEAD:
        return "HEAD"sv;
    case EVHTTP_REQ_PUT:
        return "PUT"sv;
    case EVHTTP_REQ_DELETE:
        return "DELETE"sv;
    case EVHTTP_REQ_OPTIONS:
        return "OPTIONS"sv;
    default:
        return "?"sv;
    }
}

// Log an in-handler segment only if it was slow enough to matter.
void step(std::uint64_t id, std::string_view name, clock::time_point since)
{
    if (auto const ms = ms_since(since); ms >= StepWarnMs) {
        tr_logAddInfo(fmt::format("[rpc-diag] #{:d} step '{:s}' took {:d}ms", id, name, ms));
    }
}

// Per-request context handed to libevent's on-complete callback so we can log
// exactly when (and whether) the whole reply finished flushing to the socket.
struct Context {
    std::uint64_t id = 0U;
    clock::time_point enter;
};

// Fires after evhttp has written the entire reply to the socket. If a transfer is
// aborted (e.g. the client times out mid-download) libevent may free the request
// without invoking this -- in that case the *absence* of a "#N COMPLETE" line is
// itself the signal that the send never finished. The tiny leak of `ctx` on that
// path is acceptable for a throwaway triage build.
void on_complete(struct evhttp_request* /*req*/, void* arg)
{
    auto* const ctx = static_cast<Context*>(arg);
    tr_logAddInfo(
        fmt::format(
            "[rpc-diag] #{:d} COMPLETE at {:s}: entire reply flushed to socket {:d}ms after the request was accepted",
            ctx->id,
            now_str(),
            ms_since(ctx->enter)));
    delete ctx;
}

// --- Freeze watchdog (POSIX / Ubuntu-only; throwaway) --------------------------
// The heartbeat below only *reports* a stall after the loop unblocks. To catch a
// freeze *while it is happening* -- so we can see the exact blocking call stack --
// we run an independent watchdog thread that is deliberately NOT on the event
// loop. It watches `g_last_beat_ns` (bumped by the loop's heartbeat). If the loop
// goes silent past WatchdogFreezeMs the watchdog (a) prints a loud line to stderr
// with the pid + the frozen thread's LWP/TID + a ready-to-paste gdb command, and
// (b) signals the frozen thread so a SIGUSR1 handler dumps its own backtrace
// in-context (the thread is parked in a blocking syscall, so the signal interrupts
// it, we unwind, then it resumes). It re-samples every WatchdogRedumpMs so a long
// freeze yields several stacks -- letting us tell "stuck in one syscall" from
// "churning through a huge loop". No debugger required, but gdb is there too.
#ifndef _WIN32
std::atomic<std::int64_t> g_last_beat_ns{ 0 }; // bumped by the event loop each heartbeat
std::atomic<std::int64_t> g_last_dump_ns{ 0 }; // last time the watchdog dumped a stack
std::atomic<unsigned long> g_loop_pthread{ 0UL };
std::atomic<long> g_loop_tid{ -1 };

auto constexpr WatchdogPollMs = 200;
auto constexpr WatchdogFreezeMs = 1500; // > heartbeat interval, so healthy beats never trip it
auto constexpr WatchdogRedumpMs = 3000; // re-sample the stack this often while still frozen

[[nodiscard]] std::int64_t steady_now_ns() noexcept
{
    return std::chrono::duration_cast<std::chrono::nanoseconds>(clock::now().time_since_epoch()).count();
}

// Runs in the *frozen* thread's context (delivered via SIGUSR1 from the watchdog).
// backtrace_symbols_fd() writes straight to the fd without malloc, so it is safe
// enough to call from a signal handler for this throwaway diagnostic.
void sigusr1_dump_backtrace(int /*sig*/)
{
    static char const banner[] = "\n=== [rpc-diag] BACKTRACE of frozen event-loop thread ===\n";
    (void)!::write(STDERR_FILENO, banner, sizeof(banner) - 1U);

    void* frames[96];
    auto const n = ::backtrace(frames, static_cast<int>(std::size(frames)));
    ::backtrace_symbols_fd(frames, n, STDERR_FILENO);

    static char const footer[] = "=== [rpc-diag] end backtrace ===\n\n";
    (void)!::write(STDERR_FILENO, footer, sizeof(footer) - 1U);
}

// Independent monitor thread -- deliberately NOT on the event loop so it keeps
// running while the loop is wedged.
void watchdog_main()
{
    auto const pid = static_cast<long>(::getpid());
    for (;;) {
        std::this_thread::sleep_for(std::chrono::milliseconds{ WatchdogPollMs });

        auto const last_beat = g_last_beat_ns.load(std::memory_order_relaxed);
        if (last_beat == 0) {
            continue; // the event loop hasn't beaten yet
        }

        auto const now_ns = steady_now_ns();
        auto const stalled_ms = (now_ns - last_beat) / 1'000'000;
        if (stalled_ms < WatchdogFreezeMs) {
            continue; // healthy
        }

        if ((now_ns - g_last_dump_ns.load()) / 1'000'000 < WatchdogRedumpMs) {
            continue; // already dumped recently for this freeze
        }
        g_last_dump_ns.store(now_ns);

        // fprintf straight to stderr (not tr_logAdd) so it shows even if the log
        // path itself is stuck behind the frozen thread.
        std::fprintf(
            stderr,
            "\n[rpc-diag] *** EVENT LOOP FROZEN *** no heartbeat for ~%lldms\n"
            "[rpc-diag]   pid=%ld  frozen-thread-LWP=%ld\n"
            "[rpc-diag]   manual inspect:  gdb -p %ld -batch -ex 'thread apply all bt'\n"
            "[rpc-diag]   auto-dumping the frozen thread's backtrace below (via SIGUSR1):\n",
            static_cast<long long>(stalled_ms),
            pid,
            g_loop_tid.load(),
            pid);
        std::fflush(stderr);

        if (auto const handle = g_loop_pthread.load(); handle != 0UL) {
            (void)::pthread_kill(static_cast<pthread_t>(handle), SIGUSR1);
        }
    }
}

// Arm the watchdog once. MUST be called from the event-loop thread so we capture
// that thread's identity for signalling.
void start_watchdog_once()
{
    static auto started = false;
    if (started) {
        return;
    }
    started = true;

    g_loop_pthread.store(static_cast<unsigned long>(::pthread_self()));
    g_loop_tid.store(static_cast<long>(::syscall(SYS_gettid)));
    g_last_beat_ns.store(steady_now_ns());

    // Pre-load libgcc's unwinder now, on a healthy thread, so backtrace() in the
    // signal handler doesn't have to dlopen anything at freeze time.
    auto warm = std::array<void*, 4>{};
    (void)::backtrace(std::data(warm), static_cast<int>(std::size(warm)));

    struct sigaction sa = {};
    sa.sa_handler = &sigusr1_dump_backtrace;
    ::sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART; // resume the interrupted syscall after we unwind
    (void)::sigaction(SIGUSR1, &sa, nullptr);

    std::thread{ watchdog_main }.detach();

    tr_logAddInfo(
        fmt::format(
            "[rpc-diag] freeze watchdog armed: pid={:d}, event-loop LWP={:d}. On a freeze it prints here and "
            "auto-dumps the stuck backtrace (fallback: gdb -p {:d}).",
            static_cast<long>(::getpid()),
            g_loop_tid.load(),
            static_cast<long>(::getpid())));
}
#endif // !_WIN32

// Event-loop stall detector. Updated on each beat; a beat arriving much later than
// HeartbeatIntervalMs means the single event/session thread was blocked.
clock::time_point heartbeat_last{};

void heartbeat_tick()
{
    auto const now = clock::now();
#ifndef _WIN32
    // Feed the freeze watchdog: this is the loop's "I'm alive" signal.
    g_last_beat_ns.store(
        std::chrono::duration_cast<std::chrono::nanoseconds>(now.time_since_epoch()).count(),
        std::memory_order_relaxed);
#endif
    auto const elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - heartbeat_last).count();
    heartbeat_last = now;
    if (elapsed >= HeartbeatStallThresholdMs) {
        tr_logAddInfo(
            fmt::format(
                "[rpc-diag] heartbeat STALL at {:s}: event loop was blocked ~{:d}ms (expected ~{:d}ms). "
                "Something monopolized the single RPC/session event thread during this window.",
                now_str(),
                elapsed,
                HeartbeatIntervalMs));
    }
}
} // namespace rpc_diag

// Prevent clickjacking on the browser-facing WebUI and RPC responses.
// https://github.com/transmission/transmission/issues/8726
// https://cheatsheetseries.owasp.org/cheatsheets/Clickjacking_Defense_Cheat_Sheet.html.
void add_clickjacking_prevention_headers(struct evkeyvalq* headers)
{
    // Send X-Frame-Options for older browsers + CSP frame-ancestors for newer ones
    evhttp_add_header(headers, "X-Frame-Options", "SAMEORIGIN");
    evhttp_add_header(headers, "Content-Security-Policy", "frame-ancestors 'self'");
}

void send_simple_response(struct evhttp_request* req, int code, char const* text = nullptr)
{
    char const* code_text = tr_webGetResponseStr(code);
    struct evbuffer* body = evbuffer_new();

    evbuffer_add_printf(body, "<h1>%d: %s</h1>", code, code_text);

    if (text != nullptr) {
        evbuffer_add_printf(body, "%s", text);
    }

    evhttp_send_reply(req, code, code_text, body);

    evbuffer_free(body);
}

// ---

[[nodiscard]] constexpr char const* mimetype_guess(std::string_view path)
{
    // these are the ones we need for serving the web client's files...
    auto constexpr Types = std::to_array<std::pair<std::string_view, char const*>>({
        { ".css"sv, "text/css" },
        { ".gif"sv, "image/gif" },
        { ".html"sv, "text/html" },
        { ".ico"sv, "image/vnd.microsoft.icon" },
        { ".js"sv, "application/javascript" },
        { ".png"sv, "image/png" },
        { ".svg"sv, "image/svg+xml" },
    });

    for (auto const& [suffix, mime_type] : Types) {
        if (tr_strv_ends_with(path, suffix)) {
            return mime_type;
        }
    }

    return "application/octet-stream";
}

[[nodiscard]] evbuffer* make_response(struct evhttp_request* req, tr_rpc_server const* server, std::string_view content)
{
    auto* const out = evbuffer_new();
    auto const* const input_headers = evhttp_request_get_input_headers(req);
    auto* const output_headers = evhttp_request_get_output_headers(req);

    char const* encoding = evhttp_find_header(input_headers, "Accept-Encoding");

    if (bool const do_compress = encoding != nullptr && tr_strv_contains(encoding, "gzip"sv); !do_compress) {
        evbuffer_add(out, std::data(content), std::size(content));
        tr_logAddInfo(
            fmt::format(
                "[rpc-diag] make_response: {:d} bytes sent uncompressed (client did not advertise gzip)",
                std::size(content)));
    } else {
        auto const compress_start = rpc_diag::clock::now();
        auto const max_compressed_len = libdeflate_deflate_compress_bound(server->compressor.get(), std::size(content));

        auto iov = evbuffer_iovec{};
        evbuffer_reserve_space(out, static_cast<ev_ssize_t>(std::max(std::size(content), max_compressed_len)), &iov, 1);

        auto const compressed_len = libdeflate_gzip_compress(
            server->compressor.get(),
            std::data(content),
            std::size(content),
            iov.iov_base,
            iov.iov_len);
        auto used_gzip = false;
        if (0 < compressed_len && compressed_len < std::size(content)) {
            iov.iov_len = compressed_len;
            evhttp_add_header(output_headers, "Content-Encoding", "gzip");
            used_gzip = true;
        } else {
            std::ranges::copy(content, static_cast<char*>(iov.iov_base));
            iov.iov_len = std::size(content);
        }

        evbuffer_commit_space(out, &iov, 1);
        tr_logAddInfo(
            fmt::format(
                "[rpc-diag] make_response: gzip {:s} in {:d}ms; {:d} bytes in -> {:d} bytes out ({:d}% of original)",
                used_gzip ? "applied" : "skipped (did not shrink payload)",
                rpc_diag::ms_since(compress_start),
                std::size(content),
                iov.iov_len,
                std::size(content) != 0U ? iov.iov_len * 100U / std::size(content) : 0U));
    }

    return out;
}

void add_time_header(struct evkeyvalq* headers, char const* key, time_t now)
{
    // RFC 2616 says this must follow RFC 1123's date format, so use gmtime instead of localtime
    evhttp_add_header(headers, key, fmt::format("{:%a %b %d %T %Y%n}", fmt::gmtime(now)).c_str());
}

void serve_file(struct evhttp_request* req, tr_rpc_server const* server, std::string_view filename)
{
    auto* const output_headers = evhttp_request_get_output_headers(req);
    if (auto const cmd = evhttp_request_get_command(req); cmd != EVHTTP_REQ_GET) {
        evhttp_add_header(output_headers, "Allow", "GET");
        send_simple_response(req, HTTP_BADMETHOD);
        return;
    }

    auto content = std::vector<char>{};

    if (auto error = tr_error{}; !tr_file_read(filename, content, &error)) {
        send_simple_response(req, HTTP_NOTFOUND, fmt::format("{} ({})", filename, error.message()).c_str());
        return;
    }

    auto const now = tr_time();
    add_time_header(output_headers, "Date", now);
    add_time_header(output_headers, "Expires", now + (24 * 60 * 60));
    evhttp_add_header(output_headers, "Content-Type", mimetype_guess(filename));

    auto* const response = make_response(req, server, std::string_view{ std::data(content), std::size(content) });
    evhttp_send_reply(req, HTTP_OK, "OK", response);
    evbuffer_free(response);
}

void handle_web_client(struct evhttp_request* req, tr_rpc_server const* server)
{
    if (std::empty(server->web_client_dir_)) {
        send_simple_response(
            req,
            HTTP_NOTFOUND,
            "<p>Couldn't find Transmission's web interface files!</p>"
            "<p>Users: to tell Transmission where to look, "
            "set the TRANSMISSION_WEB_HOME environment "
            "variable to the folder where the web interface's "
            "index.html is located.</p>"
            "<p>Package Builders: to set a custom default at compile time, "
            "#define PACKAGE_DATA_DIR in libtransmission/platform.c "
            "or tweak tr_getClutchDir() by hand.</p>");
        return;
    }

    // convert the URL path component into a filesystem path, e.g.
    // "/transmission/web/images/favicon.png" ->
    // "/usr/share/transmission/web/images/favicon.png")
    auto subpath = std::string_view{ evhttp_request_get_uri(req) };

    // remove the web base path eg "/transmission/web/"
    {
        auto const& base_path = server->url();
        static auto constexpr Web = TrHttpServerWebRelativePath;
        subpath = subpath.substr(std::size(base_path) + std::size(Web));
    }

    // remove any trailing query / fragment
    subpath = subpath.substr(0, subpath.find_first_of("?#"sv));

    // if the query is empty, use the default
    if (std::empty(subpath)) {
        static auto constexpr DefaultPage = "index.html"sv;
        subpath = DefaultPage;
    }

    if (tr_strv_contains(subpath, ".."sv)) {
        if (auto* const con = evhttp_request_get_connection(req); con != nullptr) {
#if LIBEVENT_VERSION_NUMBER >= 0x02020001
            char const* remote_host = nullptr;
#else
            char* remote_host = nullptr;
#endif
            auto remote_port = ev_uint16_t{};
            evhttp_connection_get_peer(con, &remote_host, &remote_port);
            tr_logAddWarn(
                fmt::format(
                    fmt::runtime(_("Rejected request from {host} (possible directory traversal attack)")),
                    fmt::arg("host", remote_host)));
        }
        send_simple_response(req, HTTP_NOTFOUND);
    } else {
        serve_file(req, server, tr_pathbuf{ server->web_client_dir_, '/', subpath });
    }
}

void handle_rpc_from_json(struct evhttp_request* req, tr_rpc_server* server, std::string_view json)
{
    auto const exec_start = rpc_diag::clock::now();
    tr_logAddInfo(
        fmt::format(
            "[rpc-diag] handle_rpc: dispatching {:d}-byte request body to tr_rpc_request_exec() (synchronous, on the event thread)",
            std::size(json)));

    tr_rpc_request_exec(
        server->session,
        json,
        // NOLINTNEXTLINE(cppcoreguidelines-rvalue-reference-param-not-moved)
        [req, server, exec_start](tr_variant&& content) {
            tr_logAddInfo(
                fmt::format(
                    "[rpc-diag] handle_rpc: tr_rpc_request_exec() returned after {:d}ms; building the reply next",
                    rpc_diag::ms_since(exec_start)));

            if (!content.has_value()) {
                evhttp_send_reply(req, HTTP_NOCONTENT, "OK", nullptr);
                return;
            }

            auto const serialize_start = rpc_diag::clock::now();
            auto const body = tr_variant_serde::json().compact().to_string(content);
            tr_logAddInfo(
                fmt::format(
                    "[rpc-diag] handle_rpc: serialized JSON reply to {:d} bytes in {:d}ms",
                    std::size(body),
                    rpc_diag::ms_since(serialize_start)));

            auto* const output_headers = evhttp_request_get_output_headers(req);
            auto* const response = make_response(req, server, body);
            evhttp_add_header(output_headers, "Content-Type", "application/json; charset=UTF-8");

            auto const send_start = rpc_diag::clock::now();
            evhttp_send_reply(req, HTTP_OK, "OK", response);
            evbuffer_free(response);
            tr_logAddInfo(
                fmt::format(
                    "[rpc-diag] handle_rpc: evhttp_send_reply() returned in {:d}ms -- note this only *queues* the reply; "
                    "watch for the matching '#N COMPLETE' line to see when the socket actually drains",
                    rpc_diag::ms_since(send_start)));
        });
}

void handle_rpc(struct evhttp_request* req, tr_rpc_server* server)
{
    if (auto const cmd = evhttp_request_get_command(req); cmd == EVHTTP_REQ_POST) {
        auto* const input_buffer = evhttp_request_get_input_buffer(req);
        auto json = std::string_view{ reinterpret_cast<char const*>(evbuffer_pullup(input_buffer, -1)),
                                      evbuffer_get_length(input_buffer) };
        handle_rpc_from_json(req, server, json);
        return;
    }

    send_simple_response(req, HTTP_BADMETHOD);
}

bool is_address_allowed(tr_rpc_server const* server, char const* address)
{
    if (!server->is_whitelist_enabled()) {
        return true;
    }

    // Convert IPv4-mapped address to IPv4 address
    // so that it can match with IPv4 whitelist entries
    auto native = std::string{};
    if (auto ipv4_mapped = tr_address::from_string(address); ipv4_mapped) {
        if (auto addr = ipv4_mapped->from_ipv4_mapped(); addr) {
            native = addr->display_name();
        }
    }
    auto const* const addr = std::empty(native) ? address : native.c_str();

    auto const& src = server->whitelist_;
    return std::ranges::any_of(src, [&addr](auto const& s) { return tr_wildmat(addr, s.c_str()); });
}

bool isIPAddressWithOptionalPort(char const* host)
{
    auto address = sockaddr_storage{};
    int address_len = sizeof(address);

    /* TODO: move to net.{c,h} */
    return evutil_parse_sockaddr_port(host, reinterpret_cast<sockaddr*>(&address), &address_len) != -1;
}

bool isHostnameAllowed(tr_rpc_server const* server, evhttp_request* const req)
{
    /* If password auth is enabled, any hostname is permitted. */
    if (server->is_password_enabled()) {
        return true;
    }

    /* If whitelist is disabled, no restrictions. */
    if (!server->settings_.is_host_whitelist_enabled) {
        return true;
    }

    auto const* const host = evhttp_request_get_host(req);

    /* No host header, invalid request. */
    if (host == nullptr) {
        return false;
    }

    /* IP address is always acceptable. */
    if (isIPAddressWithOptionalPort(host)) {
        return true;
    }

    /* Host header might include the port. */
    auto const hostname = std::string_view{ host, strcspn(host, ":") };

    /* localhost is always acceptable. */
    if (hostname == "localhost"sv || hostname == "localhost."sv) {
        return true;
    }

    auto const& src = server->host_whitelist_;
    auto const hostname_sz = tr_urlbuf{ hostname };
    return std::ranges::any_of(src, [&hostname_sz](auto const& str) { return tr_wildmat(hostname_sz.c_str(), str.c_str()); });
}

bool test_session_id(tr_rpc_server const* server, evhttp_request* const req)
{
    auto const* const input_headers = evhttp_request_get_input_headers(req);
    char const* const session_id = evhttp_find_header(input_headers, std::data(TrRpcSessionIdHeader));
    return session_id != nullptr && server->session->sessionId() == session_id;
}

bool is_authorized(tr_rpc_server const* server, char const* auth_header)
{
    if (!server->is_password_enabled()) {
        return true;
    }

    // https://datatracker.ietf.org/doc/html/rfc7617
    // `Basic ${base64(username)}:${base64(password)}`

    auto constexpr Prefix = "Basic "sv;
    auto auth = std::string_view{ auth_header != nullptr ? auth_header : "" };
    if (!tr_strv_starts_with(auth, Prefix)) {
        return false;
    }

    auth.remove_prefix(std::size(Prefix));
    auto const decoded_str = tr_base64_decode(auth);
    auto decoded = std::string_view{ decoded_str };
    auto const username = tr_strv_sep(&decoded, ':');
    auto const password = decoded;
    return server->username() == username && tr_ssha1_matches(server->settings().salted_password, password);
}

void handle_request(struct evhttp_request* req, void* arg)
{
    auto constexpr HttpErrorUnauthorized = 401;
    auto constexpr HttpErrorForbidden = 403;

    if (req == nullptr) {
        return;
    }

    auto* const con = evhttp_request_get_connection(req);
    if (con == nullptr) {
        return;
    }

    auto* server = static_cast<tr_rpc_server*>(arg);

#if LIBEVENT_VERSION_NUMBER >= 0x02020001
    char const* remote_host = nullptr;
#else
    char* remote_host = nullptr;
#endif
    auto remote_port = ev_uint16_t{};
    evhttp_connection_get_peer(con, &remote_host, &remote_port);

    // [rpc-diag] begin per-request tracing (see the rpc_diag namespace near the top of this file)
    auto const diag_enter = rpc_diag::clock::now();
    auto const diag_id = rpc_diag::next_id();
    {
        static auto diag_prev_enter = diag_enter;
        auto const gap_ms = std::chrono::duration_cast<std::chrono::milliseconds>(diag_enter - diag_prev_enter).count();
        diag_prev_enter = diag_enter;

        auto const* const diag_in_headers = evhttp_request_get_input_headers(req);
        auto const* const diag_clen = evhttp_find_header(diag_in_headers, "Content-Length");
        auto const diag_has_auth = evhttp_find_header(diag_in_headers, "Authorization") != nullptr;
        auto const diag_has_sid = evhttp_find_header(diag_in_headers, std::data(TrRpcSessionIdHeader)) != nullptr;
        auto const* const diag_uri = evhttp_request_get_uri(req);
        tr_logAddInfo(
            fmt::format(
                "[rpc-diag] #{:d} ENTER at {:s}: {:s} {:s} from {:s}:{:d}; content-length={:s}, has-auth={:s}, "
                "has-session-id={:s}; {:d}ms since the previous request entered handle_request()",
                diag_id,
                rpc_diag::now_str(),
                rpc_diag::method_name(evhttp_request_get_command(req)),
                diag_uri != nullptr ? diag_uri : "(none)",
                remote_host != nullptr ? remote_host : "?",
                remote_port,
                diag_clen != nullptr ? diag_clen : "(none)",
                diag_has_auth ? "yes" : "no",
                diag_has_sid ? "yes" : "no",
                gap_ms));
    }
#if LIBEVENT_VERSION_NUMBER >= 0x02010100
    // NOLINTNEXTLINE(cppcoreguidelines-owning-memory) -- freed in rpc_diag::on_complete()
    evhttp_request_set_on_complete_cb(req, rpc_diag::on_complete, new rpc_diag::Context{ .id = diag_id, .enter = diag_enter });
#endif

    auto* const output_headers = evhttp_request_get_output_headers(req);
    evhttp_add_header(output_headers, "Server", MY_REALM);
    add_clickjacking_prevention_headers(output_headers);

    if (server->is_anti_brute_force_enabled() && server->login_attempts_ >= server->settings().anti_brute_force_limit) {
        tr_logAddWarn(
            fmt::format(
                fmt::runtime(_("Rejected request from {host} (brute force protection active)")),
                fmt::arg("host", remote_host)));
        send_simple_response(req, HttpErrorForbidden);
        return;
    }

    if (!is_address_allowed(server, remote_host)) {
        tr_logAddWarn(
            fmt::format(fmt::runtime(_("Rejected request from {host} (IP not whitelisted)")), fmt::arg("host", remote_host)));
        send_simple_response(req, HttpErrorForbidden);
        return;
    }

    auto const* const input_headers = evhttp_request_get_input_headers(req);
    if (auto const cmd = evhttp_request_get_command(req); cmd == EVHTTP_REQ_OPTIONS) {
        send_simple_response(req, HTTP_OK);
        return;
    }

    if (!is_authorized(server, evhttp_find_header(input_headers, "Authorization"))) {
        tr_logAddWarn(
            fmt::format(
                fmt::runtime(_("Rejected request from {host} (failed authentication)")),
                fmt::arg("host", remote_host)));
        evhttp_add_header(output_headers, "WWW-Authenticate", "Basic realm=\"" MY_REALM "\"");
        if (server->is_anti_brute_force_enabled()) {
            ++server->login_attempts_;
        }

        send_simple_response(req, HttpErrorUnauthorized);
        return;
    }

    server->login_attempts_ = 0;

    // [rpc-diag] how long did the per-request preamble (whitelist + auth/ssha1 + header parsing) take?
    rpc_diag::step(diag_id, "auth + pre-routing checks", diag_enter);

    // eg '/transmission/web/' and '/transmission/rpc'
    auto const& base_path = server->url();
    auto const web_base_path = tr_urlbuf{ base_path, TrHttpServerWebRelativePath };
    auto const rpc_base_path = tr_urlbuf{ base_path, TrHttpServerRpcRelativePath };
    auto const deprecated_web_path = tr_urlbuf{ base_path, "web" /*no trailing slash*/ };

    auto const uri = std::string_view{ evhttp_request_get_uri(req) };

    if (!tr_strv_starts_with(uri, base_path) || uri == deprecated_web_path) {
        evhttp_add_header(output_headers, "Location", web_base_path.c_str());
        send_simple_response(req, HTTP_MOVEPERM, nullptr);
    } else if (tr_strv_starts_with(uri, web_base_path)) {
        handle_web_client(req, server);
    } else if (!isHostnameAllowed(server, req)) {
        static auto constexpr Body =
            "<p>Transmission received your request, but the hostname was unrecognized.</p>"
            "<p>To fix this, choose one of the following options:"
            "<ul>"
            "<li>Enable password authentication, then any hostname is allowed.</li>"
            "<li>Add the hostname you want to use to the whitelist in settings.</li>"
            "</ul></p>"
            "<p>If you're editing settings.json, see the 'rpc_host_whitelist' and 'rpc_host_whitelist_enabled' entries.</p>"
            "<p>This requirement has been added to help prevent "
            "<a href=\"https://en.wikipedia.org/wiki/DNS_rebinding\">DNS Rebinding</a> "
            "attacks.</p>";
        tr_logAddWarn(
            fmt::format(fmt::runtime(_("Rejected request from {host} (Host not whitelisted)")), fmt::arg("host", remote_host)));
        send_simple_response(req, 421, Body);
    } else if (
        !uri.starts_with(rpc_base_path.sv()) ||
        (uri.size() != rpc_base_path.size() && uri.substr(rpc_base_path.size()) != "/"sv)) {
        tr_logAddWarn(
            fmt::format(
                fmt::runtime(_("Unknown URI from {host}: '{uri}'")),
                fmt::arg("host", remote_host),
                fmt::arg("uri", uri)));
        // NOLINTNEXTLINE(bugprone-suspicious-stringview-data-usage)
        send_simple_response(req, HTTP_NOTFOUND, uri.data());
    }
#ifdef REQUIRE_SESSION_ID
    else if (!test_session_id(server, req)) {
        auto const session_id = std::string{ server->session->sessionId() };
        evhttp_add_header(output_headers, std::data(TrRpcSessionIdHeader), session_id.c_str());

        evhttp_add_header(output_headers, std::data(TrRpcVersionHeader), std::data(TrRpcVersionSemver));

        auto const expose_val = fmt::format("{:s}, {:s}", TrRpcSessionIdHeader, TrRpcVersionHeader);
        evhttp_add_header(output_headers, "Access-Control-Expose-Headers", expose_val.c_str());

        auto const body = fmt::format(
            "<p>Your request had an invalid session_id header.</p>"
            "<p>To fix this, follow these steps:"
            "<ol><li> When reading a response, get its {0:s} header and remember it"
            "<li> Add the updated header to your outgoing requests"
            "<li> When you get this 409 error message, resend your request with the updated header"
            "</ol></p>"
            "<p>This requirement has been added to help prevent "
            "<a href=\"https://en.wikipedia.org/wiki/Cross-site_request_forgery\">CSRF</a> "
            "attacks.</p>"
            "<p><code>{0:s}: {1:s}</code></p>",
            TrRpcSessionIdHeader,
            session_id);
        tr_logAddInfo(
            fmt::format(
                "[rpc-diag] #{:d} returning 409 Conflict (missing/stale session id); sending the client the current id to retry with",
                diag_id));
        send_simple_response(req, 409, body.c_str());
    }
#endif
    else {
        tr_logAddInfo(fmt::format("[rpc-diag] #{:d} routing to RPC handler", diag_id));
        handle_rpc(req, server);
    }
}

auto constexpr ServerStartRetryCount = 10;
auto constexpr ServerStartRetryDelayIncrement = 5s;
auto constexpr ServerStartRetryMaxDelay = 60s;

bool bindUnixSocket(
    [[maybe_unused]] struct event_base* base,
    [[maybe_unused]] struct evhttp* httpd,
    [[maybe_unused]] char const* path,
    [[maybe_unused]] tr_mode_t socket_mode)
{
#ifdef _WIN32
    tr_logAddError(
        fmt::format(
            _("Unix sockets are unsupported on Windows. Please change '{key}' in your settings."),
            fmt::arg("key", tr_quark_get_string_view(TR_KEY_rpc_bind_address))));
    return false;
#else
    auto addr = sockaddr_un{};
    addr.sun_family = AF_UNIX;
    *fmt::format_to_n(addr.sun_path, sizeof(addr.sun_path) - 1, "{:s}", path + std::size(TrUnixSocketPrefix)).out = '\0';

    unlink(addr.sun_path);

    struct evconnlistener* lev = evconnlistener_new_bind(
        base,
        nullptr,
        nullptr,
        LEV_OPT_CLOSE_ON_FREE,
        -1,
        reinterpret_cast<sockaddr const*>(&addr),
        sizeof(addr));

    if (lev == nullptr) {
        return false;
    }

    if (chmod(addr.sun_path, socket_mode) != 0) {
        tr_logAddWarn(
            fmt::format(
                fmt::runtime(_("Couldn't set RPC socket mode to {mode:#o}, defaulting to 0755")),
                fmt::arg("mode", socket_mode)));
    }

    return evhttp_bind_listener(httpd, lev) != nullptr;
#endif
}

void start_server(tr_rpc_server* server);

auto rpc_server_start_retry(tr_rpc_server* server)
{
    if (!server->start_retry_timer) {
        server->start_retry_timer = server->session->timerMaker().create([server]() { start_server(server); });
    }

    ++server->start_retry_counter;
    auto const interval = std::min(ServerStartRetryDelayIncrement * server->start_retry_counter, ServerStartRetryMaxDelay);
    server->start_retry_timer->start_single_shot(std::chrono::duration_cast<std::chrono::milliseconds>(interval));
    return interval;
}

void rpc_server_start_retry_cancel(tr_rpc_server* server)
{
    server->start_retry_timer.reset();
    server->start_retry_counter = 0;
}

int tr_evhttp_bind_socket(struct evhttp* httpd, char const* address, ev_uint16_t port)
{
#ifdef _WIN32
    struct addrinfo* result = nullptr;
    struct addrinfo hints = {};
    ZeroMemory(&hints, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    hints.ai_flags = AI_PASSIVE;

    if (getaddrinfo(address, std::to_string(port).c_str(), &hints, &result) != 0) {
        return evhttp_bind_socket(httpd, address, port);
    }

    auto const fd = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
    if (!is_valid_socket(fd)) {
        freeaddrinfo(result);
        return evhttp_bind_socket(httpd, address, port);
    }
    evutil_make_socket_nonblocking(static_cast<evutil_socket_t>(fd));
    evutil_make_listen_socket_reuseable(static_cast<evutil_socket_t>(fd));

    // Making dual stack
    if (result->ai_family == AF_INET6) {
        int off = 0;
        setsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY, reinterpret_cast<char*>(&off), sizeof(off));
    }
    // Set keep alive
    int on = 1;
    setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, reinterpret_cast<char*>(&on), sizeof(on));
    if (bind(fd, result->ai_addr, static_cast<int>(result->ai_addrlen)) != 0 || listen(fd, 128) == -1) {
        closesocket(fd);
        freeaddrinfo(result);
        return evhttp_bind_socket(httpd, address, port);
    }
    if (evhttp_accept_socket(httpd, static_cast<evutil_socket_t>(fd)) == 0) {
        freeaddrinfo(result);
        return 0;
    }
    // Fallback
    closesocket(fd);
    freeaddrinfo(result);
#endif
    return evhttp_bind_socket(httpd, address, port);
}

void start_server(tr_rpc_server* server)
{
    if (server->httpd) {
        return;
    }

    auto* const base = server->session->event_base();
    auto* const httpd = evhttp_new(base);

    evhttp_set_allowed_methods(httpd, EVHTTP_REQ_GET | EVHTTP_REQ_POST | EVHTTP_REQ_OPTIONS);

    auto const address = server->get_bind_address();
    auto const port = server->port();

    bool const success = server->bind_address_->is_unix_addr() ?
        bindUnixSocket(base, httpd, address.c_str(), server->settings().socket_mode) :
        (tr_evhttp_bind_socket(httpd, address.c_str(), port.host()) != -1);

    auto const addr_port_str = server->bind_address_->to_string(port);

    if (!success) {
        evhttp_free(httpd);

        if (server->start_retry_counter < ServerStartRetryCount) {
            auto const retry_delay = rpc_server_start_retry(server);
            auto const seconds = std::chrono::duration_cast<std::chrono::seconds>(retry_delay).count();
            tr_logAddDebug(fmt::format("Couldn't bind to {}, retrying in {} seconds", addr_port_str, seconds));
            return;
        }

        tr_logAddError(
            fmt::format(
                fmt::runtime(tr_ngettext(
                    "Couldn't bind to {address} after {count} attempt, giving up",
                    "Couldn't bind to {address} after {count} attempts, giving up",
                    ServerStartRetryCount)),
                fmt::arg("address", addr_port_str),
                fmt::arg("count", ServerStartRetryCount)));
    } else {
        evhttp_set_gencb(httpd, handle_request, server);
        server->httpd.reset(httpd);

        // [rpc-diag] start the event-loop stall detector on the same base that serves HTTP.
        rpc_diag::heartbeat_last = rpc_diag::clock::now();
        server->heartbeat_timer = server->session->timerMaker().create([]() { rpc_diag::heartbeat_tick(); });
        server->heartbeat_timer->start_repeating(std::chrono::milliseconds{ rpc_diag::HeartbeatIntervalMs });
#ifndef _WIN32
        // [rpc-diag] arm the off-loop freeze watchdog from the event-loop thread.
        rpc_diag::start_watchdog_once();
#endif
        tr_logAddInfo(
            "[rpc-diag] instrumentation active: per-request tracing + 500ms event-loop heartbeat. "
            "Run with --log-level=info to see [rpc-diag] lines.");

        tr_logAddInfo(
            fmt::format(
                fmt::runtime(_("Listening for RPC and Web requests on '{address}'")),
                fmt::arg("address", addr_port_str)));
    }

    rpc_server_start_retry_cancel(server);
}

void stop_server(tr_rpc_server* server)
{
    auto const lock = server->session->unique_lock();

    rpc_server_start_retry_cancel(server);

    auto& httpd = server->httpd;
    if (!httpd) {
        return;
    }

    auto const address = server->get_bind_address();

    server->heartbeat_timer.reset(); // [rpc-diag]
    httpd.reset();

    if (server->bind_address_->is_unix_addr()) {
        unlink(address.c_str() + std::size(TrUnixSocketPrefix));
    }

    tr_logAddInfo(
        fmt::format(
            fmt::runtime(_("Stopped listening for RPC and Web requests on '{address}'")),
            fmt::arg("address", server->bind_address_->to_string(server->port()))));
}

void restart_server(tr_rpc_server* const server)
{
    if (server->is_enabled()) {
        stop_server(server);
        start_server(server);
    }
}

auto parse_whitelist(std::string_view whitelist)
{
    auto list = std::vector<std::string>{};

    auto item = std::string_view{};
    while (tr_strv_sep(&whitelist, &item, ",;"sv)) {
        item = tr_strv_strip(item);
        if (!std::empty(item)) {
            list.emplace_back(item);
            tr_logAddInfo(fmt::format(fmt::runtime(_("Added '{entry}' to host whitelist")), fmt::arg("entry", item)));
        }
    }

    return list;
}

} // namespace

void tr_rpc_server::set_enabled(bool is_enabled)
{
    settings_.is_enabled = is_enabled;

    session->run_in_session_thread([this]() {
        if (!settings_.is_enabled) {
            stop_server(this);
        } else {
            start_server(this);
        }
    });
}

void tr_rpc_server::set_port(tr_port port) noexcept
{
    if (settings_.port == port) {
        return;
    }

    settings_.port = port;

    if (is_enabled()) {
        session->run_in_session_thread(&restart_server, this);
    }
}

void tr_rpc_server::set_url(std::string_view url)
{
    settings_.url = url;
    tr_logAddDebug(fmt::format("setting our URL to '{:s}'", url));
}

void tr_rpc_server::set_whitelist(std::string_view whitelist)
{
    settings_.whitelist_str = whitelist;
    whitelist_ = parse_whitelist(whitelist);
}

// --- PASSWORD

void tr_rpc_server::set_username(std::string_view username)
{
    settings_.username = username;
    tr_logAddDebug(fmt::format("setting our username to '{:s}'", username));
}

void tr_rpc_server::set_password(std::string_view password) noexcept
{
    auto const is_salted = tr_ssha1_test(password);
    settings_.salted_password = is_salted ? password : tr_ssha1(password);
    tr_logAddDebug(fmt::format("setting our salted password to '{:s}'", settings_.salted_password));
}

void tr_rpc_server::set_password_enabled(bool enabled)
{
    settings_.authentication_required = enabled;
    tr_logAddDebug(fmt::format("setting password-enabled to '{}'", enabled));
}

std::string tr_rpc_server::get_bind_address() const
{
    return bind_address_->to_string();
}

void tr_rpc_server::set_anti_brute_force_enabled(bool enabled) noexcept
{
    settings_.is_anti_brute_force_enabled = enabled;

    if (!enabled) {
        login_attempts_ = 0;
    }
}

// --- LIFECYCLE

tr_rpc_server::tr_rpc_server(tr_session* session_in, Settings&& settings)
    : compressor{ libdeflate_alloc_compressor(DeflateLevel), libdeflate_free_compressor }
    , web_client_dir_{ tr_getWebClientDir(session_in) }
    , bind_address_{ std::make_unique<class tr_rpc_address>() }
    , session{ session_in }
{
    load(std::move(settings));
}

void tr_rpc_server::load(Settings&& settings)
{
    settings_ = std::move(settings);

    if (std::string& path = settings_.url; !tr_strv_ends_with(path, '/')) {
        path = fmt::format("{:s}/", path);
    }

    host_whitelist_ = parse_whitelist(settings_.host_whitelist_str);
    set_password_enabled(settings_.authentication_required);
    set_whitelist(settings_.whitelist_str);
    set_username(settings_.username);
    set_password(settings_.salted_password);

    if (!bind_address_->from_string(settings_.bind_address_str)) {
        // NOTE: bind_address_ is default initialized to INADDR_ANY
        tr_logAddWarn(
            fmt::format(
                fmt::runtime(_(
                    "The '{key}' setting is '{value}' but must be an IPv4 or IPv6 address or a Unix socket path. Using default value '0.0.0.0'")),
                fmt::arg("key", tr_quark_get_string_view(TR_KEY_rpc_bind_address)),
                fmt::arg("value", settings_.bind_address_str)));
    }

    if (bind_address_->is_unix_addr()) {
        set_whitelist_enabled(false);
        settings_.is_host_whitelist_enabled = false;
    }
    if (this->is_enabled()) {
        auto const& base_path = url();
        auto const rpc_uri = bind_address_->to_string(port()) + base_path;
        tr_logAddInfo(fmt::format(fmt::runtime(_("Serving RPC and Web requests on {address}")), fmt::arg("address", rpc_uri)));
        session->run_in_session_thread(start_server, this);

        if (this->is_whitelist_enabled()) {
            tr_logAddInfo(_("Whitelist enabled"));
        }

        if (this->is_password_enabled()) {
            tr_logAddInfo(_("Password required"));
        } else if (!this->is_whitelist_enabled()) {
            tr_logAddWarn(
                "The RPC server has no password and its IP whitelist is disabled. "
                "Anyone who can reach it has full control. "
                "Consider enabling 'rpc_whitelist_enabled' or "
                "setting 'rpc_authentication_required' and 'rpc_password'.");
        }
    } else {
        session->run_in_session_thread(stop_server, this);
    }

    if (!std::empty(web_client_dir_)) {
        tr_logAddInfo(
            fmt::format(fmt::runtime(_("Serving RPC and Web requests from '{path}'")), fmt::arg("path", web_client_dir_)));
    }
}

tr_rpc_server::~tr_rpc_server()
{
    stop_server(this);
}
