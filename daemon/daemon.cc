// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <iterator> /* std::back_inserter */
#include <memory>
#include <string_view>
#include <thread>
#include <vector>

#ifdef HAVE_SYSLOG
#include <syslog.h>
#endif

#ifdef _WIN32
#include <process.h> /* getpid */
#else
#include <sys/time.h> /* timeval */
#include <unistd.h> /* getpid */
#endif

#include <event2/event.h>

#include <fmt/format.h>

#include <libtransmission/transmission.h>

#include <libtransmission/constants.h>
#include <libtransmission/error.h>
#include <libtransmission/file-utils.h>
#include <libtransmission/file.h>
#include <libtransmission/log.h>
#include <libtransmission/macros.h>
#include <libtransmission/quark.h>
#include <libtransmission/string-utils.h>
#include <libtransmission/timer-ev.h>
#include <libtransmission/torrent-builder.h>
#include <libtransmission/tr-getopt.h>
#include <libtransmission/tr-strbuf.h>
#include <libtransmission/utils.h>
#include <libtransmission/variant.h>
#include <libtransmission/version.h>
#include <libtransmission/watchdir.h>

#include "daemon.h"

struct tr_torrent_builder;
struct tr_session;
struct tr_torrent;

#ifdef WITH_SYSTEMD

#include <cinttypes>
#include <ctime>

#include <systemd/sd-daemon.h>

#else

// no-op
#define sd_notify(status, str)
#define sd_notifyf(status, fmt, ...)

#endif

#define MY_NAME TR_PROJ_APPNAME "-daemon"
#define MY_CONFIG_DIRNAME TR_PROJ_SHARED_CONFIG_DIRNAME "-daemon"

using namespace std::literals;
using tr::Watchdir;

