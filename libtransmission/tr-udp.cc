// This file Copyright © Juliusz Chroboczek.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#include <algorithm> // std::any_of, std::min
#include <array>
#include <cerrno>
#include <cstddef>
#include <cstdint> // uint16_t
#include <cstring> // std::memcpy
#include <memory>
#include <string>
#include <utility> // std::cmp_less

#ifdef _WIN32
#include <ws2tcpip.h>
#else
#include <netinet/in.h> // IPPROTO_UDP
#include <sys/socket.h> // setsockopt, SOL_SOCKET, bind
#endif

#ifdef __linux__
#include <netinet/udp.h> // UDP_GRO, UDP_SEGMENT
#endif

#include <event2/event.h>

#include <fmt/format.h>

#include "libtransmission/crypto-utils.h" // tr_base64_encode()
#include "libtransmission/log.h"
#include "libtransmission/net.h"
#include "libtransmission/session.h"
#include "libtransmission/string-utils.h"
#include "libtransmission/tr-assert.h"
#include "libtransmission/macros.h"
#include "libtransmission/tr-utp.h"

namespace
{
[[nodiscard]] bool is_loopback(sockaddr const* const sa)
{
    auto const addrport = tr_socket_address::from_sockaddr(sa);
    return addrport && (addrport->address().is_ipv4_loopback() || addrport->address().is_ipv6_loopback());
}

// Since we use a single UDP socket in order to implement multiple
// µTP sockets, try to set up huge buffers.
void set_socket_buffers(tr_socket_t fd, bool large)
{
    static auto constexpr RecvBufferSize = 4 * 1024 * 1024;
    static auto constexpr SendBufferSize = 1 * 1024 * 1024;
    static auto constexpr SmallBufferSize = 32 * 1024;

    int rbuf = 0;
    int sbuf = 0;
    socklen_t rbuf_len = sizeof(rbuf);
    socklen_t sbuf_len = sizeof(sbuf);

    int size = large ? RecvBufferSize : SmallBufferSize;
    if (setsockopt(fd, SOL_SOCKET, SO_RCVBUF, reinterpret_cast<char const*>(&size), sizeof(size)) < 0) {
        tr_logAddDebug(fmt::format("Couldn't set receive buffer: {}", tr_net_strerror(sockerrno)));
    }

    size = large ? SendBufferSize : SmallBufferSize;
    if (setsockopt(fd, SOL_SOCKET, SO_SNDBUF, reinterpret_cast<char const*>(&size), sizeof(size)) < 0) {
        tr_logAddDebug(fmt::format("Couldn't set send buffer: {}", tr_net_strerror(sockerrno)));
    }

    if (large) {
        if (getsockopt(fd, SOL_SOCKET, SO_RCVBUF, reinterpret_cast<char*>(&rbuf), &rbuf_len) < 0) {
            rbuf = 0;
        }

        if (getsockopt(fd, SOL_SOCKET, SO_SNDBUF, reinterpret_cast<char*>(&sbuf), &sbuf_len) < 0) {
            sbuf = 0;
        }

        if (rbuf < RecvBufferSize) {
            tr_logAddDebug(fmt::format("Couldn't set receive buffer: requested {}, got {}", RecvBufferSize, rbuf));
#ifdef __linux__
            tr_logAddDebug(fmt::format("Please add the line 'net.core.rmem_max = {}' to /etc/sysctl.conf", RecvBufferSize));
#endif
        }

        if (sbuf < SendBufferSize) {
            tr_logAddDebug(fmt::format("Couldn't set send buffer: requested {}, got {}", SendBufferSize, sbuf));
#ifdef __linux__
            tr_logAddDebug(fmt::format("Please add the line 'net.core.wmem_max = {}' to /etc/sysctl.conf", SendBufferSize));
#endif
        }
    }
}

// Hands one datagram to its protocol. Returns true for a µTP packet.
// buf must have room for one more byte after buflen.
bool handle_packet(
    tr_session* const session,
    unsigned char* const buf,
    size_t const buflen,
    sockaddr* const from_sa,
    socklen_t const fromlen)
{
    auto const from_str = [from_sa] {
        return tr_socket_address::from_sockaddr(from_sa).value_or(tr_socket_address{}).display_name();
    };

    // Since most packets we receive here are µTP, make quick inline
    // checks for the other protocols. The logic is as follows:
    // - all DHT packets start with 'd' (100)
    // - all UDP tracker packets start with a 32-bit (!) "action", which
    //   is between 0 and 3
    // - the above cannot be µTP packets, since these start with a 4-bit
    //   "type" between 0 and 4, followed by a 4-bit version number (1)
    if (buf[0] == 'd') {
        if (session->dht_) {
            buf[buflen] = '\0'; // libdht requires zero-terminated messages
            session->dht_->handle_message(buf, buflen, from_sa, fromlen);
        }
    } else if (buflen >= 8 && buf[0] == 0 && buf[1] == 0 && buf[2] == 0 && buf[3] <= 3) {
        if (!session->announcer_udp_->handle_message(buf, buflen, from_sa, fromlen)) {
            tr_logAddTrace(fmt::format("{} Couldn't parse UDP tracker packet.", from_str()));
        }
    } else if (session->allowsUTP() && session->utp_context != nullptr) {
        if (tr_utp_packet(buf, buflen, from_sa, fromlen, session)) {
            return true;
        }

        tr_logAddTrace(
            fmt::format(
                "{} Unexpected UDP packet... len {} [{}]",
                from_str(),
                buflen,
                tr_base64_encode({ reinterpret_cast<char const*>(buf), buflen })));
    }

    return false;
}

#ifdef UDP_GRO
// With UDP_GRO, the kernel can hand over many datagrams of one flow as one read, up to 64 KiB.
auto constexpr DatagramBufferSize = size_t{ 65536U };
#else
auto constexpr DatagramBufferSize = size_t{ 8192U };
#endif

// Lets the kernel coalesce incoming datagrams of one flow into one read, where it can.
void enable_receive_coalescing([[maybe_unused]] tr_socket_t const sock)
{
#ifdef UDP_GRO
    int const on = 1;
    (void)setsockopt(sock, IPPROTO_UDP, UDP_GRO, &on, sizeof(on));
#endif
}

// Hands each datagram in `buf` to handle_packet(). With UDP_GRO, `buf` can hold several
// datagrams of `segment_size` bytes each, the last possibly shorter.
bool handle_read(
    tr_session* const session,
    unsigned char* const buf,
    size_t const buflen,
    size_t const segment_size,
    sockaddr* const from_sa,
    socklen_t const fromlen)
{
    if (segment_size >= buflen) {
        return handle_packet(session, buf, buflen, from_sa, fromlen);
    }

    auto got_utp_packet = false;
    for (size_t offset = 0; offset < buflen; offset += segment_size) {
        auto* const segment = buf + offset;
        auto const len = std::min(segment_size, buflen - offset);
        if (segment[0] == 'd') {
            // handle_packet() zero-terminates DHT packets, which would overwrite the next segment
            thread_local auto copy = std::array<unsigned char, DatagramBufferSize>{};
            std::memcpy(std::data(copy), segment, len);
            got_utp_packet |= handle_packet(session, std::data(copy), len, from_sa, fromlen);
        } else {
            got_utp_packet |= handle_packet(session, segment, len, from_sa, fromlen);
        }
    }
    return got_utp_packet;
}

#ifdef HAVE_RECVMMSG
// Reads per recvmmsg() call. µTP at full speed delivers tens of thousands of
// packets per second, so one syscall per datagram is a large part of its cost.
auto constexpr DatagramsPerRead = size_t{ 1024U * 1024U } / DatagramBufferSize / 2U;

// Reads every pending datagram; returns true if any was a µTP packet.
bool read_datagrams(evutil_socket_t const s, tr_session* const session)
{
    struct Datagram {
        std::array<unsigned char, DatagramBufferSize> buf;
        sockaddr_storage from;
        iovec iov;
        alignas(cmsghdr) std::array<unsigned char, CMSG_SPACE(sizeof(int))> control;
    };
    // Per thread, so that a read neither shares nor value-initialises the buffers.
    thread_local auto datagrams = std::array<Datagram, DatagramsPerRead>{};
    thread_local auto headers = std::array<mmsghdr, DatagramsPerRead>{};

    auto got_utp_packet = false;
    for (;;) {
        for (size_t i = 0; i < DatagramsPerRead; ++i) {
            auto& datagram = datagrams[i];
            datagram.iov = { .iov_base = std::data(datagram.buf), .iov_len = std::size(datagram.buf) - 1U };
            headers[i] = {};
            headers[i].msg_hdr.msg_name = &datagram.from;
            headers[i].msg_hdr.msg_namelen = sizeof(datagram.from);
            headers[i].msg_hdr.msg_iov = &datagram.iov;
            headers[i].msg_hdr.msg_iovlen = 1;
            headers[i].msg_hdr.msg_control = std::data(datagram.control);
            headers[i].msg_hdr.msg_controllen = std::size(datagram.control);
        }

        auto const n_read = recvmmsg(s, std::data(headers), DatagramsPerRead, 0, nullptr);
        if (n_read <= 0) {
            return got_utp_packet;
        }

        for (int i = 0; i < n_read; ++i) {
            auto& datagram = datagrams[i];
            auto& header = headers[i];
            // An empty datagram leaves the buffer's old bytes in place, and a truncated one
            // is not the datagram its sender wrote; no protocol here sends either. Without
            // all its control data, a read's segment size is unknown.
            if (header.msg_len == 0U || (header.msg_hdr.msg_flags & (MSG_TRUNC | MSG_CTRUNC)) != 0) {
                continue;
            }
            auto segment_size = size_t{ header.msg_len };
#ifdef UDP_GRO
            for (auto* cmsg = CMSG_FIRSTHDR(&header.msg_hdr); cmsg != nullptr; cmsg = CMSG_NXTHDR(&header.msg_hdr, cmsg)) {
                if (cmsg->cmsg_level == IPPROTO_UDP && cmsg->cmsg_type == UDP_GRO) {
                    auto gso_size = int{};
                    std::memcpy(&gso_size, CMSG_DATA(cmsg), sizeof(gso_size));
                    segment_size = gso_size > 0 ? static_cast<size_t>(gso_size) : segment_size;
                }
            }
#endif
            got_utp_packet |= handle_read(
                session,
                std::data(datagram.buf),
                header.msg_len,
                segment_size,
                reinterpret_cast<sockaddr*>(&datagram.from),
                header.msg_hdr.msg_namelen);
        }

        // a short batch means the socket is drained
        if (std::cmp_less(n_read, DatagramsPerRead)) {
            return got_utp_packet;
        }
    }
}
#else
// Reads every pending datagram; returns true if any was a µTP packet.
bool read_datagrams(evutil_socket_t const s, tr_session* const session)
{
    thread_local auto buf = std::array<unsigned char, DatagramBufferSize>{};
    auto got_utp_packet = false;

    for (;;) {
        auto from = sockaddr_storage{};
        auto fromlen = socklen_t{ sizeof(from) };
        auto* const from_sa = reinterpret_cast<sockaddr*>(&from);
        auto const n_read = recvfrom(s, reinterpret_cast<char*>(std::data(buf)), std::size(buf) - 1, 0, from_sa, &fromlen);
        if (n_read <= 0) {
            return got_utp_packet;
        }

        got_utp_packet |= handle_packet(session, std::data(buf), static_cast<size_t>(n_read), from_sa, fromlen);
    }
}
#endif

void event_callback(evutil_socket_t s, [[maybe_unused]] short type, void* vsession)
{
    TR_ASSERT(vsession != nullptr);
    TR_ASSERT(type == EV_READ);

    auto* const session = static_cast<tr_session*>(vsession);
    if (read_datagrams(s, session)) {
        // To reduce protocol overhead, we wait until we've read all UDP packets
        // we can, then send one ACK for each µTP socket that received packet(s).
        tr_utp_issue_deferred_acks(session);
    }
}

void log_send_error(sockaddr const* const to, int const error_code)
{
    auto display_name = std::string{};
    if (auto const addrport = tr_socket_address::from_sockaddr(to)) {
        display_name = addrport->display_name();
    }

    tr_logAddWarn(
        fmt::format(
            "Couldn't send to {address}: {errno} ({error})",
            fmt::arg("address", display_name),
            fmt::arg("errno", error_code),
            fmt::arg("error", tr_strerror(error_code))));
}
} // namespace

