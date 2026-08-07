// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <cstdint> // uint16_t
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "libtransmission/digest.h"

/** @brief convenience function to determine if an address is an IP address (IPv4 or IPv6) */
[[nodiscard]] bool tr_addressIsIP(char const* address);

/** @brief return true if the url is a http or https or UDP url that Transmission understands */
[[nodiscard]] bool tr_urlIsValidTracker(std::string_view url);

/** @brief return true if the url is a [ http, https, ftp, sftp ] url that Transmission understands */
[[nodiscard]] bool tr_urlIsValid(std::string_view url);

struct tr_url_parsed_t {
    // http://example.com:80/over/there?name=ferret#nose

    std::string_view scheme; // "http"
    std::string_view authority; // "example.com:80"
    std::string_view host; // "example.com"
    std::string_view host_wo_brackets; // "example.com" ("[::1]" -> "::1")
    std::string_view sitename; // "example"
    std::string_view path; // /"over/there"
    std::string_view query; // "name=ferret"
    std::string_view fragment; // "nose"
    std::string_view full; // "http://example.com:80/over/there?name=ferret#nose"
    uint16_t port = 0;

    // returns a vector of key,val pairs, e.g.
    // `first=hello&second=world` -> [<"first","hello">,<"second","world">]
    [[nodiscard]] std::vector<std::pair<std::string_view, std::string_view>> query_entries() const;
};

[[nodiscard]] std::optional<tr_url_parsed_t> tr_urlParse(std::string_view url);

// like tr_urlParse(), but with the added constraint that 'scheme'
// must be one we that Transmission supports for announce and scrape
[[nodiscard]] std::optional<tr_url_parsed_t> tr_urlParseTracker(std::string_view url);

// Convenience function to get a log-safe version of a tracker URL.
// This is to avoid logging sensitive info, e.g. a personal announcer id in the URL.
[[nodiscard]] std::string tr_urlTrackerLogName(std::string_view url);

// Appends the percent-encoded form of `input` to `appendme`.
// When `escape_reserved` is false, the RFC 3986 reserved characters are
// passed through as-is; use that when encoding a URL rather than a single
// URL component, e.g. so that a webseed URL keeps its '/' separators.
void tr_urlPercentEncode(std::string& appendme, std::string_view input, bool escape_reserved = true);

// Appends the percent-encoded form of `digest`'s bytes to `appendme`.
// Appends at most `3 * std::size(digest)` chars.
void tr_urlPercentEncode(std::string& appendme, tr_sha1_digest_t const& digest);

[[nodiscard]] char const* tr_webGetResponseStr(long response_code);

[[nodiscard]] std::string tr_urlPercentDecode(std::string_view /*url*/);

// Split one HTTP header line ("Name: value") into its name and its value.
// Leading and trailing optional whitespace (spaces and tabs) is trimmed from
// the value per RFC 7230, and any trailing CR / LF is ignored.
// Returns nullopt when the line has no ':' -- e.g. the HTTP status line
// or the blank line that separates the headers from the body.
[[nodiscard]] std::optional<std::pair<std::string_view, std::string_view>> tr_httpParseHeaderLine(std::string_view line);
