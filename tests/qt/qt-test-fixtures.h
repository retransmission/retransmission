// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <type_traits>

#include <QObject>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>

#include <fmt/format.h>

#include <libtransmission/converters.h>
#include <libtransmission/variant.h>

#include "TrQtInit.h"
#include "VariantHelpers.h"

// QCOMPARE_EQ / QCOMPARE_NE only exist in Qt >= 6.3, but Transmission still
// supports Qt 5.15. These give equivalent comparisons (fuzzy for floats) and,
// on failure, render both operands as JSON via the serializer. Distinct `T`/`U`
// allow mixed operand types; an operand without a Converter is a compile error.
template<typename T, typename U>
void trcompare(T const& actual, U const& expected, bool const negate)
{
    auto ok = bool{};
    if constexpr (std::is_floating_point_v<T> && std::is_floating_point_v<U>) {
        ok = qFuzzyCompare(actual, expected);
    } else {
        ok = actual == expected;
    }

    if (negate ? !ok : ok) {
        return;
    }

    auto serde = tr_variant_serde::json();
    serde.compact();
    auto const actual_str = serde.to_string(tr::serializer::to_variant(actual));
    auto const expected_str = serde.to_string(tr::serializer::to_variant(expected));
    QFAIL(fmt::format("got '{:s}', expected '{:s}'", actual_str, expected_str).c_str());
}

#define TRCOMPARE_EQ(actual, expected) trcompare((actual), (expected), false)
#define TRCOMPARE_NE(actual, expected) trcompare((actual), (expected), true)

// Probes whether this build's Qt can resolve a pointer-to-member signal at
// runtime. Some prebuilt Qt packages (seen on the netbsd/freebsd CI runners,
// where a modern Qt is compiled by a compiler too old for it) miscompile the
// `connect()` machinery, so *every* pointer-to-member connection silently fails
// with "signal not found" and never delivers. A failed connect returns an
// invalid QMetaObject::Connection, which we detect here without needing an event
// loop, moc, or a QApplication. Keying test execution on this capability -- vs a
// hardcoded platform or compiler-version list -- lets the Qt runtime tests
// re-enable themselves automatically once the environment can run them.
[[nodiscard]] inline bool qtPointerConnectWorks()
{
    auto timer = QTimer{};
    auto receiver = QObject{};
    auto const connection = QObject::connect(&timer, &QTimer::timeout, &receiver, &QObject::deleteLater);
    return static_cast<bool>(connection);
}

// Skip the entire test (call from initTestCase) when this build's Qt can't
// deliver pointer-to-member signals; see qtPointerConnectWorks().
#define TR_QT_SKIP_UNLESS_SIGNALS_WORK() \
    do { \
        if (!qtPointerConnectWorks()) { \
            QSKIP( \
                "Qt pointer-to-member connect is non-functional in this build's Qt " \
                "(likely prebuilt with a compiler too old for it); skipping Qt runtime tests."); \
        } \
    } while (false)

class BasicTest
{
public:
    BasicTest()
    {
        trqt::trqt_init();
    }

    BasicTest(BasicTest&&) = delete;
    BasicTest(BasicTest const&) = delete;
    BasicTest& operator=(BasicTest&&) = delete;
    BasicTest& operator=(BasicTest const&) = delete;
    virtual ~BasicTest() = default;
};

class SandboxedTest : public BasicTest
{
public:
    SandboxedTest() = default;
    SandboxedTest(SandboxedTest&&) = delete;
    SandboxedTest(SandboxedTest const&) = delete;
    SandboxedTest& operator=(SandboxedTest&&) = delete;
    SandboxedTest& operator=(SandboxedTest const&) = delete;
    ~SandboxedTest() override = default;

    [[nodiscard]] bool isValid() const
    {
        return sandbox_.isValid();
    }

    [[nodiscard]] QString sandboxDir() const
    {
        return sandbox_.path();
    }

private:
    QTemporaryDir sandbox_{};
};
