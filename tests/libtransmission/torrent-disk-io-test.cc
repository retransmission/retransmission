// This file Copyright (C) 2026 Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#include <algorithm>
#include <array>
#include <atomic>
#include <charconv>
#include <cstdint>
#include <cstring>
#include <functional>
#include <future>
#include <memory>
#include <string>
#include <vector>

#include <event2/util.h>

#ifndef _WIN32
#include <sys/stat.h> // chmod()
#include <unistd.h> // geteuid()
#endif

#include <gtest/gtest.h>

#include <libtransmission/transmission.h>

#include <libtransmission/error.h>
#include <libtransmission/local-data.h>
#include <libtransmission/peer-common.h>
#include <libtransmission/peer-mgr.h>
#include <libtransmission/peer-socket-tcp.h>
#include <libtransmission/quark.h>
#include <libtransmission/torrent.h>
#include <libtransmission/variant.h>
#include <libtransmission/webseed.h>

#include "loopback-server.h"
#include "test-fixtures.h"

namespace tr::test
{
namespace
{
auto constexpr MaxWaitMsec = 5000;

class RequestBudgetPeer final : public tr_peer
{
public:
    using tr_peer::tr_peer;

    [[nodiscard]] Speed get_piece_speed(uint64_t, tr_direction) const override
    {
        return {};
    }

    [[nodiscard]] std::string display_name() const override
    {
        return {};
    }

    [[nodiscard]] tr_bitfield const& has() const noexcept override
    {
        return active_requests();
    }

    [[nodiscard]] size_t active_req_count(tr_direction const direction) const noexcept override
    {
        return direction == tr_direction::ClientToPeer ? active_requests().count() : 0U;
    }

    void request_blocks(tr_block_span_t const* const spans, size_t const n_spans) override
    {
        for (auto index = size_t{}; index < n_spans; ++index) {
            set_active_requests(spans[index]);
        }
    }

    void ban() override
    {
        clear_active_requests();
    }
};

// Helpers for the torrent call sites that read and write through tr::LocalData.
class TorrentDiskIoTestBase : public SessionTest
{
protected:
    [[nodiscard]] static std::unique_ptr<tr::LocalData::BlockData> zeroBlock(tr_torrent const* tor, tr_block_index_t block)
    {
        auto data = std::make_unique<tr::LocalData::BlockData>();
        data->resize(tor->block_size(block));
        std::ranges::fill(*data, uint8_t{ 0U });
        return data;
    }

    // Runs `func` on the session thread and waits for it to finish.
    void inSessionThread(std::function<void()> const& func)
    {
        auto done = std::atomic<bool>{ false };
        session_->run_in_session_thread([&func, &done]() {
            func();
            done = true;
        });
        ASSERT_TRUE(waitFor([&done]() { return done.load(); }, MaxWaitMsec));
    }
};

// Covers the call sites on the synchronous backend. The fixture parks
// completions instead of shuffling them, so each test decides when
// they arrive.
class TorrentDiskIoTest : public TorrentDiskIoTestBase
{
protected:
    // These tests say exactly when each completion arrives, so they
    // stay on the synchronous backend even when TR_LOCAL_DATA_WORKERS
    // asks the fixture for the threaded one.
    [[nodiscard]] bool useLocalDataWorkersFromEnv() const override
    {
        return false;
    }

    void SetUp() override
    {
        TorrentDiskIoTestBase::SetUp();
        session_->local_data.set_completions(tr::LocalData::Completions::Deferred);
    }
};

// Covers the same call sites on the threaded backend, where writes and
// piece hashes finish on worker threads.
class TorrentDiskIoWorkersTest : public TorrentDiskIoTestBase
{
protected:
    [[nodiscard]] bool useLocalDataWorkersFromEnv() const override
    {
        return false;
    }

    void SetUp() override
    {
        settings().insert_or_assign(TR_KEY_disk_io_workers, int64_t{ 2 });
        TorrentDiskIoTestBase::SetUp();
    }
};

class RequestBudgetTest : public TorrentDiskIoWorkersTest
{
protected:
    static auto constexpr BudgetBlocks = size_t{ 64U };
    static auto constexpr TorrentSize = uint64_t{ 4U * 1024U * 1024U };

    struct Request {
        uint32_t piece;
        uint32_t offset;
        uint32_t length;
    };

    struct ConnectedPeer {
        evutil_socket_t remote = EVUTIL_INVALID_SOCKET;
        std::vector<uint8_t> input;
        std::vector<Request> requests;
        bool handshaken = false;
        bool interested = false;

        ~ConnectedPeer()
        {
            if (remote != EVUTIL_INVALID_SOCKET) {
                evutil_closesocket(remote);
            }
        }
    };

