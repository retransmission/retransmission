// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

// Peer transport throughput, TCP against µTP, with nothing else in the path.
//
// Two sessions in one process share one connection over loopback: session A
// writes bytes into its peer IO and session B's peer IO discards them. Both
// ends are the app's own tr_peerIo, peer sockets, bandwidth allocation and
// libutp, so the result isolates the transport from handshakes, the peer
// protocol, torrents and disk.
//
//     transport-benchmark [--transport tcp|utp|both] [--size-mib N] [--seconds N] [--sample-ms N]
//                         [--repeat N] [--timeout SECONDS]
//
// --seconds streams for that long instead of a fixed size. --sample-ms prints the receive
// rate every N ms as `sample <transport> <seconds since start> <MiB/s>`, which lets a caller
// follow how the rate reacts to changes on the link. Before the first sample it prints
// `started <transport> <Unix time>`, so a caller can put samples on its own clock.

#include <algorithm>
#include <array>
#include <atomic>
#include <charconv>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <future>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#ifdef _WIN32
#include <winsock2.h>
#include <windows.h> // GetProcessTimes()
#else
#include <netinet/in.h>
#include <sys/socket.h>
#endif

#include <event2/util.h>

#include <fmt/format.h>

#ifdef WITH_UTP
#include <libutp/utp.h>
#endif

#include "libtransmission/transmission.h"

#include "libtransmission/error.h"
#include "libtransmission/file.h"
#include "libtransmission/net.h"
#include "libtransmission/peer-io.h"
#include "libtransmission/peer-socket-tcp.h"
#include "libtransmission/peer-socket-utp.h"
#include "libtransmission/quark.h"
#include "libtransmission/session.h"
#include "libtransmission/timer.h"
#include "libtransmission/utils.h" // tr_time_msec()
#include "libtransmission/variant.h"

using namespace std::literals;

namespace
{
using Clock = std::chrono::steady_clock;

struct Options {
    std::vector<std::string_view> transports = { "tcp"sv, "utp"sv };
    uint64_t size_mib = 1024U;
    std::optional<std::chrono::seconds> duration; // streams for this long instead of size_mib
    std::chrono::milliseconds sample_interval = {}; // zero: no samples
    int repeat = 1;
    std::chrono::seconds timeout = 60s;
};

struct Result {
    std::string_view transport;
    uint64_t bytes = 0;
    double connect_seconds = 0;
    double transfer_seconds = 0;
    double cpu_seconds = 0;
};

// The two ends of one measured connection. Each peer IO runs on its own session's thread.
struct Transfer {
    uint64_t total_bytes = 0;

    std::shared_ptr<tr_peerIo> sender;
    std::unique_ptr<tr::Timer> refill_timer; // sender thread only
    std::shared_ptr<tr_peerIo> receiver;

    uint64_t bytes_queued = 0; // sender thread only
    std::atomic<uint64_t> bytes_received = 0;
    std::optional<Clock::time_point> first_byte_at; // receiver thread only
    Clock::time_point last_byte_at;

    std::promise<std::string> done; // empty on success, else an error message
    std::atomic<bool> finished = false;

    // With --seconds, the bytes received and the time when the run ended (main thread only).
    // Bytes still in flight then arrive during teardown and are not counted.
    std::optional<std::pair<uint64_t, Clock::time_point>> ended;