#ifdef HAVE_SENDMMSG
// Datagrams waiting to go out on one socket in one sendmmsg() call.
//
// With UDP_SEGMENT, consecutive datagrams to one address and of one size (the last may be
// shorter) go out as one message that the kernel splits as late as it can, as it does
// for TCP. That saves per-datagram work through the whole network stack.
class tr_session::tr_udp_core::SendQueue
{
public:
    // µTP packets stay below 1500 bytes; anything larger is sent on its own.
    static constexpr auto MaxDatagramSize = size_t{ 2048U };
    static constexpr auto Capacity = size_t{ 64U };

    [[nodiscard]] static constexpr bool fits(size_t const buflen, socklen_t const tolen) noexcept
    {
        return buflen <= MaxDatagramSize && tolen <= sizeof(sockaddr_storage);
    }

    // Returns true when the queue is full.
    bool add(void const* const buf, size_t const buflen, sockaddr const* const to, socklen_t const tolen)
    {
        TR_ASSERT(fits(buflen, tolen));
        TR_ASSERT(size_ < Capacity);
        auto& datagram = datagrams_[size_];
        std::memcpy(std::data(datagram.buf), buf, buflen);
        std::memcpy(&datagram.to, to, tolen);
        datagram.tolen = tolen;
        iovs_[size_] = { .iov_base = std::data(datagram.buf), .iov_len = buflen };
        return ++size_ == Capacity;
    }

