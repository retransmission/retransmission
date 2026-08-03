// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#include <atomic>
#include <compare>
#include <cstddef> // size_t
#include <mutex>
#include <new> // placement new, ::operator new()
#include <string_view>
#include <unordered_map>
#include <utility> // std::exchange()

#include "libtransmission/shared-string.h"

namespace tr::detail
{

/**
 * One pooled string, and the references to it that keep it alive.
 *
 * The characters sit right after this header in the same heap block —
 * create() over-allocates to make room — so a pooled string costs one
 * allocation, not a node plus a separate string buffer, and readers
 * dereference a single heap region.
 */
struct string_pool_node {
    [[nodiscard]] static string_pool_node* create(std::string_view const key)
    {
        // The plain ::operator new() / ::operator delete() pair used here and
        // in destroy() is only correct up to this alignment; anything stricter
        // needs the std::align_val_t overloads.
        static_assert(alignof(string_pool_node) <= __STDCPP_DEFAULT_NEW_ALIGNMENT__);

        return new (::operator new(sizeof(string_pool_node) + std::size(key) + 1U)) string_pool_node{ key };
    }

    static void destroy(string_pool_node* const node) noexcept
    {
        node->~string_pool_node();
        ::operator delete(node);
    }

    [[nodiscard]] std::string_view sv() const noexcept
    {
        return { c_str(), size_ };
    }

    [[nodiscard]] char const* c_str() const noexcept
    {
        return reinterpret_cast<char const*>(this + 1);
    }

    std::atomic<size_t> refcount = 1U;

private:
    explicit string_pool_node(std::string_view const key)
        : size_{ std::size(key) }
    {
        auto* const chars = reinterpret_cast<char*>(this + 1);
        key.copy(chars, size_);
        chars[size_] = '\0'; // so c_str() can return the buffer as-is
    }

    // Private so a stray `delete`, which would not know about the
    // trailing characters, cannot compile; destroy() is the only
    // teardown path.
    ~string_pool_node() = default;

    size_t size_;
};

} // namespace tr::detail

namespace
{

using Node = tr::detail::string_pool_node;

/**
 * The strings shared by every tr::shared_string in the process.
 *
 * The pool's own reference to a string is weak: the string is destroyed
 * and its entry erased once the last shared_string naming it is gone.
 * That is the difference from a tr_quark, which lives as long as the
 * process does.
 *
 * Copying and destroying a shared_string is lock-free; interning a
 * string, and dropping the last reference to one, take the pool's lock.
 */
struct string_pool {
    std::mutex mutex;

    // Each key views its own node's text, so interning text that is
    // already pooled needs no temporary std::string to look it up.
    std::unordered_map<std::string_view, Node*> nodes;

    [[nodiscard]] static string_pool& instance()
    {
        // Leaked on purpose: a shared_string destroyed during static
        // destruction must not reach a pool that has already been torn down.
        static auto* const pool = new string_pool{};
        return *pool;
    }
};

// Interns `key` and returns its node, with one reference held.
[[nodiscard]] Node* acquire_node(std::string_view const key)
{
    auto& pool = string_pool::instance();
    auto const lock = std::scoped_lock{ pool.mutex };

    if (auto const iter = pool.nodes.find(key); iter != std::end(pool.nodes)) {
        // Take a reference only while the count is nonzero. A node whose
        // count hit zero is owned by the remove_ref() call that zeroed it,
        // which will delete it without relocking, so it must never be revived.
        auto* const node = iter->second;
        for (auto count = node->refcount.load(std::memory_order_relaxed); count != 0U;) {
            if (node->refcount.compare_exchange_weak(count, count + 1U, std::memory_order_relaxed)) {
                return node;
            }
        }

        // Unlink the dying node so a fresh one can take the key.
        // Its owning remove_ref() call still deletes it.
        pool.nodes.erase(iter);
    }

    auto* const node = Node::create(key);
    pool.nodes.emplace(node->sv(), node);
    return node;
}

void add_ref(Node* const node) noexcept
{
    if (node != nullptr) {
        node->refcount.fetch_add(1U, std::memory_order_relaxed);
    }
}

// Taking the lock below can throw if the mutex is broken, but this runs
// from ~shared_string(), which must not propagate; terminating is the
// correct behavior here.
// NOLINTNEXTLINE(bugprone-exception-escape)
void remove_ref(Node* const node) noexcept
{
    if (node == nullptr || node->refcount.fetch_sub(1U, std::memory_order_acq_rel) != 1U) {
        return;
    }

    // The count hit zero, and acquire_node() never revives a zero-count
    // node, so this call owns the node. acquire_node() may have already
    // unlinked it and pooled an equal replacement, though, so erase only
    // the entry that still points at this node.
    auto& pool = string_pool::instance();
    {
        auto const lock = std::scoped_lock{ pool.mutex };
        if (auto const iter = pool.nodes.find(node->sv()); iter != std::end(pool.nodes) && iter->second == node) {
            pool.nodes.erase(iter);
        }
    }

    Node::destroy(node);
}

} // namespace

namespace tr
{

shared_string::shared_string(std::string_view const sv)
{
    if (!std::empty(sv)) {
        node_ = acquire_node(sv);
    }
}

shared_string::shared_string(char const* const c_str)
    : shared_string{ std::string_view{ c_str != nullptr ? c_str : "" } }
{
}

shared_string::shared_string(shared_string const& that) noexcept
    : node_{ that.node_ }
{
    add_ref(node_);
}

shared_string::shared_string(shared_string&& that) noexcept
    : node_{ std::exchange(that.node_, nullptr) }
{
}

// Self-assignment is covered: a shared_string always has the same node as
// itself, so the comparison below skips the whole body.
// NOLINTNEXTLINE(bugprone-unhandled-self-assignment,cert-oop54-cpp)
shared_string& shared_string::operator=(shared_string const& that) noexcept
{
    if (node_ != that.node_) {
        add_ref(that.node_);
        remove_ref(node_);
        node_ = that.node_;
    }

    return *this;
}

shared_string& shared_string::operator=(shared_string&& that) noexcept
{
    if (this != &that) {
        remove_ref(node_);
        node_ = std::exchange(that.node_, nullptr);
    }

    return *this;
}

shared_string::~shared_string()
{
    remove_ref(node_);
}

shared_string& shared_string::operator=(std::string_view const sv)
{
    return *this = shared_string{ sv };
}

shared_string& shared_string::operator=(char const* const c_str)
{
    return *this = std::string_view{ c_str != nullptr ? c_str : "" };
}

void shared_string::clear() noexcept
{
    remove_ref(node_);
    node_ = nullptr;
}

std::string_view shared_string::sv() const noexcept
{
    return node_ != nullptr ? node_->sv() : std::string_view{};
}

char const* shared_string::c_str() const noexcept
{
    return node_ != nullptr ? node_->c_str() : "";
}

bool shared_string::empty() const noexcept
{
    return node_ == nullptr;
}

bool shared_string::operator==(shared_string const& that) const noexcept
{
    return node_ == that.node_;
}

std::strong_ordering shared_string::operator<=>(shared_string const& that) const noexcept
{
    return sv() <=> that.sv();
}

bool shared_string::operator==(std::string_view const that) const noexcept
{
    return sv() == that;
}

size_t shared_string::pool_size()
{
    auto& pool = string_pool::instance();
    auto const lock = std::scoped_lock{ pool.mutex };
    return std::size(pool.nodes);
}

} // namespace tr
