// This file Copyright © Retransmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#include <algorithm>
#include <array>
#include <cerrno>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional> // std::less
#include <iterator> // std::back_inserter
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <utility> // std::pair
#include <vector>

#ifdef _WIN32
#include <winsock2.h> // must come before iphlpapi.h
#include <iphlpapi.h>
#include <ws2tcpip.h>
#else
#include <ifaddrs.h>
#include <net/if.h>
#include <sys/ioctl.h>
#endif

#ifdef __linux__
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#endif

#ifdef __APPLE__
#include <netinet6/in6_var.h> // SIOCGIFAFLAG_IN6
#endif

#include <event2/util.h>

#include <fmt/format.h>

#include "libtransmission/error.h"
#include "libtransmission/log.h"
#include "libtransmission/net.h"
#include "libtransmission/peer-socket.h"
#include "libtransmission/session.h"
#include "libtransmission/string-utils.h"
#include "libtransmission/tr-assert.h"
#include "libtransmission/tr-strbuf.h"
#include "libtransmission/transmission.h"
#include "libtransmission/types.h"

using namespace std::literals;

std::string tr_net_strerror(int err)
{
#ifdef _WIN32

    auto buf = std::array<char, 512>{};
    (void)FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM, nullptr, err, 0, std::data(buf), std::size(buf), nullptr);
    return std::string{ tr_strv_strip(std::data(buf)) };

#else

    return std::string{ tr_strerror(err) };

#endif
}

tr_error tr_net_error(int const err)
{
    return { err, tr_net_strerror(err) };
}

std::string_view tr_ip_protocol_to_sv(tr_address_type type)
{
    using namespace std::literals;

    switch (type) {
    case TR_AF_INET:
        return "IPv4"sv;
    case TR_AF_INET6:
        return "IPv6"sv;
    default:
        TR_ASSERT_MSG(false, "invalid address family");
        return {};
    }
}

int tr_ip_protocol_to_af(tr_address_type type)
{
    switch (type) {
    case TR_AF_INET:
        return AF_INET;
    case TR_AF_INET6:
        return AF_INET6;
    default:
        TR_ASSERT_MSG(false, "invalid address family");
        return {};
    }
}

tr_address_type tr_af_to_ip_protocol(int af)
{
    switch (af) {
    case AF_INET:
        return TR_AF_INET;
    case AF_INET6:
        return TR_AF_INET6;
    default:
        TR_ASSERT_MSG(false, "invalid address family");
        return NUM_TR_AF_INET_TYPES;
    }
}

int tr_make_listen_socket_ipv6only(tr_socket_t const sock)
{
#if defined(IPV6_V6ONLY)
    int optval = 1;
    return setsockopt(sock, IPPROTO_IPV6, IPV6_V6ONLY, reinterpret_cast<char const*>(&optval), sizeof(optval));
#else
    return 0;
#endif
}

bool tr_net_interface_is_default(std::string_view const bind_interface)
{
    return std::empty(tr_strv_strip(bind_interface));
}

bool tr_net_interface_is_blocked(std::string_view const bind_interface)
{
    return tr_strv_strip(bind_interface) == TrBindInterfaceBlocked;
}

std::string_view tr_net_effective_bind_interface(std::string_view const bind_interface)
{
    return tr_strv_strip(bind_interface);
}

bool tr_net_is_bind_address(std::string_view const value, tr_address_type const type)
{
    auto const stripped = tr_strv_strip(value);
    if (std::empty(stripped)) {
        return true;
    }

    // from_string() also takes "1.2.3.4:80" and "[::1]:80", but the setting
    // is bound as written, so it must be the address alone
    if (stripped.find_first_of(type == TR_AF_INET ? ":"sv : "[]"sv) != std::string_view::npos) {
        return false;
    }

    auto const addr = tr_address::from_string(stripped);
    return addr && addr->type == type;
}

unsigned tr_net_interface_index(std::string_view const bind_interface)
{
    auto const name = tr_net_effective_bind_interface(bind_interface);
    if (std::empty(name) || tr_net_interface_is_blocked(name)) {
        return 0U;
    }

    return if_nametoindex(std::string{ name }.c_str());
}