namespace
{
auto constexpr MyName = MY_NAME;

auto constexpr Usage = TR_PROJ_APPNAME_CAPITALIZED " " LONG_VERSION_STRING "  " TR_PROJ_URL_HOMEPAGE
                                                   "\n"
                                                   "A fast and easy BitTorrent client\n"
                                                   "\n" MY_NAME " is a headless " TR_PROJ_APPNAME_CAPITALIZED
                                                   " session that can be\n"
                                                   "controlled via " TR_PROJ_APPNAME "-qt, " TR_PROJ_APPNAME
                                                   "-remote, or its web interface.\n"
                                                   "\n"
                                                   "Usage: " MY_NAME " [options]";

using Arg = tr_option::Arg;
static_assert(TrDefaultRpcWhitelist == "127.0.0.1,::1", "update 'allowed' desc");
static_assert(TrDefaultPeerPort == 51413, "update 'peerport' desc");
static_assert(TrDefaultPeerLimitTorrent == 50, "update 'peerlimit-torrent' desc");
static_assert(TrDefaultPeerLimitGlobal == 200, "update 'peerlimit-global' desc");
static_assert(TrDefaultRpcPort == 9091, R"(update "port" desc)");
auto constexpr Options = std::to_array<tr_option>({
    { .val = 'a',
      .longName = "allowed",
      .description = "Allowed IP addresses. (Default: '127.0.0.1,::1')",
      .shortName = "a",
      .arg = Arg::Required,
      .argName = "<list>" },
    { .val = 'b',
      .longName = "blocklist",
      .description = "Enable peer blocklists",
      .shortName = "b",
      .arg = Arg::None,
      .argName = nullptr },
    { .val = 'B',
      .longName = "no-blocklist",
      .description = "Disable peer blocklists",
      .shortName = "B",
      .arg = Arg::None,
      .argName = nullptr },
    { .val = 'c',
      .longName = "watch-dir",
      .description = "Where to watch for new torrent files",
      .shortName = "c",
      .arg = Arg::Required,
      .argName = "<directory>" },
    { .val = 'C',
      .longName = "no-watch-dir",
      .description = "Disable the watch-dir",
      .shortName = "C",
      .arg = Arg::None,
      .argName = nullptr },
    { .val = 941,
      .longName = "incomplete-dir",
      .description = "Where to store new torrents until they're complete",
      .shortName = nullptr,
      .arg = Arg::Required,
      .argName = "<directory>" },
    { .val = 942,
      .longName = "no-incomplete-dir",
      .description = "Don't store incomplete torrents in a different location",
      .shortName = nullptr,
      .arg = Arg::None,
      .argName = nullptr },
    { .val = 'd',
      .longName = "dump-settings",
      .description = "Dump the settings and exit",
      .shortName = "d",
      .arg = Arg::None,
      .argName = nullptr },
    { .val = 943,
      .longName = "default-trackers",
      .description = "Trackers for public torrents to use automatically",
      .shortName = nullptr,
      .arg = Arg::Required,
      .argName = "<list>" },
    { .val = 'e',
      .longName = "logfile",
      .description = "Dump the log messages to this filename",
      .shortName = "e",
      .arg = Arg::Required,
      .argName = "<filename>" },
    { .val = 'f',
      .longName = "foreground",
      .description = "Run in the foreground instead of daemonizing",
      .shortName = "f",
      .arg = Arg::None,
      .argName = nullptr },
    { .val = 'g',
      .longName = "config-dir",
      .description = "Where to look for configuration files",
      .shortName = "g",
      .arg = Arg::Required,
      .argName = "<path>" },
    { .val = 'p',
      .longName = "port",
      .description = "RPC port (Default: 9091)",
      .shortName = "p",
      .arg = Arg::Required,
      .argName = "<port>" },
    { .val = 't',
      .longName = "auth",
      .description = "Require authentication",
      .shortName = "t",
      .arg = Arg::None,
      .argName = nullptr },
    { .val = 'T',
      .longName = "no-auth",
      .description = "Don't require authentication",
      .shortName = "T",
      .arg = Arg::None,
      .argName = nullptr },
    { .val = 'u',
      .longName = "username",
      .description = "Set username for authentication",
      .shortName = "u",
      .arg = Arg::Required,
      .argName = "<username>" },
    { .val = 'v',
      .longName = "password",
      .description = "Set password for authentication",
      .shortName = "v",
      .arg = Arg::Required,
      .argName = "<password>" },
    { .val = 'V',
      .longName = "version",
      .description = "Show version number and exit",
      .shortName = "V",
      .arg = Arg::None,
      .argName = nullptr },
    { .val = 810,
      .longName = "log-level",
      .description = "Must be 'critical', 'error', 'warn', 'info', 'debug', or 'trace'.",
      .shortName = nullptr,
      .arg = Arg::Required,
      .argName = "<level>" },
    { .val = 811,
      .longName = "log-error",
      .description = "Deprecated. Use --log-level=error",
      .shortName = nullptr,
      .arg = Arg::None,
      .argName = nullptr },
    { .val = 812,
      .longName = "log-info",
      .description = "Deprecated. Use --log-level=info",
      .shortName = nullptr,
      .arg = Arg::None,
      .argName = nullptr },
    { .val = 813,
      .longName = "log-debug",
      .description = "Deprecated. Use --log-level=debug",
      .shortName = nullptr,
      .arg = Arg::None,
      .argName = nullptr },
    { .val = 'w',
      .longName = "download-dir",
      .description = "Where to save downloaded data",
      .shortName = "w",
      .arg = Arg::Required,
      .argName = "<path>" },
    { .val = 800,
      .longName = "paused",
      .description = "Pause all torrents on startup",
      .shortName = nullptr,
      .arg = Arg::None,
      .argName = nullptr },
    { .val = 'o',
      .longName = "dht",
      .description = "Enable distributed hash tables (DHT)",
      .shortName = "o",
      .arg = Arg::None,
      .argName = nullptr },
    { .val = 'O',
      .longName = "no-dht",
      .description = "Disable distributed hash tables (DHT)",
      .shortName = "O",
      .arg = Arg::None,
      .argName = nullptr },
    { .val = 'y',
      .longName = "lpd",
      .description = "Enable local peer discovery (LPD)",
      .shortName = "y",
      .arg = Arg::None,
      .argName = nullptr },
    { .val = 'Y',
      .longName = "no-lpd",
      .description = "Disable local peer discovery (LPD)",
      .shortName = "Y",
      .arg = Arg::None,
      .argName = nullptr },
    { .val = 830,
      .longName = "preferred-transports",
      .description = "Comma-separated list specifying preference of transport protocols",
      .shortName = nullptr,
      .arg = Arg::Required,
      .argName = "<protocol(s)>" },
    { .val = 831,
      .longName = "utp",
      .description = "*DEPRECATED* Enable µTP for peer connections",
      .shortName = nullptr,
      .arg = Arg::None,
      .argName = nullptr },
    { .val = 832,
      .longName = "no-utp",
      .description = "*DEPRECATED* Disable µTP for peer connections",
      .shortName = nullptr,
      .arg = Arg::None,
      .argName = nullptr },
    { .val = 'P',
      .longName = "peerport",
      .description = "Port for incoming peers (Default: 51413)",
      .shortName = "P",
      .arg = Arg::Required,
      .argName = "<port>" },
    { .val = 'm',
      .longName = "portmap",
      .description = "Enable portmapping via NAT-PMP or UPnP",
      .shortName = "m",
      .arg = Arg::None,
      .argName = nullptr },
    { .val = 'M',
      .longName = "no-portmap",
      .description = "Disable portmapping",
      .shortName = "M",
      .arg = Arg::None,
      .argName = nullptr },
    { .val = 'L',
      .longName = "peerlimit-global",
      .description = "Maximum overall number of peers (Default: 200)",
      .shortName = "L",
      .arg = Arg::Required,
      .argName = "<limit>" },
    { .val = 'l',
      .longName = "peerlimit-torrent",
      .description = "Maximum number of peers per torrent (Default: 50)",
      .shortName = "l",
      .arg = Arg::Required,
      .argName = "<limit>" },
    { .val = 910,
      .longName = "encryption-required",
      .description = "Encrypt all peer connections",
      .shortName = "er",
      .arg = Arg::None,
      .argName = nullptr },
    { .val = 911,
      .longName = "encryption-preferred",
      .description = "Prefer encrypted peer connections",
      .shortName = "ep",
      .arg = Arg::None,
      .argName = nullptr },
    { .val = 912,
      .longName = "encryption-tolerated",
      .description = "Prefer unencrypted peer connections",
      .shortName = "et",
      .arg = Arg::None,
      .argName = nullptr },
    { .val = 'i',
      .longName = "bind-address-ipv4",
      .description = "Where to listen for peer connections",
      .shortName = "i",
      .arg = Arg::Required,
      .argName = "<ipv4 addr>" },
    { .val = 'I',
      .longName = "bind-address-ipv6",
      .description = "Where to listen for peer connections",
      .shortName = "I",
      .arg = Arg::Required,
      .argName = "<ipv6 addr>" },
    { .val = 'r',
      .longName = "rpc-bind-address",
      .description = "Where to listen for RPC connections",
      .shortName = "r",
      .arg = Arg::Required,
      .argName = "<ip addr>" },
    { .val = 953,
      .longName = "global-seedratio",
      .description = "All torrents, unless overridden by a per-torrent setting, should seed until a specific ratio",
      .shortName = "gsr",
      .arg = Arg::Required,
      .argName = "ratio" },
    { .val = 954,
      .longName = "no-global-seedratio",
      .description = "All torrents, unless overridden by a per-torrent setting, should seed regardless of ratio",
      .shortName = "GSR",
      .arg = Arg::None,
      .argName = nullptr },
    { .val = 994,
      .longName = "sequential-download",
      .description = "Enable sequential download by default",
      .shortName = "seq",
      .arg = Arg::None,
      .argName = nullptr },
    { .val = 995,
      .longName = "no-sequential-download",
      .description = "Disable sequential download by default",
      .shortName = "SEQ",
      .arg = Arg::None,
      .argName = nullptr },
    { .val = 'x',
      .longName = "pid-file",
      .description = "Enable PID file",
      .shortName = "x",
      .arg = Arg::Required,
      .argName = "<pid-file>" },
    { .val = 0, .longName = nullptr, .description = nullptr, .shortName = nullptr, .arg = Arg::None, .argName = nullptr },
});
static_assert(Options[std::size(Options) - 2].val != 0);
} // namespace