    void flush(tr_socket_t const sock)
    {
        for (size_t next = 0; next < size_;) {
            auto const n_messages = build_messages(next);
            if (auto const n_sent = sendmmsg(sock, std::data(messages_), n_messages, 0); n_sent > 0) {
                next = message_ends_[n_sent - 1];
                continue;
            }

            // sendmmsg() reports an error only for the first message
            auto const error_code = errno;
            if (message_ends_[0] - next > 1U && is_gso_error(error_code)) {
                // Without UDP_SEGMENT support no send can segment. Otherwise the refusal is about
                // the route, such as an MTU below the segment size, so it holds for this address;
                // sent unsegmented, an oversized datagram is fragmented instead.
                if (error_code == ENOPROTOOPT || error_code == EOPNOTSUPP) {
                    use_gso_ = false;
                } else {
                    refuse_gso(next);
                }
                tr_logAddDebug(fmt::format("UDP segmentation offload refused: {}", tr_strerror(error_code)));
                continue;
            }
            log_send_error(reinterpret_cast<sockaddr const*>(&datagrams_[next].to), error_code);
            next = message_ends_[0];
        }
        size_ = 0;
    }

private:
#ifdef UDP_SEGMENT
    // Linux accepts up to 64 segments and 64 KiB per message.
    static constexpr auto MaxSegments = size_t{ 64U };
    static constexpr auto MaxSegmentedBytes = size_t{ 63U * 1024U };
#endif

