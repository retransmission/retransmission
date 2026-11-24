// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#pragma once

#ifndef __TRANSMISSION__
#error only libtransmission should #include this header.
#endif

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

struct tr_session;

namespace tr::platform
{

// The memory this process can count on: the machine's RAM, or its cgroup's
// limit where that is lower, as in a container. No value if the OS reports
// neither.
[[nodiscard]] std::optional<uint64_t> get_memory_size();

#ifdef __linux__
namespace detail
{

// The lowest memory limit among this process's cgroups and their ancestors.
// `proc_self_cgroup` is the text of /proc/self/cgroup, and `cgroup_root` is
// where the cgroup filesystems are mounted. No value if no limit file holds
// a number. cgroup v2 spells no limit as "max"; v1 spells it as a number
// above any machine's RAM, which is returned as is.
// Public for tests; everything else calls get_memory_size().
[[nodiscard]] std::optional<uint64_t> cgroup_memory_limit(std::string_view proc_self_cgroup, std::string_view cgroup_root);

} // namespace detail
#endif

} // namespace tr::platform

/** @brief return the directory where the Web Client's web ui files are kept */
[[nodiscard]] std::string tr_getWebClientDir(tr_session const* session);

/** @brief return the directory where session id lock files are stored */
[[nodiscard]] std::string tr_getSessionIdDir();
