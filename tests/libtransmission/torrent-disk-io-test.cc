// This file Copyright (C) 2026 Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <charconv>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <future>
#include <iterator>
#include <memory>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <tuple>
#include <vector>

#include <event2/util.h>

#include <gtest/gtest.h>

#include <libtransmission/transmission.h>

#include <libtransmission/bitfield.h>
#include <libtransmission/crypto-utils.h>
#include <libtransmission/error.h>
#include <libtransmission/file.h>
#include <libtransmission/local-data.h>
#include <libtransmission/peer-common.h>
#include <libtransmission/peer-io.h>
#include <libtransmission/peer-mgr.h>
#include <libtransmission/peer-msgs.h>
#include <libtransmission/peer-socket-tcp.h>
#include <libtransmission/tr-strbuf.h>
#include <libtransmission/quark.h>
#include <libtransmission/rpcimpl.h>
#include <libtransmission/torrent-builder.h>
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

// On Windows, EVUTIL_INVALID_SOCKET is an unsigned SOCKET while
// evutil_socket_t is signed, so spell the conversion out once.
auto constexpr InvalidSocket = TR_IF_WIN32(
    static_cast<evutil_socket_t>(EVUTIL_INVALID_SOCKET),
    evutil_socket_t{ EVUTIL_INVALID_SOCKET });

[[nodiscard]] uint32_t readUint32(uint8_t const* const bytes) noexcept
{
    auto value = uint32_t{};
    std::memcpy(&value, bytes, sizeof(value));
    return ntohl(value);
}

void appendUint32(std::vector<uint8_t>& message, uint32_t const value)
{
    for (auto const shift : { 24U, 16U, 8U, 0U }) {
        message.push_back(static_cast<uint8_t>(value >> shift));
    }
}

// Sends `message` from the remote end of a peer connection to the session.
// The remote socket is nonblocking and its send buffer may be smaller
// than a block message (8 KiB on macOS), so send in as many passes
// as it takes. The session thread drains the other end meanwhile.
void sendFromRemote(evutil_socket_t const remote, std::vector<uint8_t> const& message)
{
    auto const deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds{ MaxWaitMsec };
    auto remaining = std::span<uint8_t const>{ message };
    while (!std::empty(remaining)) {
        auto const sent = send(remote, reinterpret_cast<char const*>(std::data(remaining)), std::size(remaining), 0);
        if (sent > 0) {
            remaining = remaining.subspan(static_cast<size_t>(sent));
            continue;
        }

        auto const err = EVUTIL_SOCKET_ERROR();
        ASSERT_TRUE(sent < 0 && (err == TR_IF_WIN32(WSAEWOULDBLOCK, EAGAIN) || err == TR_IF_WIN32(WSAEWOULDBLOCK, EWOULDBLOCK)))
            << err;
        ASSERT_TRUE(std::chrono::steady_clock::now() < deadline) << "the peer never drained its socket";
        std::this_thread::sleep_for(std::chrono::milliseconds{ 1 });
    }
}

// Lets `sock` hold a burst of block messages. A macOS socketpair holds
// 8 KiB, and a test that drains it every 10 ms then moves under 1 MiB/s.
// Tries smaller sizes because NetBSD refuses any above kern.sbmax.
void growSocketBuffers(evutil_socket_t const sock)
{
    for (auto const option : { SO_SNDBUF, SO_RCVBUF }) {
        for (auto size = 1024 * 1024; size >= 16 * 1024; size /= 2) {
            if (setsockopt(sock, SOL_SOCKET, option, reinterpret_cast<char const*>(&size), sizeof(size)) == 0) {
                break;
            }
        }
    }
}

// Appends to `input` what the session has sent to the remote end of a
// peer connection so far.
void receiveAtRemote(evutil_socket_t const remote, std::vector<uint8_t>& input)
{
    auto buffer = std::array<uint8_t, 4096>{};
    for (;;) {
        // Analyzer false positive: it carries std::future::wait_for's internal lock past its scope.
        // NOLINTNEXTLINE(clang-analyzer-unix.BlockInCriticalSection)
        auto const count = recv(remote, reinterpret_cast<char*>(std::data(buffer)), std::size(buffer), 0);
        if (count <= 0) {
            break;
        }
        input.insert(std::end(input), std::begin(buffer), std::begin(buffer) + count);
    }
}

// Takes each complete peer message off the front of `input`, and passes
// its length and body to `on_message`.
template<typename OnMessage>
void takeMessages(std::vector<uint8_t>& input, OnMessage const& on_message)
{
    while (std::size(input) >= 4U) {
        auto const length = readUint32(std::data(input));
        if (std::size(input) < 4U + length) {
            break;
        }
        on_message(length, std::data(input) + 4U);
        input.erase(std::begin(input), std::begin(input) + 4U + length);
    }
}

class RequestBudgetPeer final : public tr_peer
{
public:
    using tr_peer::tr_peer;

    [[nodiscard]] Speed get_piece_speed(uint64_t /*now*/, tr_direction /*direction*/) const override
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

    // Two 24 KiB pieces of zeroes. The middle block ends piece 0 and
    // starts piece 1, so writing it last completes both.
    [[nodiscard]] tr_torrent* straddlingTorrentInit()
    {
        static auto constexpr PieceSize = size_t{ 24576U };
        auto const piece_hash = tr_sha1::digest(std::string(PieceSize, '\0'));
        auto pieces = std::string{};
        std::ranges::transform(piece_hash, std::back_inserter(pieces), [](std::byte const b) { return static_cast<char>(b); });
        pieces += pieces;

        auto const benc = "d4:infod6:lengthi" + std::to_string(2U * PieceSize) + "e4:name8:straddle12:piece lengthi" +
            std::to_string(PieceSize) + "e6:pieces" + std::to_string(std::size(pieces)) + ':' + pieces + "ee";
        auto builder = tr_torrent_builder{ session_ };
        EXPECT_TRUE(builder.set_metainfo(benc));
        builder.set_paused(true);
        return createTorrentAndWaitForVerifyDone(&builder);
    }

