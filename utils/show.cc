// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#include <algorithm>
#include <array>
#include <cstdint> // int64_t
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <chrono>
#include <future>
#include <iterator> // std::size()
#include <optional>
#include <string>
#include <string_view>
#include <utility> // std::move()
#include <vector>

#include <fmt/chrono.h>
#include <fmt/format.h>

#include <libtransmission/transmission.h>

#include <libtransmission/error.h>
#include <libtransmission/log.h>
#include <libtransmission/macros.h>
#include <libtransmission/quark.h>
#include <libtransmission/string-utils.h>
#include <libtransmission/torrent-metainfo.h>
#include <libtransmission/tr-getopt.h>
#include <libtransmission/utils.h>
#include <libtransmission/values.h>
#include <libtransmission/variant.h>
#include <libtransmission/version.h>
#include <libtransmission/web.h>
#include <libtransmission/web-utils.h>

using namespace std::literals;
using namespace tr::Values;

#define MY_NAME TR_PROJ_APPNAME "-show"

namespace
{
auto constexpr TimeoutSecs = std::chrono::seconds{ 30 };

char constexpr MyName[] = MY_NAME;
char constexpr Usage[] = "Usage: " MY_NAME " [options] <torrent-file>";

using Arg = tr_option::Arg;
auto constexpr Options = std::to_array<tr_option>({
    { 'd', "header", "Show only header section", "d", Arg::None, nullptr },
    { 'i', "info", "Show only info section", "i", Arg::None, nullptr },
    { 't', "trackers", "Show only trackers section", "t", Arg::None, nullptr },
    { 'f', "files", "Show only file list", "f", Arg::None, nullptr },
    { 'D', "no-header", "Do not show header section", "D", Arg::None, nullptr },
    { 'I', "no-info", "Do not show info section", "I", Arg::None, nullptr },
    { 'T', "no-trackers", "Do not show trackers section", "T", Arg::None, nullptr },
    { 'F', "no-files", "Do not show files section", "F", Arg::None, nullptr },
    { 'b', "bytes", "Show file sizes in bytes", "b", Arg::None, nullptr },
    { 'm', "magnet", "Give a magnet link for the specified torrent", "m", Arg::None, nullptr },
    { 's', "scrape", "Ask the torrent's trackers how many peers are in the torrent's swarm", "s", Arg::None, nullptr },
    { 'u', "unsorted", "Do not sort files by name", "u", Arg::None, nullptr },
    { 'V', "version", "Show version number and exit", "V", Arg::None, nullptr },
    { 0, nullptr, nullptr, nullptr, Arg::None, nullptr },
});
static_assert(Options[std::size(Options) - 2].val != 0);
} // namespace