namespace
{
#ifdef __linux__
// The origins of the IPv6 addresses, keyed by interface index and address.
// getifaddrs() leaves out the flags that tell them apart, so this asks the
// kernel directly. Empty if it can't be asked.
[[nodiscard]] std::map<std::pair<unsigned, tr_address>, tr_net_address_origin> ipv6_address_origins()
{
    auto origins = std::map<std::pair<unsigned, tr_address>, tr_net_address_origin>{};

    auto const fd = socket(AF_NETLINK, SOCK_RAW | SOCK_CLOEXEC, NETLINK_ROUTE);
    if (fd < 0) {
        return origins;
    }
    auto const fd_uniq = std::unique_ptr<int const, void (*)(int const*)>{ &fd, [](int const* p) { tr_net_close_socket(*p); } };

    struct {
        nlmsghdr header;
        ifaddrmsg body;
    } request = {};
    request.header.nlmsg_len = NLMSG_LENGTH(sizeof(ifaddrmsg));
    request.header.nlmsg_type = RTM_GETADDR;
    request.header.nlmsg_flags = NLM_F_REQUEST | NLM_F_DUMP;
    request.header.nlmsg_seq = 1U;
    request.body.ifa_family = AF_INET6;
    if (send(fd, &request, request.header.nlmsg_len, 0) < 0) {
        return origins;
    }

    // aligned for the header; the kernel pads every message and attribute
    // after it to NLMSG_ALIGNTO and RTA_ALIGNTO, so the casts below are safe
    alignas(nlmsghdr) auto buf = std::array<char, 32768U>{};
    for (;;) {
        auto len = recv(fd, std::data(buf), std::size(buf), 0);
        if (len <= 0) {
            return origins;
        }

        // NOLINTBEGIN(cppcoreguidelines-pro-type-reinterpret-cast,cppcoreguidelines-pro-type-cstyle-cast,cppcoreguidelines-pro-bounds-pointer-arithmetic,clang-diagnostic-cast-align)
        for (
            auto const* msg = reinterpret_cast<nlmsghdr const*>(std::data(buf)); NLMSG_OK(msg, len);
            msg = NLMSG_NEXT(msg, len)) {
            if (msg->nlmsg_type == NLMSG_DONE || msg->nlmsg_type == NLMSG_ERROR) {
                return origins;
            }
            if (msg->nlmsg_type != RTM_NEWADDR) {
                continue;
            }

            auto const* const ifa = static_cast<ifaddrmsg const*>(NLMSG_DATA(msg));
            auto flags = uint32_t{ ifa->ifa_flags };
            // IFA_LOCAL is the local address when the address has a peer, as on a
            // point-to-point link, and IFA_ADDRESS is then the peer's
            auto address = std::optional<tr_address>{};
            auto local = std::optional<tr_address>{};

            auto attr_len = IFA_PAYLOAD(msg);
            for (auto const* attr = IFA_RTA(ifa); RTA_OK(attr, attr_len); attr = RTA_NEXT(attr, attr_len)) {
                if ((attr->rta_type == IFA_ADDRESS || attr->rta_type == IFA_LOCAL) && RTA_PAYLOAD(attr) == sizeof(in6_addr)) {
                    auto addr6 = in6_addr{};
                    std::memcpy(&addr6, RTA_DATA(attr), sizeof(addr6));
                    auto& dst = attr->rta_type == IFA_LOCAL ? local : address;
                    dst = tr_address{ .type = TR_AF_INET6, .addr = { .addr6 = addr6 } };
                } else if (attr->rta_type == IFA_FLAGS && RTA_PAYLOAD(attr) == sizeof(uint32_t)) {
                    // the full flags; ifa_flags has only their low 8 bits
                    std::memcpy(&flags, RTA_DATA(attr), sizeof(flags));
                }
            }
            // NOLINTEND(cppcoreguidelines-pro-type-reinterpret-cast,cppcoreguidelines-pro-type-cstyle-cast,cppcoreguidelines-pro-bounds-pointer-arithmetic,clang-diagnostic-cast-align)

            auto const addr = local ? local : address;
            if (!addr) {
                continue;
            }

            // An address the kernel or a user added for good is permanent. The rest
            // come and go with router advertisements and leases: SLAAC hands out
            // /64s and DHCPv6 /128s.
            auto origin = tr_net_address_origin::Unknown;
            if ((flags & IFA_F_TEMPORARY) != 0U) {
                origin = tr_net_address_origin::SlaacPrivacy;
            } else if ((flags & (IFA_F_STABLE_PRIVACY | IFA_F_MANAGETEMPADDR)) != 0U) {
                origin = tr_net_address_origin::SlaacStable;
            } else if ((flags & IFA_F_PERMANENT) == 0U && ifa->ifa_prefixlen == 64U) {
                origin = tr_net_address_origin::SlaacStable;
            } else if ((flags & IFA_F_PERMANENT) == 0U && ifa->ifa_prefixlen == 128U) {
                origin = tr_net_address_origin::Dhcp;
            }

            origins.try_emplace({ ifa->ifa_index, *addr }, origin);
        }
    }
}
#endif

#if defined(__APPLE__) && defined(SIOCGIFAFLAG_IN6)
// the origin the kernel records in an IPv6 address's flags
[[nodiscard]] tr_net_address_origin ipv6_address_origin(char const* if_name, sockaddr const* sa)
{
    auto const fd = socket(AF_INET6, SOCK_DGRAM, 0);
    if (fd < 0) {
        return tr_net_address_origin::Unknown;
    }

    auto ifr = in6_ifreq{};
    std::strncpy(ifr.ifr_name, if_name, sizeof(ifr.ifr_name) - 1U);
    std::memcpy(&ifr.ifr_addr, sa, sizeof(ifr.ifr_addr));
    auto const ok = ioctl(fd, SIOCGIFAFLAG_IN6, &ifr) == 0;
    tr_net_close_socket(fd);
    if (!ok) {
        return tr_net_address_origin::Unknown;
    }

    auto const flags = ifr.ifr_ifru.ifru_flags6;
    if ((flags & IN6_IFF_TEMPORARY) != 0) {
        return tr_net_address_origin::SlaacPrivacy;
    }
    if ((flags & IN6_IFF_AUTOCONF) != 0) {
        return tr_net_address_origin::SlaacStable;
    }
    if ((flags & IN6_IFF_DYNAMIC) != 0) {
        return tr_net_address_origin::Dhcp;
    }
    return tr_net_address_origin::Unknown;
}
#endif

// an IPv6 address's special-purpose range (RFC 6890's registry) that an
// interface's own address can be in, or empty; "gua" is the rest of 2000::/3
[[nodiscard]] std::string_view ipv6_range_tag(tr_address const& address)
{
    if (address.is_ipv6_loopback()) {
        return "loopback"sv;
    }
    if (address.is_ipv6_link_local()) {
        return "link-local"sv;
    }
    // both carved out of 2000::/3, so checked before the table's "gua"
    if (address.is_ipv6_teredo()) {
        return "teredo"sv;
    }
    if (address.is_ipv6_6to4()) {
        return "6to4"sv;
    }

    // the ranges tr_address has no predicate for, most specific first
    struct Range {
        std::array<uint8_t, 6U> prefix;
        size_t bits;
        std::string_view tag;
    };
    static auto constexpr Ranges = std::array<Range, 7U>{ {
        { .prefix = { 0x00, 0x64, 0xFF, 0x9B, 0x00, 0x01 }, .bits = 48U, .tag = "nat64"sv }, // local-use, RFC 8215
        { .prefix = { 0x00, 0x64, 0xFF, 0x9B, 0x00, 0x00 }, .bits = 96U, .tag = "nat64"sv }, // well-known, RFC 6052
        { .prefix = { 0x20, 0x01, 0x0D, 0xB8 }, .bits = 32U, .tag = "documentation"sv }, // RFC 3849
        { .prefix = { 0x3F, 0xFF }, .bits = 20U, .tag = "documentation"sv }, // RFC 9637
        { .prefix = { 0x20 }, .bits = 3U, .tag = "gua"sv }, // global unicast, RFC 4291
        { .prefix = { 0xFC }, .bits = 7U, .tag = "ula"sv }, // RFC 4193
        { .prefix = { 0xFE, 0xC0 }, .bits = 10U, .tag = "site-local"sv }, // deprecated by RFC 3879
    } };

    auto const& bytes = address.addr.addr6.s6_addr;
    auto const in_range = [&bytes](Range const& range) {
        // the 96-bit NAT64 prefix is its first 4 bytes followed by zeros
        auto const n_bytes = std::min<size_t>(range.bits, 48U) / 8U;
        for (size_t i = 0; i < n_bytes; ++i) {
            if (bytes[i] != range.prefix[i]) {
                return false;
            }
        }
        if (range.bits == 96U) {
            return std::all_of(&bytes[6], &bytes[12], [](auto const byte) { return byte == 0U; });
        }
        if (auto const rest = range.bits % 8U; rest != 0U && range.bits < 48U) {
            auto const mask = static_cast<uint8_t>(0xFFU << (8U - rest));
            return (bytes[n_bytes] & mask) == (range.prefix[n_bytes] & mask);
        }
        return true;
    };

    auto const range = std::ranges::find_if(Ranges, in_range);
    return range != std::end(Ranges) ? range->tag : std::string_view{};
}
} // namespace