namespace
{
[[nodiscard]] std::string get_config_dir(int argc, char const* const* argv)
{
    int c = {};
    char const* optstr = nullptr;
    int const ind = tr_optind;

    while ((c = tr_getopt(Usage, argc, argv, std::data(Options), &optstr)) != TR_OPT_DONE) {
        if (c == 'g') {
            return optstr;
        }
    }

    tr_optind = ind;

    return tr::platform::get_default_config_dir(MY_CONFIG_DIRNAME);
}

auto on_file_added(tr_session* session, std::string_view dirname, std::string_view basename)
{
    auto const lowercase = tr_strlower(basename);
    auto const is_torrent = lowercase.ends_with(".torrent"sv);
    auto const is_magnet = lowercase.ends_with(".magnet"sv);

    if (!is_torrent && !is_magnet) {
        return Watchdir::Action::Done;
    }

    auto const filename = tr_pathbuf{ dirname, '/', basename };
    auto builder = tr_torrent_builder{ session };

    bool retry = false;

    if (is_torrent) {
        if (!builder.set_metainfo_from_file(filename)) {
            retry = true;
        }
    } else // is_magnet
    {
        auto content = std::vector<char>{};
        auto error = tr_error{};
        if (!tr_file_read(filename, content, &error)) {
            tr_logAddWarn(
                fmt::format(
                    "Couldn't read '{path}': {error} ({error_code})",
                    fmt::arg("path", basename),
                    fmt::arg("error", error.message()),
                    fmt::arg("error_code", error.code())));
            retry = true;
        } else {
            auto const content_sv = std::string_view{ content.data(), content.size() };
            if (!builder.set_metainfo_from_magnet_link(content_sv)) {
                retry = true;
            }
        }
    }

    if (retry) {
        return Watchdir::Action::Retry;
    }

    if (tr_torrentNew(&builder, nullptr) == nullptr) {
        tr_logAddError(fmt::format("Couldn't add torrent file '{path}'", fmt::arg("path", basename)));
    } else {
        if (tr_sessionGetDeleteSource(session)) {
            tr_logAddInfo(fmt::format("Removing torrent file '{path}'", fmt::arg("path", basename)));

            if (auto error = tr_error{}; !tr_sys_path_remove(filename, &error)) {
                tr_logAddError(
                    fmt::format(
                        "Couldn't remove '{path}': {error} ({error_code})",
                        fmt::arg("path", basename),
                        fmt::arg("error", error.message()),
                        fmt::arg("error_code", error.code())));
            }
        } else {
            tr_sys_path_rename(filename, tr_pathbuf{ filename, ".added"sv });
        }
    }

    return Watchdir::Action::Done;
}

[[nodiscard]] constexpr char const* level_name(tr_log_level level)
{
    switch (level) {
    case TR_LOG_CRITICAL:
        return "CRT";
    case TR_LOG_ERROR:
        return "ERR";
    case TR_LOG_WARN:
        return "WRN";
    case TR_LOG_DEBUG:
        return "dbg";
    case TR_LOG_TRACE:
        return "trc";
    default:
        return "inf";
    }
}

void print_message(
    FILE* ostream,
    std::chrono::system_clock::time_point now,
    tr_log_level level,
    std::string_view name,
    std::string_view message,
    std::string_view filename,
    long line)
{
    auto out = tr_strbuf<char, 2048U>{};

    if (std::empty(name)) {
        fmt::format_to(std::back_inserter(out), "{:s} ({:s}:{:d})", message, filename, line);
    } else {
        fmt::format_to(std::back_inserter(out), "{:s} {:s} ({:s}:{:d})", name, message, filename, line);
    }

    if (ostream != nullptr) {
        auto buf = std::array<char, 64>{};
        auto const timestr = tr_logGetTimeStr(now, std::data(buf), std::size(buf));
        fmt::print(ostream, "[{:s}] {:s} {:s}\n", timestr, level_name(level), out.c_str());
    }
#ifdef HAVE_SYSLOG

    else /* daemon... write to syslog */
    {
        int priority = {};

        /* figure out the syslog priority */
        switch (level) {
        case TR_LOG_CRITICAL:
            priority = LOG_CRIT;
            break;

        case TR_LOG_ERROR:
            priority = LOG_ERR;
            break;

        case TR_LOG_WARN:
            priority = LOG_WARNING;
            break;

        case TR_LOG_INFO:
            priority = LOG_INFO;
            break;

        default:
            priority = LOG_DEBUG;
            break;
        }

        syslog(priority, "%s", out.c_str());
    }

#endif
}

void print_message(
    FILE* ostream,
    tr_log_level level,
    std::string_view name,
    std::string_view message,
    std::string_view filename,
    long line)
{
    print_message(ostream, std::chrono::system_clock::now(), level, name, message, filename, line);
}

void pump_log_messages(FILE* log_stream)
{
    for (auto const& l : tr_logGetQueue()) {
        print_message(log_stream, l.when, l.level, l.name.sv(), l.message, l.file, l.line);
    }

    // two reasons to not flush stderr:
    // 1. it's usually redundant, since stderr flushes itself
    // 2. when running as a systemd unit, it's redirected to a socket
    if (log_stream != stderr) {
        (void)std::fflush(log_stream);
    }
}

void periodic_update(evutil_socket_t /*fd*/, short /*what*/, void* arg)
{
    static_cast<tr_daemon*>(arg)->periodic_update();
}

[[nodiscard]] auto get_default_settings()
{
    auto defaults = tr::Settings{ 6U };
    defaults.try_emplace(TR_KEY_pidfile, tr_variant::unmanaged_string(""sv));
    defaults.try_emplace(TR_KEY_rpc_enabled, true);
    defaults.try_emplace(TR_KEY_start_paused, false);
    defaults.try_emplace(TR_KEY_watch_dir, tr_variant::unmanaged_string(""sv));
    defaults.try_emplace(TR_KEY_watch_dir_enabled, false);
    defaults.try_emplace(TR_KEY_watch_dir_force_generic, false);
    return defaults;
}

[[nodiscard]] auto load_settings(std::string_view const config_dir)
{
    return tr_sessionLoadSettings(config_dir, get_default_settings());
}

} // namespace