    // Writes the straddling torrent's blocks, the middle one last.
    void writeStraddlingBlocks(tr_torrent* const tor, bool const bad_piece_1)
    {
        for (auto const block : { tr_block_index_t{ 0U }, tr_block_index_t{ 2U }, tr_block_index_t{ 1U } }) {
            auto data = zeroBlock(tor, block);
            if (bad_piece_1 && block == 2U) {
                std::ranges::fill(*data, uint8_t{ 1U });
            }
            tor->save_block(block, std::move(data));
            session_->local_data.pump();
        }
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
    static auto constexpr TorrentSize = uint64_t{ 4U } * 1024U * 1024U;

    struct Request {
        uint32_t piece;
        uint32_t offset;
        uint32_t length;
    };

    struct ConnectedPeer {
        ConnectedPeer() = default;
        ConnectedPeer(ConnectedPeer const&) = delete;
        ConnectedPeer(ConnectedPeer&&) = delete;
        ConnectedPeer& operator=(ConnectedPeer const&) = delete;
        ConnectedPeer& operator=(ConnectedPeer&&) = delete;

        ~ConnectedPeer()
        {
            if (remote != InvalidSocket) {
                evutil_closesocket(remote);
            }
        }

        evutil_socket_t remote = InvalidSocket;
        std::vector<uint8_t> input;
        std::vector<Request> requests;
        bool handshaken = false;
        bool interested = false;
    };

    void SetUp() override
    {
        settings().insert_or_assign(TR_KEY_disk_write_budget_mib, int64_t{ 1 });
        TorrentDiskIoWorkersTest::SetUp();
    }

    void TearDown() override
    {
        session_->local_data.set_workers_paused(false);
        blockingRunInSessionThread([this] { webseed_.reset(); });
        peers_.clear();
        TorrentDiskIoWorkersTest::TearDown();
    }

    [[nodiscard]] ConnectedPeer& addPeer(tr_torrent& tor)
    {
        auto peer = std::make_unique<ConnectedPeer>();
        auto sockets = std::array<evutil_socket_t, 2>{ InvalidSocket, InvalidSocket };
        EXPECT_EQ(0, evutil_socketpair(TR_IF_WIN32(AF_INET, AF_UNIX), SOCK_STREAM, 0, sockets.data()));
        EXPECT_EQ(0, evutil_make_socket_nonblocking(sockets[0]));
        EXPECT_EQ(0, evutil_make_socket_nonblocking(sockets[1]));
        peer->remote = sockets[1];
        auto const address = *tr_address::from_string("127.0.0." + std::to_string(peers_.size() + 2U));
        auto const socket_address = tr_socket_address{ address, tr_port::from_host(8080) };
        blockingRunInSessionThread([&] {
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
        sendFromRemote(peer.remote, message);
    }

    static void collectMessages(ConnectedPeer& peer)
    {
        receiveAtRemote(peer.remote, peer.input);
        if (!peer.handshaken) {
            if (peer.input.size() < 68U) {
                return;
            }
            EXPECT_EQ(19U, peer.input.front());
            peer.input.erase(peer.input.begin(), peer.input.begin() + 68U);
            peer.handshaken = true;
        }
        takeMessages(peer.input, [&peer](uint32_t const length, uint8_t const* const body) {
            if (length == 13U && body[0] == 6U) {
                peer.requests.push_back(
                    Request{ .piece = readUint32(body + 1U),
                             .offset = readUint32(body + 5U),
                             .length = readUint32(body + 9U) });
            } else if (length == 1U && body[0] == 2U) {
                peer.interested = true;
            }
        });
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
        blockingRunInSessionThread([this, &spare] { spare = session_->spare_request_blocks().value(); });
        return spare;
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
protected:
    // Runs an RPC method and returns its result.
    [[nodiscard]] tr_variant::Map rpc(tr_quark const method, tr_variant::Map params)
    {
        auto request = tr_variant::Map{ 4U };
        request.try_emplace(TR_KEY_id, 1);
        request.try_emplace(TR_KEY_jsonrpc, JsonRpc::Version);
        request.try_emplace(TR_KEY_method, tr_variant::unmanaged_string(method));
        request.try_emplace(TR_KEY_params, std::move(params));

        auto response = tr_variant{};
        tr_rpc_request_exec(session_, std::move(request), [&response](tr_variant&& resp) { response = std::move(resp); });
        if (auto* const map = response.get_if<tr_variant::Map>(); map != nullptr) {
            if (auto* const result = map->find_if<tr_variant::Map>(TR_KEY_result); result != nullptr) {
                return std::move(*result);
            }
        }
        return {};
    }
};

// Completes torrents whose files start in the incomplete dir.
class IncompleteDirWorkersTest : public TorrentDiskIoWorkersTest
{
protected:
    void SetUp() override
    {
        settings().insert_or_assign(TR_KEY_incomplete_dir_enabled, true);
        TorrentDiskIoWorkersTest::SetUp();
    }

    // Completes `tor` from this thread by skipping the file that holds
    // its missing piece. A paused write holds back the disk barriers
    // until then. `before` runs on the session thread first. Returns the
    // torrent's current dir as the completeness callback saw it.
    [[nodiscard]] std::string completeBehindAWrite(tr_torrent* const tor, std::function<void()> const& before = {})
    {
        auto dir = std::string{};
        auto called = std::atomic<bool>{ false };
        tr_sessionSetCompletenessCallback(session_, [tor, &dir, &called](tr_torrent_id_t, tr_completeness, bool) {
            dir = tor->current_dir().sv();
            called = true;
        });

        session_->local_data.set_workers_paused(true);
        blockingRunInSessionThread([tor, &before] {
            auto const block = tor->block_span_for_piece(0U).begin;
            tor->save_block(block, zeroBlock(tor, block));
            if (before) {
                before();
            }
        });
        auto const file = tr_file_index_t{ 0U };
        tr_torrentSetFileDLs(tor, std::span{ &file, 1U }, false);

        // the callback waits for the move out, which waits for the write
        blockingRunInSessionThread([&called] { EXPECT_FALSE(called); });
        session_->local_data.set_workers_paused(false);
        EXPECT_TRUE(waitFor([&called] { return called.load(); }, MaxWaitMsec));
        tr_sessionSetCompletenessCallback(session_, nullptr);
        return dir;
    }

    void expectFilesIn(tr_torrent* const tor, std::string_view const dir)
    {
        blockingRunInSessionThread([tor, dir] {
            EXPECT_EQ(dir, tor->current_dir().sv());
            EXPECT_TRUE(std::empty(tor->incomplete_dir()));
            for (auto file = tr_file_index_t{ 1U }; file < tor->file_count(); ++file) {
                auto const found = tor->find_file(file);
                ASSERT_TRUE(found);
                EXPECT_EQ(dir, found->base);
            }
        });
    }
};

class WriteBudgetTest
    : public TorrentDiskIoWorkersTest
    , public ::testing::WithParamInterface<int64_t>
{
protected:
    void SetUp() override
    {
        settings().insert_or_assign(TR_KEY_disk_write_budget_mib, GetParam());
        TorrentDiskIoWorkersTest::SetUp();
    }
};

} // namespace

// A peer downloading from a seed. Unlike the fixtures above, this one
// follows TR_LOCAL_DATA_WORKERS and TR_LOCAL_DATA_SHUFFLE, so CI serves
// its requests on every backend. Outside the anonymous namespace so that
// tr_torrent::ResumeHelper can befriend it.
class UploadTest : public TorrentDiskIoTestBase
{
protected:
    static auto constexpr PieceSize = uint32_t{ 2U * TrBlockSize };
    static auto constexpr PieceCount = uint32_t{ 4U };
    static auto constexpr TotalSize = uint64_t{ PieceSize } * PieceCount;
    static auto constexpr MsgRequest = uint8_t{ 6U };
    static auto constexpr MsgPiece = uint8_t{ 7U };
    static auto constexpr MsgCancel = uint8_t{ 8U };
    static auto constexpr MsgReject = uint8_t{ 16U };

    struct Reply {
        uint8_t id = 0U;
        uint32_t piece = 0U;
        uint32_t offset = 0U;
        std::string data;
    };

    void TearDown() override
    {
        blockingRunInSessionThread([this] { msgs_.reset(); });
        if (remote_ != InvalidSocket) {
            evutil_closesocket(remote_);
        }
        TorrentDiskIoTestBase::TearDown();
    }

    // Bytes that differ from block to block, so that a misplaced block shows.
    [[nodiscard]] static std::string content(uint64_t const begin, uint64_t const len)
    {
        auto str = std::string{};
        str.reserve(len);
        for (auto byte = begin; byte < begin + len; ++byte) {
            str.push_back(static_cast<char>(((byte * 31U) + 7U) ^ (byte / TrBlockSize)));
        }
        return str;
    }

    [[nodiscard]] std::string seedPath() const
    {
        return tr_pathbuf{ tr_sessionGetDownloadDir(session_), "/upload.bin" }.c_str();
    }

    // A seed of `piece_count` pieces of content() in one file.
    [[nodiscard]] tr_torrent* makeSeed(uint32_t const piece_count = PieceCount)
    {
        auto const total_size = uint64_t{ PieceSize } * piece_count;
        auto const data = content(0U, total_size);
        auto hashes = std::string{};
        for (auto piece = uint32_t{}; piece < piece_count; ++piece) {
            auto const hash = tr_sha1::digest(std::string_view{ data }.substr(size_t{ piece } * PieceSize, PieceSize));
            hashes.append(reinterpret_cast<char const*>(std::data(hash)), std::size(hash));
        }

        createFileWithContents(seedPath(), data);

        auto info = tr_variant::Map{};
        info.try_emplace(TR_KEY_name, "upload.bin");
        info.try_emplace(TR_KEY_length, total_size);
        info.try_emplace(TR_KEY_piece_length, PieceSize);
        info.try_emplace(TR_KEY_pieces, std::move(hashes));
        auto metainfo = tr_variant::Map{};
        metainfo.try_emplace(TR_KEY_info, std::move(info));

        auto builder = tr_torrent_builder{ session_ };
        EXPECT_TRUE(builder.set_metainfo(tr_variant_serde::benc().to_string(std::move(metainfo))));
        builder.set_paused(true);
        auto* const tor = createTorrentAndWaitForVerifyDone(&builder);
        EXPECT_TRUE(tor != nullptr && tor->is_done());
        return tor;
    }

    // Connects a peer that has finished its handshake, and unchokes it.
    void connectPeer(tr_torrent* const tor)
    {
        auto sockets = std::array<evutil_socket_t, 2>{ InvalidSocket, InvalidSocket };
        ASSERT_EQ(0, evutil_socketpair(TR_IF_WIN32(AF_INET, AF_UNIX), SOCK_STREAM, 0, sockets.data()));
        ASSERT_EQ(0, evutil_make_socket_nonblocking(sockets[0]));
        ASSERT_EQ(0, evutil_make_socket_nonblocking(sockets[1]));
        for (auto const sock : sockets) {
            growSocketBuffers(sock);
        }
        remote_ = sockets[1];

        blockingRunInSessionThread([this, tor, local = sockets[0]] {
            auto const address = *tr_address::from_string("127.0.0.2");
            auto io = tr_peerIo::new_incoming(
                session_,
                &session_->top_bandwidth_,
                tr_peer_socket_tcp::create(
                    *session_,
                    tr_socket_address{ address, tr_port::from_host(8080) },
                    static_cast<tr_socket_t>(local)));
            io->set_torrent_hash(tor->info_hash());
            io->set_supports_fext(true);
            auto info = std::make_shared<tr_peer_info>(address, uint8_t{}, TR_PEER_FROM_INCOMING, [] { return tr_port{}; });
            msgs_ = tr_peerMsgs::create(
                *tor,
                std::move(info),
                std::move(io),
                tr_peer_id_t{},
                [](tr_peerMsgs*, tr_peer_event const&, void*) {},
                nullptr);
            msgs_->set_choke(false);
        });
    }

    // Marks every piece as needing a check before its data is sent, as
    // when its file has changed since it was last checked.
    void forgetCheckedPieces(tr_torrent* const tor)
    {
        blockingRunInSessionThread([tor] {
            auto const mtimes = std::vector<time_t>(tor->file_count());
            tr_torrent::ResumeHelper{ *tor }.load_checked_pieces(tr_bitfield{ tor->piece_count() }, std::data(mtimes));
        });
    }

    // Has the peer send its one request with `send_request`, and runs
    // `during` on the session thread while the read for that request is
    // in flight, whatever the backend.
    void runDuringARead(tr_torrent* const tor, std::function<void()> const& send_request, std::function<void()> const& during)
    {
        auto const threaded = session_->settings().disk_io_workers != 0U;

        blockingRunInSessionThread([this, tor, threaded] {
            auto& local_data = session_->local_data;
            if (threaded) {
                // a closed file keeps the page cache from answering,
                // and paused workers keep the disk from answering
                session_->openFiles().close_torrent(tor->id());
                local_data.set_workers_paused(true);
            } else {
                // the read's completion parks, and nothing pumps it
                local_data.set_wake({});
                local_data.set_completions(tr::LocalData::Completions::Deferred);
            }
        });

        // the read starts as soon as the request arrives
        send_request();
        auto const is_reading = [this] {
            auto n_requests = size_t{};
            blockingRunInSessionThread(
                [this, &n_requests] { n_requests = msgs_->active_req_count(tr_direction::PeerToClient); });
            return n_requests == 1U;
        };
        ASSERT_TRUE(waitFor(is_reading, MaxWaitMsec));

        blockingRunInSessionThread([this, threaded, &during] {
            during();

            auto& local_data = session_->local_data;
            if (threaded) {
                local_data.set_workers_paused(false);
            } else {
                local_data.set_wake([this] { session_->queue_session_thread([this] { session_->local_data.pump(); }); });
                local_data.pump();
            }
        });
    }

    // Takes back the piece's first block, as a neighbor's failed hash
    // takes back a block that the two pieces share.
    static void loseFirstBlock(tr_torrent* const tor, tr_piece_index_t const piece)
    {
        auto helper = tr_torrent::ResumeHelper{ *tor };
        auto blocks = helper.blocks();
        blocks.unset(tor->block_span_for_piece(piece).begin);
        helper.load_blocks(std::move(blocks));
    }

    void request(uint32_t const piece, uint32_t const offset) const
    {
        sendBlockMessage(MsgRequest, piece, offset);
    }

    void cancel(uint32_t const piece, uint32_t const offset) const
    {
        sendBlockMessage(MsgCancel, piece, offset);
    }

    void sendBlockMessage(uint8_t const id, uint32_t const piece, uint32_t const offset) const
    {
        auto message = std::vector<uint8_t>{};
        appendUint32(message, 13U);
        message.push_back(id);
        appendUint32(message, piece);
        appendUint32(message, offset);
        appendUint32(message, TrBlockSize);
        sendFromRemote(remote_, message);
    }

    // Reads what the session has sent, keeping its pieces and rejects.
    // Returns how many of those have arrived so far.
    size_t collectReplies()
    {
        receiveAtRemote(remote_, input_);
        takeMessages(input_, [this](uint32_t const length, uint8_t const* const body) {
            if (length >= 13U && (body[0] == MsgPiece || body[0] == MsgReject)) {
                auto data = std::string{};
                if (body[0] == MsgPiece) {
                    data.assign(reinterpret_cast<char const*>(body + 9U), length - 9U);
                }
                replies_.emplace_back(
                    Reply{ .id = body[0],
                           .piece = readUint32(body + 1U),
                           .offset = readUint32(body + 5U),
                           .data = std::move(data) });
            }
        });

        return std::size(replies_);
    }

    // The peer manager pulses its peers to refill their output buffers.
    // This peer isn't in a swarm, so pulse it here.
    [[nodiscard]] bool waitForReplies(size_t const n)
    {
        return waitFor(
            [this, n] {
                blockingRunInSessionThread([this] { msgs_->pulse(); });
                return collectReplies() >= n;
            },
            MaxWaitMsec);
    }

    static void expectPiece(Reply const& reply)
    {
        EXPECT_EQ(MsgPiece, reply.id);
        EXPECT_EQ(content((uint64_t{ reply.piece } * PieceSize) + reply.offset, TrBlockSize), reply.data)
            << reply.piece << ':' << reply.offset;
    }

    std::vector<Reply> replies_;

private:
    std::shared_ptr<tr_peerMsgs> msgs_;
    evutil_socket_t remote_ = InvalidSocket;
    std::vector<uint8_t> input_;
};

TEST_P(WriteBudgetTest, followsTheSettingAtStartupAndRuntime)
{
    static auto constexpr MiB = uint64_t{ 1024U } * 1024U;
    auto const check = [this](int64_t const budget_mib) {
        // A budget of zero would admit no requests, so it counts as 1 MiB.
        auto const bytes = static_cast<uint64_t>(std::max(int64_t{ 1 }, budget_mib)) * MiB;
        EXPECT_EQ(bytes, session_->effective_write_budget_bytes());
        EXPECT_EQ(bytes / TrBlockSize, session_->spare_request_blocks().value());
        EXPECT_EQ(std::min(bytes / 2U, 32U * MiB), session_->local_data.retained_bytes());
    };

    blockingRunInSessionThread([&] { check(GetParam()); });

    for (auto const budget_mib : { int64_t{ 0 }, int64_t{ 1 }, int64_t{ 64 }, int64_t{ 128 } }) {
        SCOPED_TRACE(budget_mib);
        auto config = tr_sessionGetSettings(session_);
        config.insert_or_assign(TR_KEY_disk_write_budget_mib, budget_mib);
        tr_sessionSet(session_, config);
        blockingRunInSessionThread([&] { check(budget_mib); });
    }
}

INSTANTIATE_TEST_SUITE_P(WriteBudget, WriteBudgetTest, ::testing::Values(int64_t{ 0 }, int64_t{ 1 }, int64_t{ 64 }));

TEST_F(TorrentDiskIoTest, blockIsNotOursUntilItsWriteFinishes)
{
    auto* const tor = zeroTorrentInit(ZeroTorrentState::Partial);
    auto const block = tor->block_span_for_piece(0).begin;

    blockingRunInSessionThread([this, tor, block] {
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

    blockingRunInSessionThread([tor, block] {
        ASSERT_TRUE(tor->on_block_received(block));
        EXPECT_FALSE(tor->error().is_local_error());

        tor->on_block_written(block, tr_error::from_errno(ENOSPC));

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
    blockingRunInSessionThread([this, tor, span] {
        ASSERT_TRUE(tor->is_piece_checked(0U));
        for (auto block = span.begin; block < span.end; ++block) {
            tor->save_block(block, zeroBlock(tor, block));
        }

        // every block is written, but the last one waits for the hash
        session_->local_data.pump();
        EXPECT_EQ(1U, tor->count_missing_blocks_in_piece(0U));
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
    blockingRunInSessionThread([this, tor, span] {
        for (auto block = span.begin; block < span.end; ++block) {
            auto data = zeroBlock(tor, block);
            std::ranges::fill(*data, uint8_t{ 1U });
            tor->save_block(block, std::move(data));
        }
        session_->local_data.pump();
        EXPECT_EQ(1U, tor->count_missing_blocks_in_piece(0U));
        EXPECT_FALSE(tor->has_piece(0U));
        EXPECT_EQ(std::byte{}, tor->create_piece_bitfield().front() & std::byte{ 0x80 });

        // the piece is downloaded again, block by block
        session_->local_data.pump();
        for (auto block = span.begin; block < span.end; ++block) {
            EXPECT_FALSE(tor->has_block_or_pending(block));
        }
        EXPECT_FALSE(tor->has_piece(0U));
        EXPECT_EQ(std::byte{}, tor->create_piece_bitfield().front() & std::byte{ 0x80 });
    });
}

TEST_F(TorrentDiskIoTest, hashResultForInvalidatedPieceIsDropped)
{
    auto* const tor = zeroTorrentInit(ZeroTorrentState::Partial);
    auto const span = tor->block_span_for_piece(0);
    auto n_completed = size_t{};
    auto const tag = tor->piece_completed_.connect_scoped([&n_completed](tr_torrent*, tr_piece_index_t) { ++n_completed; });

    blockingRunInSessionThread([this, tor, span, &n_completed] {
        for (auto block = span.begin; block < span.end; ++block) {
            ASSERT_TRUE(tor->on_block_received(block));
            tor->save_block(block, zeroBlock(tor, block));
        }

        // deliver the writes, which leaves the piece's hash in flight
        session_->local_data.pump();
        EXPECT_EQ(1U, tor->count_missing_blocks_in_piece(0));
        EXPECT_FALSE(tor->has_piece(0));

        // Invalidate the piece while its hash is still in flight.
        // The hash is now about a version of the piece that no longer
        // exists, so delivering it must not mark the piece complete again.
        tor->set_has_piece(0, false);
        session_->local_data.pump();
        EXPECT_FALSE(tor->has_piece(0));
        EXPECT_EQ(0U, n_completed);
    });
}

TEST_F(TorrentDiskIoTest, lazyCheckOfALostPieceIsDropped)
{
    auto* const tor = zeroTorrentInit(ZeroTorrentState::Complete);
    ASSERT_NE(nullptr, tor);

    blockingRunInSessionThread([this, tor] {
        // a read from a piece whose files changed since its last check...
        tor->checked_pieces_.set(0, false);
        auto read_error = std::optional<tr_error>{};
        auto const length = std::min<uint32_t>(TrBlockSize, tor->piece_size(0));
        tor->read_piece_data(0, 0, length, [&read_error](auto, auto, tr_error const& error, auto) { read_error = error; });
        EXPECT_FALSE(read_error.has_value());

        // ...loses the piece while its hash is in flight, as when a neighbor's
        // failed hash takes back a block they share. The hash may have read
        // that block mid-rewrite, so neither its result nor the data counts.
        tor->set_has_piece(0, false);
        session_->local_data.pump();
        session_->local_data.pump();
        ASSERT_TRUE(read_error.has_value());
        EXPECT_TRUE(*read_error);
        EXPECT_FALSE(tor->is_piece_checked(0));
        EXPECT_TRUE(tor->error().empty()) << tor->error().errmsg();
    });
}

TEST_F(TorrentDiskIoTest, blockSharedByTwoPiecesCountsOnceBothPass)
{
    auto* const tor = straddlingTorrentInit();
    ASSERT_NE(nullptr, tor);
    auto n_completed = size_t{};
    auto const tag = tor->piece_completed_.connect_scoped([&n_completed](tr_torrent*, tr_piece_index_t) { ++n_completed; });

    blockingRunInSessionThread([this, tor, &n_completed] {
        // the middle block's write leaves both hashes in flight
        writeStraddlingBlocks(tor, false);
        EXPECT_FALSE(tor->has_block(1U));
        EXPECT_TRUE(tor->has_block_or_pending(1U));
        EXPECT_EQ(0U, n_completed);

        // whichever hash lands first
        session_->local_data.pump();
        EXPECT_EQ(2U, n_completed);
        EXPECT_TRUE(tor->has_all());
    });
}

TEST_F(TorrentDiskIoTest, blockSharedWithAFailedPieceIsDownloadedAgain)
{
    auto* const tor = straddlingTorrentInit();
    ASSERT_NE(nullptr, tor);
    auto n_completed = size_t{};
    auto const tag = tor->piece_completed_.connect_scoped([&n_completed](tr_torrent*, tr_piece_index_t) { ++n_completed; });

    // Piece 0's hash passes before piece 1's starts. The block they share
    // still waits for piece 1, which fails and takes the block back.
    session_->local_data.set_completions(tr::LocalData::Completions::Inline);
    blockingRunInSessionThread([this, tor, &n_completed] {
        writeStraddlingBlocks(tor, true);
        EXPECT_EQ(0U, n_completed);
        EXPECT_TRUE(tor->has_block(0U));
        EXPECT_FALSE(tor->has_block_or_pending(1U));
        EXPECT_FALSE(tor->has_block_or_pending(2U));
    });
}

TEST_F(TorrentDiskIoTest, blockArrivingDuringVerificationIsRefused)
{
    auto* const tor = zeroTorrentInit(ZeroTorrentState::Partial);
    auto const block = tor->block_span_for_piece(0).begin;
    auto verified = std::atomic<bool>{ false };
    auto const tag = session_->verify_done_.connect_scoped([&verified](tr_torrent_id_t) { verified = true; });

    blockingRunInSessionThread([tor, block] {
        tr_torrentVerify(tor);

        // Stopping the torrent doesn't stop a webseed fetch, so its block
        // can still arrive. The verify may have scanned the piece already.
        EXPECT_FALSE(tor->on_block_received(block));
        EXPECT_FALSE(tor->has_block_or_pending(block));
    });

    EXPECT_TRUE(waitFor([&verified]() { return verified.load(); }, MaxWaitMsec));
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
    ASSERT_TRUE(waitFor([this] { return requestCount() >= 32U; }, 15000));
    EXPECT_EQ(32U, requestCount());
    EXPECT_EQ(32U, spareBlocks());
    auto& second = addPeer(*tor);
    ASSERT_TRUE(waitFor([this] { return requestCount() >= BudgetBlocks; }, 15000));
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
    ASSERT_TRUE(waitFor([this] { return session_->local_data.enqueued_write_bytes() == TrBlockSize; }, MaxWaitMsec));
    EXPECT_EQ(0U, spareBlocks());
    EXPECT_EQ(BudgetBlocks, requestCount());

    session_->local_data.set_workers_paused(false);
    EXPECT_TRUE(waitFor([this] { return session_->local_data.enqueued_write_bytes() == 0U; }, MaxWaitMsec));
    EXPECT_TRUE(waitFor([this] { return requestCount() > BudgetBlocks; }, MaxWaitMsec));
    EXPECT_EQ(BudgetBlocks + 1U, requestCount());
    EXPECT_EQ(0U, spareBlocks());
    peers_.clear();
    EXPECT_TRUE(waitFor([this] { return spareBlocks() == BudgetBlocks; }, MaxWaitMsec));
}

TEST_F(RequestBudgetTest, peersResumeWhenWritesFreePartOfTheBudget)
{
    auto* const tor = makeBudgetTorrent("refill.bin");
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
    std::ignore = addPeer(*tor);
    ASSERT_TRUE(waitFor([this]() { return requestCount() >= BudgetBlocks; }, 15000));

    // An eighth of the budget, received and queued behind the paused workers.
    auto constexpr NFreed = BudgetBlocks / 8U;
    auto blocks = std::vector<Request>{};
    std::ranges::copy_if(first.requests, std::back_inserter(blocks), [](Request const& r) { return r.length == TrBlockSize; });
    ASSERT_GT(std::size(blocks), NFreed);
    for (auto const& request : blocks | std::views::take(NFreed)) {
        receiveBlock(first, request);
    }
    ASSERT_TRUE(waitFor([this]() { return session_->local_data.enqueued_write_bytes() == NFreed * TrBlockSize; }, MaxWaitMsec));

    // One more block waits behind a barrier, which runs once the first
    // writes finish and pauses the workers again. The refill must arrive
    // while that block is still queued.
    blockingRunInSessionThread([this, tor] {
        session_->local_data.close_file(tor->id(), 0U, [this](tr_torrent_id_t) {
            session_->local_data.set_workers_paused(true);
        });
    });
    receiveBlock(first, blocks[NFreed]);
    ASSERT_TRUE(
        waitFor([this]() { return session_->local_data.enqueued_write_bytes() == (NFreed + 1U) * TrBlockSize; }, MaxWaitMsec));
    EXPECT_EQ(BudgetBlocks, requestCount());

    // The peer manager's pulse would request the freed blocks too, so it is stopped.
    blockingRunInSessionThread([this] { tr::detail::tr_peerMgrStopBandwidthTimer(peerManager()); });
    session_->local_data.set_workers_paused(false);
    EXPECT_TRUE(waitFor([this]() { return requestCount() == BudgetBlocks + NFreed; }, MaxWaitMsec));
    EXPECT_EQ(TrBlockSize, session_->local_data.enqueued_write_bytes());
}

TEST_F(RequestBudgetTest, peersResumeWhenTheDiskQueueDrains)
{
    auto* const tor = makeBudgetTorrent("drain.bin");
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
    std::ignore = addPeer(*tor);
    ASSERT_TRUE(waitFor([this]() { return requestCount() >= BudgetBlocks; }, 15000));

    // One block, less than an eighth of the budget, and nothing else queued.
    auto const request = std::ranges::find_if(first.requests, [](auto const& r) { return r.length == TrBlockSize; });
    ASSERT_NE(std::end(first.requests), request);
    receiveBlock(first, *request);
    ASSERT_TRUE(waitFor([this]() { return session_->local_data.enqueued_write_bytes() == TrBlockSize; }, MaxWaitMsec));

    blockingRunInSessionThread([this] { tr::detail::tr_peerMgrStopBandwidthTimer(peerManager()); });
    session_->local_data.set_workers_paused(false);
    EXPECT_TRUE(waitFor(
        [this]() { return session_->local_data.enqueued_write_bytes() == 0U && requestCount() == BudgetBlocks + 1U; },
        MaxWaitMsec));
}

TEST_F(RequestBudgetTest, unrequestedBlockIsRefusedWhileTheBudgetIsSpent)
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
    ASSERT_TRUE(waitFor([this] { return requestCount() >= 32U; }, 15000));
    auto& second = addPeer(*tor);
    ASSERT_TRUE(waitFor([this] { return requestCount() >= BudgetBlocks; }, 15000));
    EXPECT_EQ(0U, spareBlocks());

    auto const was_requested = [&first, &second](tr_block_info::Location const& loc) {
        return std::ranges::any_of(std::array{ &first, &second }, [&loc](ConnectedPeer const* const peer) {
            return std::ranges::any_of(peer->requests, [&loc](Request const& request) {
                return request.piece == loc.piece && request.offset == loc.piece_offset;
            });
        });
    };
    auto unrequested = std::optional<tr_block_index_t>{};
    for (auto block = tr_block_index_t{}; block < tor->block_count(); ++block) {
        if (!was_requested(tor->block_loc(block))) {
            unrequested = block;
            break;
        }
    }
    ASSERT_TRUE(unrequested.has_value());
    auto const requested = std::ranges::find_if(first.requests, [](Request const& item) { return item.length == TrBlockSize; });
    ASSERT_NE(first.requests.end(), requested);
    auto const requested_block = tor->piece_loc(requested->piece, requested->offset).block;

    // A peer can send a block we never asked it for. With the budget
    // spent, that block is refused. The requested block sent after it on
    // the same connection shows when the peer's messages were read.
    auto const loc = tor->block_loc(*unrequested);
    receiveBlock(first, Request{ .piece = loc.piece, .offset = loc.piece_offset, .length = tor->block_size(*unrequested) });
    receiveBlock(first, *requested);
    ASSERT_TRUE(waitFor(
        [tor, requested_block] {
            auto const lock = tor->unique_lock();
            return tor->has_block_or_pending(requested_block);
        },
        MaxWaitMsec));
    auto const lock = tor->unique_lock();
    EXPECT_FALSE(tor->has_block_or_pending(*unrequested));
    EXPECT_EQ(TrBlockSize, session_->local_data.enqueued_write_bytes());
}

TEST_F(RequestBudgetTest, duplicateWebseedResponseReleasesItsReservation)
{
    auto release_response = std::promise<void>{};
    auto const response_allowed = release_response.get_future().share();
    auto requested = std::promise<void>{};
    auto server = LoopbackServer::create(TR_AF_INET);
    if (!server) {
        GTEST_SKIP() << "IPv4 loopback is unavailable";
    }
    server->setHandler([&](evhttp_request* const request) {
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
    blockingRunInSessionThread([&] {
        webseed_ = tr_webseed::create(*tor, server->url(), nullptr, nullptr);
        auto const span = tr_block_span_t{ .begin = 0U, .end = 1U };
        webseed_->request_blocks(&span, 1U);
        EXPECT_EQ(BudgetBlocks - 1U, session_->spare_request_blocks());
        tor->save_block(0U, zeroBlock(tor, 0U));
        EXPECT_EQ(BudgetBlocks - 2U, session_->spare_request_blocks());
        tr_torrentStop(tor);
    });
    EXPECT_EQ(std::future_status::ready, requested.get_future().wait_for(5s));
    release_response.set_value();

    EXPECT_TRUE(waitFor([this] { return spareBlocks() == BudgetBlocks - 1U; }, MaxWaitMsec));
    blockingRunInSessionThread([this] {
        EXPECT_EQ(0U, webseed_->active_requests().count());
        EXPECT_EQ(TrBlockSize, session_->local_data.enqueued_write_bytes());
    });
    session_->local_data.set_workers_paused(false);
    EXPECT_TRUE(waitFor([this] { return spareBlocks() == BudgetBlocks; }, MaxWaitMsec));
}

TEST_P(WebseedRequestBudgetTest, rangeRequestsResumeAfterTheDiskQueueDrains)
{
    auto const peer_blocks = GetParam() ? size_t{ 32U } : size_t{};
    auto const available_bytes = (BudgetBlocks - peer_blocks) * TrBlockSize;
    if (GetParam()) {
        auto* const peer_torrent = makeBudgetTorrent("peer.bin");
        tr_torrentStart(peer_torrent);
        ASSERT_TRUE(waitFor(
            [peer_torrent] {
                auto const lock = peer_torrent->unique_lock();
                return peer_torrent->is_running();
            },
            MaxWaitMsec));
        auto& peer = addPeer(*peer_torrent);
        ASSERT_TRUE(waitFor(
            [&peer, peer_blocks] {
                collectMessages(peer);
                return peer.requests.size() >= peer_blocks;
            },
            15000));
        EXPECT_EQ(peer_blocks, requestCount());
    }

    auto requests = std::atomic<size_t>{};
    auto received_bytes = std::atomic<uint64_t>{};
    auto server = LoopbackServer::create(TR_AF_INET);
    if (!server) {
        GTEST_SKIP() << "IPv4 loopback is unavailable";
    }
    server->setHandler([&](evhttp_request* const request) {
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

    auto* const tor = makeBudgetTorrent("webseed.bin", server->url());
    session_->local_data.set_workers_paused(true);
    tr_torrentStart(tor);
    ASSERT_TRUE(
        waitFor([this, available_bytes] { return session_->local_data.enqueued_write_bytes() == available_bytes; }, 15000));
    EXPECT_EQ(0U, spareBlocks());
    EXPECT_EQ(available_bytes, received_bytes.load());
    auto const requests_when_full = requests.load();
    EXPECT_FALSE(waitFor([&requests, requests_when_full]() { return requests.load() != requests_when_full; }, 2500));

    session_->local_data.set_workers_paused(false);
    ASSERT_TRUE(waitFor(
        [tor] {
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
    blockingRunInSessionThread([this, tor, id, &hash_completed] {
        tor->save_block(0U, zeroBlock(tor, 0U));
        session_->local_data.test_piece(
            id,
            1U,
            [&hash_completed](tr_torrent_id_t, tr_piece_index_t, tr_error const& error, auto hash) {
                EXPECT_FALSE(error);
                EXPECT_TRUE(hash);
                hash_completed = true;
            });
        tr_torrentRemove(tor, GetParam());
        EXPECT_EQ(tor, session_->torrents().get(id));
        EXPECT_FALSE(tor->is_running());
        EXPECT_FALSE(tor->on_block_received(1U));
    });
    session_->local_data.set_workers_paused(false);

    EXPECT_TRUE(waitForInSessionThread([this, id]() { return session_->torrents().get(id) == nullptr; }, MaxWaitMsec));
    blockingRunInSessionThread([this, &hash_completed] {
        EXPECT_TRUE(hash_completed);
        EXPECT_EQ(0U, session_->local_data.enqueued_write_bytes());
    });
    EXPECT_EQ(!GetParam(), tr_sys_path_exists(filename));
}

TEST_P(TorrentRemovalTest, rpcSeesTheRemovalBeforeDiskIoDrains)
{
    auto* const tor = zeroTorrentInit(ZeroTorrentState::Partial);
    auto const id = tor->id();
    auto const filename = tr_torrentFindFile(tor, 0U);

    // A paused write holds back the removal.
    session_->local_data.set_workers_paused(true);
    blockingRunInSessionThread([this, tor, id] {
        tor->save_block(0U, zeroBlock(tor, 0U));
        tr_torrentRemove(tor, GetParam());
        tr_torrentRemove(tor, !GetParam());

        auto fields = tr_variant::Vector{};
        fields.emplace_back(tr_quark_get_string_view(TR_KEY_id));
        auto params = tr_variant::Map{ 2U };
        params.try_emplace(TR_KEY_fields, std::move(fields));
        params.try_emplace(TR_KEY_ids, tr_quark_get_string_view(TR_KEY_recently_active));
        auto const got = rpc(TR_KEY_torrent_get, std::move(params));
        auto const* const torrents = got.find_if<tr_variant::Vector>(TR_KEY_torrents);
        ASSERT_NE(nullptr, torrents);
        EXPECT_TRUE(std::empty(*torrents));
        auto const* const removed = got.find_if<tr_variant::Vector>(TR_KEY_removed);
        ASSERT_NE(nullptr, removed);
        ASSERT_EQ(1U, std::size(*removed));
        EXPECT_EQ(int64_t{ id }, removed->front().value_if<int64_t>());

        auto const stats = rpc(TR_KEY_session_stats, {});
        EXPECT_EQ(int64_t{ 0 }, stats.value_if<int64_t>(TR_KEY_torrent_count));

        // the torrent itself stays until its disk IO drains
        EXPECT_EQ(tor, session_->torrents().get(id));
    });
    session_->local_data.set_workers_paused(false);

    EXPECT_TRUE(waitForInSessionThread([this, id] { return session_->torrents().get(id) == nullptr; }, MaxWaitMsec));
    // the first request's keep-or-delete choice stands
    EXPECT_EQ(!GetParam(), tr_sys_path_exists(filename));
}

INSTANTIATE_TEST_SUITE_P(KeepOrDeleteData, TorrentRemovalTest, ::testing::Bool());

TEST_F(TorrentDiskIoWorkersTest, requestBudgetTracksChangesAndPeerDestruction)
{
    auto* const tor = zeroTorrentInit(ZeroTorrentState::Partial);
    blockingRunInSessionThread([this, tor] {
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
    blockingRunInSessionThread([this, &budget] { budget = session_->spare_request_blocks(); });
    ASSERT_TRUE(budget.has_value());
    EXPECT_GT(*budget, 0U);

    // a queued write takes its block out of the budget until it lands
    session_->local_data.set_workers_paused(true);
    blockingRunInSessionThread([this, tor, block, &budget] {
        ASSERT_TRUE(tor->on_block_received(block));
        tor->save_block(block, zeroBlock(tor, block));
        EXPECT_EQ(*budget - 1U, session_->spare_request_blocks());
    });

    session_->local_data.set_workers_paused(false);
    EXPECT_TRUE(waitForInSessionThread([tor, block]() { return tor->has_block(block); }, MaxWaitMsec));
    blockingRunInSessionThread([this, &budget] { EXPECT_EQ(budget, session_->spare_request_blocks()); });
}

TEST_F(TorrentDiskIoWorkersTest, writesBehindBarriersConsumeTheRequestBudget)
{
    auto* const tor = zeroTorrentInit(ZeroTorrentState::Partial);
    auto const block = tor->block_span_for_piece(0).begin;

    session_->local_data.set_workers_paused(true);
    blockingRunInSessionThread([this, tor, block] {
        auto const budget = session_->spare_request_blocks();
        ASSERT_TRUE(budget.has_value());
        tor->save_block(block, zeroBlock(tor, block));
        session_->local_data.close_torrent(tor->id());
        tor->save_block(block + 1U, zeroBlock(tor, block + 1U));
        EXPECT_EQ(*budget - 2U, session_->spare_request_blocks());
    });

    session_->local_data.set_workers_paused(false);
    EXPECT_TRUE(
        waitForInSessionThread([tor, block]() { return tor->has_block(block) && tor->has_block(block + 1U); }, MaxWaitMsec));
    EXPECT_EQ(0U, session_->local_data.enqueued_write_bytes());
}

TEST_F(TorrentDiskIoWorkersTest, blockCountsOnlyAfterItsWriteFinishes)
{
    auto* const tor = zeroTorrentInit(ZeroTorrentState::Partial);
    auto const block = tor->block_span_for_piece(0).begin;

    blockingRunInSessionThread([tor, block] {
        ASSERT_TRUE(tor->on_block_received(block));
        tor->save_block(block, zeroBlock(tor, block));

        // the write is on a worker, so the block is pending but not ours
        EXPECT_TRUE(tor->has_block_or_pending(block));
        EXPECT_FALSE(tor->on_block_received(block));
    });

    EXPECT_TRUE(waitForInSessionThread([tor, block]() { return tor->has_block(block); }, MaxWaitMsec));
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
    blockingRunInSessionThread([this, tor, block, &first_dir, &last_dir, &first_state, &last_state, &closed] {
        tor->save_block(block, zeroBlock(tor, block));
        tr_torrentSetLocation(tor, first_dir, true, &first_state);
        tr_torrentSetLocation(tor, last_dir, true, &last_state);
        EXPECT_EQ(TR_LOC_MOVING, first_state);
        EXPECT_EQ(TR_LOC_MOVING, last_state);
        session_->local_data.close_torrent(tor->id(), [&closed](tr_torrent_id_t) { closed = true; });
    });

    session_->local_data.set_workers_paused(false);
    EXPECT_TRUE(waitFor([&closed] { return closed.load(); }, MaxWaitMsec));
    blockingRunInSessionThread([tor, &last_dir, &first_state, &last_state] {
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
    blockingRunInSessionThread([tor, span] {
        for (auto block = span.begin; block < span.end; ++block) {
            tor->save_block(block, zeroBlock(tor, block));
        }
        tr_torrentVerify(tor);
        EXPECT_EQ(TR_STATUS_CHECK_WAIT, tor->activity());
    });
    EXPECT_FALSE(verified);

    session_->local_data.set_workers_paused(false);
    EXPECT_TRUE(waitFor([&verified]() { return verified.load(); }, MaxWaitMsec));
    blockingRunInSessionThread([this, tor] {
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
    blockingRunInSessionThread([this, tor, block, &closed] {
        tor->save_block(block, zeroBlock(tor, block));
        tr_torrentVerify(tor);
        tr_torrentStop(tor);
        EXPECT_EQ(TR_STATUS_STOPPED, tor->activity());
        session_->local_data.close_torrent(tor->id(), [&closed](tr_torrent_id_t) { closed = true; });
    });

    session_->local_data.set_workers_paused(false);
    EXPECT_TRUE(waitFor([&closed] { return closed.load(); }, MaxWaitMsec));
    blockingRunInSessionThread([tor, &verified] {
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
    blockingRunInSessionThread([tor, block] {
        // the paused write holds the verify back...
        tor->save_block(block, zeroBlock(tor, block));
        tr_torrentVerify(tor);

        // ...while piece 1's writes land, which defers its hash
        tor->set_has_piece(1U, false);
        auto const [begin, end] = tor->block_span_for_piece(1U);
        for (auto written = begin; written < end; ++written) {
            tor->on_block_written(written, {});
        }
        tr_torrentStop(tor);
    });

    session_->local_data.set_workers_paused(false);
    EXPECT_TRUE(waitFor([&hashed] { return hashed.load(); }, MaxWaitMsec));
}

TEST_F(TorrentDiskIoWorkersTest, completedPieceIsHashedFromBufferedBlocks)
{
    auto* const tor = zeroTorrentInit(ZeroTorrentState::Partial);
    auto const span = tor->block_span_for_piece(0);

    // The hash passing is what completes the piece.
    auto n_completed = std::atomic<size_t>{};
    auto const tag = tor->piece_completed_.connect_scoped([&n_completed](tr_torrent*, tr_piece_index_t) { ++n_completed; });

    blockingRunInSessionThread([tor, span] {
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

TEST_F(TorrentDiskIoWorkersTest, failedWriteSetsLocalError)
{
    auto* const tor = zeroTorrentInit(ZeroTorrentState::Partial);
    auto const block = tor->block_span_for_piece(0).begin;

    // Replace the block's file with a directory. Nobody can open that
    // for writing, on any platform or as root.
    auto const filename = tr_torrentFindFile(tor, 0);
    ASSERT_FALSE(std::empty(filename));
    ASSERT_TRUE(tr_sys_path_remove(filename));
    ASSERT_TRUE(tr_sys_dir_create(filename, 0, 0700));

    blockingRunInSessionThread([tor, block] {
        ASSERT_TRUE(tor->on_block_received(block));
        tor->save_block(block, zeroBlock(tor, block));
    });

    EXPECT_TRUE(waitForInSessionThread([tor]() { return tor->error().is_local_error(); }, MaxWaitMsec));

    // the block was not counted, and is no longer pending
    blockingRunInSessionThread([tor, block] {
        EXPECT_FALSE(tor->has_block(block));
        EXPECT_FALSE(tor->has_block_or_pending(block));
    });
}

TEST_F(TorrentDiskIoWorkersTest, doneStepOfAnUndoneCompletionIsDropped)
{
    auto* const tor = zeroTorrentInit(ZeroTorrentState::Partial);
    auto const id = tor->id();
    auto n_done = std::atomic<size_t>{};
    tr_sessionSetCompletenessCallback(session_, [&n_done](tr_torrent_id_t, tr_completeness const completeness, bool) {
        if (completeness != TR_LEECH) {
            ++n_done;
        }
    });

    // A paused write holds back each completion's done step.
    session_->local_data.set_workers_paused(true);
    blockingRunInSessionThread([tor] {
        auto const block = tor->block_span_for_piece(0U).begin;
        tor->save_block(block, zeroBlock(tor, block));
    });

    // Skipping the file that holds the missing piece completes the torrent.
    // Wanting it again undoes that, and skipping it again completes it anew.
    auto const file = tr_file_index_t{ 0U };
    for (auto const wanted : { false, true, false }) {
        tr_torrentSetFileDLs(tor, std::span{ &file, 1U }, wanted);
    }

    // This close queues behind both done steps.
    auto closed = std::atomic<bool>{ false };
    blockingRunInSessionThread(
        [this, id, &closed] { session_->local_data.close_torrent(id, [&closed](tr_torrent_id_t) { closed = true; }); });
    session_->local_data.set_workers_paused(false);
    EXPECT_TRUE(waitFor([&closed] { return closed.load(); }, MaxWaitMsec));
    tr_sessionSetCompletenessCallback(session_, nullptr);

    EXPECT_EQ(1U, n_done.load());
}

TEST_F(IncompleteDirWorkersTest, doneCallbackWaitsForTheMoveOut)
{
    auto* const tor = zeroTorrentInit(ZeroTorrentState::Partial);
    auto const download_dir = std::string{ tor->download_dir().sv() };

    EXPECT_EQ(download_dir, completeBehindAWrite(tor));
    expectFilesIn(tor, download_dir);
}

TEST_F(IncompleteDirWorkersTest, setLocationQueuedBeforeCompletionPicksTheDir)
{
    auto* const tor = zeroTorrentInit(ZeroTorrentState::Partial);
    auto const target = tr_pathbuf{ sandboxDir(), "/target" };
    auto state = -1;

    EXPECT_EQ(target.sv(), completeBehindAWrite(tor, [tor, &target, &state] {
                  tr_torrentSetLocation(tor, target, true, &state);
              }));
    EXPECT_EQ(TR_LOC_DONE, state);
    expectFilesIn(tor, target);
}

TEST_F(IncompleteDirWorkersTest, failedSetLocationStillLeavesTheIncompleteDir)
{
    auto* const tor = zeroTorrentInit(ZeroTorrentState::Partial);
    auto const download_dir = std::string{ tor->download_dir().sv() };

    // nothing can be created under a regular file
    auto const blocker = tr_pathbuf{ sandboxDir(), "/blocker" };
    createFileWithContents(blocker, std::string_view{ "x" });
    auto const target = tr_pathbuf{ blocker, "/target" };
    auto state = -1;

    EXPECT_EQ(download_dir, completeBehindAWrite(tor, [tor, &target, &state] {
                  tr_torrentSetLocation(tor, target, true, &state);
              }));
    EXPECT_EQ(TR_LOC_ERROR, state);
    expectFilesIn(tor, download_dir);
}

TEST_F(UploadTest, servesEveryRequestedBlock)
{
    auto* const tor = makeSeed();
    ASSERT_NE(nullptr, tor);
    connectPeer(tor);

    // one burst of pipelined requests, as a fast leecher sends
    for (auto piece = uint32_t{}; piece < PieceCount; ++piece) {
        for (auto offset = uint32_t{}; offset < PieceSize; offset += TrBlockSize) {
            request(piece, offset);
        }
    }

    auto const n_blocks = size_t{ TotalSize / TrBlockSize };
    ASSERT_TRUE(waitForReplies(n_blocks));
    EXPECT_EQ(n_blocks, std::size(replies_));
    for (auto const& reply : replies_) {
        expectPiece(reply);
    }
}

TEST_F(UploadTest, checksAPieceBeforeSendingIt)
{
    auto* const tor = makeSeed();
    ASSERT_NE(nullptr, tor);
    connectPeer(tor);
    forgetCheckedPieces(tor);

    request(1U, 0U);
    request(1U, TrBlockSize);

    ASSERT_TRUE(waitForReplies(2U));
    for (auto const& reply : replies_) {
        expectPiece(reply);
    }

    // only the piece that was asked for
    blockingRunInSessionThread([tor] {
        EXPECT_TRUE(tor->is_piece_checked(1U));
        EXPECT_FALSE(tor->is_piece_checked(0U));
        EXPECT_TRUE(tor->error().empty()) << tor->error().errmsg();
    });
}

TEST_F(UploadTest, rejectsACorruptPiece)
{
    auto* const tor = makeSeed();
    ASSERT_NE(nullptr, tor);

    // piece 2 changes on disk
    auto const fd = tr_sys_file_open(seedPath(), TR_SYS_FILE_WRITE, 0);
    ASSERT_NE(TR_BAD_SYS_FILE, fd);
    auto const garbage = std::string(TrBlockSize, 'x');
    EXPECT_TRUE(tr_sys_file_write_at(fd, std::data(garbage), std::size(garbage), uint64_t{ 2U } * PieceSize, nullptr));
    tr_sys_file_close(fd);

    connectPeer(tor);
    forgetCheckedPieces(tor);

    request(2U, 0U);
    request(3U, 0U);

    ASSERT_TRUE(waitForReplies(2U));
    for (auto const& reply : replies_) {
        if (reply.piece == 2U) {
            EXPECT_EQ(MsgReject, reply.id);
        } else {
            expectPiece(reply);
        }
    }

    blockingRunInSessionThread([tor] {
        EXPECT_FALSE(tor->is_piece_checked(2U));
        EXPECT_TRUE(tor->error().is_local_error());
        EXPECT_TRUE(tor->error().errmsg().contains("Piece #2 is corrupt")) << tor->error().errmsg();
    });
}

TEST_F(UploadTest, answersARequestCancelledDuringItsCheckOnce)
{
    auto* const tor = makeSeed();
    ASSERT_NE(nullptr, tor);
    connectPeer(tor);
    forgetCheckedPieces(tor);

    // Cancelled while its piece is checked, where the check finishes later,
    // so the read that follows the check has nobody to answer.
    request(1U, 0U);
    cancel(1U, 0U);
    request(1U, TrBlockSize);
    request(3U, 0U);
    ASSERT_TRUE(waitForReplies(3U));

    // later requests aren't held back by the cancelled one's read...
    request(3U, TrBlockSize);
    ASSERT_TRUE(waitForReplies(4U));

    // ...which got a reject or its piece, but not both
    auto const is_cancelled = [](Reply const& reply) {
        return reply.piece == 1U && reply.offset == 0U;
    };
    EXPECT_EQ(1, std::ranges::count_if(replies_, is_cancelled));
    for (auto const& reply : replies_) {
        if (!is_cancelled(reply)) {
            expectPiece(reply);
        }
    }
}

TEST_F(UploadTest, rejectsAPieceLostDuringItsRead)
{
    auto* const tor = makeSeed();
    ASSERT_NE(nullptr, tor);
    connectPeer(tor);

    runDuringARead(
        tor,
        [this] { request(1U, TrBlockSize); },
        [tor] {
            loseFirstBlock(tor, 1U);
            EXPECT_FALSE(tor->has_piece(1U));
        });

    // the block that was read is intact, but its piece no longer vouches for it
    ASSERT_TRUE(waitForReplies(1U));
    EXPECT_EQ(MsgReject, replies_.front().id);
    EXPECT_EQ(1U, replies_.front().piece);
    EXPECT_EQ(TrBlockSize, replies_.front().offset);
}

TEST_F(UploadTest, servesMoreThanItKeepsInFlight)
{
    static auto constexpr NPieces = static_cast<uint32_t>((tr::LocalData::read_ahead_bytes() / PieceSize) + 8U);
    auto* const tor = makeSeed(NPieces);
    ASSERT_NE(nullptr, tor);
    connectPeer(tor);

    // more requests than the read-ahead budget lets into flight at once
    for (auto piece = uint32_t{}; piece < NPieces; ++piece) {
        for (auto offset = uint32_t{}; offset < PieceSize; offset += TrBlockSize) {
            request(piece, offset);
        }
    }

    auto const n_blocks = size_t{ NPieces } * (PieceSize / TrBlockSize);
    ASSERT_TRUE(waitForReplies(n_blocks));
    EXPECT_EQ(n_blocks, std::size(replies_));
    for (auto const& reply : replies_) {
        expectPiece(reply);
    }
}

} // namespace tr::test