std::vector<std::string_view> tr_net_address_tags(tr_net_interface_address const& address)
{
    auto tags = std::vector<std::string_view>{};
    if (!address.address.is_ipv6()) {
        return tags;
    }

    if (auto const range = ipv6_range_tag(address.address); !std::empty(range)) {
        tags.emplace_back(range);
    }

    switch (address.origin) {
    case tr_net_address_origin::Dhcp:
        tags.emplace_back("dhcp"sv);
        break;
    case tr_net_address_origin::SlaacStable:
        tags.emplace_back("slaac-stable"sv);
        break;
    case tr_net_address_origin::SlaacPrivacy:
        tags.emplace_back("slaac-privacy"sv);
        break;
    case tr_net_address_origin::Unknown:
        break;
    }

    return tags;
}

std::vector<tr_net_interface> tr_net_interfaces()
{
    auto interfaces = std::vector<tr_net_interface>{};

#if defined(__APPLE__) || defined(__linux__)
    // if_nameindex() also lists the interfaces that have no address yet
    auto const list = std::unique_ptr<struct if_nameindex, void (*)(struct if_nameindex*)>{ if_nameindex(), if_freenameindex };
    if (!list) {
        return interfaces;
    }

    auto index_by_name = std::map<std::string, unsigned, std::less<>>{};
    for (auto const* cur = list.get(); cur->if_index != 0U; ++cur) {
        interfaces.push_back({ .name = cur->if_name, .addresses = {} });
        index_by_name.try_emplace(cur->if_name, cur->if_index);
    }

    std::ranges::sort(interfaces, {}, &tr_net_interface::name);

    struct ifaddrs* ifa = nullptr;
    if (getifaddrs(&ifa) != 0) {
        return interfaces;
    }

#ifdef __linux__
    auto const origins = ipv6_address_origins();
#endif

    auto const ifa_uniq = std::unique_ptr<ifaddrs, void (*)(struct ifaddrs*)>{ ifa, freeifaddrs };
    for (; ifa != nullptr; ifa = ifa->ifa_next) {
        auto const if_addr = tr_socket_address::from_sockaddr(ifa->ifa_addr);
        if (!if_addr) {
            continue;
        }

        // Linux lists a labelled IPv4 address under its label, e.g. "eth0:1",
        // which names no interface: the address belongs to "eth0"
        auto name = std::string_view{ ifa->ifa_name };
        if (!index_by_name.contains(name)) {
            name = name.substr(0, name.find(':'));
        }

        auto const iter = std::ranges::lower_bound(interfaces, name, {}, &tr_net_interface::name);
        if (iter == std::end(interfaces) || iter->name != name) {
            continue;
        }

        auto& entry = iter->addresses.emplace_back(tr_net_interface_address{ .address = if_addr->address() });
        if (!entry.address.is_ipv6()) {
            continue;
        }

#ifdef __linux__
        if (auto const index = index_by_name.find(iter->name); index != std::end(index_by_name)) {
            if (auto const origin = origins.find({ index->second, entry.address }); origin != std::end(origins)) {
                entry.origin = origin->second;
            }
        }
#elif defined(__APPLE__) && defined(SIOCGIFAFLAG_IN6)
        entry.origin = ipv6_address_origin(ifa->ifa_name, ifa->ifa_addr);
#endif
    }

    // IPv4 first; then IPv6 from the widest reach to the narrowest, and the
    // addresses that last before the temporary ones within a range
    auto const rank = [](tr_net_interface_address const& entry) {
        if (entry.address.is_ipv4()) {
            return std::pair{ 0, 0 };
        }

        static auto constexpr RangeOrder = std::array{ "gua"sv, "ula"sv, "nat64"sv };
        auto const tag = ipv6_range_tag(entry.address);
        auto range = static_cast<int>(std::size(RangeOrder)) + 1;
        if (auto const iter = std::ranges::find(RangeOrder, tag); iter != std::end(RangeOrder)) {
            range = static_cast<int>(iter - std::begin(RangeOrder)) + 1;
        } else if (tag == "link-local"sv || tag == "loopback"sv) {
            range += 1;
        }

        auto origin = 3;
        switch (entry.origin) {
        case tr_net_address_origin::SlaacStable:
            origin = 0;
            break;
        case tr_net_address_origin::Dhcp:
            origin = 1;
            break;
        case tr_net_address_origin::SlaacPrivacy:
            origin = 2;
            break;
        case tr_net_address_origin::Unknown:
            break;
        }

        return std::pair{ range, origin };
    };

    for (auto& iface : interfaces) {
        std::ranges::stable_sort(iface.addresses, {}, rank);
    }
#endif

    return interfaces;
}

