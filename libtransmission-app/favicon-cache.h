// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <array>
#include <chrono>
#include <cstddef> // size_t
#include <deque>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

#include <fmt/format.h>

#include <libtransmission/file-utils.h>
#include <libtransmission/file.h>
#include <libtransmission/web-utils.h>
#include <libtransmission/web.h>

namespace tr::app
{

template<typename Icon>
class FaviconCache
{
public:
    using IconFunc = std::function<void(Icon const*)>;

    FaviconCache()
        : cache_dir_{ app_cache_dir() }
        , icons_dir_{ fmt::format("{:s}/{:s}", cache_dir_, "favicons") }
        , scraped_sitenames_filename_{ fmt::format("{:s}/favicons-scraped.txt", cache_dir_) }
    {
    }

    [[nodiscard]] Icon const* find(std::string_view sitename) const noexcept
    {
        auto const iter = icons_.find(sitename);
        return iter != std::end(icons_) ? &iter->second : nullptr;
    }

    void load(std::string_view url_in, IconFunc callback)
    {
        std::call_once(scan_once_flag_, &FaviconCache::scan_file_cache, this);

        auto const url = tr_urlParse(url_in);
        if (!url) // invalid url?
        {
            callback({});
            return;
        }

        // If it's being fetched, wait for that answer.
        if (auto const iter = in_flight_.find(url->sitename); iter != std::end(in_flight_)) {
            iter->second.callbacks.emplace_back(std::move(callback));
            return;
        }

        // If we already have it, use it.
        if (auto const* const icon = find(url->sitename); icon != nullptr) {
            callback(icon);
            return;
        }

        // We don't already have it, so fetch it.
        // Add a placeholder to icons_ prevent repeat downloads.
        auto const sitename = std::string{ url->sitename };
        icons_.try_emplace(sitename);

        // ports to try
        auto n_ports = 0;
        auto ports = std::array<int, 2>{};
        ports[n_ports++] = 80;
        if (url->port != 80) {
            ports[n_ports++] = url->port;
        }

        auto& site = in_flight_[sitename];
        site.callbacks.emplace_back(std::move(callback));
        for (auto i = 0; i < n_ports; ++i) {
            for (auto const* const scheme : { "http", "https" }) {
                for (auto const* const suffix : { "ico", "png", "gif", "jpg" }) {
                    waiting_.emplace_back(
                        sitename,
                        fmt::format("{:s}://{:s}:{:d}/favicon.{:s}", scheme, url->host, ports[i], suffix));
                    ++site.n_unanswered;
                }
            }
        }

        // tr_web starts a thread, so create it on first use
        if (!web_) {
            web_ = tr_web::create(mediator_);
        }

        fetch_waiting();
    }

    static constexpr auto Width = 16;
    static constexpr auto Height = 16;

private:
    struct InFlight {
        std::vector<IconFunc> callbacks;
        size_t n_unanswered = {};
    };

    struct WaitingFetch {
        std::string sitename;
        std::string url;
    };

    [[nodiscard]] Icon create_from_file(std::string_view filename) const;
    [[nodiscard]] Icon create_from_data(void const* data, size_t datalen) const;
    [[nodiscard]] std::string app_cache_dir() const;
    void add_to_ui_thread(std::function<void()> idlefunc);

    void scan_file_cache()
    {
        // ensure the folders exist
        tr_sys_dir_create(cache_dir_, TR_SYS_DIR_CREATE_PARENTS, 0700);
        tr_sys_dir_create(icons_dir_, TR_SYS_DIR_CREATE_PARENTS, 0700);

        // remember which hosts we've asked for a favicon so that we
        // don't re-ask them every time we start a new session
        if (auto ifs = std::ifstream{ scraped_sitenames_filename_ }; ifs.is_open()) {
            auto sitename = std::string{};
            while (std::getline(ifs, sitename)) {
                icons_.try_emplace(sitename);
            }
        }

        // load the cached favicons
        for (auto const& sitename : tr_sys_dir_get_files(icons_dir_)) {
            auto const filename = fmt::format("{:s}/{:s}", icons_dir_, sitename);

            if (auto icon = create_from_file(filename); !icon) {
                tr_sys_path_remove(filename);
            } else {
                icons_[sitename] = std::move(icon);
            }
        }
    }