namespace
{
struct app_opts {
    std::string_view filename;
    bool scrape = false;
    bool show_magnet = false;
    bool show_version = false;
    bool unsorted = false;
    bool print_header = true;
    bool print_info = true;
    bool print_trackers = true;
    bool print_files = true;
    bool show_bytesize = false;
};

int parseCommandLine(app_opts& opts, int argc, char const* const* argv)
{
    int c;
    char const* optarg;

    while ((c = tr_getopt(Usage, argc, argv, std::data(Options), &optarg)) != TR_OPT_DONE) {
        switch (c) {
        case 'b':
            opts.show_bytesize = true;
            break;

        // these select a single section, so each one clears the others
        case 'd':
        case 'i':
        case 't':
        case 'f':
            opts.print_header = c == 'd';
            opts.print_info = c == 'i';
            opts.print_trackers = c == 't';
            opts.print_files = c == 'f';
            break;

        case 'D':
            opts.print_header = false;
            break;

        case 'I':
            opts.print_info = false;
            break;

        case 'T':
            opts.print_trackers = false;
            break;

        case 'F':
            opts.print_files = false;
            break;

        case 'm':
            opts.show_magnet = true;
            break;

        case 's':
            opts.scrape = true;
            break;

        case 'u':
            opts.unsorted = true;
            break;

        case 'V':
            opts.show_version = true;
            break;

        case TR_OPT_UNK:
            opts.filename = optarg;
            break;

        default:
            return 1;
        }
    }

    return 0;
}

[[nodiscard]] auto toString(time_t now)
{
    return now == 0 ? "Unknown" : fmt::format("{:%a %b %d %T %Y}", *std::localtime(&now));
}

void showInfo(app_opts const& opts, tr_torrent_metainfo const& metainfo)
{
    /**
    ***  General Info
    **/
    if (opts.print_info) {
        fmt::print("GENERAL\n\n");
        fmt::print("  Name: {:s}\n", metainfo.name());
        if (metainfo.has_v1_metadata()) {
            fmt::print("  Hash v1: {:s}\n", metainfo.info_hash_string());
        }
        if (metainfo.has_v2_metadata()) {
            fmt::print("  Hash v2: {:s}\n", metainfo.info_hash2_string());
        }
        fmt::print("  Created by: {:s}\n", std::empty(metainfo.creator()) ? "Unknown" : metainfo.creator());
        fmt::print("  Created on: {:s}\n\n", toString(metainfo.date_created()));

        if (!std::empty(metainfo.comment())) {
            fmt::print("  Comment: {:s}\n", metainfo.comment());
        }

        if (!std::empty(metainfo.source())) {
            fmt::print("  Source: {:s}\n", metainfo.source());
        }

        fmt::print("  Piece Count: {:d}\n", metainfo.piece_count());
        fmt::print("  Piece Size: {:s}\n", Memory{ metainfo.piece_size(), Memory::Units::Bytes }.to_string());
        fmt::print("  Total Size: {:s}\n", Storage{ metainfo.total_size(), Storage::Units::Bytes }.to_string());
        fmt::print("  Privacy: {:s}\n", metainfo.is_private() ? "Private torrent" : "Public torrent");
    }

    /**
    ***  Trackers
    **/

    if (opts.print_trackers) {
        fmt::print("\nTRACKERS\n");
        auto current_tier = std::optional<tr_tracker_tier_t>{};
        auto print_tier = size_t{ 1 };
        for (auto const& tracker : metainfo.announce_list()) {
            if (current_tier != tracker.tier) {
                current_tier = tracker.tier;
                fmt::print("\n  Tier #{:d}\n", print_tier);
                ++print_tier;
            }

            fmt::print("  {:s}\n", tracker.announce.sv());
        }

        if (auto const n_webseeds = metainfo.webseed_count(); n_webseeds > 0) {
            fmt::print("\nWEBSEEDS\n\n");

            for (size_t i = 0; i < n_webseeds; ++i) {
                fmt::print("  {:s}\n", metainfo.webseed(i));
            }
        }
    }

    /**
    ***  Files
    **/

    if (opts.print_files) {
        if (!opts.show_bytesize) {
            fmt::print("\nFILES\n\n");
        }

        // `subpath` borrows from `metainfo` and is kept for sorting: a bytesize
        // line leads with the size, which would dominate a sort of whole lines.
        struct Row {
            std::string_view subpath;
            std::string line;
        };

        auto rows = std::vector<Row>{};
        rows.reserve(metainfo.file_count());
        for (tr_file_index_t i = 0, n = metainfo.file_count(); i < n; ++i) {
            auto const& subpath = metainfo.file_subpath(i);
            auto line = opts.show_bytesize ?
                fmt::format("{:d} {:s}", metainfo.file_size(i), subpath) :
                fmt::format("  {:s} ({:s})", subpath, Storage{ metainfo.file_size(i), Storage::Units::Bytes }.to_string());
            rows.push_back({ subpath, std::move(line) });
        }

        if (!opts.unsorted) {
            if (opts.show_bytesize) {
                std::ranges::sort(rows, {}, &Row::subpath);
            } else {
                std::ranges::sort(rows, {}, &Row::line);
            }
        }

        for (auto const& row : rows) {
            fmt::print("{:s}\n", row.line);
        }
    }
}

void doScrape(tr_torrent_metainfo const& metainfo)
{
    auto mediator = tr_web::Mediator{};
    auto web = tr_web::create(mediator);

    auto const& hash = metainfo.info_hash();
    auto const hash_sv = std::string_view{ reinterpret_cast<char const*>(std::data(hash)), std::size(hash) };

    for (auto const& tracker : metainfo.announce_list()) {
        if (std::empty(tracker.scrape)) {
            continue;
        }

        // build the full scrape URL
        auto scrape_url = std::string{ tracker.scrape.sv() };
        scrape_url += tr_strv_contains(scrape_url, '?') ? '&' : '?';
        scrape_url += "info_hash="sv;
        tr_urlPercentEncode(scrape_url, hash);
        fmt::print("{:s} ... ", scrape_url);
        fflush(stdout);

        // execute the http scrape.
        // the callback runs on tr_web's own thread;
        // the promise hands the response back to this one.
        auto response_promise = std::promise<tr_web::FetchResponse>{};
        auto response_future = response_promise.get_future();
        web->fetch(
            { std::move(scrape_url),
              [&response_promise](tr_web::FetchResponse const& resp) { response_promise.set_value(resp); },
              nullptr,
              TimeoutSecs });
        auto const response = response_future.get();

        // check the response code
        if (auto const code = response.status; code != 200 /*HTTP OK*/) {
            fmt::print("error: unexpected response {:d} '{:s}'\n", code, tr_webGetResponseStr(code));
            continue;
        }

        // print it out
        auto const otop = tr_variant_serde::benc().inplace().parse(response.body);
        if (!otop) {
            fmt::print("error parsing scrape response\n");
            continue;
        }

        // the swarm counts are keyed by raw info hash under "files"
        auto const* const top = otop->get_if<tr_variant::Map>();
        auto const* const files = top != nullptr ? top->find_if<tr_variant::Map>(TR_KEY_files) : nullptr;
        auto matched = false;

        if (files != nullptr) {
            for (auto const& [key, val] : *files) {
                if (hash_sv != tr_quark_get_string_view(key)) {
                    continue;
                }

                auto const* const counts = val.get_if<tr_variant::Map>();
                auto const count_of = [counts](tr_quark const quark) {
                    // -1 means the tracker did not report the count
                    return counts != nullptr ? counts->value_if<int64_t>(quark).value_or(-1) : -1;
                };
                fmt::print("{:d} seeders, {:d} leechers\n", count_of(TR_KEY_complete), count_of(TR_KEY_incomplete));
                matched = true;
            }
        }

        if (!matched) {
            fmt::print("no match\n");
        }
    }
}

} // namespace