bool tr_daemon::reopen_log_file(char const* filename)
{
    auto* const old_stream = log_stream_;

    auto* new_stream = std::fopen(filename, "a");
    if (new_stream == nullptr) {
        auto const err = errno;
        auto const errmsg = fmt::format(
            "Couldn't open '{path}': {error} ({error_code})",
            fmt::arg("path", filename),
            fmt::arg("error", tr_strerror(err)),
            fmt::arg("error_code", err));
        fmt::print(stderr, "{:s}\n", errmsg);
        return false;
    }

    log_stream_ = new_stream;

    if (old_stream != nullptr && old_stream != stderr) {
        (void)std::fclose(old_stream);
    }

    return true;
}

void tr_daemon::report_status()
{
    auto const up = tr_sessionGetRawSpeed_KBps(my_session_, tr_direction::Up);
    auto const dn = tr_sessionGetRawSpeed_KBps(my_session_, tr_direction::Down);

    if (up > 0 || dn > 0) {
        sd_notifyf(0, "STATUS=Uploading %.2f KBps, Downloading %.2f KBps.\n", up, dn);
    } else {
        sd_notify(0, "STATUS=Idle.\n");
    }
}

void tr_daemon::periodic_update()
{
    pump_log_messages(log_stream_);
    report_status();
}