    void mark_site_as_scraped(std::string_view sitename) const
    {
        if (auto ofs = std::ofstream{ scraped_sitenames_filename_, std::ios_base::out | std::ios_base::app }; ofs.is_open()) {
            ofs << sitename << '\n';
        }
    }

    // returns nullptr if the response isn't an icon
    [[nodiscard]] Icon const* cache_icon(std::string const& sitename, std::string const& contents, long const code)
    {
        if (std::empty(contents) || code < 200 || code >= 300) {
            return nullptr;
        }

        auto icon = create_from_data(std::data(contents), std::size(contents));
        if (!icon) {
            return nullptr;
        }

        // cache it in memory
        auto& perm = icons_[sitename];
        perm = std::move(icon);

        // cache it on disk
        tr_file_save(fmt::format("{:s}/{:s}", icons_dir_, sitename), contents);

        return &perm;
    }

    // a member function so tests can specialize it
    [[nodiscard]] static constexpr std::chrono::seconds fetch_timeout() noexcept
    {
        return std::chrono::seconds{ 15 };
    }

    void fetch_waiting()
    {
        while (!std::empty(waiting_) && n_fetches_ < tr_web::MaxTotalConnections) {
            auto fetch = std::move(waiting_.front());
            waiting_.pop_front();
            if (!in_flight_.contains(fetch.sitename)) { // already answered
                continue;
            }

            ++n_fetches_;

            auto on_fetch_response =
                [this, alive = std::weak_ptr{ alive_ }, sitename = std::move(fetch.sitename)](auto const& response) {
                    add_to_ui_thread([this, alive, sitename, body = response.body, code = response.status] {
                        if (!alive.expired()) {
                            check_response(sitename, body, code);
                        }
                    });
                };
            web_->fetch({ std::move(fetch.url), std::move(on_fetch_response), nullptr, fetch_timeout() });
        }
    }

    void check_response(std::string const& sitename, std::string const& contents, long const code)
    {
        --n_fetches_;

        auto callbacks = std::vector<IconFunc>{};
        Icon const* icon = nullptr;
        if (auto const iter = in_flight_.find(sitename); iter != std::end(in_flight_)) { // not yet answered
            auto& site = iter->second;
            --site.n_unanswered;
            icon = cache_icon(sitename, contents, code);
            if (icon != nullptr || site.n_unanswered == 0U) {
                mark_site_as_scraped(sitename);
                callbacks = std::move(site.callbacks);
                in_flight_.erase(iter);
            }
        }

        fetch_waiting();

        for (auto const& callback : callbacks) {
            callback(icon);
        }
    }

    std::once_flag scan_once_flag_;
    std::string const cache_dir_;
    std::string const icons_dir_;
    std::string const scraped_sitenames_filename_;

    std::map<std::string /*sitename*/, Icon, std::less<>> icons_;

    std::map<std::string /*sitename*/, InFlight, std::less<>> in_flight_;
    // Fetches wait here for a free tr_web connection.
    // Waiting in curl's queue instead would spend their timeout.
    std::deque<WaitingFetch> waiting_;
    size_t n_fetches_ = 0; // in tr_web, including those of answered sites

    // expires with the cache, which UI tasks can outlive
    std::shared_ptr<bool> const alive_ = std::make_shared<bool>();

    tr_web::Mediator mediator_;

    // ~tr_web() joins its curl thread, so fetch callbacks must not own it
    std::unique_ptr<tr_web> web_;
};

} // namespace tr::app