#if defined(__APPLE__) || defined(__linux__)
namespace
{
// The interface binding failure last warned about, so an outage warns once
// instead of once per socket. Sockets are bound on more than one thread.
struct BindFailureWarning {
    std::mutex mutex;
    std::string name;
    int err = 0;
};

[[nodiscard]] BindFailureWarning& bind_failure_warning()
{
    static auto warning = BindFailureWarning{};
    return warning;
}

// true the first time binding to `name` fails with `err` since a socket last bound
[[nodiscard]] bool should_warn_bind_failure(std::string_view const name, int const err)
{
    auto& warning = bind_failure_warning();
    auto const lock = std::scoped_lock{ warning.mutex };
    if (warning.name == name && warning.err == err) {
        return false;
    }
    warning.name = name;
    warning.err = err;
    return true;
}

// a successful bind ends the outage, so the next one warns again
void forget_bind_failure()
{
    auto& warning = bind_failure_warning();
    auto const lock = std::scoped_lock{ warning.mutex };
    warning.name.clear();
    warning.err = 0;
}

// Each interface's last refused bind, for tr_net_interface_status().
// Kept per interface, and apart from BindFailureWarning, which any
// successful bind clears: the RPC listener binding to its own interface
// mustn't hide that peer sockets are failing, nor its failures theirs.
struct BindRefusals {
    std::mutex mutex;
    std::map<std::string, int, std::less<>> err_by_name;
};

[[nodiscard]] BindRefusals& bind_refusals()
{
    static auto refusals = BindRefusals{};
    return refusals;
}

void record_bind_outcome(std::string_view const name, int const err)
{
    auto& refusals = bind_refusals();
    auto const lock = std::scoped_lock{ refusals.mutex };
    if (err != 0) {
        refusals.err_by_name.insert_or_assign(std::string{ name }, err);
    } else if (auto const iter = refusals.err_by_name.find(name); iter != std::end(refusals.err_by_name)) {
        refusals.err_by_name.erase(iter);
    }
}

#if defined(__APPLE__)
[[nodiscard]] bool bind_socket_to_interface(
    tr_socket_t const sock,
    tr_address_type const type,
    std::string_view /*name*/,
    unsigned const if_index)
{
    auto const level = type == TR_AF_INET ? IPPROTO_IP : IPPROTO_IPV6;
    auto const option = type == TR_AF_INET ? IP_BOUND_IF : IPV6_BOUND_IF;
    auto const index = static_cast<int>(if_index);
    return setsockopt(sock, level, option, reinterpret_cast<char const*>(&index), sizeof(index)) != -1;
}
#elif defined(__linux__)
[[nodiscard]] bool bind_socket_to_interface(
    tr_socket_t const sock,
    tr_address_type /*type*/,
    std::string_view const name,
    unsigned /*if_index*/)
{
    // one option covers both address families, and it filters what the socket receives too
    auto const ifname = std::string{ name };
    return setsockopt(sock, SOL_SOCKET, SO_BINDTODEVICE, ifname.c_str(), static_cast<socklen_t>(std::size(ifname) + 1U)) != -1;
}
#endif

[[nodiscard]] std::string bind_failure_reason([[maybe_unused]] int const err)
{
#ifdef __linux__
    // SO_BINDTODEVICE needs CAP_NET_RAW on kernels before 5.7
    if (err == EPERM) {
        return fmt::format("{} ({}); binding to an interface needs Linux 5.7 or CAP_NET_RAW", tr_net_strerror(err), err);
    }
#endif
    return fmt::format("{} ({})", tr_net_strerror(err), err);
}
} // namespace
#endif

