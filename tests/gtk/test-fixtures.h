// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include "../libtransmission-app/test-fixtures.h"

#include "Prefs.h"

// A `SandboxedTest` whose prefs read and write the sandbox directory.
//
// The client loads its settings once per process, on first use, from
// whichever directory `gtr_pref_init()` named at that moment; later
// tests in the same executable keep that map and only redirect saves.
class GtkTest : public SandboxedTest
{
protected:
    void SetUp() override
    {
        SandboxedTest::SetUp();
        gtr_pref_init(sandbox_dir());
    }
};