    [[nodiscard]] static bool is_gso_error(int const error_code) noexcept
    {
        return error_code == EMSGSIZE || error_code == EIO || error_code == EINVAL || error_code == ENOPROTOOPT ||
            error_code == EOPNOTSUPP;
    }

    void refuse_gso(size_t const datagram)
    {
        auto& refused = gso_refused_[n_gso_refused_++ % std::size(gso_refused_)];
        refused.tolen = datagrams_[datagram].tolen;
        std::memcpy(&refused.to, &datagrams_[datagram].to, refused.tolen);
    }

    [[nodiscard]] bool is_gso_refused(size_t const datagram) const noexcept
    {
        auto const& sent = datagrams_[datagram];
        auto const n = std::min(n_gso_refused_, std::size(gso_refused_));
        return std::any_of(std::begin(gso_refused_), std::begin(gso_refused_) + n, [&sent](auto const& refused) {
            return refused.tolen == sent.tolen && std::memcmp(&refused.to, &sent.to, sent.tolen) == 0;
        });
    }

    [[nodiscard]] bool same_destination(size_t const a, size_t const b) const noexcept
    {
        return datagrams_[a].tolen == datagrams_[b].tolen &&
            std::memcmp(&datagrams_[a].to, &datagrams_[b].to, datagrams_[a].tolen) == 0;
    }

