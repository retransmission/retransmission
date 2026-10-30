// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#pragma once

#ifdef _WIN32
#include <cstdint>

// Smallest integer type that can represent all values returned
// by `DWORD GetLastError()` and `int WSAGetLastError()`.
//
// N.B. DWORD is an unsigned 32-bit integer, so int64_t is the next
// smallest signed integer type that can represent all its values.
using tr_error_code_t = int64_t;
static_assert(sizeof(int) <= sizeof(int64_t));

#else /* _WIN32 */

using tr_error_code_t = int;

#endif /* _WIN32 */