int tr_main(int argc, char* argv[])
{
    tr_lib_init();

    tr_locale_set_global("");

    tr_logSetQueueEnabled(false);
    tr_logSetLevel(TR_LOG_ERROR);

    auto opts = app_opts{};
    if (parseCommandLine(opts, argc, (char const* const*)argv) != 0) {
        return EXIT_FAILURE;
    }

    if (opts.show_version) {
        fmt::print(stderr, "{:s} {:s}\n", MyName, LONG_VERSION_STRING);
        return EXIT_SUCCESS;
    }

    /* make sure the user specified a filename */
    if (std::empty(opts.filename)) {
        fmt::print(stderr, "ERROR: No torrent file specified.\n");
        tr_getopt_usage(MyName, Usage, std::data(Options));
        fmt::print(stderr, "\n");
        return EXIT_FAILURE;
    }

    /* try to parse the torrent file */
    auto metainfo = tr_torrent_metainfo{};
    auto error = tr_error{};
    auto const parsed = metainfo.parse_torrent_file(opts.filename, nullptr, &error);
    if (error) {
        fmt::print(stderr, "Error parsing torrent file '{:s}': {:s} ({:d})\n", opts.filename, error.message(), error.code());
    }
    if (!parsed) {
        return EXIT_FAILURE;
    }

    if (opts.show_magnet) {
        fmt::print("{:s}", metainfo.magnet());
    } else {
        if (opts.print_header) {
            fmt::print("Name: {:s}\n", metainfo.name());
            fmt::print("File: {:s}\n", opts.filename);
            fmt::print("\n");
            fflush(stdout);
        }

        if (opts.scrape) {
            doScrape(metainfo);
        } else {
            showInfo(opts, metainfo);
        }
    }

    /* cleanup */
    putc('\n', stdout);
    return EXIT_SUCCESS;
}
