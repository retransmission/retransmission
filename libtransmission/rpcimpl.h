// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <cstdint> // int16_t
#include <functional>

struct tr_session;
struct tr_variant;

#define RPC_VERSION_VARS(major, minor, patch) \
    auto inline constexpr TrRpcVersionSemver = std::string_view{ #major "." #minor "." #patch }; \
    auto inline constexpr TrRpcVersionSemverMajor = major;

// When bumping here, remember to bump `RpcVersion` and maybe `RpcVersionMin`.
RPC_VERSION_VARS(6, 1, 0)

#undef RPC_VERSION_VARS

namespace JsonRpc
{
auto inline constexpr Version = std::string_view{ "2.0" };

namespace Error
{
enum Code : int16_t {
    PARSE_ERROR = -32700,
    INVALID_REQUEST = -32600,
    METHOD_NOT_FOUND = -32601,
    INVALID_PARAMS = -32602,
    INTERNAL_ERROR = -32603,
    SUCCESS = 0,
    SET_ANNOUNCE_LIST = 1,
    INVALID_TRACKER_LIST = 2,
    PATH_NOT_ABSOLUTE = 3,
    UNRECOGNIZED_INFO = 4,
    SYSTEM_ERROR = 5,
    FILE_IDX_OOR = 6,
    PIECE_IDX_OOR = 7,
    HTTP_ERROR = 8,
    CORRUPT_TORRENT = 9,
    INVALID_BLOCKLIST_DATA = 10,
};

[[nodiscard]] std::string_view to_string(Code code);
} // namespace Error
} // namespace JsonRpc

using tr_rpc_response_func = std::function<void(tr_variant&& response)>;

void tr_rpc_request_exec(tr_session* session, tr_variant request, tr_rpc_response_func&& callback = {});

void tr_rpc_request_exec(tr_session* session, std::string_view request, tr_rpc_response_func&& callback = {});
