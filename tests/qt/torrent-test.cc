// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#include <cstdint>
#include <initializer_list>
#include <string>
#include <utility>
#include <vector>

#include <QApplication>
#include <QTest>

#include <fmt/format.h>

#include <libtransmission/quark.h>
#include <libtransmission/types.h>
#include <libtransmission/values.h>
#include <libtransmission/variant.h>

#include "Prefs.h"
#include "Torrent.h"
#include "TrQtInit.h"

using tr::Values::Percent;

namespace
{
[[nodiscard]] std::string describe(Percent const& actual, Percent const& expected)
{
    auto const str = [](Percent const& p) {
        return fmt::format("{:d}/{:d}{:s}", p.have(), p.total(), p.is_complete() ? " (complete)" : "");
    };
    return fmt::format("got {:s}, expected {:s}", str(actual), str(expected));
}

#define TRCOMPARE_PERCENT(actual, expected) QVERIFY2((actual) == (expected), describe((actual), (expected)).c_str())

// feeds `torrent_get` fields to `tor`, as `TorrentModel` does
void update(Torrent& tor, std::initializer_list<std::pair<tr_quark, tr_variant>> const fields)
{
    auto keyvals = std::vector<Torrent::keyval_t>{};
    keyvals.reserve(std::size(fields));
    for (auto const& [key, value] : fields) {
        keyvals.emplace_back(key, &value);
    }
    tor.update(keyvals);
}

void set_metadata_complete(Torrent& tor)
{
    update(tor, { { TR_KEY_metadata_have, int64_t{ 1000 } }, { TR_KEY_metadata_size, int64_t{ 1000 } } });
}

class TorrentTest : public QObject
{
    Q_OBJECT

private slots:
    void metadata_percent_from_counts()
    {
        auto const prefs = Prefs{};
        auto tor = Torrent{ prefs, 1 };
        update(tor, { { TR_KEY_metadata_have, int64_t{ 500 } }, { TR_KEY_metadata_size, int64_t{ 1000 } } });
        TRCOMPARE_PERCENT(tor.metadataPercentDone(), (Percent{ 500U, 1000U }));
        QVERIFY(!tor.hasMetadata());

        update(tor, { { TR_KEY_metadata_have, int64_t{ 1000 } } });
        QVERIFY(tor.hasMetadata());
    }

    // no peer has reported the size yet
    void metadata_size_zero_is_unknown()
    {
        auto const prefs = Prefs{};
        auto tor = Torrent{ prefs, 1 };
        update(
            tor,
            {
                { TR_KEY_metadata_have, int64_t{ 0 } },
                { TR_KEY_metadata_size, int64_t{ 0 } },
                { TR_KEY_metadata_percent_complete, 0.0 },
            });
        TRCOMPARE_PERCENT(tor.metadataPercentDone(), Percent{});
        QVERIFY(!tor.hasMetadata());
    }

    // daemons that predate `metadata_have` and `metadata_size`
    void metadata_percent_falls_back_to_the_double()
    {
        auto const prefs = Prefs{};
        auto tor = Torrent{ prefs, 1 };
        update(tor, { { TR_KEY_metadata_percent_complete, 0.5 } });
        TRCOMPARE_PERCENT(tor.metadataPercentDone(), (Percent{ 1U, 2U }));
        QVERIFY(!tor.hasMetadata());

        update(tor, { { TR_KEY_metadata_percent_complete, 1.0 } });
        QVERIFY(tor.hasMetadata());
    }

    void percents_from_byte_counts()
    {
        auto const prefs = Prefs{};
        auto tor = Torrent{ prefs, 1 };
        set_metadata_complete(tor);
        update(
            tor,
            {
                { TR_KEY_size_when_done, int64_t{ 100 } },
                { TR_KEY_left_until_done, int64_t{ 25 } },
                { TR_KEY_have_valid, int64_t{ 60 } },
                { TR_KEY_have_unchecked, int64_t{ 15 } },
                { TR_KEY_total_size, int64_t{ 200 } },
            });
        TRCOMPARE_PERCENT(tor.percentDone(), (Percent{ 75U, 100U }));
        TRCOMPARE_PERCENT(tor.percentComplete(), (Percent{ 75U, 200U }));
    }

    // every size is 0 until the metadata arrives, which would read as complete
    void percents_need_metadata()
    {
        auto const prefs = Prefs{};
        auto tor = Torrent{ prefs, 1 };
        update(
            tor,
            {
                { TR_KEY_metadata_have, int64_t{ 0 } },
                { TR_KEY_metadata_size, int64_t{ 0 } },
                { TR_KEY_size_when_done, int64_t{ 0 } },
                { TR_KEY_left_until_done, int64_t{ 0 } },
                { TR_KEY_total_size, int64_t{ 0 } },
            });
        QVERIFY(!tor.percentDone().is_complete());
        QVERIFY(!tor.percentComplete().is_complete());
    }

