// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#include <array>
#include <chrono>
#include <cstddef> // size_t
#include <ctime> // time_t
#include <memory>
#include <optional>
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
#include "libtransmission/string-utils.h" // tr_strv_strip()
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

// Refuse to decompress anything absurdly large. Real blocklists are tens of MB,
// so this bounds a decompression bomb -- a tiny archive that expands to GBs --
// while leaving genuine lists plenty of headroom.
auto constexpr MaxDecompressedSize = size_t{ 128U } * 1024U * 1024U;
} // namespace

// Decompress a downloaded blocklist. Blocklists are distributed either as
// plain text or wrapped in a container/compressor: gzip, tar (possibly
// gzipped), or zip. Let libarchive transparently peel off any filter (gzip,
// xz, ...) and format (tar, zip, ...) and hand back the first regular file's
// contents. The "raw" format is registered last, so a plain, unwrapped
// blocklist is read as a single entry rather than rejected.
std::string decompress(std::string_view body)
{
    auto* const arc = archive_read_new();
    archive_read_support_filter_all(arc);
    archive_read_support_format_all(arc);
    archive_read_support_format_raw(arc);

    auto content = std::string{};
    content.reserve(std::size(body)); // exact for plain text, a lower bound once decompressed

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
                if (std::size(content) > MaxDecompressedSize) {
                    // likely a decompression bomb; discard it entirely so a
                    // truncated prefix isn't mistaken for a valid blocklist
                    tr_logAddWarn(
                        fmt::format(
                            fmt::runtime(_("Blocklist is larger than {limit} MiB; ignoring it")),
                            fmt::arg("limit", MaxDecompressedSize / (1024U * 1024U))));
                    content.clear();
                    break;
                }
            }
            break;
        }
    }

    archive_read_free(arc);
    return content;
}

std::string normalize_blocklist_url(std::string_view url)
{
    auto const trimmed = tr_strv_strip(url);
    if (std::empty(trimmed)) {
        return {};
    }

    // leave a URL that already carries a scheme (foo://...) alone
    if (trimmed.find("://"sv) != std::string_view::npos) {
        return std::string{ trimmed };
    }

    return fmt::format("https://{:s}", trimmed);
}

// Per-request state, kept alive by the tr_web fetch callback for the duration
// of the download. `cancelled` lets Updater::cancel() (and ~Updater) suppress a
// completion without racing the network thread.
struct Pending {
    Updater::Mediator* mediator = nullptr;
    tr_blocklist_update_func on_done;
    bool cancelled = false;
};

namespace
{
// True if `text` has at least one line that isn't blank or a comment -- i.e.
// something the parser could turn into a rule. A download that decompresses to
// only blanks/comments (or nothing at all) would otherwise replace a good
// blocklist with an empty one; mirrors parseFile()'s comment handling in
// blocklist.cc.
[[nodiscard]] bool has_rule_lines(std::string_view text)
{
    for (auto remain = text; !std::empty(remain);) {
        auto const eol = remain.find('\n');
        if (auto const line = tr_strv_strip(remain.substr(0U, eol));
            !std::empty(line) && !tr_strv_starts_with(line, "#"sv) && !tr_strv_starts_with(line, "//"sv)) {
            return true;
        }
        if (eol == std::string_view::npos) {
            break;
        }
        remain.remove_prefix(eol + 1U);
    }
    return false;
}

void finish_request(tr_web::FetchResponse const& response, std::shared_ptr<Pending> const& pending)
{
    if (pending->cancelled) {
        return;
    }

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

    // peel off any compression/archive, then hand the plain text to the
    // mediator to persist and parse
    auto const content = decompress(response.body);

    // A rule-less response (empty, whitespace, or comments only) would replace a
    // good blocklist with nothing. Report it as invalid and leave the installed
    // list untouched rather than silently wiping it -- a provider hiccup (an
    // empty file or a "temporarily unavailable" placeholder) shouldn't drop the
    // user's protection to zero on an unattended auto-update.
    if (!has_rule_lines(content)) {
        result.status = tr_blocklist_update_status::InvalidData;
        pending->on_done(result);
        return;
    }

    auto error = std::string{};
    if (auto const n_rules = pending->mediator->set_blocklist_content(content, error); !n_rules) {
        result.status = tr_blocklist_update_status::SaveError;
        result.error = std::move(error);
    } else if (*n_rules == 0U) {
        result.status = tr_blocklist_update_status::InvalidData;
    } else {
        result.status = tr_blocklist_update_status::Ok;
        result.n_rules = *n_rules;
    }

    pending->on_done(result);
}
} // namespace

