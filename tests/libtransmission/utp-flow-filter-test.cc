// This file Copyright © Retransmission authors and contributors.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#include <array>
#include <cstdint>
#include <ctime>
#include <string_view>

#include <libtransmission/net.h>
#include <libtransmission/tr-utp.h>

#include "test-fixtures.h"

using namespace std::literals;

namespace
{
using UtpFlowFilterTest = ::tr::test::TransmissionTest;

// a µTP header to the connection `id`: type and version, extension, then the ID
[[nodiscard]] std::array<uint8_t, 20U> packet(uint16_t const id)
{
    auto ret = std::array<uint8_t, 20U>{};
    ret[0] = 0x21U; // ST_STATE, version 1
    ret[2] = static_cast<uint8_t>(id >> 8U);
    ret[3] = static_cast<uint8_t>(id & 0xFFU);
    return ret;
}

[[nodiscard]] tr_socket_address peer(std::string_view const address)
{
    return { *tr_address::from_string(address), tr_port::from_host(51413) };
}
} // namespace

TEST_F(UtpFlowFilterTest, silencesConnectionsSeenBeforeAndNoOthers)
{
    auto filter = tr_utp_flow_filter{};
    auto const now = time_t{ 1000 };

    EXPECT_TRUE(filter.allow(peer("192.0.2.1"), packet(7), now));
    EXPECT_TRUE(filter.allow(peer("192.0.2.2"), packet(9), now));
    filter.silence(now);

    // the connections open at the move send nothing more
    EXPECT_FALSE(filter.allow(peer("192.0.2.1"), packet(7), now + 1));
    EXPECT_FALSE(filter.allow(peer("192.0.2.2"), packet(9), now + 1));

    // a new connection to the same peer has another ID, and goes out
    EXPECT_TRUE(filter.allow(peer("192.0.2.1"), packet(8), now + 1));
    EXPECT_TRUE(filter.allow(peer("192.0.2.3"), packet(7), now + 1));
}

TEST_F(UtpFlowFilterTest, silenceEnds)
{
    auto filter = tr_utp_flow_filter{};
    auto const now = time_t{ 1000 };

    EXPECT_TRUE(filter.allow(peer("192.0.2.1"), packet(7), now));
    filter.silence(now);
    EXPECT_FALSE(filter.allow(peer("192.0.2.1"), packet(7), now + 599));

    // libutp has given up on the old connection by then, so its ID may come back
    EXPECT_TRUE(filter.allow(peer("192.0.2.1"), packet(7), now + 600));
}

TEST_F(UtpFlowFilterTest, connectionsAfterTheMoveArentSilencedByIt)
{
    auto filter = tr_utp_flow_filter{};
    auto const now = time_t{ 1000 };

    filter.silence(now);
    EXPECT_TRUE(filter.allow(peer("192.0.2.1"), packet(7), now + 1));
    EXPECT_TRUE(filter.allow(peer("192.0.2.1"), packet(7), now + 2));
}
