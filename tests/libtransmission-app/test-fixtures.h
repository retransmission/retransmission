// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <cstdlib> // std::getenv()
#include <string>
#include <string_view>

#include <fmt/format.h>

#include <gtest/gtest.h>

#include <libtransmission/file.h>
#include <libtransmission/macros.h>

#include "libtransmission-app/app.h"

class TransmissionTest : public ::testing::Test
{
protected:
    static void SetUpTestSuite()
    {
        tr::app::init();
    }
};

// Creates a unique temporary directory on construction and recursively removes
// it on destruction. A lightweight counterpart to the sandbox helper in
// tests/libtransmission/ that avoids that fixture's heavier dependencies.
class Sandbox
{
public:
    Sandbox()
        : path_{ create_sandbox(get_default_parent_dir(), TR_PROJ_APPNAME "-app-test-XXXXXX") }
    {
    }

    ~Sandbox()
    {
        rimraf(path_);
    }

    Sandbox(Sandbox const&) = delete;
    Sandbox(Sandbox&&) = delete;
    Sandbox& operator=(Sandbox const&) = delete;
    Sandbox& operator=(Sandbox&&) = delete;

    [[nodiscard]] std::string const& path() const noexcept
    {
        return path_;
    }

private:
    [[nodiscard]] static std::string get_default_parent_dir()
    {
        if (auto const* const tmpdir = std::getenv("TMPDIR"); tmpdir != nullptr) {
            return tmpdir;
        }

        return tr_sys_dir_get_current();
    }

    [[nodiscard]] static std::string create_sandbox(std::string_view const parent_dir, std::string_view const tmpl)
    {
        auto path = fmt::format("{:s}/{:s}", tr_sys_path_resolve(parent_dir), tmpl);
        tr_sys_dir_create_temp(std::data(path));
        tr_sys_path_native_separators(std::data(path));
        return path;
    }

    static void rimraf(std::string_view const path)
    {
        if (auto const info = tr_sys_path_get_info(path); info && info->isFolder()) {
            if (auto const dir = tr_sys_dir_open(path); dir != TR_BAD_SYS_DIR) {
                for (char const* name = nullptr; (name = tr_sys_dir_read_name(dir)) != nullptr;) {
                    if (auto const name_sv = std::string_view{ name }; name_sv != "." && name_sv != "..") {
                        rimraf(fmt::format("{:s}/{:s}", path, name));
                    }
                }
                tr_sys_dir_close(dir);
            }
        }

        tr_sys_path_remove(path);
    }

    std::string const path_;
};

// A `TransmissionTest` that additionally owns a per-test `Sandbox` directory.
class SandboxedTest : public TransmissionTest
{
protected:
    [[nodiscard]] std::string const& sandbox_dir() const noexcept
    {
        return sandbox_.path();
    }

private:
    Sandbox sandbox_;
};