Updater::Updater(Mediator& mediator)
    : mediator_{ mediator }
    , timer_{ mediator.timer_maker().create([this]() { on_auto_update_timer(); }) }
{
}

Updater::~Updater()
{
    // If a fetch is still in flight (its shared Pending is held alive by the
    // tr_web callback), make sure a late completion becomes a no-op instead of
    // touching a mediator that's tearing down.
    if (auto pending = latest_.lock()) {
        pending->cancelled = true;
    }
}

void Updater::update(tr_blocklist_update_func on_done)
{
    mediator_.run_in_session_thread([this, on_done = std::move(on_done)]() mutable {
        // Supersede any still-in-flight update so only the newest one installs a
        // result. Resolve the superseded request now with a Superseded status so
        // a caller waiting on it isn't left hanging; its own completion later is a
        // no-op (see finish_request's cancelled check), so on_done fires exactly
        // once. (The superseded download may still run to completion internally;
        // tr_web has no per-request abort.)
        if (auto previous = latest_.lock()) {
            previous->cancelled = true;
            if (previous->on_done) {
                auto superseded = tr_blocklist_update_result{};
                superseded.status = tr_blocklist_update_status::Superseded;
                previous->on_done(superseded);
            }
        }

        auto pending = std::make_shared<Pending>(Pending{ .mediator = &mediator_, .on_done = std::move(on_done) });
        latest_ = pending;
        mediator_.fetch(
            { mediator_.blocklist_url(),
              [pending](tr_web::FetchResponse const& response) { finish_request(response, pending); },
              nullptr });
    });
}

void Updater::cancel()
{
    mediator_.run_in_session_thread([this]() {
        if (auto pending = latest_.lock()) {
            pending->cancelled = true;
        }
    });
}

void Updater::restart_timer()
{
    mediator_.run_in_session_thread([this]() { arm_timer(); });
}

void Updater::arm_timer()
{
    if (!mediator_.enabled() || std::empty(mediator_.blocklist_url()) || !mediator_.updates_enabled()) {
        timer_->stop();
        return;
    }

    auto const now = std::chrono::seconds{ tr_time() };
    auto const due = std::chrono::seconds{ mediator_.mtime() } + UpdateInterval;
    auto const wait = due > now + StartupDelay ? due - now : StartupDelay;
    timer_->start_single_shot(wait);
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
    timer_->start_single_shot(UpdateInterval);
}

} // namespace tr::blocklist

// ---
// tr_session glue + public C++ API

std::string tr_session::BlocklistMediator::blocklist_url() const
{
    return tr::blocklist::normalize_blocklist_url(session_.blocklistUrl());
}

bool tr_session::BlocklistMediator::enabled() const noexcept
{
    return session_.blocklist_enabled();
}

bool tr_session::BlocklistMediator::updates_enabled() const noexcept
{
    return session_.blocklist_updates_enabled();
}

std::optional<size_t> tr_session::BlocklistMediator::set_blocklist_content(std::string_view content, std::string& error)
{
    // tr_blocklistSetContent() needs a source file, so persist the decompressed
    // content into a tmpfile first, then clean it up.
    auto const filename = tr_pathbuf{ session_.configDir(), "/blocklist.tmp"sv };
    if (auto err = tr_error{}; !tr_file_save(filename, content, &err)) {
        error = fmt::format(
            fmt::runtime(_("Couldn't save '{path}': {error} ({error_code})")),
            fmt::arg("path", filename),
            fmt::arg("error", err.message()),
            fmt::arg("error_code", err.code()));
        return std::nullopt;
    }

    auto const n_rules = tr_blocklistSetContent(&session_, filename);
    tr_sys_path_remove(filename);

    // nullopt from the parser means "no valid rules"; collapse it to 0 so the
    // Updater reports InvalidData rather than SaveError.
    return n_rules.value_or(0U);
}

void tr_session::on_blocklist_settings_changed()
{
    if (blocklist_updater_) {
        blocklist_updater_->restart_timer();
    }
}

void tr_blocklistUpdate(tr_session* session, tr_blocklist_update_func on_done)
{
    TR_ASSERT(session != nullptr);
    // null once the session is tearing down (see closeImplPart1); mirror the
    // guard in tr_blocklistUpdateCancel() instead of dereferencing blindly.
    if (auto* const updater = session->blocklist_updater()) {
        updater->update(std::move(on_done));
    }
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