    void finish(std::string error)
    {
        if (!finished.exchange(true)) {
            done.set_value(std::move(error));
        }
    }
};

auto constexpr ChunkSize = size_t{ 16U } * 1024U; // one BitTorrent block
auto const Chunk = std::array<std::byte, ChunkSize>{};
auto constexpr RefillInterval = 50ms;
// Bytes sent but not yet received, at most: a peer's 500-block request queue (peer-msgs'
// PeerReqQDefault). Without a cap, an unlimited sender copies gigabytes into its output
// buffer, and that copying, not the transport, sets the rate.
auto constexpr MaxUnreceived = uint64_t{ 500U } * ChunkSize;
auto constexpr InfoHash = tr_sha1_digest_t{ std::byte{ 1 } };

// Runs `func` on `session`'s thread and waits for it.
template<typename Func>
void runBlocking(tr_session* const session, Func const& func)
{
    auto finished = std::promise<void>{};
    session->run_in_session_thread([&] {
        func();
        finished.set_value();
    });
    finished.get_future().wait();
}

// Keeps the sender's output buffer as full as its bandwidth allows, as peer-msgs does with piece data.
void refill(tr_peerIo* const io, Transfer& transfer)
{
    // A sender that kept writing after the run ended would hold up the teardown on its thread.
    if (transfer.finished) {
        return;
    }

    auto space = io->get_write_buffer_space(tr_time_msec());
    auto const received = transfer.bytes_received.load();
    while (space > 0U && transfer.bytes_queued < transfer.total_bytes && transfer.bytes_queued - received < MaxUnreceived) {
        auto const n = std::min<uint64_t>({ ChunkSize,
                                            space,
                                            transfer.total_bytes - transfer.bytes_queued,
                                            MaxUnreceived - (transfer.bytes_queued - received) });
        io->write_bytes(std::data(Chunk), n, true /*is_piece_data*/);
        transfer.bytes_queued += n;
        space -= n;
    }
}

void onError(tr_peerIo* /*io*/, tr_error const& error, void* vtransfer)
{
    static_cast<Transfer*>(vtransfer)->finish(std::string{ error.message() });
}

void onSenderWrote(tr_peerIo* const io, size_t /*bytes_written*/, bool /*was_piece_data*/, void* vtransfer)
{
    refill(io, *static_cast<Transfer*>(vtransfer));
}

ReadState onSenderReadable(tr_peerIo* const io, void* /*vtransfer*/, size_t* /*setme_piece_byte_count*/)
{
    io->read_buffer_discard();
    return ReadState::Later;
}

ReadState onReceiverReadable(tr_peerIo* const io, void* vtransfer, size_t* const setme_piece_byte_count)
{
    auto& transfer = *static_cast<Transfer*>(vtransfer);
    auto const now = Clock::now();
    if (!transfer.first_byte_at) {
        transfer.first_byte_at = now;
    }

    auto const n = io->read_buffer_size();
    io->read_buffer_discard(n);
    *setme_piece_byte_count = n;
    transfer.last_byte_at = now;

    if (transfer.bytes_received += n; transfer.bytes_received >= transfer.total_bytes) {
        transfer.finish({});
    }

    return ReadState::Later;
}

void startSender(tr_session* const session, Transfer& transfer, std::shared_ptr<tr_peerIo> io)
{
    transfer.sender = std::move(io);
    transfer.sender->set_callbacks(onSenderReadable, onSenderWrote, onError, &transfer);
    refill(transfer.sender.get(), transfer);

    // Write callbacks alone leave the buffer empty once a bandwidth period's budget is spent,
    // so also refill on a timer, as peer-msgs does on its pulse.
    transfer.refill_timer = session->timerMaker().create([&transfer] { refill(transfer.sender.get(), transfer); });
    transfer.refill_timer->start_repeating(RefillInterval);
}

void startReceiver(tr_session* const session, Transfer& transfer, std::shared_ptr<tr_peer_socket> socket)
{
    transfer.receiver = tr_peerIo::new_incoming(session, &session->top_bandwidth_, std::move(socket));
    transfer.receiver->set_callbacks(onReceiverReadable, nullptr, onError, &transfer);
}

#ifdef WITH_UTP
// The receiving session's µTP accepts go to the benchmark instead of its peer manager.
Transfer* utp_transfer = nullptr;

uint64 onUtpAccept(utp_callback_arguments* const args)
{
    auto* const session = static_cast<tr_session*>(utp_context_get_userdata(args->context));
    auto storage = sockaddr_storage{};
    auto len = socklen_t{ sizeof(storage) };
    utp_getpeername(args->socket, reinterpret_cast<sockaddr*>(&storage), &len);
    auto const address = tr_socket_address::from_sockaddr(reinterpret_cast<sockaddr const*>(&storage));
    if (utp_transfer == nullptr || !address) {
        utp_close(args->socket);
        return 0;
    }

    startReceiver(session, *utp_transfer, tr_peer_socket_utp::create(*address, args->socket, session->timerMaker()));
    return 0;
}

// libutp's built-in clock keeps unguarded process-wide state. A daemon has one µTP context,
// but this benchmark runs two on two threads, which would race on that state and skew the
// delay samples that LEDBAT congestion control reacts to.
uint64 utpMilliseconds(utp_callback_arguments* /*args*/)
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch()).count();
}