    // Fills messages_ from datagram `first` on; returns how many messages it made.
    size_t build_messages(size_t const first)
    {
        auto n_messages = size_t{};
        for (auto start = first; start < size_; ++n_messages) {
            auto n_datagrams = size_t{ 1U };
            auto const segment_size = iovs_[start].iov_len;
#ifdef UDP_SEGMENT
            auto const may_segment = use_gso_ && start + 1U < size_ && !is_gso_refused(start);
            for (auto bytes = segment_size; may_segment && start + n_datagrams < size_ && n_datagrams < MaxSegments;) {
                auto const len = iovs_[start + n_datagrams].iov_len;
                if (!same_destination(start, start + n_datagrams) || len > segment_size || bytes + len > MaxSegmentedBytes) {
                    break;
                }
                bytes += len;
                ++n_datagrams;
                if (len < segment_size) {
                    break; // only the last segment may be shorter
                }
            }
#endif

            auto& message = messages_[n_messages];
            message = {};
            message.msg_hdr.msg_name = &datagrams_[start].to;
            message.msg_hdr.msg_namelen = datagrams_[start].tolen;
            message.msg_hdr.msg_iov = &iovs_[start];
            message.msg_hdr.msg_iovlen = n_datagrams;
#ifdef UDP_SEGMENT
            if (n_datagrams > 1U) {
                auto& control = controls_[n_messages];
                message.msg_hdr.msg_control = std::data(control);
                message.msg_hdr.msg_controllen = std::size(control);
                auto* const cmsg = CMSG_FIRSTHDR(&message.msg_hdr);
                cmsg->cmsg_level = IPPROTO_UDP;
                cmsg->cmsg_type = UDP_SEGMENT;
                cmsg->cmsg_len = CMSG_LEN(sizeof(uint16_t));
                auto const gso_size = static_cast<uint16_t>(segment_size);
                std::memcpy(CMSG_DATA(cmsg), &gso_size, sizeof(gso_size));
            }
#endif
            start += n_datagrams;
            message_ends_[n_messages] = start;
        }
        return n_messages;
    }

    struct Datagram {
        std::array<unsigned char, MaxDatagramSize> buf;
        sockaddr_storage to;
        socklen_t tolen;
    };

    std::array<Datagram, Capacity> datagrams_ = {};
    std::array<iovec, Capacity> iovs_ = {}; // contiguous, so one message can span several datagrams
    std::array<mmsghdr, Capacity> messages_ = {};
    std::array<size_t, Capacity> message_ends_ = {}; // one past each message's last datagram
#ifdef UDP_SEGMENT
    struct alignas(cmsghdr) Control : std::array<unsigned char, CMSG_SPACE(sizeof(uint16_t))> {};
    std::array<Control, Capacity> controls_ = {};
#endif
    struct Destination {
        sockaddr_storage to;
        socklen_t tolen;
    };
    // The latest addresses the kernel refused segmented sends to
    std::array<Destination, 8> gso_refused_ = {};
    size_t n_gso_refused_ = 0;

    size_t size_ = 0;
    bool use_gso_ = true;
};
#else
// No batched sends here: sendto_soon() always falls back to sendto().
class tr_session::tr_udp_core::SendQueue
{
public:
    [[nodiscard]] static constexpr bool fits(size_t /*buflen*/, socklen_t /*tolen*/) noexcept
    {
        return false;
    }

    // NOLINTNEXTLINE(readability-convert-member-functions-to-static): same calls as the batching SendQueue
    bool add(void const* /*buf*/, size_t /*buflen*/, sockaddr const* /*to*/, socklen_t /*tolen*/)
    {
        return true;
    }