namespace
{
void set_preferred_transports(tr_variant::Map& settings, std::string_view comma_delimited_protocols)
{
    auto preferred_protocols = tr_variant::Vector{};
    preferred_protocols.reserve(2U);

    auto protocol = std::string_view{};
    while (tr_strv_sep(&comma_delimited_protocols, &protocol, ',')) {
        preferred_protocols.emplace_back(protocol);
    }

    settings.insert_or_assign(TR_KEY_preferred_transports, std::move(preferred_protocols));
}

// TODO(tearfur): remove everything in this helper namespace and the corresponding args in 5.0.0
namespace utp_arg_helpers
{
auto constexpr UtpVal = "utp"sv;

void utp(tr::Settings& settings)
{
    if (auto const pt = settings.value_if<std::string_view>(TR_KEY_preferred_transports); pt && pt != UtpVal) {
        settings.erase(TR_KEY_preferred_transports);
    }

    if (auto* const pt = settings.find_if<tr_variant::Vector>(TR_KEY_preferred_transports)) {
        if (std::ranges::none_of(*pt, [](tr_variant const& e) { return e.value_if<std::string_view>() == UtpVal; })) {
            pt->emplace(std::begin(*pt), tr_variant::unmanaged_string(UtpVal));
        }
    } else {
        settings.insert_or_assign(TR_KEY_utp_enabled, true);
    }
}

void no_utp(tr::Settings& settings)
{
    if (auto const pt = settings.value_if<std::string_view>(TR_KEY_preferred_transports); pt == UtpVal) {
        settings.erase(TR_KEY_preferred_transports);
    }

    if (auto* const pt = settings.find_if<tr_variant::Vector>(TR_KEY_preferred_transports)) {
        std::erase_if(*pt, [](tr_variant const& e) { return e.value_if<std::string_view>() == UtpVal; });
    } else {
        settings.insert_or_assign(TR_KEY_utp_enabled, false);
    }
}
} // namespace utp_arg_helpers
} // namespace