tr_net_bind_status tr_net_interface_status(std::string_view const bind_interface)
{
    using State = tr_net_bind_status::State;

    auto const name = tr_net_effective_bind_interface(bind_interface);
    if (std::empty(name)) {
        return { .state = State::Default, .error = {} };
    }
    if (tr_net_interface_is_blocked(name)) {
        return { .state = State::Blocked, .error = {} };
    }

#if defined(__APPLE__) || defined(__linux__)
    // checked live, since no bind is attempted on an interface that's gone
    if (tr_net_interface_index(name) == 0U) {
        return { .state = State::Missing, .error = {} };
    }

    auto& refusals = bind_refusals();
    auto const lock = std::scoped_lock{ refusals.mutex };
    if (auto const iter = refusals.err_by_name.find(name); iter != std::end(refusals.err_by_name)) {
        return { .state = State::BindFailed, .error = bind_failure_reason(iter->second) };
    }
    return { .state = State::Ok, .error = {} };
#else
    return { .state = State::Unsupported, .error = {} };
#endif
}

bool tr_netSetSocketInterface(
    tr_socket_t const sock,
    [[maybe_unused]] tr_address_type const type,
    std::string_view const bind_interface,
    bool const suppress_msgs)
{
    auto const name = tr_net_effective_bind_interface(bind_interface);

#if defined(__APPLE__) || defined(__linux__)
    // a socket under another binding ends any outage, so the next one warns again
    if (std::empty(name) || tr_net_interface_is_blocked(name)) {
        forget_bind_failure();
    }
#endif

    if (std::empty(name)) {
        return true;
    }

    // a deliberate "bind to nothing" setting: refuse quietly, since
    // the session logs the policy once when the setting changes
    if (tr_net_interface_is_blocked(name)) {
        tr_logAddTrace(fmt::format("Refusing socket {}: interface binding is '{}'", sock, name));
        set_sockerrno(ENETDOWN);
        return false;
    }

#if defined(__APPLE__) || defined(__linux__)
    // looked up first so a missing interface fails the same way on every platform
    auto const if_index = tr_net_interface_index(name);
    if (if_index == 0U) {
        if (!suppress_msgs && should_warn_bind_failure(name, ENXIO)) {
            tr_logAddWarn(fmt::format("Couldn't bind socket to interface '{}': interface not found", name));
        } else {
            tr_logAddTrace(fmt::format("Couldn't bind socket {} to interface '{}': interface not found", sock, name));
        }
        // after logging, which may change errno
        set_sockerrno(ENXIO);
        return false;
    }

    if (!bind_socket_to_interface(sock, type, name, if_index)) {
        auto const err = sockerrno;
        record_bind_outcome(name, err);
        if (!suppress_msgs && should_warn_bind_failure(name, err)) {
            tr_logAddWarn(fmt::format("Couldn't bind socket to interface '{}': {}", name, bind_failure_reason(err)));
        } else {
            tr_logAddTrace(fmt::format("Couldn't bind socket {} to interface '{}': {}", sock, name, bind_failure_reason(err)));
        }
        set_sockerrno(err);
        return false;
    }

    forget_bind_failure();
    record_bind_outcome(name, 0);
    tr_logAddTrace(fmt::format("Bound socket {} to interface '{}'", sock, name));
    return true;
#else
    // neither macOS nor Linux: no way to bind a socket to an interface.
    // The session warns once when the setting changes; per-socket failures stay quiet
    if (!suppress_msgs) {
        tr_logAddDebug(fmt::format("Interface binding is not supported on this platform: '{}'", name));
    }
    set_sockerrno(ENXIO);
    return false;
#endif
}

std::optional<std::string> tr_netCurlInterfaceString(std::string_view const bind_interface)
{
    // blocked gets no interface string: tr_web refuses the fetch before
    // curl is involved, rather than naming an interface that can't exist
    auto const name = tr_net_effective_bind_interface(bind_interface);
    if (std::empty(name) || tr_net_interface_is_blocked(name)) {
        return {};
    }

    return fmt::format("if!{}", name);
}

// - TCP Sockets

void tr_netSetDiffServ([[maybe_unused]] tr_socket_t s, [[maybe_unused]] int tos, tr_address_type type)
{
    if (!is_valid_socket(s)) {
        return;
    }

    if (type == TR_AF_INET) {
#if defined(IP_TOS) && !defined(_WIN32)

        if (setsockopt(s, IPPROTO_IP, IP_TOS, &tos, sizeof(tos)) == -1) {
            tr_logAddDebug(fmt::format("Can't set TOS '{}': {}", tos, tr_net_strerror(sockerrno)));
        }
#endif
    } else if (type == TR_AF_INET6) {
#if defined(IPV6_TCLASS) && !defined(_WIN32)
        if (setsockopt(s, IPPROTO_IPV6, IPV6_TCLASS, &tos, sizeof(tos)) == -1) {
            tr_logAddDebug(fmt::format("Can't set IPv6 QoS '{}': {}", tos, tr_net_strerror(sockerrno)));
        }
#endif
    } else {
        /* program should never reach here! */
        tr_logAddDebug("Something goes wrong while setting TOS/Traffic-Class");
    }
}

namespace
{
[[nodiscard]] tr_socket_t tr_netBindTCPImpl(
    tr_address const& addr,
    tr_port port,
    bool suppress_msgs,
    int* err_out,
    std::string_view bind_interface)
{
    TR_ASSERT(addr.is_valid());

    auto const fd = socket(tr_ip_protocol_to_af(addr.type), SOCK_STREAM, 0);
    if (!is_valid_socket(fd)) {
        *err_out = sockerrno;
        return TR_BAD_SOCKET;
    }

    if (evutil_make_socket_nonblocking(static_cast<evutil_socket_t>(fd)) == -1) {
        *err_out = sockerrno;
        tr_net_close_socket(fd);
        return TR_BAD_SOCKET;
    }

    int optval = 1;
    (void)setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, reinterpret_cast<char const*>(&optval), sizeof(optval));
    (void)evutil_make_listen_socket_reuseable(static_cast<evutil_socket_t>(fd));