    void flush(tr_socket_t /*sock*/)
    {
    }
};
#endif

// BEP-32 explains why we need to bind to one IPv6 address

tr_session::tr_udp_core::tr_udp_core(tr_session& session, tr_port udp_port)
    : udp_port_{ udp_port }
    , session_{ session }
    , send_queue4_{ std::make_unique<SendQueue>() }
    , send_queue6_{ std::make_unique<SendQueue>() }
    , flush_timer_{ session.timerMaker().create([this] {
        is_flush_scheduled_ = false;
        flush();
    }) }
{
    if (std::empty(udp_port_)) {
        return;
    }

    if (!session.has_ip_protocol(TR_AF_INET)) {
        // no IPv4; do nothing
    } else if (tr_socket_t const sock = socket(PF_INET, SOCK_DGRAM, 0); is_valid_socket(sock)) {
        (void)evutil_make_listen_socket_reuseable(static_cast<evutil_socket_t>(sock));

        auto const addr = session_.bind_address(TR_AF_INET);
        auto const [ss, sslen] = tr_socket_address::to_sockaddr(addr, udp_port_);

        if (evutil_make_socket_nonblocking(static_cast<evutil_socket_t>(sock)) != 0) {
            auto const error_code = errno;
            tr_logAddWarn(
                fmt::format(
                    "Couldn't make IPv4 socket non-blocking {address}: {error} ({error_code})",
                    fmt::arg("address", tr_socket_address::display_name(addr, udp_port_)),
                    fmt::arg("error", tr_strerror(error_code)),
                    fmt::arg("error_code", error_code)));

            tr_net_close_socket(sock);
        } else if (bind(sock, reinterpret_cast<sockaddr const*>(&ss), sslen) != 0) {
            auto const error_code = errno;
            tr_logAddWarn(
                fmt::format(
                    "Couldn't bind IPv4 socket {address}: {error} ({error_code})",
                    fmt::arg("address", tr_socket_address::display_name(addr, udp_port_)),
                    fmt::arg("error", tr_strerror(error_code)),
                    fmt::arg("error_code", error_code)));

            tr_net_close_socket(sock);
        } else {
            tr_logAddInfo(fmt::format("Bound UDP IPv4 address {:s}", tr_socket_address::display_name(addr, udp_port_)));
            session_.setSocketDiffServ(sock, TR_AF_INET);
            set_socket_buffers(sock, session_.allowsUTP());
            enable_receive_coalescing(sock);
            udp4_socket_ = sock;
            udp4_event_.reset(
                tr::evhelpers::event_new_pri2(
                    session_.event_base(),
                    static_cast<evutil_socket_t>(udp4_socket_),
                    EV_READ | EV_PERSIST,
                    event_callback,
                    &session_));
            event_add(udp4_event_.get(), nullptr);
        }
    }

    if (!session.has_ip_protocol(TR_AF_INET6)) {
        // no IPv6; do nothing
    } else if (tr_socket_t const sock = socket(PF_INET6, SOCK_DGRAM, 0); is_valid_socket(sock)) {
        (void)evutil_make_listen_socket_reuseable(static_cast<evutil_socket_t>(sock));
        (void)tr_make_listen_socket_ipv6only(static_cast<evutil_socket_t>(sock));

        auto const addr = session_.bind_address(TR_AF_INET6);
        auto const [ss, sslen] = tr_socket_address::to_sockaddr(addr, udp_port_);

        if (evutil_make_socket_nonblocking(static_cast<evutil_socket_t>(sock)) != 0) {
            auto const error_code = errno;
            tr_logAddWarn(
                fmt::format(
                    "Couldn't make IPv6 socket non-blocking {address}: {error} ({error_code})",
                    fmt::arg("address", tr_socket_address::display_name(addr, udp_port_)),
                    fmt::arg("error", tr_strerror(error_code)),
                    fmt::arg("error_code", error_code)));

            tr_net_close_socket(sock);
        } else if (bind(sock, reinterpret_cast<sockaddr const*>(&ss), sslen) != 0) {
            auto const error_code = errno;
            tr_logAddWarn(
                fmt::format(
                    "Couldn't bind IPv6 socket {address}: {error} ({error_code})",
                    fmt::arg("address", tr_socket_address::display_name(addr, udp_port_)),
                    fmt::arg("error", tr_strerror(error_code)),
                    fmt::arg("error_code", error_code)));

            tr_net_close_socket(sock);
        } else {
            tr_logAddInfo(fmt::format("Bound UDP IPv6 address {:s}", tr_socket_address::display_name(addr, udp_port_)));
            session_.setSocketDiffServ(sock, TR_AF_INET6);
            set_socket_buffers(sock, session_.allowsUTP());
            enable_receive_coalescing(sock);
            udp6_socket_ = sock;
            udp6_event_.reset(
                tr::evhelpers::event_new_pri2(
                    session_.event_base(),
                    static_cast<evutil_socket_t>(udp6_socket_),
                    EV_READ | EV_PERSIST,
                    event_callback,
                    &session_));
            event_add(udp6_event_.get(), nullptr);
        }
    }

    if (!is_valid_socket(udp4_socket_) && !is_valid_socket(udp6_socket_)) {
        tr_logAddError("Couldn't create any UDP sockets.");
    }
}

