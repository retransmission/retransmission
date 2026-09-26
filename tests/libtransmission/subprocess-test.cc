// This file Copyright (C) 2017-2022 Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#include <array>
#include <cerrno>
#include <cstdlib> // setenv
#include <cstring> // strerror
#include <fstream>
#include <map>
#include <string>
#include <string_view>

#include <gtest/gtest.h>

#include <libtransmission/error.h>
#include <libtransmission/file.h>
#include <libtransmission/string-utils.h>
#include <libtransmission/subprocess.h>
#include <libtransmission/macros.h>

#include "test-fixtures.h"

#ifdef _WIN32
#include <windows.h>
#define setenv(key, value, unused) SetEnvironmentVariableA(key, value)
#else
#include <csignal>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace tr::test
{

namespace
{
std::string getTestProgramPath(std::string_view const filename)
{
    auto const exe_path = tr_sys_path_resolve(testing::internal::GetArgvs().front());
    auto const exe_dir = tr_sys_path_dirname(exe_path);
    return fmt::format("{:s}/{:s}", exe_dir, filename);
}

class SubprocessTest
    : public ::tr::test::TransmissionTest
    , public testing::WithParamInterface<std::string>
{
protected:
    Sandbox sandbox_;

    [[nodiscard]] std::string buildSandboxPath(std::string const& filename) const
    {
        auto path = fmt::format("{:s}/{:s}", sandbox_.path(), filename);
        tr_sys_path_native_separators(&path.front());
        return path;
    }

    [[nodiscard]] static std::string nativeCwd()
    {
        auto path = tr_sys_dir_get_current();
        tr_sys_path_native_separators(path.data());
        return path;
    }

    std::string const arg_dump_args_{ "--dump-args" };
    std::string const arg_dump_env_{ "--dump-env" };
    std::string const arg_dump_cwd_{ "--dump-cwd" };

    std::string self_path_;

    static void waitForFileToBeReadable(std::string const& path)
    {
        auto const test = [&path]() {
            return std::ifstream{ path, std::ios_base::in }.is_open();
        };
        EXPECT_TRUE(waitFor(test, 30000));
    }

    void SetUp() override
    {
        self_path_ = GetParam();
    }
};
} // namespace

TEST_P(SubprocessTest, SpawnAsyncMissingExec)
{
    auto const missing_exe_path = std::string{ TR_IF_WIN32("C:\\", "/") "tr-missing-test-exe" TR_IF_WIN32(".exe", "") };

    auto args = std::to_array<char const*>({ missing_exe_path.data(), nullptr });

    auto error = tr_error{};
    auto const ret = tr_spawn_async(std::data(args), {}, {}, &error);
    EXPECT_FALSE(ret);
    EXPECT_TRUE(error);
    EXPECT_NE(0, error.code());
    EXPECT_NE(""sv, error.message());
}

TEST_P(SubprocessTest, SpawnAsyncArgs)
{
    auto const result_path = buildSandboxPath("result.txt");
    bool const allow_batch_metachars = TR_IF_WIN32(!tr_strlower(self_path_).ends_with(".cmd"sv), true);

    auto const test_arg1 = std::string{ "arg1 " };
    auto const test_arg2 = std::string{ " arg2" };
    auto const test_arg3 = std::string{};
    auto const test_arg4 = std::string{ "\"arg3'^! $PATH %PATH% \\" };

    auto const args = std::to_array<char const*>({ self_path_.c_str(),
                                                   result_path.data(),
                                                   arg_dump_args_.data(),
                                                   test_arg1.data(),
                                                   test_arg2.data(),
                                                   test_arg3.data(),
                                                   allow_batch_metachars ? test_arg4.data() : nullptr,
                                                   nullptr });

    auto error = tr_error{};
    bool const ret = tr_spawn_async(std::data(args), {}, {}, &error);
    EXPECT_TRUE(ret) << args[0] << ' ' << args[1];
    EXPECT_FALSE(error) << error;

    waitForFileToBeReadable(result_path);

    auto in = std::ifstream{ result_path, std::ios_base::in };
    EXPECT_TRUE(in.is_open()) << strerror(errno);

    auto line = std::string{};
    EXPECT_TRUE(std::getline(in, line));
    EXPECT_EQ(test_arg1, line);

    EXPECT_TRUE(std::getline(in, line));
    EXPECT_EQ(test_arg2, line);

    EXPECT_TRUE(std::getline(in, line));
    EXPECT_EQ(test_arg3, line);

    if (allow_batch_metachars) {
        EXPECT_TRUE(std::getline(in, line));
        EXPECT_EQ(test_arg4, line);
    }

    EXPECT_FALSE(std::getline(in, line));
}

TEST_P(SubprocessTest, SpawnAsyncEnv)
{
    auto const result_path = buildSandboxPath("result.txt");

    auto const test_env_key1 = std::string{ "VAR1" };
    auto const test_env_key2 = std::string{ "_VAR_2_" };
    auto const test_env_key3 = std::string{ "vAr#" };
    auto const test_env_key4 = std::string{ "FOO" };
    auto const test_env_key5 = std::string{ "ZOO" };
    auto const test_env_key6 = std::string{ "TR_MISSING_TEST_ENV_KEY" };

    auto const test_env_value1 = std::string{ "value1 " };
    auto const test_env_value2 = std::string{ " value2" };
    auto const test_env_value3 = std::string{ " \"value3'^! $PATH %PATH% " };
    auto const test_env_value4 = std::string{ "bar" };
    auto const test_env_value5 = std::string{ "jar" };

    auto args = std::to_array<char const*>({
        self_path_.c_str(), //
        result_path.data(), //
        arg_dump_env_.data(), //
        test_env_key1.data(), //
        test_env_key2.data(), //
        test_env_key3.data(), //
        test_env_key4.data(), //
        test_env_key5.data(), //
        test_env_key6.data(), //
        nullptr, //
    });

    auto const env = std::map<std::string_view, std::string_view>{
        { test_env_key1, test_env_value1 },
        { test_env_key2, test_env_value2 },
        { test_env_key3, test_env_value3 },
        { test_env_key5, test_env_value5 },
    };

    setenv("FOO", "bar", 1 /*true*/); // inherited
    setenv("ZOO", "tar", 1 /*true*/); // overridden

    auto error = tr_error{};
    bool const ret = tr_spawn_async(std::data(args), env, {}, &error);
    EXPECT_TRUE(ret);
    EXPECT_FALSE(error) << error;

    waitForFileToBeReadable(result_path);

    auto in = std::ifstream{ result_path, std::ios_base::in };
    EXPECT_TRUE(in.is_open()) << strerror(errno);

    auto line = std::string{};
    EXPECT_TRUE(std::getline(in, line));
    EXPECT_EQ(test_env_value1, line);

    EXPECT_TRUE(std::getline(in, line));
    EXPECT_EQ(test_env_value2, line);

    EXPECT_TRUE(std::getline(in, line));
    EXPECT_EQ(test_env_value3, line);

    EXPECT_TRUE(std::getline(in, line));
    EXPECT_EQ(test_env_value4, line);

    EXPECT_TRUE(std::getline(in, line));
    EXPECT_EQ(test_env_value5, line);

    EXPECT_TRUE(std::getline(in, line));
    EXPECT_EQ("<null>"sv, line);

    EXPECT_FALSE(std::getline(in, line));
}

TEST_P(SubprocessTest, SpawnAsyncCwdExplicit)
{
    auto const test_dir = sandbox_.path();
    auto const result_path = buildSandboxPath("result.txt");

    auto const args = std::to_array<char const*>({ self_path_.c_str(), result_path.c_str(), arg_dump_cwd_.c_str(), nullptr });

    auto error = tr_error{};
    bool const ret = tr_spawn_async(std::data(args), {}, test_dir, &error);
    EXPECT_TRUE(ret);
    EXPECT_FALSE(error) << error;

    waitForFileToBeReadable(result_path);

    auto in = std::ifstream{ result_path, std::ios_base::in };
    EXPECT_TRUE(in.is_open()) << strerror(errno);

    auto line = std::string{};
    EXPECT_TRUE(std::getline(in, line));
    auto expected = std::string{ test_dir };
    tr_sys_path_native_separators(std::data(expected));
    auto actual = line;
    tr_sys_path_native_separators(std::data(actual));
    EXPECT_EQ(expected, actual);

    EXPECT_FALSE(std::getline(in, line));
}

TEST_P(SubprocessTest, SpawnAsyncCwdInherit)
{
    auto const result_path = buildSandboxPath("result.txt");
    auto const expected_cwd = nativeCwd();

    auto const args = std::to_array<char const*>({ self_path_.c_str(), result_path.data(), arg_dump_cwd_.data(), nullptr });

    auto error = tr_error{};
    auto const ret = tr_spawn_async(std::data(args), {}, {}, &error);
    EXPECT_TRUE(ret);
    EXPECT_FALSE(error) << error;

    waitForFileToBeReadable(result_path);

    auto in = std::ifstream{ result_path, std::ios_base::in };
    EXPECT_TRUE(in.is_open()) << strerror(errno);

    auto line = std::string{};
    EXPECT_TRUE(std::getline(in, line));
    auto actual = line;
    tr_sys_path_native_separators(std::data(actual));
    EXPECT_EQ(expected_cwd, actual);

    EXPECT_FALSE(std::getline(in, line));
}

TEST_P(SubprocessTest, SpawnAsyncCwdMissing)
{
    auto const result_path = buildSandboxPath("result.txt");

    auto const args = std::to_array<char const*>({ self_path_.c_str(), result_path.data(), arg_dump_cwd_.data(), nullptr });

    auto error = tr_error{};
    auto const ret = tr_spawn_async(std::data(args), {}, TR_IF_WIN32("C:\\", "/") "tr-missing-test-work-dir", &error);
    EXPECT_FALSE(ret);
    EXPECT_TRUE(error);
    EXPECT_NE(0, error.code());
    EXPECT_NE(""sv, error.message());
}

#ifndef _WIN32

namespace
{
void noopSignalHandler(int /*signum*/)
{
}
} // namespace

TEST_P(SubprocessTest, SpawnAsyncKeepsSigchldHandler)
{
    struct sigaction action{};
    action.sa_handler = &noopSignalHandler;
    sigemptyset(&action.sa_mask);
    struct sigaction saved{};
    ASSERT_EQ(0, sigaction(SIGCHLD, &action, &saved)) << tr_strerror(errno);

    auto const result_path = buildSandboxPath("result.txt");
    auto const args = std::to_array<char const*>({ self_path_.c_str(), result_path.c_str(), arg_dump_cwd_.c_str(), nullptr });

    auto error = tr_error{};
    EXPECT_TRUE(tr_spawn_async(std::data(args), {}, {}, &error)) << error;
    waitForFileToBeReadable(result_path);

    struct sigaction current{};
    EXPECT_EQ(0, sigaction(SIGCHLD, nullptr, &current)) << tr_strerror(errno);
    EXPECT_EQ(&noopSignalHandler, current.sa_handler);

    EXPECT_EQ(0, sigaction(SIGCHLD, &saved, nullptr)) << tr_strerror(errno);
}

TEST_P(SubprocessTest, SpawnAsyncLeavesOtherChildrenAlone)
{
    auto const other_pid = fork();
    ASSERT_NE(-1, other_pid) << tr_strerror(errno);
    if (other_pid == 0) {
        _exit(42);
    }

    // Wait for it to exit, but leave it for the final waitpid() to reap.
    auto info = siginfo_t{};
    ASSERT_EQ(0, waitid(P_PID, static_cast<id_t>(other_pid), &info, WEXITED | WNOWAIT)) << tr_strerror(errno);

    auto const result_path = buildSandboxPath("result.txt");
    auto const args = std::to_array<char const*>({ self_path_.c_str(), result_path.c_str(), arg_dump_cwd_.c_str(), nullptr });

    auto error = tr_error{};
    EXPECT_TRUE(tr_spawn_async(std::data(args), {}, {}, &error)) << error;
    waitForFileToBeReadable(result_path);

    // Run any SIGCHLD handler now, so one that reaps other children has done so before the check.
    ASSERT_EQ(0, raise(SIGCHLD));

    auto status = int{};
    ASSERT_EQ(other_pid, waitpid(other_pid, &status, 0)) << tr_strerror(errno);
    ASSERT_TRUE(WIFEXITED(status));
    EXPECT_EQ(42, WEXITSTATUS(status));
}

TEST_P(SubprocessTest, SpawnAsyncLeavesNoChild)
{
    auto const result_path = buildSandboxPath("result.txt");
    auto const args = std::to_array<char const*>({ self_path_.c_str(), result_path.c_str(), arg_dump_cwd_.c_str(), nullptr });

    // Spawn from a helper process: it starts with no children, so other tests' children can't interfere.
    // The helper blocks SIGCHLD, so no handler can reap the spawned program before the check.
    auto const helper_pid = fork();
    ASSERT_NE(-1, helper_pid) << tr_strerror(errno);
    if (helper_pid == 0) {
        auto mask = sigset_t{};
        sigemptyset(&mask);
        sigaddset(&mask, SIGCHLD);
        if (sigprocmask(SIG_BLOCK, &mask, nullptr) != 0) {
            _exit(2);
        }

        if (!tr_spawn_async(std::data(args), {}, {}, nullptr)) {
            _exit(3);
        }

        if (!waitFor([&result_path]() { return tr_sys_path_exists(result_path); }, 30000)) {
            _exit(4);
        }

        _exit(waitpid(-1, nullptr, WNOHANG) == -1 && errno == ECHILD ? 0 : 1);
    }

    auto status = int{};
    ASSERT_EQ(helper_pid, waitpid(helper_pid, &status, 0)) << tr_strerror(errno);
    ASSERT_TRUE(WIFEXITED(status));
    EXPECT_EQ(0, WEXITSTATUS(status)) << "1: the helper still has a child, 2: sigprocmask() failed, "
                                         "3: tr_spawn_async() failed, 4: no result file";
}

#endif

INSTANTIATE_TEST_SUITE_P(Subprocess,
                         SubprocessTest,
                         TR_IF_WIN32(::testing::Values( //
                                         getTestProgramPath("subprocess-test.exe"),
                                         getTestProgramPath("subprocess-test.cmd")),
                                     ::testing::Values( //
                                         getTestProgramPath("subprocess-test"))));

} // namespace tr::test
