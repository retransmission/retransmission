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

// Real blocklists are tens of MB, Refuse to block anything >128GB.
auto constexpr MaxDecompressedSize = size_t{ 128U } * 1024U * 1024U;
} // namespace

// Decompress a downloaded blocklist. Supports txt, tar, gz, tgz formats.
// Omit other libarchive formats for YAGNI, we can add later if/when needed.
std::string decompress(std::string_view const body)
{
    auto* const arc = archive_read_new();
    if (arc == nullptr) {
        return {};
    }
    archive_read_support_filter_gzip(arc);
    archive_read_support_format_tar(arc);
    archive_read_support_format_zip(arc);
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
    url = tr_strv_strip(url);
    if (std::empty(url)) {
        return {};
    }

    // leave a URL that already carries a scheme (foo://...) alone
    if (url.find("://"sv) != std::string_view::npos) {
        return std::string{ url };
    }

    return fmt::format("https://{:s}", url);
}

// Per-request state, kept alive by the tr_web fetch callback during the fetch.
struct Pending {
    Updater::Mediator* mediator = nullptr;
    tr_blocklist_update_func on_done;
    bool cancelled = false;
};

namespace
{
// True if `text` has at least one line that could be parsed as a rule.
// This is to prevent an empty dl rules from replacing a good blocklist.
[[nodiscard]] bool has_rule_lines(std::string_view const text)
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

    auto const content = decompress(response.body);

    // don't use empty responses
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
    // If our fetch is still in flight, make its completion callback a no-op
    if (auto pending = latest_.lock()) {
        pending->cancelled = true;
    }
}

void Updater::update(tr_blocklist_update_func on_done)
{
    mediator_.run_in_session_thread([this, on_done = std::move(on_done)]() mutable {
        // Supersede any in-flight req so only the newest one updates the list.
        // The older fetch still runs, since tr_web doesn't have an abort API.
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

// --- tr_session's Updater::Mediator

tr_session::BlocklistMediator::BlocklistMediator(tr_session& session) noexcept
    : session_{ session }
{
}

void tr_session::BlocklistMediator::fetch(tr_web::FetchOptions&& options)
{
    session_.fetch(std::move(options));
}

void tr_session::BlocklistMediator::run_in_session_thread(std::function<void()> func)
{
    session_.run_in_session_thread(std::move(func));
}

tr::TimerMaker& tr_session::BlocklistMediator::timer_maker() noexcept
{
    return session_.timerMaker();
}

[[nodiscard]] time_t tr_session::BlocklistMediator::mtime() const
{
    return session_.blocklist_mtime();
}

std::string tr_session::BlocklistMediator::blocklist_url() const
{
    return tr::blocklist::normalize_blocklist_url(session_.blocklistUrl());
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

    // edge case: nullopt from the parser means "no valid rules".
    // Collapse it to 0 so the Updater reports InvalidData
    return n_rules.value_or(0U);
}

bool tr_session::BlocklistMediator::enabled() const noexcept
{
    return session_.blocklist_enabled();
}

bool tr_session::BlocklistMediator::updates_enabled() const noexcept
{
    return session_.blocklist_updates_enabled();
}

// --- tr_session

void tr_session::on_blocklist_settings_changed()
{
    if (auto* const updater = blocklist_updater()) {
        updater->restart_timer();
    }
}

// --- C API

void tr_blocklistUpdate(tr_session* session, tr_blocklist_update_func on_done)
{
    TR_ASSERT(session != nullptr);
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
