// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#include <array>
#include <chrono>
#include <cstddef> // size_t
#include <ctime> // time_t
#include <memory>
#include <string>
#include <string_view>
#include <utility> // std::move

#include <fmt/format.h>

#include <archive.h>
#include <archive_entry.h>

#include "libtransmission/transmission.h"

#include "libtransmission/blocklist-download.h"
#include "libtransmission/error.h"
#include "libtransmission/file-utils.h" // tr_file_save()
#include "libtransmission/file.h" // tr_sys_path_remove()
#include "libtransmission/log.h"
#include "libtransmission/session.h"
#include "libtransmission/timer.h"
#include "libtransmission/tr-assert.h"
#include "libtransmission/tr-strbuf.h" // tr_pathbuf
#include "libtransmission/utils.h" // _(), tr_time()
#include "libtransmission/web-utils.h" // tr_webGetResponseStr()
#include "libtransmission/web.h"

using namespace std::literals;

namespace tr::blocklist
{
namespace
{
using namespace std::chrono_literals;

// how often to automatically re-download the blocklist
auto constexpr UpdateInterval = std::chrono::hours{ 24 * 7 };

// wait this long after startup before the first auto-update, so we don't
// hammer the network the instant the app launches
auto constexpr StartupDelay = std::chrono::seconds{ 60 };

// Decompress a downloaded blocklist. Blocklists are distributed either as
// plain text or wrapped in a container/compressor: gzip, tar (possibly
// gzipped), or zip. Let libarchive transparently peel off any filter (gzip,
// xz, ...) and format (tar, zip, ...) and hand back the first regular file's
// contents. The "raw" format is registered last, so a plain, unwrapped
// blocklist is read as a single entry rather than rejected.
[[nodiscard]] std::string decompress(std::string_view body)
{
    auto* const arc = archive_read_new();
    archive_read_support_filter_all(arc);
    archive_read_support_format_all(arc);
    archive_read_support_format_raw(arc);

    auto content = std::string{};

    if (archive_read_open_memory(arc, std::data(body), std::size(body)) == ARCHIVE_OK) {
        struct archive_entry* entry = nullptr;
        while (archive_read_next_header(arc, &entry) == ARCHIVE_OK) {
            // a blocklist is a single file; skip directories and take the
            // first file we find
            if (archive_entry_filetype(entry) == AE_IFDIR) {
                continue;
            }

            auto buf = std::array<char, 16U * 1024U>{};
            for (;;) {
                auto const n_read = archive_read_data(arc, std::data(buf), std::size(buf));
                if (n_read <= 0) {
                    break; // 0 == end of entry; < 0 == read error, give up on this entry
                }
                content.append(std::data(buf), static_cast<size_t>(n_read));
            }
            break;
        }
    }

    archive_read_free(arc);
    return content;
}
} // namespace

// Per-request state, kept alive by the tr_web fetch callback for the duration
// of the download. `cancelled` lets Updater::cancel() (and ~Updater) suppress a
// completion without racing the network thread.
struct Pending {
    tr_session* session = nullptr;
    tr_blocklist_update_func on_done;
    bool cancelled = false;
};

namespace
{
void finish_request(tr_web::FetchResponse const& response, std::shared_ptr<Pending> const& pending)
{
    if (pending->cancelled) {
        return;
    }

    auto* const session = pending->session;
    auto result = tr_blocklist_update_result{};

    if (response.status != 200) {
        // we failed to download the blocklist...
        result.status = tr_blocklist_update_status::DownloadError;
        result.error = fmt::format(
            fmt::runtime(_("Couldn't fetch blocklist: {error} ({error_code})")),
            fmt::arg("error", tr_webGetResponseStr(response.status)),
            fmt::arg("error_code", response.status));
        pending->on_done(result);
        return;
    }

    // tr_blocklistSetContent() needs a source file, so decompress the
    // download and save it into a tmpfile first
    auto const content = decompress(response.body);
    auto const filename = tr_pathbuf{ session->configDir(), "/blocklist.tmp"sv };
    if (auto error = tr_error{}; !tr_file_save(filename, content, &error)) {
        result.status = tr_blocklist_update_status::SaveError;
        result.error = fmt::format(
            fmt::runtime(_("Couldn't save '{path}': {error} ({error_code})")),
            fmt::arg("path", filename),
            fmt::arg("error", error.message()),
            fmt::arg("error_code", error.code()));
        pending->on_done(result);
        return;
    }

    auto const n_rules = tr_blocklistSetContent(session, filename);
    tr_sys_path_remove(filename);
    if (!n_rules || *n_rules == 0U) {
        result.status = tr_blocklist_update_status::InvalidData;
    } else {
        result.status = tr_blocklist_update_status::Ok;
        result.n_rules = *n_rules;
    }

    pending->on_done(result);
}
} // namespace

Updater::Updater(tr_session* session)
    : session_{ session }
    , timer_{ session->timerMaker().create([this]() { on_auto_update_timer(); }) }
{
}

Updater::~Updater()
{
    // If a fetch is still in flight (its shared Pending is held alive by the
    // tr_web callback), make sure a late completion becomes a no-op instead of
    // touching a session that's tearing down.
    if (auto pending = latest_.lock()) {
        pending->cancelled = true;
    }
}

void Updater::update(tr_blocklist_update_func on_done)
{
    session_->run_in_session_thread([this, on_done = std::move(on_done)]() mutable {
        // Supersede any still-in-flight update so only the newest one installs a
        // result and fires its callback. (The superseded download may still run
        // to completion internally; tr_web has no per-request abort.)
        if (auto previous = latest_.lock()) {
            previous->cancelled = true;
        }

        auto pending = std::make_shared<Pending>(
            Pending{ .session = session_, .on_done = std::move(on_done), .cancelled = false });
        latest_ = pending;
        session_->fetch(
            { std::string{ session_->blocklistUrl() },
              [pending](tr_web::FetchResponse const& response) { finish_request(response, pending); },
              nullptr });
    });
}

void Updater::cancel()
{
    session_->run_in_session_thread([this]() {
        if (auto pending = latest_.lock()) {
            pending->cancelled = true;
        }
    });
}

void Updater::restart_timer()
{
    session_->run_in_session_thread([this]() { arm_timer(); });
}

void Updater::arm_timer()
{
    if (!session_->blocklist_enabled() || std::empty(session_->blocklistUrl()) || !session_->blocklist_updates_enabled()) {
        timer_->stop();
        return;
    }

    auto const now = tr_time();
    auto const interval_secs = std::chrono::duration_cast<std::chrono::seconds>(UpdateInterval).count();
    auto const startup_secs = std::chrono::duration_cast<std::chrono::seconds>(StartupDelay).count();
    auto const due = session_->blocklist_mtime() + interval_secs;
    auto const wait_secs = due > now + startup_secs ? due - now : startup_secs;
    timer_->start_single_shot(std::chrono::seconds{ wait_secs });
}

void Updater::on_auto_update_timer()
{
    update([](tr_blocklist_update_result const& result) {
        if (result.status == tr_blocklist_update_status::Ok) {
            tr_logAddInfo(
                fmt::format(
                    fmt::runtime(_("Automatically updated blocklist, which now has {count} rules")),
                    fmt::arg("count", result.n_rules)));
        } else if (!std::empty(result.error)) {
            tr_logAddWarn(std::string{ result.error });
        } else {
            tr_logAddWarn(_("Couldn't update blocklist"));
        }
    });

    // re-arm for the next interval regardless of this attempt's outcome,
    // so a broken URL retries on the normal cadence instead of hot-looping
    timer_->start_single_shot(std::chrono::duration_cast<std::chrono::milliseconds>(UpdateInterval));
}

} // namespace tr::blocklist

// ---
// tr_session glue + public C++ API

void tr_session::on_blocklist_settings_changed()
{
    if (blocklist_updater_) {
        blocklist_updater_->restart_timer();
    }
}

void tr_blocklistUpdate(tr_session* session, tr_blocklist_update_func on_done)
{
    TR_ASSERT(session != nullptr);
    session->blocklist_updater()->update(std::move(on_done));
}

void tr_blocklistUpdateCancel(tr_session* session)
{
    TR_ASSERT(session != nullptr);
    if (auto* const updater = session->blocklist_updater()) {
        updater->cancel();
    }
}

time_t tr_blocklistGetMTime(tr_session const* session)
{
    TR_ASSERT(session != nullptr);
    return session->blocklist_mtime();
}

bool tr_blocklistUpdatesEnabled(tr_session const* session)
{
    TR_ASSERT(session != nullptr);
    return session->blocklist_updates_enabled();
}

void tr_blocklistSetUpdatesEnabled(tr_session* session, bool enabled)
{
    TR_ASSERT(session != nullptr);
    session->set_blocklist_updates_enabled(enabled);
}