tr_session::tr_udp_core::~tr_udp_core()
{
    flush_timer_.reset();
    flush();

    udp6_event_.reset();

    if (is_valid_socket(udp6_socket_)) {
        tr_net_close_socket(udp6_socket_);
        udp6_socket_ = TR_BAD_SOCKET;
    }

    udp4_event_.reset();

    if (is_valid_socket(udp4_socket_)) {
        tr_net_close_socket(udp4_socket_);
        udp4_socket_ = TR_BAD_SOCKET;
    }
}

void tr_session::tr_udp_core::sendto(void const* buf, size_t buflen, struct sockaddr const* to, socklen_t const tolen) const
{
    if (to->sa_family != AF_INET && to->sa_family != AF_INET6) {
        errno = EAFNOSUPPORT;
    } else if (auto const sock = to->sa_family == AF_INET ? udp4_socket_ : udp6_socket_; !is_valid_socket(sock)) {
        // don't warn on bad sockets; the system may not support IPv6
        return;
    } else if (!session_.has_source_address(tr_af_to_ip_protocol(to->sa_family)) && !is_loopback(to)) {
        // don't try to send if we don't have a route in this IP protocol
        return;
    }
    // NOLINTNEXTLINE(readability-redundant-casting)
    else if (::sendto(sock, static_cast<char const*>(buf), static_cast<TR_IF_WIN32(int, size_t)>(buflen), 0, to, tolen) != -1) {
        return;
    }

    log_send_error(to, errno);
}

void tr_session::tr_udp_core::sendto_soon(void const* buf, size_t buflen, struct sockaddr const* to, socklen_t const tolen)
{
    auto* queue = static_cast<SendQueue*>(nullptr);
    if (to->sa_family == AF_INET) {
        queue = send_queue4_.get();
    } else if (to->sa_family == AF_INET6) {
        queue = send_queue6_.get();
    }
    auto const sock = to->sa_family == AF_INET ? udp4_socket_ : udp6_socket_;
    if (queue == nullptr || !is_valid_socket(sock) || !SendQueue::fits(buflen, tolen) ||
        (!session_.has_source_address(tr_af_to_ip_protocol(to->sa_family)) && !is_loopback(to))) {
        // nothing to batch: sendto() sends it or explains why not
        sendto(buf, buflen, to, tolen);
        return;
    }

    if (queue->add(buf, buflen, to, tolen)) {
        queue->flush(sock);
    } else if (!is_flush_scheduled_) {
        is_flush_scheduled_ = true;
        flush_timer_->start_single_shot(std::chrono::milliseconds::zero());
    }
}

void tr_session::tr_udp_core::flush()
{
    if (send_queue4_) {
        send_queue4_->flush(udp4_socket_);
    }
    if (send_queue6_) {
        send_queue6_->flush(udp6_socket_);
    }
}