    void verify_progress_from_checked_bytes()
    {
        auto const prefs = Prefs{};
        auto tor = Torrent{ prefs, 1 };
        set_metadata_complete(tor);
        update(
            tor,
            {
                { TR_KEY_total_size, int64_t{ 200 } },
                { TR_KEY_checked_bytes, int64_t{ 50 } },
                { TR_KEY_recheck_progress, 0.9 },
            });
        TRCOMPARE_PERCENT(tor.getVerifyProgress(), (Percent{ 50U, 200U }));
    }

    // daemons that predate `checked_bytes`
    void verify_progress_falls_back_to_the_double()
    {
        auto const prefs = Prefs{};
        auto tor = Torrent{ prefs, 1 };
        update(tor, { { TR_KEY_metadata_percent_complete, 1.0 }, { TR_KEY_recheck_progress, 0.25 } });
        TRCOMPARE_PERCENT(tor.getVerifyProgress(), (Percent{ 1U, 4U }));
    }

    void peer_progress_from_have_total()
    {
        auto const prefs = Prefs{};
        auto tor = Torrent{ prefs, 1 };
        set_metadata_complete(tor);
        update(tor, { { TR_KEY_total_size, int64_t{ 200 } } });

        auto peer = Peer{};
        peer.have_total = 50U;
        peer.progress = 0.9;
        TRCOMPARE_PERCENT(tor.peerProgress(peer), (Percent{ 50U, 200U }));
    }

    // `have_total` and `total_size` are both 0 until the metadata arrives
    void peer_progress_before_metadata_uses_the_double()
    {
        auto const prefs = Prefs{};
        auto tor = Torrent{ prefs, 1 };
        update(tor, { { TR_KEY_metadata_have, int64_t{ 0 } }, { TR_KEY_metadata_size, int64_t{ 0 } } });

        auto peer = Peer{};
        peer.have_total = 0U;
        peer.progress = 1.0;
        QVERIFY(tor.peerProgress(peer).is_complete());
    }

    // daemons that predate `have_total`
    void peer_progress_falls_back_to_the_double()
    {
        auto const prefs = Prefs{};
        auto tor = Torrent{ prefs, 1 };
        update(tor, { { TR_KEY_metadata_percent_complete, 1.0 }, { TR_KEY_total_size, int64_t{ 200 } } });

        auto peer = Peer{};
        peer.progress = 0.25;
        TRCOMPARE_PERCENT(tor.peerProgress(peer), (Percent{ 1U, 4U }));
    }

    void seed_ratio_percent_against_the_limit()
    {
        auto const prefs = Prefs{};
        auto tor = Torrent{ prefs, 1 };
        update(tor, { { TR_KEY_seed_ratio_mode, int64_t{ TR_RATIOLIMIT_UNLIMITED } } });
        QVERIFY(!tor.seedRatioPercentDone());

        update(
            tor,
            {
                { TR_KEY_seed_ratio_mode, int64_t{ TR_RATIOLIMIT_SINGLE } },
                { TR_KEY_seed_ratio_limit, 2.0 },
                { TR_KEY_upload_ratio, 1.0 },
            });
        QVERIFY(tor.seedRatioPercentDone());
        TRCOMPARE_PERCENT(*tor.seedRatioPercentDone(), (Percent{ 1U, 2U }));
    }

    // a zero goal is always met
    void seed_ratio_percent_without_a_goal_is_complete()
    {
        auto const prefs = Prefs{};
        auto tor = Torrent{ prefs, 1 };
        update(tor, { { TR_KEY_seed_ratio_mode, int64_t{ TR_RATIOLIMIT_SINGLE } }, { TR_KEY_seed_ratio_limit, 2.0 } });

        for (auto const ratio : { tr::Values::Ratio::WireNone, tr::Values::Ratio::WireInfinite }) {
            update(tor, { { TR_KEY_upload_ratio, ratio } });
            QVERIFY(tor.seedRatioPercentDone() && tor.seedRatioPercentDone()->is_complete());
        }

        update(tor, { { TR_KEY_seed_ratio_limit, 0.0 }, { TR_KEY_upload_ratio, 1.0 } });
        QVERIFY(tor.seedRatioPercentDone() && tor.seedRatioPercentDone()->is_complete());
    }
};
} // namespace

int main(int argc, char** argv)
{
    trqt::trqt_init();
    auto const app = QApplication{ argc, argv };
    auto test = TorrentTest{};
    return QTest::qExec(&test, argc, argv);
}

#include "torrent-test.moc"