    void SetUp() override
    {
        settings().insert_or_assign(TR_KEY_disk_write_budget_mib, int64_t{ 1 });
        TorrentDiskIoWorkersTest::SetUp();
    }

    void TearDown() override
    {
        session_->local_data.set_workers_paused(false);
        inSessionThread([this]() { webseed_.reset(); });
        peers_.clear();
        TorrentDiskIoWorkersTest::TearDown();
    }

    [[nodiscard]] ConnectedPeer& addPeer(tr_torrent& tor)
    {
        auto peer = std::make_unique<ConnectedPeer>();
        auto sockets = std::array<evutil_socket_t, 2>{ EVUTIL_INVALID_SOCKET, EVUTIL_INVALID_SOCKET };
        EXPECT_EQ(0, evutil_socketpair(TR_IF_WIN32(AF_INET, AF_UNIX), SOCK_STREAM, 0, sockets.data()));
        EXPECT_EQ(0, evutil_make_socket_nonblocking(sockets[0]));
        EXPECT_EQ(0, evutil_make_socket_nonblocking(sockets[1]));
        peer->remote = sockets[1];
        auto const address = *tr_address::from_string("127.0.0." + std::to_string(peers_.size() + 2U));
        auto const socket_address = tr_socket_address{ address, tr_port::from_host(8080) };
        inSessionThread([&]() {
            tr_peerMgrAddIncoming(
                peerManager(),
                tr_peer_socket_tcp::create(*session_, socket_address, static_cast<tr_socket_t>(sockets[0])));
        });
        auto const protocol = std::string_view{ "\023BitTorrent protocol" };
        auto handshake = std::vector<uint8_t>{ protocol.begin(), protocol.end() };
        handshake.resize(28U, 0U);
        handshake.back() = 4U;
        auto const& hash = tor.info_hash();
        auto const* hash_bytes = reinterpret_cast<uint8_t const*>(hash.data());
        handshake.insert(handshake.end(), hash_bytes, hash_bytes + hash.size());
        handshake.resize(68U, static_cast<uint8_t>(peers_.size() + 1U));
        receive(*peer, handshake);
        receive(*peer, { 0U, 0U, 0U, 1U, 14U, 0U, 0U, 0U, 1U, 1U });
        peers_.push_back(std::move(peer));
        return *peers_.back();
    }

    [[nodiscard]] tr_torrent* makeBudgetTorrent(std::string_view const name, std::string_view const webseed = {})
    {
        auto const piece_hash = tr_sha1::digest(std::string(TrBlockSize, '\0'));
        auto hashes = std::string{};
        for (auto block = size_t{}; block < TorrentSize / TrBlockSize; ++block) {
            hashes.append(reinterpret_cast<char const*>(piece_hash.data()), piece_hash.size());
        }
        auto info = tr_variant::Map{};
        info.try_emplace(TR_KEY_name, name);
        info.try_emplace(TR_KEY_length, TorrentSize);
        info.try_emplace(TR_KEY_piece_length, TrBlockSize);
        info.try_emplace(TR_KEY_pieces, std::move(hashes));
        auto metainfo = tr_variant::Map{};
        metainfo.try_emplace(TR_KEY_info, std::move(info));
        if (!webseed.empty()) {
            metainfo.try_emplace(TR_KEY_url_list, webseed);
        }
        auto builder = tr_torrent_builder{ session_ };
        EXPECT_TRUE(builder.set_metainfo(tr_variant_serde::benc().to_string(std::move(metainfo))));
        builder.set_paused(true);
        builder.set_sequential_download(true);
        return createTorrentAndWaitForVerifyDone(&builder);
    }

    static void receive(ConnectedPeer const& peer, std::vector<uint8_t> const& message)
    {
        auto const sent = send(peer.remote, reinterpret_cast<char const*>(message.data()), message.size(), 0);
        ASSERT_EQ(message.size(), static_cast<size_t>(sent));
    }

    [[nodiscard]] static uint32_t readUint32(uint8_t const* const bytes) noexcept
    {
        auto value = uint32_t{};
        std::memcpy(&value, bytes, sizeof(value));
        return ntohl(value);
    }

    static void collectMessages(ConnectedPeer& peer)
    {
        auto buffer = std::array<uint8_t, 4096>{};
        for (;;) {
            auto const count = recv(peer.remote, reinterpret_cast<char*>(buffer.data()), buffer.size(), 0);
            if (count <= 0) {
                break;
            }
            peer.input.insert(peer.input.end(), buffer.begin(), buffer.begin() + count);
        }
        if (!peer.handshaken) {
            if (peer.input.size() < 68U) {
                return;
            }
            EXPECT_EQ(19U, peer.input.front());
            peer.input.erase(peer.input.begin(), peer.input.begin() + 68U);
            peer.handshaken = true;
        }
        while (peer.input.size() >= 4U) {
            auto const length = readUint32(peer.input.data());
            if (peer.input.size() < 4U + length) {
                break;
            }
            if (length == 13U && peer.input[4] == 6U) {
                peer.requests.push_back(
                    { readUint32(peer.input.data() + 5U),
                      readUint32(peer.input.data() + 9U),
                      readUint32(peer.input.data() + 13U) });
            } else if (length == 1U && peer.input[4] == 2U) {
                peer.interested = true;
            }
            peer.input.erase(peer.input.begin(), peer.input.begin() + 4U + length);
        }
    }