bool tr_daemon::parse_args(int argc, char const* const* argv, bool* dump_settings, bool* foreground, int* exit_code)
{
    int c = {};
    char const* optstr = nullptr;

    *dump_settings = false;
    *foreground = false;

    auto& map = settings_;

    tr_optind = 1;

    while ((c = tr_getopt(Usage, argc, argv, std::data(Options), &optstr)) != TR_OPT_DONE) {
        switch (c) {
        case 'a':
            map.insert_or_assign(TR_KEY_rpc_whitelist, optstr);
            map.insert_or_assign(TR_KEY_rpc_whitelist_enabled, true);
            break;

        case 'b':
            map.insert_or_assign(TR_KEY_blocklist_enabled, true);
            break;

        case 'B':
            map.insert_or_assign(TR_KEY_blocklist_enabled, false);
            break;

        case 'c':
            map.insert_or_assign(TR_KEY_watch_dir, optstr);
            map.insert_or_assign(TR_KEY_watch_dir_enabled, true);
            break;

        case 'C':
            map.insert_or_assign(TR_KEY_watch_dir_enabled, false);
            break;

        case 941:
            map.insert_or_assign(TR_KEY_incomplete_dir, optstr);
            map.insert_or_assign(TR_KEY_incomplete_dir_enabled, true);
            break;

        case 942:
            map.insert_or_assign(TR_KEY_incomplete_dir_enabled, false);
            break;

        case 943:
            map.insert_or_assign(TR_KEY_default_trackers, optstr);
            break;

        case 994:
            map.insert_or_assign(TR_KEY_sequential_download, true);
            break;

        case 995:
            map.insert_or_assign(TR_KEY_sequential_download, false);
            break;

        case 'd':
            *dump_settings = true;
            break;

        case 'e':
            if (reopen_log_file(optstr)) {
                log_file_name_ = optstr;
            }

            break;

        case 'f':
            *foreground = true;
            break;

        case 'g': /* handled above */
            break;

        case 'V': /* version */
            fmt::print(stderr, "{:s} {:s}\n", MyName, LONG_VERSION_STRING);
            *exit_code = 0;
            return false;

        case 'o':
            map.insert_or_assign(TR_KEY_dht_enabled, true);
            break;

        case 'O':
            map.insert_or_assign(TR_KEY_dht_enabled, false);
            break;

        case 'p':
            if (auto const rpc_port = tr_num_parse<uint16_t>(optstr); rpc_port) {
                map.insert_or_assign(TR_KEY_rpc_port, *rpc_port);
            }
            break;

        case 't':
            map.insert_or_assign(TR_KEY_rpc_authentication_required, true);
            break;

        case 'T':
            map.insert_or_assign(TR_KEY_rpc_authentication_required, false);
            break;

        case 'u':
            map.insert_or_assign(TR_KEY_rpc_username, optstr);
            break;

        case 'v':
            map.insert_or_assign(TR_KEY_rpc_password, optstr);
            break;

        case 'w':
            map.insert_or_assign(TR_KEY_download_dir, optstr);
            break;

        case 'P':
            if (auto const peer_port = tr_num_parse<uint16_t>(optstr); peer_port) {
                map.insert_or_assign(TR_KEY_peer_port, *peer_port);
            }
            break;

        case 'm':
            map.insert_or_assign(TR_KEY_port_forwarding_enabled, true);
            break;

        case 'M':
            map.insert_or_assign(TR_KEY_port_forwarding_enabled, false);
            break;

        case 'L':
            if (auto const peer_limit_global = tr_num_parse<int64_t>(optstr); peer_limit_global && *peer_limit_global >= 0) {
                map.insert_or_assign(TR_KEY_peer_limit_global, *peer_limit_global);
            }
            break;

        case 'l':
            if (auto const peer_limit_tor = tr_num_parse<int64_t>(optstr); peer_limit_tor && *peer_limit_tor >= 0) {
                map.insert_or_assign(TR_KEY_peer_limit_per_torrent, *peer_limit_tor);
            }
            break;

        case 800:
            map.insert_or_assign(TR_KEY_start_paused, true);
            break;

        case 910:
            map.insert_or_assign(TR_KEY_encryption, TR_ENCRYPTION_REQUIRED);
            break;

        case 911:
            map.insert_or_assign(TR_KEY_encryption, TR_ENCRYPTION_PREFERRED);
            break;

        case 912:
            map.insert_or_assign(TR_KEY_encryption, TR_CLEAR_PREFERRED);
            break;

        case 'i':
            map.insert_or_assign(TR_KEY_bind_address_ipv4, optstr);
            break;

        case 'I':
            map.insert_or_assign(TR_KEY_bind_address_ipv6, optstr);
            break;

        case 'r':
            map.insert_or_assign(TR_KEY_rpc_bind_address, optstr);
            break;

        case 953:
            if (auto const ratio_limit = tr_num_parse<double>(optstr); ratio_limit) {
                map.insert_or_assign(TR_KEY_seed_ratio_limit, *ratio_limit);
            }
            map.insert_or_assign(TR_KEY_seed_ratio_limited, true);
            break;

        case 954:
            map.insert_or_assign(TR_KEY_seed_ratio_limited, false);
            break;

        case 'x':
            map.insert_or_assign(TR_KEY_pidfile, optstr);
            break;

        case 'y':
            map.insert_or_assign(TR_KEY_lpd_enabled, true);
            break;

        case 'Y':
            map.insert_or_assign(TR_KEY_lpd_enabled, false);
            break;

        case 810:
            if (auto const level = tr_logGetLevelFromKey(optstr); level) {
                map.insert_or_assign(TR_KEY_message_level, *level);
            } else {
                fmt::print(stderr, "Couldn't parse log level '{level}'\n", fmt::arg("level", optstr));
            }
            break;

        case 811:
            fmt::print(stderr, "WARN: --log-error is deprecated. Use --log-level=error\n");
            map.insert_or_assign(TR_KEY_message_level, TR_LOG_ERROR);
            break;

        case 812:
            fmt::print(stderr, "WARN: --log-info is deprecated. Use --log-level=info\n");
            map.insert_or_assign(TR_KEY_message_level, TR_LOG_INFO);
            break;

        case 813:
            fmt::print(stderr, "WARN: --log-debug is deprecated. Use --log-level=debug\n");
            map.insert_or_assign(TR_KEY_message_level, TR_LOG_DEBUG);
            break;

        case 830:
            set_preferred_transports(map, optstr);
            break;

        case 831:
            utp_arg_helpers::utp(map);
            break;

        case 832:
            utp_arg_helpers::no_utp(map);
            break;

        case TR_OPT_UNK:
            fmt::print(stderr, "Unexpected argument: {:s}\n", optstr);
            tr_getopt_usage(MyName, Usage, std::data(Options));
            *exit_code = 1;
            return false;

        default:
            tr_getopt_usage(MyName, Usage, std::data(Options));
            *exit_code = 0;
            return false;
        }
    }

    return true;
}

void tr_daemon::reconfigure()
{
    if (my_session_ == nullptr) {
        tr_logAddInfo("Deferring reload until session is fully started.");
        seen_hup_ = true;
    } else {
#ifdef WITH_SYSTEMD
        auto ts = timespec{};
        if (clock_gettime(CLOCK_MONOTONIC, &ts) < 0) {
            auto error = tr_error{};
            error.set_from_errno(errno);
            tr_logAddError(
                fmt::format(
                    "Failed to reload: Failed to get current monotonic time: {errmsg} ({errno})",
                    fmt::arg("errmsg", error.message()),
                    fmt::arg("errno", error.code())));
            return;
        }

        sd_notifyf(
            0,
            "STATUS=Reloading...\nRELOADING=1\nMONOTONIC_USEC=%" PRIu64 "\n",
            (static_cast<uint64_t>(ts.tv_sec) * 1000000U) + (static_cast<uint64_t>(ts.tv_nsec) / 1000U));
#endif

        /* reopen the logfile to allow for log rotation */
        if (log_file_name_ != nullptr) {
            reopen_log_file(log_file_name_);
        }

        auto const config_dir = tr_sessionGetConfigDir(my_session_);
        tr_logAddInfo(fmt::format("Reloading settings from '{path}'", fmt::arg("path", config_dir)));

        tr_sessionSet(my_session_, load_settings(config_dir));
        tr_sessionReloadBlocklists(my_session_);

        sd_notify(0, "STATUS=Reload complete.\nREADY=1\n");
    }
}

