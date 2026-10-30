// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#ifdef _WIN32
#include <windows.h>
#else
#include <cerrno>
#endif

#include "libtransmission/error.h"
#include "libtransmission/string-utils.h"

bool tr_error::is_enoent() const noexcept
{
#ifdef _WIN32
    return code_ == ERROR_FILE_NOT_FOUND || code_ == ERROR_PATH_NOT_FOUND;
#else
    return code_ == ENOENT;
#endif
}

bool tr_error::is_enospc() const noexcept
{
#ifdef _WIN32
    return code_ == ERROR_DISK_FULL;
#else
    return code_ == ENOSPC;
#endif
}

bool tr_error::is_transient() const noexcept
{
#ifdef _WIN32
    return code_ == WSAEWOULDBLOCK || code_ == WSAEINTR || code_ == WSAEINPROGRESS;
#else
    return code_ == EAGAIN || code_ == EWOULDBLOCK || code_ == EINTR || code_ == EINPROGRESS;
#endif
}

tr_error tr_error::from_errno(int const errnum)
{
    auto error = tr_error{};
    error.set_from_errno(errnum);
    return error;
}

void tr_error::set_from_errno(int const errnum)
{
    code_ = errnum;

    message_ = errnum != 0 ? tr_strerror(errnum) : "";
}