uint64 utpMicroseconds(utp_callback_arguments* /*args*/)
{
    return std::chrono::duration_cast<std::chrono::microseconds>(Clock::now().time_since_epoch()).count();
}

void useThreadSafeUtpClock(tr_session* const session)
{
    runBlocking(session, [session] {
        utp_set_callback(session->utp_context, UTP_GET_MILLISECONDS, utpMilliseconds);
        utp_set_callback(session->utp_context, UTP_GET_MICROSECONDS, utpMicroseconds);
    });
}
#endif

// A loopback TCP listener that the benchmark accepts on itself, bypassing the receiving session's own listener.
class Listener
{
public:
    Listener()
        : sock_{ socket(AF_INET, SOCK_STREAM, 0) }
    {
        auto addr = sockaddr_in{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        auto len = socklen_t{ sizeof(addr) };
        if (!is_valid_socket(sock_) || bind(sock_, reinterpret_cast<sockaddr*>(&addr), len) != 0 || listen(sock_, 1) != 0 ||
            getsockname(sock_, reinterpret_cast<sockaddr*>(&addr), &len) != 0 || evutil_make_socket_nonblocking(sock_) != 0) {
            throw std::runtime_error{ "couldn't open a loopback TCP listener" };
        }
        address_ = *tr_socket_address::from_sockaddr(reinterpret_cast<sockaddr const*>(&addr));
    }

    Listener(Listener const&) = delete;
    Listener& operator=(Listener const&) = delete;
    Listener(Listener&&) = delete;
    Listener& operator=(Listener&&) = delete;

    ~Listener()
    {
        tr_net_close_socket(sock_);
    }

    [[nodiscard]] constexpr auto const& address() const noexcept
    {
        return address_;
    }

    [[nodiscard]] constexpr auto sock() const noexcept
    {
        return sock_;
    }

private:
    tr_socket_t sock_ = TR_BAD_SOCKET;
    tr_socket_address address_;
};

class Session
{
public:
    explicit Session(std::filesystem::path const& config_dir)
    {
        std::filesystem::create_directories(config_dir / "Downloads");
        auto settings = tr::Settings{};
        settings.insert_or_assign(TR_KEY_download_dir, (config_dir / "Downloads").string());
        settings.insert_or_assign(TR_KEY_bind_address_ipv4, "127.0.0.1"sv);
        settings.insert_or_assign(TR_KEY_bind_address_ipv6, "::1"sv);
        settings.insert_or_assign(TR_KEY_peer_port_random_on_start, true);
        settings.insert_or_assign(TR_KEY_port_forwarding_enabled, false);
        settings.insert_or_assign(TR_KEY_dht_enabled, false);
        settings.insert_or_assign(TR_KEY_lpd_enabled, false);
        settings.insert_or_assign(TR_KEY_tcp_enabled, true);
        settings.insert_or_assign(TR_KEY_utp_enabled, true);
        settings.insert_or_assign(TR_KEY_message_level, TR_LOG_ERROR);
        settings.insert_or_assign(TR_KEY_ip_endpoints_ipv4, tr_variant::Vector{});
        settings.insert_or_assign(TR_KEY_ip_endpoints_ipv6, tr_variant::Vector{});
        session_ = tr_sessionInit(config_dir.string(), false, settings);
    }

