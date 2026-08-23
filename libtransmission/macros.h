// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#pragma once

#if __cpp_lib_constexpr_vector >= 201907L
#define TR_CONSTEXPR_VEC constexpr
#else
#define TR_CONSTEXPR_VEC
#endif

#if __cpp_lib_constexpr_string >= 201907L
#define TR_CONSTEXPR_STR constexpr
#else
#define TR_CONSTEXPR_STR
#endif

#if __cplusplus >= 202302L // _MSVC_LANG value for C++23 not available yet
#define TR_CONSTEXPR23 constexpr
#else
#define TR_CONSTEXPR23
#endif

// ---

#ifdef _WIN32
#define TR_IF_WIN32(ThenValue, ElseValue) ThenValue
#else
#define TR_IF_WIN32(ThenValue, ElseValue) ElseValue
#endif

// ---

#define TR_PROJ_DOMAIN_TLD "org"
#define TR_PROJ_DOMAIN_SLD "retransmission"

#define TR_PROJ_DOMAIN_APEX TR_PROJ_DOMAIN_SLD "." TR_PROJ_DOMAIN_TLD
#define TR_PROJ_DOMAIN_APEX_REVERSED TR_PROJ_DOMAIN_TLD "." TR_PROJ_DOMAIN_SLD
#define TR_PROJ_DOMAIN_DHT "dht." TR_PROJ_DOMAIN_APEX

#define TR_PROJ_APPNAME "retransmission"
#define TR_PROJ_APPNAME_CAPITALIZED "Retransmission"
#define TR_PROJ_APPNAME_RDNS TR_PROJ_DOMAIN_APEX_REVERSED "." TR_PROJ_APPNAME

#define TR_PROJ_URL_HOMEPAGE "https://" TR_PROJ_DOMAIN_APEX
#define TR_PROJ_URL_DONATE TR_PROJ_URL_HOMEPAGE "/donate"
#define TR_PROJ_URL_HELP TR_PROJ_URL_HOMEPAGE "/help"
#define TR_PROJ_URL_GIT "https://github.com/retransmission/retransmission"
#define TR_PROJ_URL_FORUM "https://forum." TR_PROJ_DOMAIN_APEX

#define TR_PROJ_URL_IPV4 "https://ipv4." TR_PROJ_DOMAIN_APEX
#define TR_PROJ_URL_IPV6 "https://ipv6." TR_PROJ_DOMAIN_APEX
#define TR_PROJ_URL_PORTCHECK "https://portcheck." TR_PROJ_DOMAIN_APEX

// The D-Bus names live in `libtransmission-app/interop-names.h` as literal strings,
// where the interop tests can read them out at configure time.

#define TR_PROJ_WEB_SERVER_BASE_PATH "/" TR_PROJ_APPNAME "/"

// Interop contract, shared with separately-built clients.
// `libtransmission-app/interop-names.h` inventories every such name.

// The appname the interactive clients pass to `tr::platform::get_default_config_dir()`.
// It lets a user's settings and torrents survive replacing one client with another.
// The daemon deliberately does not use it, and defaults to its own dir.
#define TR_PROJ_SHARED_CONFIG_DIRNAME "transmission"

// A third-party RPC client works against every such client
// only while they all spell these headers the same way.
#define TR_PROJ_SHARED_RPC_SESSION_ID_HEADER "X-Transmission-Session-Id"
#define TR_PROJ_SHARED_RPC_VERSION_HEADER "X-Transmission-Rpc-Version"
