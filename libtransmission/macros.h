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

#ifdef __UCLIBC__
#define TR_UCLIBC_CHECK_VERSION(major, minor, micro) \
    (__UCLIBC_MAJOR__ > (major) || (__UCLIBC_MAJOR__ == (major) && __UCLIBC_MINOR__ > (minor)) || \
     (__UCLIBC_MAJOR__ == (major) && __UCLIBC_MINOR__ == (minor) && __UCLIBC_SUBLEVEL__ >= (micro)))
#else
#define TR_UCLIBC_CHECK_VERSION(major, minor, micro) 0
#endif

// ---

#define TR_PROJ_DOMAIN_TLD "org"
#define TR_PROJ_DOMAIN_SLD "retransmission"

#define TR_PROJ_APPNAME "retransmission"
#define TR_PROJ_APPNAME_CAPITALIZED "Retransmission"
#define TR_PROJ_APPNAME_RDNS TR_PROJ_DOMAIN_APEX_REVERSED "." TR_PROJ_APPNAME

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

#define TR_PROJ_DBUS_SERVICE TR_PROJ_DOMAIN_TLD "." TR_PROJ_DOMAIN_SLD "." TR_PROJ_APPNAME_CAPITALIZED
#define TR_PROJ_DBUS_PATH "/" TR_PROJ_DOMAIN_TLD "/" TR_PROJ_DOMAIN_SLD "/" TR_PROJ_APPNAME_CAPITALIZED
#define TR_PROJ_DBUS_INTERFACE TR_PROJ_DBUS_SERVICE

#define TR_PROJ_WEB_SERVER_BASE_PATH "/" TR_PROJ_APPNAME "/"
#define TR_PROJ_RPC_SESSION_ID_HEADER "X-" TR_PROJ_APPNAME_CAPITALIZED "-Session-Id"
#define TR_PROJ_RPC_VERSION_HEADER "X-" TR_PROJ_APPNAME_CAPITALIZED "-Rpc-Version"