void tr_daemon::stop()
{
    event_base_loopexit(ev_base_, nullptr);
}

int tr_daemon::start([[maybe_unused]] bool foreground)
{
    sd_notifyf(0, "MAINPID=%d\n", static_cast<int>(getpid()));

    /* setup event state */
    ev_base_ = event_base_new();

    if (ev_base_ == nullptr || !setup_signals()) {
        auto const error_code = errno;
        auto const errmsg = fmt::format(
            "Couldn't initialize daemon: {error} ({error_code})",
            fmt::arg("error", tr_strerror(error_code)),
            fmt::arg("error_code", error_code));
        print_message(log_stream_, TR_LOG_ERROR, MyName, errmsg, __FILE__, __LINE__);
        cleanup_signals();
        return 1;
    }

    /* start the session */

    // If the lock is held by another instance, we should wait for it.
    // The kernel drops the lock unconditionally when the other instance exits,
    // so never get stuck waiting on a dead process' lock.
    static auto constexpr LockPatience = std::chrono::seconds{ 15 };
    static auto constexpr LockRetryInterval = std::chrono::seconds{ 1 };

    if (auto const deadline = std::chrono::steady_clock::now() + LockPatience; tr_configDirIsContended(config_dir_)) {
        // print_message rather than the log, which is not routed anywhere this early.
        // A start that pauses for fifteen seconds has to say why while it happens.
        print_message(
            log_stream_,
            TR_LOG_INFO,
            MyName,
            fmt::format("Waiting for another process to release '{path}'", fmt::arg("path", config_dir_)),
            __FILE__,
            __LINE__);

        for (;;) {
            std::this_thread::sleep_for(LockRetryInterval);
            if (!tr_configDirIsContended(config_dir_) || std::chrono::steady_clock::now() >= deadline) {
                break;
            }
        }
    }

    auto* session = tr_sessionInit(config_dir_, true, settings_);
    tr_logAddInfo(fmt::format("Loading settings from '{path}'", fmt::arg("path", config_dir_)));

    // The session's own lock is the one that decides. Each probe above released the lock
    // it took to ask, so a free answer only ever described that moment.
    if (tr_sessionConfigDirIsContended(session)) {
        auto const errmsg = fmt::format("Another process is already using '{path}'.", fmt::arg("path", config_dir_));
        print_message(log_stream_, TR_LOG_ERROR, MyName, errmsg, __FILE__, __LINE__);
        tr_sessionClose(session, 1);
        cleanup_signals();
        return 1;
    }

    tr_sessionSetRPCCallback(session, [this](tr_rpc_callback_type const type, std::optional<tr_torrent_id_t> const /*tor_id*/) {
        if (type == TR_RPC_SESSION_CLOSE) {
            stop();
        }

        return TR_RPC_OK;
    });

    tr_sessionSaveSettings(session, config_dir_, settings_);

    auto const& map = settings_;
    auto pidfile_created = false;
    auto const pid_filename = map.value_if<std::string_view>(TR_KEY_pidfile).value_or(""sv);
    if (!std::empty(pid_filename)) {
        auto error = tr_error{};
        tr_sys_file_t fp = tr_sys_file_open(
            pid_filename,
            TR_SYS_FILE_WRITE | TR_SYS_FILE_CREATE | TR_SYS_FILE_TRUNCATE,
            0666,
            &error);

        if (fp != TR_BAD_SYS_FILE) {
            auto const out = std::to_string(getpid());
            tr_sys_file_write(fp, std::data(out), std::size(out), nullptr);
            tr_sys_file_close(fp);
            tr_logAddInfo(fmt::format("Saved pidfile '{path}'", fmt::arg("path", pid_filename)));
            pidfile_created = true;
        } else {
            tr_logAddError(
                fmt::format(
                    "Couldn't save '{path}': {error} ({error_code})",
                    fmt::arg("path", pid_filename),
                    fmt::arg("error", error.message()),
                    fmt::arg("error_code", error.code())));
        }
    }

    if (map.value_if<bool>(TR_KEY_rpc_authentication_required).value_or(false)) {
        tr_logAddInfo("Requiring authentication");
    }

    my_session_ = session;

    /* If we got a SIGHUP during startup, process that now. */
    if (seen_hup_) {
        reconfigure();
    }

    /* maybe add a watchdir */
    auto watchdir = std::unique_ptr<Watchdir>{};
    if (map.value_if<bool>(TR_KEY_watch_dir_enabled).value_or(false)) {
        auto const force_generic = map.value_if<bool>(TR_KEY_watch_dir_force_generic).value_or(false);

        if (auto dir = map.value_if<std::string_view>(TR_KEY_watch_dir).value_or(""sv); !std::empty(dir)) {
            tr_logAddInfo(fmt::format("Watching '{path}' for new torrent files", fmt::arg("path", dir)));

            auto handler = [session](std::string_view dirname, std::string_view basename) {
                return on_file_added(session, dirname, basename);
            };

            auto timer_maker = tr::EvTimerMaker{ ev_base_ };
            watchdir = force_generic ? Watchdir::create_generic(dir, handler, timer_maker) :
                                       Watchdir::create(dir, handler, timer_maker, ev_base_);
        }
    }

    /* load the torrents */
    {
        auto builder = tr_torrent_builder{ my_session_ };

        if (map.value_if<bool>(TR_KEY_start_paused).value_or(false)) {
            builder.set_paused(true);
        }

        tr_sessionLoadTorrents(my_session_, &builder);
    }

#ifdef HAVE_SYSLOG

    if (!foreground) {
        openlog(MyName, LOG_CONS | LOG_PID, LOG_DAEMON);
    }

#endif

    /* Create new timer event to report daemon status */
    event* status_ev = nullptr;
    {
        static constexpr auto OneSec = timeval{ .tv_sec = 1, .tv_usec = 0 }; // 1 second
        status_ev = event_new(ev_base_, -1, EV_PERSIST, &::periodic_update, this);

        if (status_ev == nullptr) {
            auto const error_code = errno;
            tr_logAddError(
                fmt::format(
                    "Couldn't create event: {error} ({error_code})",
                    fmt::arg("error", tr_strerror(error_code)),
                    fmt::arg("error_code", error_code)));
            goto CLEANUP;
        }

        if (event_add(status_ev, &OneSec) == -1) {
            auto const error_code = errno;
            tr_logAddError(
                fmt::format(
                    "Couldn't add event: {error} ({error_code})",
                    fmt::arg("error", tr_strerror(error_code)),
                    fmt::arg("error_code", error_code)));
            goto CLEANUP;
        }
    }

    sd_notify(0, "READY=1\n");

    /* Run daemon event loop */
    if (event_base_dispatch(ev_base_) == -1) {
        auto const error_code = errno;
        tr_logAddError(
            fmt::format(
                "Couldn't launch daemon event loop: {error} ({error_code})",
                fmt::arg("error", tr_strerror(error_code)),
                fmt::arg("error_code", error_code)));
        goto CLEANUP;
    }

CLEANUP:
    sd_notify(0, fmt::format("STATUS=Closing {:s} session...\n", TR_PROJ_APPNAME).c_str());

    fmt::print("Closing {:s} session...", TR_PROJ_APPNAME);

    watchdir.reset();

    if (status_ev != nullptr) {
        event_del(status_ev);
        event_free(status_ev);
    }

    cleanup_signals();

    event_base_free(ev_base_);

    tr_sessionSaveSettings(my_session_, config_dir_, settings_);
    tr_sessionClose(my_session_);
    pump_log_messages(log_stream_);
    fmt::print(" done.\n");

    /* shutdown */
#ifdef HAVE_SYSLOG

    if (!foreground) {
        syslog(LOG_INFO, "%s", "Closing session");
        closelog();
    }

#endif

    /* cleanup */
    if (pidfile_created) {
        tr_sys_path_remove(pid_filename);
    }

    sd_notify(0, "STATUS=\n");

    return 0;
}