    [[nodiscard]] size_t requestCount()
    {
        auto count = size_t{};
        for (auto const& peer : peers_) {
            collectMessages(*peer);
            count += peer->requests.size();
        }
        return count;
    }

    [[nodiscard]] size_t spareBlocks()
    {
        auto spare = size_t{};
        inSessionThread([this, &spare]() { spare = session_->spare_request_blocks().value(); });
        return spare;
    }

    static void appendUint32(std::vector<uint8_t>& message, uint32_t const value)
    {
        for (auto const shift : { 24U, 16U, 8U, 0U }) {
            message.push_back(static_cast<uint8_t>(value >> shift));
        }
    }

    static void receiveBlock(ConnectedPeer const& peer, Request const& request)
    {
        auto message = std::vector<uint8_t>{};
        appendUint32(message, 9U + request.length);
        message.push_back(7U);
        appendUint32(message, request.piece);
        appendUint32(message, request.offset);
        message.resize(message.size() + request.length, 0U);
        receive(peer, message);
    }

    std::vector<std::unique_ptr<ConnectedPeer>> peers_;
    std::unique_ptr<tr_webseed> webseed_;
};

class WebseedRequestBudgetTest
    : public RequestBudgetTest
    , public ::testing::WithParamInterface<bool>
{
};

class TorrentRemovalTest
    : public TorrentDiskIoWorkersTest
    , public ::testing::WithParamInterface<bool>
{
};

} // namespace

TEST_F(TorrentDiskIoTest, blockIsNotOursUntilItsWriteFinishes)
{
    auto* const tor = zeroTorrentInit(ZeroTorrentState::Partial);
    auto const block = tor->block_span_for_piece(0).begin;

    inSessionThread([this, tor, block]() {
        ASSERT_TRUE(tor->on_block_received(block));
        tor->save_block(block, zeroBlock(tor, block));

        // the write hasn't finished, so the block isn't ours yet
        EXPECT_FALSE(tor->has_block(block));
        EXPECT_TRUE(tor->has_block_or_pending(block));

        // and a second copy of it is refused while that write is in flight
        EXPECT_FALSE(tor->on_block_received(block));

        session_->local_data.pump();
        EXPECT_TRUE(tor->has_block(block));

        // now that we have it, another copy is still refused
        EXPECT_FALSE(tor->on_block_received(block));
    });
}

TEST_F(TorrentDiskIoTest, failedWriteStopsTorrent)
{
    auto* const tor = zeroTorrentInit(ZeroTorrentState::Partial);
    auto const block = tor->block_span_for_piece(0).begin;

    inSessionThread([tor, block]() {
        ASSERT_TRUE(tor->on_block_received(block));
        EXPECT_FALSE(tor->error().is_local_error());

        auto error = tr_error{};
        error.set_from_errno(ENOSPC);
        tor->on_block_written(block, error);

        EXPECT_TRUE(tor->error().is_local_error());
        EXPECT_FALSE(tor->is_running());

        // the block was not counted, and is no longer pending
        EXPECT_FALSE(tor->has_block(block));
        EXPECT_FALSE(tor->has_block_or_pending(block));
    });
}

TEST_F(TorrentDiskIoTest, writtenPieceIsNotAdvertisedUntilItsHashCompletes)
{
    auto* const tor = zeroTorrentInit(ZeroTorrentState::Partial);
    auto const span = tor->block_span_for_piece(0U);
    inSessionThread([this, tor, span]() {
        ASSERT_TRUE(tor->is_piece_checked(0U));
        for (auto block = span.begin; block < span.end; ++block) {
            tor->save_block(block, zeroBlock(tor, block));
        }

        session_->local_data.pump();
        EXPECT_TRUE(tor->has_blocks(span));
        EXPECT_FALSE(tor->has_piece(0U));
        EXPECT_FALSE(tor->has_all());
        EXPECT_EQ(std::byte{}, tor->create_piece_bitfield().front() & std::byte{ 0x80 });

        session_->local_data.pump();
        EXPECT_TRUE(tor->has_piece(0U));
        EXPECT_TRUE(tor->has_all());
        EXPECT_TRUE(tor->is_piece_checked(0U));
        EXPECT_EQ(std::byte{ 0x80 }, tor->create_piece_bitfield().front() & std::byte{ 0x80 });
    });
}

TEST_F(TorrentDiskIoTest, failedHashNeverMakesThePieceAvailable)
{
    auto* const tor = zeroTorrentInit(ZeroTorrentState::Partial);
    auto const span = tor->block_span_for_piece(0U);
    inSessionThread([this, tor, span]() {
        for (auto block = span.begin; block < span.end; ++block) {
            auto data = zeroBlock(tor, block);
            std::ranges::fill(*data, uint8_t{ 1U });
            tor->save_block(block, std::move(data));
        }
        session_->local_data.pump();
        EXPECT_TRUE(tor->has_blocks(span));
        EXPECT_FALSE(tor->has_piece(0U));
        EXPECT_EQ(std::byte{}, tor->create_piece_bitfield().front() & std::byte{ 0x80 });

        session_->local_data.pump();
        EXPECT_FALSE(tor->has_blocks(span));
        EXPECT_FALSE(tor->has_piece(0U));
        EXPECT_EQ(std::byte{}, tor->create_piece_bitfield().front() & std::byte{ 0x80 });
    });
}

TEST_F(TorrentDiskIoTest, hashResultForInvalidatedPieceIsDropped)
{
    auto* const tor = zeroTorrentInit(ZeroTorrentState::Partial);
    auto const span = tor->block_span_for_piece(0);

    inSessionThread([this, tor, span]() {
        for (auto block = span.begin; block < span.end; ++block) {
            ASSERT_TRUE(tor->on_block_received(block));
            tor->save_block(block, zeroBlock(tor, block));
        }

        // deliver the writes, which leaves the piece's hash in flight
        session_->local_data.pump();
        EXPECT_TRUE(tor->has_blocks(span));
        EXPECT_FALSE(tor->has_piece(0));

        // Invalidate the piece while its hash is still in flight.
        // The hash is now about a version of the piece that no longer
        // exists, so delivering it must not mark the piece complete again.
        tor->set_has_piece(0, false);
        session_->local_data.pump();
        EXPECT_FALSE(tor->has_piece(0));
    });
}

TEST_F(TorrentDiskIoTest, requestBudgetIsUnboundedOnTheSynchronousBackend)
{
    EXPECT_FALSE(session_->spare_request_blocks().has_value());
}

// ---

TEST_F(RequestBudgetTest, peersResumeOnlyAfterReceivedDataIsWritten)
{
    auto* const tor = zeroTorrentInit(ZeroTorrentState::NoFiles);
    tr_torrentStart(tor);
    ASSERT_TRUE(waitFor(
        [tor]() {
            auto const lock = tor->unique_lock();
            return tor->is_running();
        },
        MaxWaitMsec));
    session_->local_data.set_workers_paused(true);
    auto& first = addPeer(*tor);
    ASSERT_TRUE(waitFor([this]() { return requestCount() >= 32U; }, 15000));
    EXPECT_EQ(32U, requestCount());
    EXPECT_EQ(32U, spareBlocks());
    auto& second = addPeer(*tor);
    ASSERT_TRUE(waitFor([this]() { return requestCount() >= BudgetBlocks; }, 15000));
    EXPECT_EQ(32U, second.requests.size());
    auto& waiting = addPeer(*tor);
    ASSERT_TRUE(waitFor(
        [&waiting]() {
            collectMessages(waiting);
            return waiting.interested;
        },
        15000));
    EXPECT_EQ(BudgetBlocks, requestCount());
    EXPECT_TRUE(waiting.requests.empty());
    EXPECT_EQ(0U, spareBlocks());

    auto const request = std::ranges::find_if(first.requests, [](Request const& item) { return item.length == TrBlockSize; });
    ASSERT_NE(first.requests.end(), request);
    receiveBlock(first, *request);
    ASSERT_TRUE(waitFor([this]() { return session_->local_data.enqueued_write_bytes() == TrBlockSize; }, MaxWaitMsec));
    EXPECT_EQ(0U, spareBlocks());
    EXPECT_EQ(BudgetBlocks, requestCount());

    session_->local_data.set_workers_paused(false);
    EXPECT_TRUE(waitFor([this]() { return session_->local_data.enqueued_write_bytes() == 0U; }, MaxWaitMsec));
    EXPECT_TRUE(waitFor([this]() { return requestCount() > BudgetBlocks; }, MaxWaitMsec));
    EXPECT_EQ(BudgetBlocks + 1U, requestCount());
    EXPECT_EQ(0U, spareBlocks());
    peers_.clear();
    EXPECT_TRUE(waitFor([this]() { return spareBlocks() == BudgetBlocks; }, MaxWaitMsec));
}

TEST_F(RequestBudgetTest, duplicateWebseedResponseReleasesItsReservation)
{
    auto release_response = std::promise<void>{};
    auto const response_allowed = release_response.get_future().share();
    auto requested = std::promise<void>{};
    auto server = LoopbackServer{};
    server.setHandler([&](evhttp_request* const request) {
        requested.set_value();
        EXPECT_EQ(std::future_status::ready, response_allowed.wait_for(5s));
        LoopbackServer::reply(request, 206, "Partial Content", std::string(TrBlockSize, '\0'));
    });
    auto* const tor = makeBudgetTorrent("duplicate.bin");
    tr_torrentStart(tor);
    ASSERT_TRUE(waitFor(
        [tor]() {
            auto const lock = tor->unique_lock();
            return tor->is_running();
        },
        MaxWaitMsec));

    session_->local_data.set_workers_paused(true);
    inSessionThread([&]() {
        webseed_ = tr_webseed::create(*tor, server.url(), nullptr, nullptr);
        auto const span = tr_block_span_t{ .begin = 0U, .end = 1U };
        webseed_->request_blocks(&span, 1U);
        EXPECT_EQ(BudgetBlocks - 1U, session_->spare_request_blocks());
        tor->save_block(0U, zeroBlock(tor, 0U));
        EXPECT_EQ(BudgetBlocks - 2U, session_->spare_request_blocks());
        tr_torrentStop(tor);
    });
    EXPECT_EQ(std::future_status::ready, requested.get_future().wait_for(5s));
    release_response.set_value();

    EXPECT_TRUE(waitFor([this]() { return spareBlocks() == BudgetBlocks - 1U; }, MaxWaitMsec));
    inSessionThread([this]() {
        EXPECT_EQ(0U, webseed_->active_requests().count());
        EXPECT_EQ(TrBlockSize, session_->local_data.enqueued_write_bytes());
    });
    session_->local_data.set_workers_paused(false);
    EXPECT_TRUE(waitFor([this]() { return spareBlocks() == BudgetBlocks; }, MaxWaitMsec));
}

TEST_P(WebseedRequestBudgetTest, rangeRequestsResumeAfterTheDiskQueueDrains)
{
    auto const peer_blocks = GetParam() ? size_t{ 32U } : size_t{};
    auto const available_bytes = (BudgetBlocks - peer_blocks) * TrBlockSize;
    if (GetParam()) {
        auto* const peer_torrent = makeBudgetTorrent("peer.bin");
        tr_torrentStart(peer_torrent);
        ASSERT_TRUE(waitFor(
            [peer_torrent]() {
                auto const lock = peer_torrent->unique_lock();
                return peer_torrent->is_running();
            },
            MaxWaitMsec));
        auto& peer = addPeer(*peer_torrent);
        ASSERT_TRUE(waitFor(
            [&peer, peer_blocks]() {
                collectMessages(peer);
                return peer.requests.size() >= peer_blocks;
            },
            15000));
        EXPECT_EQ(peer_blocks, requestCount());
    }

    auto requests = std::atomic<size_t>{};
    auto received_bytes = std::atomic<uint64_t>{};
    auto server = LoopbackServer{};
    server.setHandler([&](evhttp_request* const request) {
        auto const* header = evhttp_find_header(evhttp_request_get_input_headers(request), "Range");
        if (header == nullptr) {
            ADD_FAILURE() << "missing HTTP range";
            LoopbackServer::reply(request, 400, "Bad Request", {});
            return;
        }
        auto const range = std::string_view{ header };
        auto const dash = range.find('-');
        auto begin = uint64_t{};
        auto end = uint64_t{};
        if (!range.starts_with("bytes=") || dash == std::string_view::npos ||
            std::from_chars(range.data() + 6U, range.data() + dash, begin).ec != std::errc{} ||
            std::from_chars(range.data() + dash + 1U, range.data() + range.size(), end).ec != std::errc{} || begin > end ||
            end >= TorrentSize) {
            ADD_FAILURE() << "invalid HTTP range: " << range;
            LoopbackServer::reply(request, 400, "Bad Request", {});
            return;
        }
        auto const length = end - begin + 1U;
        EXPECT_LE(length, available_bytes);
        ++requests;
        received_bytes += length;
        auto const content_range = fmt::format("bytes {}-{}/{}", begin, end, TorrentSize);
        evhttp_add_header(evhttp_request_get_output_headers(request), "Content-Range", content_range.c_str());
        LoopbackServer::reply(request, 206, "Partial Content", std::string(static_cast<size_t>(length), '\0'));
    });

    auto* const tor = makeBudgetTorrent("webseed.bin", server.url());
    session_->local_data.set_workers_paused(true);
    tr_torrentStart(tor);
    ASSERT_TRUE(
        waitFor([this, available_bytes]() { return session_->local_data.enqueued_write_bytes() == available_bytes; }, 15000));
    EXPECT_EQ(0U, spareBlocks());
    EXPECT_EQ(available_bytes, received_bytes.load());
    auto const requests_when_full = requests.load();
    EXPECT_FALSE(waitFor([&requests, requests_when_full]() { return requests.load() != requests_when_full; }, 2500));

    session_->local_data.set_workers_paused(false);
    ASSERT_TRUE(waitFor(
        [tor]() {
            auto const lock = tor->unique_lock();
            return tor->is_done();
        },
        30000));
    EXPECT_EQ(TorrentSize, received_bytes.load());
    EXPECT_GT(requests.load(), requests_when_full);
    EXPECT_EQ(BudgetBlocks - peer_blocks, spareBlocks());
    EXPECT_EQ(0U, session_->local_data.enqueued_write_bytes());
    if (GetParam()) {
        EXPECT_EQ(peer_blocks, requestCount());
    }
}

INSTANTIATE_TEST_SUITE_P(SharedBudget, WebseedRequestBudgetTest, ::testing::Bool());

TEST_P(TorrentRemovalTest, waitsForWritesAndHashesBeforeUnregistering)
{
    auto* const tor = zeroTorrentInit(ZeroTorrentState::Partial);
    auto const id = tor->id();
    auto const filename = tr_torrentFindFile(tor, 0U);
    auto hash_completed = false;

    session_->local_data.set_workers_paused(true);
    inSessionThread([this, tor, id, &hash_completed]() {
        tor->save_block(0U, zeroBlock(tor, 0U));
        session_->local_data.test_piece(
            id,
            1U,
            [&hash_completed](tr_torrent_id_t, tr_piece_index_t, tr_error const& error, auto hash) {
                EXPECT_FALSE(error);
                EXPECT_TRUE(hash);
                hash_completed = true;
            });
        tr_torrentRemove(tor, GetParam(), {});
        EXPECT_EQ(tor, session_->torrents().get(id));
        EXPECT_FALSE(tor->is_running());
        EXPECT_FALSE(tor->on_block_received(1U));
    });
    session_->local_data.set_workers_paused(false);

    EXPECT_TRUE(waitFor(
        [this, id]() {
            auto removed = false;
            inSessionThread([this, id, &removed]() { removed = session_->torrents().get(id) == nullptr; });
            return removed;
        },
        MaxWaitMsec));
    inSessionThread([this, &hash_completed]() {
        EXPECT_TRUE(hash_completed);
        EXPECT_EQ(0U, session_->local_data.enqueued_write_bytes());
    });
    EXPECT_EQ(!GetParam(), tr_sys_path_exists(filename));
}

INSTANTIATE_TEST_SUITE_P(KeepOrDeleteData, TorrentRemovalTest, ::testing::Bool());

TEST_F(TorrentDiskIoWorkersTest, requestBudgetTracksChangesAndPeerDestruction)
{
    auto* const tor = zeroTorrentInit(ZeroTorrentState::Partial);
    inSessionThread([this, tor]() {
        auto const budget = session_->spare_request_blocks();
        ASSERT_TRUE(budget);
        {
            auto first = RequestBudgetPeer{ *tor };
            auto second = RequestBudgetPeer{ *tor };
            first.set_active_requests({ .begin = 0U, .end = 3U });
            first.set_active_requests({ .begin = 1U, .end = 4U });
            second.set_active_requests({ .begin = 2U, .end = 4U });
            EXPECT_EQ(*budget - 6U, session_->spare_request_blocks());

            first.unset_active_request(1U);
            first.unset_active_request(1U);
            EXPECT_EQ(*budget - 5U, session_->spare_request_blocks());
            first.set_active_requests({ .begin = 2U, .end = 4U }, false);
            EXPECT_EQ(*budget - 3U, session_->spare_request_blocks());

            second.ban();
            second.clear_active_requests();
            EXPECT_EQ(*budget - 1U, session_->spare_request_blocks());
        }
        EXPECT_EQ(budget, session_->spare_request_blocks());
    });
}

TEST_F(TorrentDiskIoWorkersTest, queuedWritesConsumeTheRequestBudget)
{
    auto* const tor = zeroTorrentInit(ZeroTorrentState::Partial);
    auto const block = tor->block_span_for_piece(0).begin;

    auto budget = std::optional<size_t>{};
    inSessionThread([this, &budget]() { budget = session_->spare_request_blocks(); });
    ASSERT_TRUE(budget.has_value());
    EXPECT_GT(*budget, 0U);

    // a queued write takes its block out of the budget until it lands
    session_->local_data.set_workers_paused(true);
    inSessionThread([this, tor, block, &budget]() {
        ASSERT_TRUE(tor->on_block_received(block));
        tor->save_block(block, zeroBlock(tor, block));
        EXPECT_EQ(*budget - 1U, session_->spare_request_blocks());
    });

    session_->local_data.set_workers_paused(false);
    EXPECT_TRUE(waitFor([tor, block]() { return tor->has_block(block); }, MaxWaitMsec));
    inSessionThread([this, &budget]() { EXPECT_EQ(budget, session_->spare_request_blocks()); });
}

TEST_F(TorrentDiskIoWorkersTest, writesBehindBarriersConsumeTheRequestBudget)
{
    auto* const tor = zeroTorrentInit(ZeroTorrentState::Partial);
    auto const block = tor->block_span_for_piece(0).begin;

    session_->local_data.set_workers_paused(true);
    inSessionThread([this, tor, block]() {
        auto const budget = session_->spare_request_blocks();
        ASSERT_TRUE(budget.has_value());
        tor->save_block(block, zeroBlock(tor, block));
        session_->local_data.close_torrent(tor->id());
        tor->save_block(block + 1U, zeroBlock(tor, block + 1U));
        EXPECT_EQ(*budget - 2U, session_->spare_request_blocks());
    });

    session_->local_data.set_workers_paused(false);
    EXPECT_TRUE(waitFor([tor, block]() { return tor->has_block(block) && tor->has_block(block + 1U); }, MaxWaitMsec));
    EXPECT_EQ(0U, session_->local_data.enqueued_write_bytes());
}

TEST_F(TorrentDiskIoWorkersTest, blockCountsOnlyAfterItsWriteFinishes)
{
    auto* const tor = zeroTorrentInit(ZeroTorrentState::Partial);
    auto const block = tor->block_span_for_piece(0).begin;

    inSessionThread([tor, block]() {
        ASSERT_TRUE(tor->on_block_received(block));
        tor->save_block(block, zeroBlock(tor, block));

        // the write is on a worker, so the block is pending but not ours
        EXPECT_TRUE(tor->has_block_or_pending(block));
        EXPECT_FALSE(tor->on_block_received(block));
    });

    EXPECT_TRUE(waitFor([tor, block]() { return tor->has_block(block); }, MaxWaitMsec));
    EXPECT_EQ(0U, session_->local_data.enqueued_write_bytes());
}

TEST_F(TorrentDiskIoWorkersTest, queuedMovesUseTheLatestSourceDirectory)
{
    auto* const tor = zeroTorrentInit(ZeroTorrentState::Partial);
    auto const block = tor->block_span_for_piece(0).begin;
    auto const first_dir = tr_pathbuf{ sandboxDir(), "/first" };
    auto const last_dir = tr_pathbuf{ sandboxDir(), "/last" };
    auto first_state = -1;
    auto last_state = -1;
    auto closed = std::atomic<bool>{ false };

    session_->local_data.set_workers_paused(true);
    inSessionThread([this, tor, block, &first_dir, &last_dir, &first_state, &last_state, &closed]() {
        tor->save_block(block, zeroBlock(tor, block));
        tr_torrentSetLocation(tor, first_dir, true, &first_state);
        tr_torrentSetLocation(tor, last_dir, true, &last_state);
        EXPECT_EQ(TR_LOC_MOVING, first_state);
        EXPECT_EQ(TR_LOC_MOVING, last_state);
        session_->local_data.close_torrent(tor->id(), [&closed](tr_torrent_id_t) { closed = true; });
    });

    session_->local_data.set_workers_paused(false);
    EXPECT_TRUE(waitFor([&closed]() { return closed.load(); }, MaxWaitMsec));
    inSessionThread([tor, &last_dir, &first_state, &last_state]() {
        EXPECT_EQ(TR_LOC_DONE, first_state);
        EXPECT_EQ(TR_LOC_DONE, last_state);
        EXPECT_EQ(last_dir.sv(), tor->download_dir().sv());
        for (auto file = tr_file_index_t{}; file < tor->file_count(); ++file) {
            auto const found = tor->find_file(file);
            ASSERT_TRUE(found);
            EXPECT_EQ(last_dir.sv(), found->base);
        }
    });
}

TEST_F(TorrentDiskIoWorkersTest, verificationWaitsForPendingWrites)
{
    auto* const tor = zeroTorrentInit(ZeroTorrentState::Partial);
    auto const span = tor->block_span_for_piece(0);
    auto verified = std::atomic<bool>{ false };
    auto const tag = session_->verify_done_.connect_scoped([&verified](tr_torrent_id_t) { verified = true; });

    session_->local_data.set_workers_paused(true);
    inSessionThread([tor, span]() {
        for (auto block = span.begin; block < span.end; ++block) {
            tor->save_block(block, zeroBlock(tor, block));
        }
        tr_torrentVerify(tor);
        EXPECT_EQ(TR_STATUS_CHECK_WAIT, tor->activity());
    });
    EXPECT_FALSE(verified);

    session_->local_data.set_workers_paused(false);
    EXPECT_TRUE(waitFor([&verified]() { return verified.load(); }, MaxWaitMsec));
    inSessionThread([this, tor]() {
        EXPECT_TRUE(tor->has_piece(0));
        EXPECT_EQ(0U, session_->local_data.stats().hashes_from_buffers);
        EXPECT_EQ(0U, session_->local_data.stats().hashes_from_disk);
    });
}

TEST_F(TorrentDiskIoWorkersTest, stoppingCancelsVerificationWaitingForWrites)
{
    auto* const tor = zeroTorrentInit(ZeroTorrentState::Partial);
    auto const block = tor->block_span_for_piece(0).begin;
    auto closed = std::atomic<bool>{ false };
    auto verified = std::atomic<bool>{ false };
    auto const tag = session_->verify_done_.connect_scoped([&verified](tr_torrent_id_t) { verified = true; });

    session_->local_data.set_workers_paused(true);
    inSessionThread([this, tor, block, &closed]() {
        tor->save_block(block, zeroBlock(tor, block));
        tr_torrentVerify(tor);
        tr_torrentStop(tor);
        EXPECT_EQ(TR_STATUS_STOPPED, tor->activity());
        session_->local_data.close_torrent(tor->id(), [&closed](tr_torrent_id_t) { closed = true; });
    });

    session_->local_data.set_workers_paused(false);
    EXPECT_TRUE(waitFor([&closed]() { return closed.load(); }, MaxWaitMsec));
    inSessionThread([tor, &verified]() {
        EXPECT_EQ(TR_STATUS_STOPPED, tor->activity());
        EXPECT_FALSE(verified);
    });
}

TEST_F(TorrentDiskIoWorkersTest, cancelledVerificationRestoresDeferredPieceHashes)
{
    auto* const tor = zeroTorrentInit(ZeroTorrentState::Partial);
    auto const block = tor->block_span_for_piece(0).begin;
    auto hashed = std::atomic<bool>{ false };
    auto const tag = tor->piece_completed_.connect_scoped([&hashed](tr_torrent*, tr_piece_index_t const piece) {
        if (piece == 1U) {
            hashed = true;
        }
    });

    session_->local_data.set_workers_paused(true);
    inSessionThread([tor, block]() {
        tor->save_block(block, zeroBlock(tor, block));
        tr_torrentVerify(tor);
        tor->on_block_written(tor->block_span_for_piece(1U).begin, {});
        tr_torrentStop(tor);
    });

    session_->local_data.set_workers_paused(false);
    EXPECT_TRUE(waitFor([&hashed]() { return hashed.load(); }, MaxWaitMsec));
}

TEST_F(TorrentDiskIoWorkersTest, completedPieceIsHashedFromBufferedBlocks)
{
    auto* const tor = zeroTorrentInit(ZeroTorrentState::Partial);
    auto const span = tor->block_span_for_piece(0);

    // The hash passing is what completes the piece.
    auto n_completed = std::atomic<size_t>{};
    auto const tag = tor->piece_completed_.connect_scoped([&n_completed](tr_torrent*, tr_piece_index_t) { ++n_completed; });

    inSessionThread([tor, span]() {
        for (auto block = span.begin; block < span.end; ++block) {
            ASSERT_TRUE(tor->on_block_received(block));
            tor->save_block(block, zeroBlock(tor, block));
        }
    });

    EXPECT_TRUE(waitFor([&n_completed]() { return n_completed > 0U; }, MaxWaitMsec));

    // and the hash came from the blocks still in memory
    auto const stats = session_->local_data.stats();
    EXPECT_EQ(1U, stats.hashes_from_buffers);
    EXPECT_EQ(0U, stats.hashes_from_disk);
}

#ifndef _WIN32
TEST_F(TorrentDiskIoWorkersTest, failedWriteSetsLocalError)
{
    if (geteuid() == 0) {
        GTEST_SKIP() << "root ignores file permissions";
    }

    auto* const tor = zeroTorrentInit(ZeroTorrentState::Partial);
    auto const block = tor->block_span_for_piece(0).begin;

    // the block's file can't be opened for writing
    auto const filename = tr_torrentFindFile(tor, 0);
    ASSERT_FALSE(std::empty(filename));
    ASSERT_EQ(0, chmod(filename.c_str(), 0444));

    inSessionThread([tor, block]() {
        ASSERT_TRUE(tor->on_block_received(block));
        tor->save_block(block, zeroBlock(tor, block));
    });

    EXPECT_TRUE(waitFor([tor]() { return tor->error().is_local_error(); }, MaxWaitMsec));

    // the block was not counted, and is no longer pending
    EXPECT_FALSE(tor->has_block(block));
    EXPECT_FALSE(tor->has_block_or_pending(block));

    chmod(filename.c_str(), 0644);
}
#endif

} // namespace tr::test
