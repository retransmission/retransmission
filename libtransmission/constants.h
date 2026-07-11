// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <cstdint>
#include <string_view>

#include "libtransmission/macros.h"

inline auto constexpr TrInet6AddrStrlen = 46U;

inline auto constexpr TrAddrStrlen = 64U;

inline auto constexpr TrDefaultBlocklistFilename = std::string_view{ "blocklist.bin" };
inline auto constexpr TrDefaultHttpServerBasePath = std::string_view{ TR_PROJ_WEB_SERVER_BASE_PATH };
inline auto constexpr TrDefaultPeerLimitGlobal = 200U;
inline auto constexpr TrDefaultPeerLimitTorrent = 50U;
inline auto constexpr TrDefaultPeerPort = 51413U;
inline auto constexpr TrDefaultPeerSocketTos = std::string_view{ "le" };
inline auto constexpr TrDefaultRpcPort = 9091U;
inline auto constexpr TrDefaultRpcWhitelist = std::string_view{ "127.0.0.1,::1" };

inline auto constexpr TrMaxRecentDirs = 6U;

inline auto constexpr TrHttpServerRpcRelativePath = std::string_view{ "rpc" };
inline auto constexpr TrHttpServerWebRelativePath = std::string_view{ "web/" };
inline auto constexpr TrRpcSessionIdHeader = std::string_view{ TR_PROJ_RPC_SESSION_ID_HEADER };
inline auto constexpr TrRpcVersionHeader = std::string_view{ TR_PROJ_RPC_VERSION_HEADER };

inline auto constexpr TrBlockSize = uint32_t{ 1024U * 16U };