    if (addr.is_ipv6() && tr_make_listen_socket_ipv6only(fd) == -1 &&
        sockerrno != ENOPROTOOPT) // if the kernel doesn't support it, ignore it
    {
        *err_out = sockerrno;
        tr_net_close_socket(fd);
        return TR_BAD_SOCKET;
    }

    if (!tr_netSetSocketInterface(fd, addr.type, bind_interface, suppress_msgs)) {
        *err_out = sockerrno;
        tr_net_close_socket(fd);
        return TR_BAD_SOCKET;
    }

    auto const [sock, addrlen] = tr_socket_address::to_sockaddr(addr, port);

    if (bind(fd, reinterpret_cast<sockaddr const*>(&sock), addrlen) == -1) {
        int const err = sockerrno;

        if (!suppress_msgs) {
            tr_logAddError(
                fmt::format(
                    fmt::runtime(
                        err == EADDRINUSE ?
                            "Couldn't bind port {port} on {address}: {error} ({error_code}) -- Is another copy of {appname} already running?" :
                            "Couldn't bind port {port} on {address}: {error} ({error_code})"),
                    fmt::arg("appname", TR_PROJ_APPNAME_CAPITALIZED),
                    fmt::arg("address", addr.display_name()),
                    fmt::arg("port", port.host()),
                    fmt::arg("error", tr_net_strerror(err)),
                    fmt::arg("error_code", err)));
        }

        tr_net_close_socket(fd);
        *err_out = err;
        return TR_BAD_SOCKET;
    }

    if (!suppress_msgs) {
        tr_logAddDebug(fmt::format("Bound socket {:d} to port {:d} on {:s}", fd, port.host(), addr.display_name()));
    }

#ifdef TCP_FASTOPEN

#ifndef SOL_TCP
#define SOL_TCP IPPROTO_TCP
#endif

    optval = 5;
    (void)setsockopt(fd, SOL_TCP, TCP_FASTOPEN, reinterpret_cast<char const*>(&optval), sizeof(optval));

#endif

#ifdef _WIN32
    if (listen(fd, SOMAXCONN) == -1)
#else /* _WIN32 */
    /* Listen queue backlog will be capped to the operating system's limit. */
    if (listen(fd, INT_MAX) == -1)
#endif /* _WIN32 */
    {
        *err_out = sockerrno;
        tr_net_close_socket(fd);
        return TR_BAD_SOCKET;
    }

    return fd;
}
} // namespace

tr_socket_t tr_netBindTCP(tr_address const& addr, tr_port port, bool suppress_msgs, std::string_view bind_interface)
{
    int unused = 0;
    return tr_netBindTCPImpl(addr, port, suppress_msgs, &unused, bind_interface);
}

std::optional<std::pair<tr_socket_address, tr_socket_t>> tr_netAccept(tr_session const* session, tr_socket_t listening_sockfd)
{
    TR_ASSERT(session != nullptr);

    // accept the incoming connection
    auto sock = sockaddr_storage{};
    socklen_t len = sizeof(struct sockaddr_storage);
    auto const sockfd = accept(listening_sockfd, reinterpret_cast<sockaddr*>(&sock), &len);
    if (!is_valid_socket(sockfd)) {
        return {};
    }

    // get the address and port,
    // make the socket unblocking,
    // and confirm we don't have too many peers
    auto const addrport = tr_socket_address::from_sockaddr(reinterpret_cast<struct sockaddr*>(&sock));
    if (!addrport || evutil_make_socket_nonblocking(static_cast<evutil_socket_t>(sockfd)) == -1 ||
        tr_peer_socket::limit_reached(session)) {
        tr_net_close_socket(sockfd);
        return {};
    }

    return std::pair{ *addrport, sockfd };
}

void tr_net_close_socket(tr_socket_t sockfd)
{
    evutil_closesocket(static_cast<evutil_socket_t>(sockfd));
}

// --- tr_port

tr_port tr_port::from_network(uint16_t const nport) noexcept
{
    return tr_port{ ntohs(nport) };
}

uint16_t tr_port::network() const noexcept
{
    return htons(hport_);
}

std::pair<tr_port, std::byte const*> tr_port::from_compact(std::byte const* compact) noexcept
{
    auto nport = uint16_t{};
    static_assert(sizeof(nport) == CompactPortBytes);
    std::copy_n(compact, CompactPortBytes, reinterpret_cast<std::byte*>(&nport));
    compact += CompactPortBytes;

    return std::make_pair(tr_port::from_network(nport), compact);
}

// --- tr_address

std::optional<tr_address> tr_address::from_string(std::string_view address_sv)
{
    auto const address_sz = tr_strbuf<char, TrAddrStrlen>{ address_sv };

    auto ss = sockaddr_storage{};
    auto sslen = int{ sizeof(ss) };
    if (evutil_parse_sockaddr_port(address_sz.c_str(), reinterpret_cast<sockaddr*>(&ss), &sslen) != 0) {
        return {};
    }

    auto addr = tr_address{};
    switch (ss.ss_family) {
    case AF_INET:
        addr.addr.addr4 = reinterpret_cast<sockaddr_in*>(&ss)->sin_addr;
        addr.type = TR_AF_INET;
        return addr;

    case AF_INET6:
        addr.addr.addr6 = reinterpret_cast<sockaddr_in6*>(&ss)->sin6_addr;
        addr.type = TR_AF_INET6;
        return addr;

    default:
        return {};
    }
}

