// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <compare>
#include <cstddef> // size_t
#include <functional> // std::hash
#include <string_view>

namespace tr
{

namespace detail
{
struct string_pool_node;
} // namespace detail

/**
 * A reference to a pooled string.
 * Equal strings share a single pooled string,
 * which is freed after the last shared_string sharing it goes away.
 */
class shared_string
{
public:
    shared_string() noexcept = default;
    explicit shared_string(std::string_view sv);
    explicit shared_string(char const* c_str);
    shared_string(shared_string const& that) noexcept;
    shared_string(shared_string&& that) noexcept;
    shared_string& operator=(shared_string const& that) noexcept;
    shared_string& operator=(shared_string&& that) noexcept;
    shared_string& operator=(char const* c_str);
    shared_string& operator=(std::string_view sv);
    ~shared_string();

    void clear() noexcept;

    [[nodiscard]] bool empty() const noexcept;
    [[nodiscard]] char const* c_str() const noexcept;
    [[nodiscard]] std::string_view sv() const noexcept;

    [[nodiscard]] bool operator==(std::string_view that) const noexcept;
    [[nodiscard]] bool operator==(shared_string const& that) const noexcept;
    [[nodiscard]] std::strong_ordering operator<=>(shared_string const& that) const noexcept;

    // How many distinct strings are currently pooled.
    [[nodiscard]] static size_t pool_size();

private:
    detail::string_pool_node* node_ = nullptr;
};

} // namespace tr

template<>
struct std::hash<tr::shared_string> {
    // Hashes the text, matching std::hash<std::string_view>, so a
    // shared_string and the equal std::string_view agree on their bucket.
    [[nodiscard]] std::size_t operator()(tr::shared_string const& str) const noexcept
    {
        return std::hash<std::string_view>{}(str.sv());
    }
};
