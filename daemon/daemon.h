// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <cstdio>
#include <string>
#include <vector>

#include <libtransmission/utils-ev.h>
#include <libtransmission/variant.h>

struct event_base;
struct tr_error;
struct tr_session;

class tr_daemon
{
public:
    [[nodiscard]] bool spawn(bool foreground, int* exit_code, tr_error& error);
    [[nodiscard]] bool init(int argc, char const* const* argv, bool* foreground, int* ret);
    void handle_error(tr_error const& /*error*/) const;
    [[nodiscard]] int start(bool foreground);
    void periodic_update();
    void reconfigure();
    void stop();

private:
#ifdef HAVE_SYS_SIGNALFD_H
    int sigfd_ = -1;
#endif /* HAVE_SYS_SIGNALFD_H */
    std::vector<tr::evhelpers::event_unique_ptr> sig_evs_;
    bool seen_hup_ = false;
    std::string config_dir_;
    tr::Settings settings_;
    tr_session* my_session_ = nullptr;
    char const* log_file_name_ = nullptr;
    struct event_base* ev_base_ = nullptr;
    FILE* log_stream_ = nullptr;

    [[nodiscard]] bool parse_args(int argc, char const* const* argv, bool* dump_settings, bool* foreground, int* exit_code);
    bool reopen_log_file(char const* filename);
    [[nodiscard]] bool setup_signals();
    void cleanup_signals();
    void report_status();
};