[[nodiscard]] std::string tr_address::display_name() const
{
    auto buf = std::array<char, std::max(INET_ADDRSTRLEN, INET6_ADDRSTRLEN)>{};
    TR_ASSERT(is_valid());
    if (auto* name = evutil_inet_ntop(tr_ip_protocol_to_af(type), &addr, std::data(buf), std::size(buf))) {
        return std::string{ name };
    }
    return std::string{ "Invalid address" };
}

std::pair<tr_address, std::byte const*> tr_address::from_compact_ipv4(std::byte const* compact) noexcept
{
    static auto constexpr Addr4Len = tr_address::CompactAddrBytes[TR_AF_INET];

    auto address = tr_address{};
    static_assert(sizeof(address.addr.addr4) == Addr4Len);
    address.type = TR_AF_INET;
    std::copy_n(compact, Addr4Len, reinterpret_cast<std::byte*>(&address.addr));
    compact += Addr4Len;

    return { address, compact };
}

std::pair<tr_address, std::byte const*> tr_address::from_compact_ipv6(std::byte const* compact) noexcept
{
    static auto constexpr Addr6Len = tr_address::CompactAddrBytes[TR_AF_INET6];

    auto address = tr_address{};
    address.type = TR_AF_INET6;
    std::copy_n(compact, Addr6Len, reinterpret_cast<std::byte*>(&address.addr.addr6.s6_addr));
    compact += Addr6Len;

    return { address, compact };
}

std::optional<unsigned> tr_address::to_interface_index() const
{
    if (!is_valid()) {
        tr_logAddDebug("Invalid target address to find interface index");
        return {};
    }

    tr_logAddDebug(fmt::format("Find interface index for {}", display_name()));

#ifdef _WIN32
    auto p_addresses = std::unique_ptr<void, void (*)(void*)>{ nullptr, operator delete };

    // The recommended method of calling the GetAdaptersAddresses function is to
    // pre-allocate a 15KB working buffer pointed to by the AdapterAddresses parameter.
    // On typical computers, this dramatically reduces the chances that the
    // GetAdaptersAddresses function returns ERROR_BUFFER_OVERFLOW, which would require
    // calling GetAdaptersAddresses function multiple times.
    // https://learn.microsoft.com/en-us/windows/win32/api/iphlpapi/nf-iphlpapi-getadaptersaddresses
    for (auto p_addresses_size = ULONG{ 15000 } /* 15KB */;;) {
        p_addresses.reset(operator new(p_addresses_size, std::nothrow));
        if (!p_addresses) {
            tr_logAddDebug("Could not allocate memory for interface list");
            return {};
        }

        if (auto ret = GetAdaptersAddresses(
                AF_UNSPEC,
                GAA_FLAG_SKIP_FRIENDLY_NAME,
                nullptr,
                reinterpret_cast<PIP_ADAPTER_ADDRESSES>(p_addresses.get()),
                &p_addresses_size);
            ret != ERROR_BUFFER_OVERFLOW) {
            if (ret != ERROR_SUCCESS) {
                tr_logAddDebug(fmt::format("Failed to retrieve interface list: {} ({})", ret, tr_win32_format_message(ret)));
                return {};
            }
            break;
        }
    }

    for (auto const* cur = reinterpret_cast<PIP_ADAPTER_ADDRESSES>(p_addresses.get()); cur != nullptr; cur = cur->Next) {
        if (cur->OperStatus != IfOperStatusUp) {
            continue;
        }

        for (auto const* sa_p = cur->FirstUnicastAddress; sa_p != nullptr; sa_p = sa_p->Next) {
            if (auto if_addr = tr_socket_address::from_sockaddr(sa_p->Address.lpSockaddr);
                if_addr && if_addr->address() == *this) {
                auto const ret = type == TR_AF_INET ? cur->IfIndex : cur->Ipv6IfIndex;
                tr_logAddDebug(fmt::format("Found interface index for {}: {}", display_name(), ret));
                return ret;
            }
        }
    }
#else
    struct ifaddrs* ifa = nullptr;
    if (getifaddrs(&ifa) != 0) {
        auto err = errno;
        tr_logAddDebug(fmt::format("Failed to retrieve interface list: {} ({})", err, tr_strerror(err)));
        return {};
    }
    auto const ifa_uniq = std::unique_ptr<ifaddrs, void (*)(struct ifaddrs*)>{ ifa, freeifaddrs };

    for (; ifa != nullptr; ifa = ifa->ifa_next) {
        if (ifa->ifa_addr == nullptr || (ifa->ifa_flags & IFF_UP) == 0U) {
            continue;
        }

        if (auto if_addr = tr_socket_address::from_sockaddr(ifa->ifa_addr); if_addr && if_addr->address() == *this) {
            auto const ret = if_nametoindex(ifa->ifa_name);
            tr_logAddDebug(fmt::format("Found interface index for {}: {}", display_name(), ret));
            return ret;
        }
    }
#endif

    tr_logAddDebug(fmt::format("Could not find interface index for {}", display_name()));
    return {};
}

std::strong_ordering tr_address::operator<=>(tr_address const& that) const noexcept
{
    // IPv6 addresses are always "greater than" IPv4
    if (auto const val = this->type <=> that.type; val != 0) {
        return val;
    }

    auto const val = this->is_ipv4() ?
        memcmp(&this->addr.addr4, &that.addr.addr4, sizeof(this->addr.addr4)) :
        memcmp(&this->addr.addr6.s6_addr, &that.addr.addr6.s6_addr, sizeof(this->addr.addr6.s6_addr));
    if (val < 0) {
        return std::strong_ordering::less;
    }
    if (val > 0) {
        return std::strong_ordering::greater;
    }
    return std::strong_ordering::equal;
}

