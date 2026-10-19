// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#include <string>

#include <giomm/resource.h>

#include <gtest/gtest.h>

#include "Utils.h"

// The client adds its resources' icons/ to the icon theme and asks for its icon by name,
// so the embedded copy must sit there under that name, in the theme's directory layout.
TEST(ResourcesTest, embedsAppIconUnderItsName)
{
    auto const path = gtr_get_full_resource_path("icons/scalable/apps/" TR_GTK_ICON_NAME ".svg");
    EXPECT_TRUE(Gio::Resource::get_file_exists_global_nothrow(path)) << path;
}