bool tr_daemon::init(int argc, char const* const* argv, bool* foreground, int* ret)
{
    config_dir_ = get_config_dir(argc, argv);

    /* load settings from defaults + config file */
    settings_ = load_settings(config_dir_);

    bool dump_settings = false;

    *ret = 0;

    /* overwrite settings from the command line */
    if (!parse_args(argc, argv, &dump_settings, foreground, ret)) {
        return false;
    }

    if (*foreground && log_stream_ == nullptr) {
        log_stream_ = stderr;
    }

    if (dump_settings) {
        fmt::print("{:s}\n", tr_variant_serde::json().to_string(settings_.clone()));
        return false;
    }

    return true;
}

void tr_daemon::handle_error(tr_error const& error) const
{
    auto const errmsg = fmt::format("Couldn't daemonize: {:s} ({:d})", error.message(), error.code());
    print_message(log_stream_, TR_LOG_ERROR, MyName, errmsg, __FILE__, __LINE__);
}

// NOLINTNEXTLINE(bugprone-exception-escape)
int tr_main(int argc, char** argv)
{
    tr_lib_init();

    tr_locale_set_global("");

    auto foreground = bool{};
    auto ret = int{};
    auto daemon = tr_daemon{};
    if (!daemon.init(argc, argv, &foreground, &ret)) {
        return ret;
    }

    if (auto error = tr_error{}; !daemon.spawn(foreground, &ret, error)) {
        daemon.handle_error(error);
    }
    return ret;
}