// https://en.wikipedia.org/wiki/Reserved_IP_addresses
//
// https://www.rfc-editor.org/rfc/rfc4291.html#section-2.4
// address type         Binary prefix        IPv6 notation   Section
// ------------         -------------        -------------   -------
// Unspecified          00...0  (128 bits)   ::/128          2.5.2
// Loopback             00...1  (128 bits)   ::1/128         2.5.3
// Multicast            11111111             FF00::/8        2.7
// Link-Local unicast   1111111010           FE80::/10       2.5.6
// Global Unicast       (everything else)
[[nodiscard]] bool tr_address::is_global_unicast() const noexcept
{
    return !is_ipv4_current_network() && //
        !is_ipv4_10_private() && //
        !is_ipv4_carrier_grade_nat() && //
        !is_ipv4_loopback() && //
        !is_ipv4_link_local() && //
        !is_ipv4_172_private() && //
        !is_ipv4_ietf_protocol_assignment() && //
        !is_ipv4_test_net_1() && //
        !is_ipv4_6to4_relay() && //
        !is_ipv4_192_168_private() && //
        !is_ipv4_benchmark() && //
        !is_ipv4_test_net_2() && //
        !is_ipv4_test_net_3() && //
        !is_ipv4_multicast() && //
        !is_ipv4_mcast_test_net() && //
        !is_ipv4_reserved_class_e() && //
        !is_ipv4_limited_broadcast() && //
        !is_ipv6_unspecified() && //
        !is_ipv6_loopback() && //
        !is_ipv6_multicast() && //
        !is_ipv6_link_local();
}

std::optional<tr_address> tr_address::from_ipv4_mapped() const noexcept
{
    if (!is_ipv6_ipv4_mapped()) {
        return {};
    }

    return from_compact_ipv4(reinterpret_cast<std::byte const*>(&addr.addr6.s6_addr) + 12).first;
}

// --- tr_socket_addrses

std::string tr_socket_address::display_name(tr_address const& address, tr_port port)
{
    return fmt::format(fmt::runtime(address.is_ipv6() ? "[{:s}]:{:d}" : "{:s}:{:d}"), address.display_name(), port.host());
}

bool tr_socket_address::is_valid_for_peers(tr_peer_from from) const noexcept
{
    // Loopback is only meaningful from a source that can name our own host.
    auto const loopback_allowed = from == TR_PEER_FROM_INCOMING || from == TR_PEER_FROM_LPD || from == TR_PEER_FROM_RESUME;

    return is_valid() && !std::empty(port_) && !address_.is_ipv6_link_local() && !address_.is_ipv6_ipv4_mapped() &&
        !address_.is_ipv4_current_network() && !address_.is_ipv6_unspecified() && !address_.is_ipv4_multicast() &&
        !address_.is_ipv6_multicast() && (loopback_allowed || (!address_.is_ipv4_loopback() && !address_.is_ipv6_loopback()));
}

std::optional<tr_socket_address> tr_socket_address::from_string(std::string_view sockaddr_sv)
{
    auto ss = sockaddr_storage{};
    auto sslen = int{ sizeof(ss) };
    if (evutil_parse_sockaddr_port(
            tr_strbuf<char, TrAddrStrlen>{ sockaddr_sv }.c_str(),
            reinterpret_cast<sockaddr*>(&ss),
            &sslen) != 0) {
        return {};
    }

    return from_sockaddr(reinterpret_cast<struct sockaddr const*>(&ss));
}

std::optional<tr_socket_address> tr_socket_address::from_sockaddr(struct sockaddr const* from)
{
    if (from == nullptr) {
        return {};
    }

    if (from->sa_family == AF_INET) {
        auto const* const sin = reinterpret_cast<struct sockaddr_in const*>(from);
        auto addr = tr_address{};
        addr.type = TR_AF_INET;
        addr.addr.addr4 = sin->sin_addr;
        return tr_socket_address{ addr, tr_port::from_network(sin->sin_port) };
    }

    if (from->sa_family == AF_INET6) {
        auto const* const sin6 = reinterpret_cast<struct sockaddr_in6 const*>(from);
        auto addr = tr_address{};
        addr.type = TR_AF_INET6;
        addr.addr.addr6 = sin6->sin6_addr;
        return tr_socket_address{ addr, tr_port::from_network(sin6->sin6_port) };
    }

    tr_logAddDebug(fmt::format("Unsupported address family {:d}", from->sa_family));
    return {};
}

std::pair<sockaddr_storage, socklen_t> tr_socket_address::to_sockaddr(tr_address const& addr, tr_port port) noexcept
{
    auto ss = sockaddr_storage{};

    if (addr.is_ipv4()) {
        auto* const ss4 = reinterpret_cast<sockaddr_in*>(&ss);
        ss4->sin_addr = addr.addr.addr4;
        ss4->sin_family = AF_INET;
        ss4->sin_port = port.network();
        return { ss, sizeof(sockaddr_in) };
    }

    auto* const ss6 = reinterpret_cast<sockaddr_in6*>(&ss);
    ss6->sin6_addr = addr.addr.addr6;
    ss6->sin6_family = AF_INET6;
    ss6->sin6_flowinfo = 0;
    ss6->sin6_port = port.network();
    return { ss, sizeof(sockaddr_in6) };
}
