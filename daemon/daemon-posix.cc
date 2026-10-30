// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#include <cassert>
#include <cerrno>
#include <csignal>
#include <string_view>
#include <vector>

#include <fcntl.h>
#include <unistd.h> /* fork(), setsid(), chdir(), dup2(), close(), pipe() */

#ifdef HAVE_SYS_SIGNALFD_H
#include <sys/signalfd.h>
#endif /* HAVE_SYS_SIGNALFD_H */

#include <event2/event.h>

#include <fmt/format.h>

#include <libtransmission/error.h>
#include <libtransmission/string-utils.h>
#include <libtransmission/utils-ev.h>

#include "daemon.h"

namespace
{
void set_system_error(tr_error& error, int code, std::string_view message)
{
    error.set(code, fmt::format("{:s}: {:s} ({:d})", message, tr_strerror(code), code));
}
} // namespace

#ifdef HAVE_SYS_SIGNALFD_H

namespace
{
void handle_signals(evutil_socket_t fd, short /*what*/, void* arg)
{
    struct signalfd_siginfo fdsi = {};
    auto* const daemon = static_cast<tr_daemon*>(arg);

    if (read(fd, &fdsi, sizeof(fdsi)) != sizeof(fdsi)) {
        assert("Error reading signal descriptor" && 0);
    } else {
        switch (fdsi.ssi_signo) {
        case SIGHUP:
            daemon->reconfigure();
            break;
        case SIGINT:
        case SIGTERM:
            daemon->stop();
            break;
        default:
            assert("Unexpected signal" && 0);
        }
    }
}
} // namespace

bool tr_daemon::setup_signals()
{
    sigset_t mask = {};

    sigemptyset(&mask);
    sigaddset(&mask, SIGINT);
    sigaddset(&mask, SIGTERM);
    sigaddset(&mask, SIGHUP);

    if (sigprocmask(SIG_BLOCK, &mask, nullptr) < 0) {
        return false;
    }

    sigfd_ = signalfd(-1, &mask, 0);
    if (sigfd_ < 0) {
        return false;
    }

    if (auto const sig_ev = event_new(ev_base_, sigfd_, EV_READ | EV_PERSIST, handle_signals, this)) {
        sig_evs_.emplace_back(sig_ev);
        return event_add(sig_ev, nullptr) >= 0;
    }
    return false;
}

#else /* no signalfd API, use evsignal */

namespace
{

void reconfigure_marshall(evutil_socket_t /*fd*/, short /*events*/, void* vdaemon)
{
    static_cast<tr_daemon*>(vdaemon)->reconfigure();
}

void stop_marshall(evutil_socket_t /*fd*/, short /*events*/, void* vdaemon)
{
    static_cast<tr_daemon*>(vdaemon)->stop();
}

bool setup_signal(
    struct event_base* ev_base,
    std::vector<tr::evhelpers::event_unique_ptr>& sig_evs,
    int sig,
    void (*callback)(evutil_socket_t, short, void*),
    void* arg)
{
    if (auto const sig_ev = evsignal_new(ev_base, sig, callback, arg)) {
        sig_evs.emplace_back(sig_ev);
        return evsignal_add(sig_ev, nullptr) >= 0;
    }
    return false;
}

} // namespace

bool tr_daemon::setup_signals()
{
    return setup_signal(ev_base_, sig_evs_, SIGHUP, reconfigure_marshall, this) &&
        setup_signal(ev_base_, sig_evs_, SIGINT, stop_marshall, this) &&
        setup_signal(ev_base_, sig_evs_, SIGTERM, stop_marshall, this);
}

#endif /* HAVE_SYS_SIGNALFD_H */

void tr_daemon::cleanup_signals()
{
    sig_evs_.clear();

#ifdef HAVE_SYS_SIGNALFD_H
    if (sigfd_ >= 0) {
        close(sigfd_);
        sigfd_ = -1;
    }
#endif /* HAVE_SYS_SIGNALFD_H */
}

bool tr_daemon::spawn(bool foreground, int* exit_code, tr_error& error)
{
    *exit_code = 1;

    if (!foreground) {
#if defined(HAVE_DAEMON) && !defined(__APPLE__) && !defined(__UCLIBC__)

        if (daemon(1, 0) == -1) {
            set_system_error(error, errno, "daemon() failed");
            return false;
        }

#else

        /* this is loosely based off of glibc's daemon() implementation
         * https://sourceware.org/git/?p=glibc.git;a=blob_plain;f=misc/daemon.c */

        switch (fork()) {
        case -1:
            set_system_error(error, errno, "fork() failed");
            return false;

        case 0:
            break;

        default:
            *exit_code = 0;
            return true;
        }

        if (setsid() == -1) {
            set_system_error(error, errno, "setsid() failed");
            return false;
        }

        {
            int const fd = open("/dev/null", O_RDWR, 0);
            dup2(fd, STDIN_FILENO);
            dup2(fd, STDOUT_FILENO);
            dup2(fd, STDERR_FILENO);
            close(fd);
        }

#endif
    }

    *exit_code = start(foreground);

    return true;
}