    Session(Session const&) = delete;
    Session& operator=(Session const&) = delete;
    Session(Session&&) = delete;
    Session& operator=(Session&&) = delete;

    ~Session()
    {
        tr_sessionClose(session_, 1.0);
    }

    [[nodiscard]] constexpr tr_session* get() const noexcept
    {
        return session_;
    }

private:
    tr_session* session_;
};

[[nodiscard]] double processCpuSeconds()
{
#ifdef _WIN32
    // MSVC's std::clock() is wall time.
    auto creation = FILETIME{};
    auto exit = FILETIME{};
    auto kernel = FILETIME{};
    auto user = FILETIME{};
    if (GetProcessTimes(GetCurrentProcess(), &creation, &exit, &kernel, &user) == 0) {
        return 0.0;
    }
    auto const ticks = [](FILETIME const& ft) {
        return (static_cast<uint64_t>(ft.dwHighDateTime) << 32U) | ft.dwLowDateTime;
    };
    return static_cast<double>(ticks(kernel) + ticks(user)) / 1e7; // 100 ns ticks
#else
    return static_cast<double>(std::clock()) / CLOCKS_PER_SEC;
#endif
}

// Waits for the transfer to finish, or for --seconds to pass, printing samples on the way.
// Returns an error message, or an empty string on success.
[[nodiscard]] std::string wait(
    Transfer& transfer,
    std::future<std::string>& done,
    std::string_view const transport,
    Clock::time_point const started,
    Options const& options)
{
    auto const deadline = started + (options.duration ? *options.duration : options.timeout);
    auto const sampling = options.sample_interval > std::chrono::milliseconds::zero();
    auto next_sample = started + options.sample_interval;
    auto sampled_at = started;
    auto sampled_bytes = uint64_t{};

    if (sampling) {
        auto const since_epoch = std::chrono::system_clock::now() - (Clock::now() - started);
        fmt::print("started {} {:.3f}\n", transport, std::chrono::duration<double>{ since_epoch.time_since_epoch() }.count());
    }

    for (;;) {
        auto const wake = sampling ? std::min(next_sample, deadline) : deadline;
        if (done.wait_until(wake) == std::future_status::ready) {
            return done.get();
        }

        if (auto const now = Clock::now(); sampling && now >= next_sample) {
            // A late wakeup makes one longer sample; the ticks it overslept are skipped, since
            // samples taken back to back would cover almost no time and read as a stall.
            auto const bytes = transfer.bytes_received.load();
            auto const seconds = std::chrono::duration<double>{ now - sampled_at }.count();
            fmt::print(
                "sample {} {:.3f} {:.1f}\n",
                transport,
                std::chrono::duration<double>{ now - started }.count(),
                static_cast<double>(bytes - sampled_bytes) / (1024.0 * 1024.0) / seconds);
            (void)std::fflush(stdout);
            sampled_bytes = bytes;
            sampled_at = now;
            while (next_sample <= now) {
                next_sample += options.sample_interval;
            }
        }

        if (Clock::now() >= deadline) {
            if (options.duration) {
                transfer.ended.emplace(transfer.bytes_received.load(), Clock::now());
                transfer.finish({});
                return {};
            }
            return "timed out after " + std::to_string(transfer.bytes_received) + " bytes";
        }
    }
}

[[nodiscard]] std::optional<Result> measure(
    Session const& sender,
    Session const& receiver,
    std::string_view const transport,
    Options const& options)
{
    auto transfer = Transfer{};
    transfer.total_bytes = options.duration ? UINT64_MAX : options.size_mib * 1024U * 1024U;
    auto done = transfer.done.get_future();

    auto const cpu_started = processCpuSeconds();
    auto const started = Clock::now();

    auto listener = std::optional<Listener>{};
    if (transport == "tcp"sv) {
        listener.emplace();
        runBlocking(sender.get(), [&] {
            auto* const session = sender.get();
            if (auto io = tr_peerIo::new_outgoing(
                    session,
                    &session->top_bandwidth_,
                    listener->address(),
                    InfoHash,
                    false /*seed*/,
                    false /*utp*/)) {
                startSender(session, transfer, std::move(io));
            }
        });
        auto const deadline = Clock::now() + options.timeout;
        while (transfer.sender && !transfer.receiver && Clock::now() < deadline) {
            runBlocking(receiver.get(), [&] {
                if (auto incoming = tr_netAccept(receiver.get(), listener->sock()); incoming) {
                    auto& [socket_address, sock] = *incoming;
                    startReceiver(receiver.get(), transfer, tr_peer_socket_tcp::create(*receiver.get(), socket_address, sock));
                }
            });
            std::this_thread::sleep_for(1ms);
        }
    } else {
#ifdef WITH_UTP
        runBlocking(receiver.get(), [&] {
            utp_transfer = &transfer;
            utp_set_callback(receiver.get()->utp_context, UTP_ON_ACCEPT, onUtpAccept);
        });
        auto const loopback = *tr_address::from_string("127.0.0.1"sv);
        auto const port = tr_port::from_host(tr_sessionGetPeerPort(receiver.get()));
        runBlocking(sender.get(), [&] {
            auto* const session = sender.get();
            if (auto io = tr_peerIo::new_outgoing_utp(
                    session,
                    &session->top_bandwidth_,
                    tr_socket_address{ loopback, port },
                    InfoHash,
                    false /*seed*/)) {
                startSender(session, transfer, std::move(io));
            }
        });
#else
        fmt::print(stderr, "this build has no µTP support\n");
        return {};
#endif
    }

    auto error = std::string{};
    if (!transfer.sender) {
        error = "couldn't open the outgoing connection";
    } else {
        error = wait(transfer, done, transport, started, options);
    }
    auto const cpu_seconds = processCpuSeconds() - cpu_started;

    // Tear down on each session's thread, where its callbacks run.
    runBlocking(sender.get(), [&] {
        transfer.refill_timer.reset();
        if (transfer.sender) {
            transfer.sender->clear_callbacks();
            transfer.sender.reset();
        }
    });
    runBlocking(receiver.get(), [&] {
        if (transfer.receiver) {
            transfer.receiver->clear_callbacks();
            transfer.receiver.reset();
        }
#ifdef WITH_UTP
        utp_transfer = nullptr;
#endif
    });

    if (std::empty(error) && !transfer.first_byte_at) {
        error = "received nothing";
    }

    if (!std::empty(error)) {
        fmt::print(stderr, "{}: {}\n", transport, error);
        return {};
    }

    auto const seconds = [](auto const from, auto const to) {
        return std::chrono::duration<double>{ to - from }.count();
    };
    return Result{
        .transport = transport,
        .bytes = transfer.ended ? transfer.ended->first : transfer.bytes_received.load(),
        .connect_seconds = seconds(started, *transfer.first_byte_at),
        .transfer_seconds = seconds(*transfer.first_byte_at, transfer.ended ? transfer.ended->second : transfer.last_byte_at),
        .cpu_seconds = cpu_seconds,
    };
}

[[nodiscard]] std::optional<Options> parseOptions(int const argc, char** const argv)
{
    auto options = Options{};
    auto const parse_number = [](std::string_view const sv, auto& setme) {
        auto const [ptr, ec] = std::from_chars(std::data(sv), std::data(sv) + std::size(sv), setme);
        return ec == std::errc{} && ptr == std::data(sv) + std::size(sv) && setme > 0;
    };

    for (int i = 1; i < argc; i += 2) {
        auto const arg = std::string_view{ argv[i] };
        auto const value = i + 1 < argc ? std::string_view{ argv[i + 1] } : std::string_view{};
        auto seconds = int64_t{};
        auto msec = int64_t{};
        if (arg == "--transport"sv && (value == "tcp"sv || value == "utp"sv)) {
            options.transports = { value };
        } else if (arg == "--transport"sv && value == "both"sv) {
            options.transports = { "tcp"sv, "utp"sv };
        } else if (auto mib = uint64_t{}; arg == "--size-mib"sv && parse_number(value, mib)) {
            options.size_mib = mib;
        } else if (arg == "--seconds"sv && parse_number(value, seconds)) {
            options.duration = std::chrono::seconds{ seconds };
        } else if (arg == "--sample-ms"sv && parse_number(value, msec)) {
            options.sample_interval = std::chrono::milliseconds{ msec };
        } else if (auto repeat = int{}; arg == "--repeat"sv && parse_number(value, repeat)) {
            options.repeat = repeat;
        } else if (arg == "--timeout"sv && parse_number(value, seconds)) {
            options.timeout = std::chrono::seconds{ seconds };
        } else {
            fmt::print(
                stderr,
                "usage: {} [--transport tcp|utp|both] [--size-mib N] [--seconds N] [--sample-ms N] [--repeat N] "
                "[--timeout SECONDS]\n",
                argv[0]);
            return {};
        }
    }

    return options;
}

int run(int argc, char** argv)
{
    auto const options = parseOptions(argc, argv);
    if (!options) {
        return 2;
    }

    auto config_template = (std::filesystem::temp_directory_path() / "transport-benchmark-XXXXXX").string();
    if (!tr_sys_dir_create_temp(std::data(config_template))) {
        fmt::print(stderr, "couldn't create a temporary directory\n");
        return 1;
    }
    auto const root = std::filesystem::path{ config_template };

    auto results = std::vector<Result>{};
    auto failed = false;
    {
        auto const sender = Session{ root / "sender" };
        auto const receiver = Session{ root / "receiver" };
#ifdef WITH_UTP
        useThreadSafeUtpClock(sender.get());
        useThreadSafeUtpClock(receiver.get());
#endif

        // Alternate transports so slow drift on the host affects both alike.
        for (int round = 0; round < options->repeat && !failed; ++round) {
            for (auto const transport : options->transports) {
                if (auto result = measure(sender, receiver, transport, *options)) {
                    results.emplace_back(*result);
                } else {
                    failed = true;
                    break;
                }
            }
        }
    }
    std::filesystem::remove_all(root);

    // Throughput counts from the first byte received, so the connect time is excluded.
    // CPU is the whole process: both sessions' threads.
    fmt::print("transport  size_mib  connect_s  transfer_s    MiB/s  Gbit/s   cpu_s  cpu_s/GiB\n");
    for (auto const& [transport, bytes, connect_seconds, transfer_seconds, cpu_seconds] : results) {
        auto const mib = static_cast<double>(bytes) / (1024.0 * 1024.0);
        fmt::print(
            "{:<9}  {:8.0f}  {:9.3f}  {:10.3f}  {:7.1f}  {:6.2f}  {:6.2f}  {:9.2f}\n",
            transport,
            mib,
            connect_seconds,
            transfer_seconds,
            mib / transfer_seconds,
            mib * 1024 * 1024 * 8 / transfer_seconds / 1e9,
            cpu_seconds,
            cpu_seconds / (mib / 1024.0));
    }

    return failed ? 1 : 0;
}
} // namespace

int main(int argc, char** argv)
{
    try {
        return run(argc, argv);
    } catch (std::exception const& error) {
        fmt::print(stderr, "{}\n", error.what());
    } catch (...) {
        fmt::print(stderr, "unknown error\n");
    }
    return 1;
}
