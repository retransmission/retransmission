// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#include <array>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <fmt/format.h>

#include "libtransmission/macros.h"
#include "libtransmission/tr-getopt.h"
#include "libtransmission/utils.h" // tr_main
#include "libtransmission/version.h"

#include <libtransmission-app/app.h>
#include <libtransmission-app/interop.h>
#include <libtransmission-app/startup-coordinator.h>

#include "Application.h"
#include "Prefs.h"
#include "RpcClient.h"
#include "Transports.h"
#include "Utils.h"

using namespace std::string_view_literals;

#define MY_NAME TR_PROJ_APPNAME "-qt"

namespace
{

char const* const DisplayName = MY_NAME;

auto constexpr FileArgsSeparator = "--"sv;
auto constexpr QtArgsSeparator = "---"sv;

using Arg = tr_option::Arg;
auto constexpr Opts = std::to_array<tr_option>({
    {
        .val = 'g',
        .longName = "config-dir",
        .description = "Where to look for configuration files",
        .shortName = "g",
        .arg = Arg::Required,
        .argName = "<path>",
    },
    {
        .val = 'm',
        .longName = "minimized",
        .description = "Start minimized in system tray",
        .shortName = "m",
        .arg = Arg::None,
        .argName = nullptr,
    },
    {
        .val = 'p',
        .longName = "port",
        .description = "Port to use when connecting to an existing session",
        .shortName = "p",
        .arg = Arg::Required,
        .argName = "<port>",
    },
    {
        .val = 'r',
        .longName = "remote",
        .description = "Connect to an existing session at the specified hostname",
        .shortName = "r",
        .arg = Arg::Required,
        .argName = "<host>",
    },
    {
        .val = 'u',
        .longName = "username",
        .description = "Username to use when connecting to an existing session",
        .shortName = "u",
        .arg = Arg::Required,
        .argName = "<username>",
    },
    {
        .val = 'v',
        .longName = "version",
        .description = "Show version number and exit",
        .shortName = "v",
        .arg = Arg::None,
        .argName = nullptr,
    },
    {
        .val = 'w',
        .longName = "password",
        .description = "Password to use when connecting to an existing session",
        .shortName = "w",
        .arg = Arg::Required,
        .argName = "<password>",
    },
    {
        .val = 0,
        .longName = nullptr,
        .description = nullptr,
        .shortName = nullptr,
        .arg = Arg::None,
        .argName = nullptr,
    },
});
static_assert(Opts[std::size(Opts) - 2].val != 0);
} // namespace

namespace
{
char const* getUsage()
{
    return "Usage:\n"
           "  " MY_NAME " [options...] [[--] torrent files...] [--- Qt options...]";
}

// The launch's torrent arguments as plain strings, for tr::interop::encode_metainfo_args().
[[nodiscard]] std::vector<std::string> argumentsOf(QStringList const& filenames)
{
    auto args = std::vector<std::string>{};
    args.reserve(std::size(filenames));

    for (auto const& filename : filenames) {
        args.push_back(filename.toStdString());
    }

    return args;
}
} // namespace

int tr_main(int argc, char** argv)
{
    tr::app::init();

    // parse the command-line arguments
    bool minimized = false;
    QString host;
    QString port;
    QString username;
    QString password;
    QString config_dir;
    QStringList filenames;

    int opt = 0;
    char const* arg = nullptr;
    int file_args_start_idx = -1;
    int qt_args_start_idx = -1;
    while (
        file_args_start_idx < 0 && qt_args_start_idx < 0 &&
        (opt = tr_getopt(getUsage(), argc, static_cast<char const* const*>(argv), std::data(Opts), &arg)) != TR_OPT_DONE) {
        switch (opt) {
        case 'g':
            config_dir = QString::fromUtf8(arg);
            break;

        case 'p':
            port = QString::fromUtf8(arg);
            break;

        case 'r':
            host = QString::fromUtf8(arg);
            break;

        case 'u':
            username = QString::fromUtf8(arg);
            break;

        case 'w':
            password = QString::fromUtf8(arg);
            break;

        case 'm':
            minimized = true;
            break;

        case 'v':
            fmt::print("{:s} {:s}\n", DisplayName, LONG_VERSION_STRING);
            return 0;

        case TR_OPT_ERR:
            fmt::print(stderr, "Invalid option\n");
            tr_getopt_usage(DisplayName, getUsage(), std::data(Opts));
            return 1;

        default:
            if (arg == FileArgsSeparator) {
                file_args_start_idx = tr_optind;
            } else if (arg == QtArgsSeparator) {
                qt_args_start_idx = tr_optind;
            } else {
                filenames.append(QString::fromUtf8(arg));
            }

            break;
        }
    }

    if (file_args_start_idx >= 0) {
        for (int i = file_args_start_idx; i < argc; ++i) {
            if (argv[i] == QtArgsSeparator) {
                qt_args_start_idx = i + 1;
                break;
            }

            filenames.push_back(QString::fromUtf8(argv[i]));
        }
    }

    // Resolve the config dir before asking whether a client is already running. The answer
    // is per config dir, and two clients on different dirs are separate instances,
    // not duplicates.
    if (config_dir.isNull()) {
        config_dir = QString::fromStdString(tr::platform::get_default_config_dir(TR_PROJ_SHARED_CONFIG_DIRNAME));
    }

    // Spell the dir the way every client does,
    // so that `-g dir`, `-g dir/` and a symlink to it all name one instance.
    auto const config_dir_str = tr::interop::canonical_config_dir_created(config_dir.toStdString());
    config_dir = Utils::qstringFromUtf8(config_dir_str);
    auto startup_coordinator = std::make_unique<tr::interop::StartupCoordinator>(
        config_dir_str,
        tr::interop::make_transport(config_dir));
    // -r/-p/-u/-w name a session on another host, which is not this config dir's instance at all.
    // -m asks for a window that opens minimized, and presenting one would do the opposite.
    // These are read into prefs further down, past the point a handed-over launch returns from.
    auto const standalone = minimized || !host.isNull() || !port.isNull() || !username.isNull() || !password.isNull();

    auto const intent = tr::interop::intent_of(standalone, !filenames.isEmpty());

    if (auto const exit_code = startup_coordinator->delegate(
            intent,
            [&filenames] { return tr::interop::encode_metainfo_args(argumentsOf(filenames)); },
            tr::interop::activation_token());
        exit_code) {
        return *exit_code;
    }

    auto prefs = Prefs{ config_dir };

    if (!host.isNull()) {
        prefs.set(TR_KEY_remote_session_host, host);
    }

    if (!port.isNull()) {
        prefs.set(TR_KEY_remote_session_port, port.toUInt());
    }

    if (!username.isNull()) {
        prefs.set(TR_KEY_remote_session_username, username);
    }

    if (!password.isNull()) {
        prefs.set(TR_KEY_remote_session_password, password);
    }

    if (!host.isNull() || !port.isNull() || !username.isNull() || !password.isNull()) {
        prefs.set(TR_KEY_remote_session_enabled, true);
    }

    if (prefs.get<bool>(TR_KEY_start_minimized)) {
        minimized = true;
    }

    // start as minimized only if the system tray present
    if (!prefs.get<bool>(TR_KEY_show_notification_area_icon)) {
        minimized = false;
    }

    auto qt_argv = std::vector<char*>{ argv[0] };
    if (qt_args_start_idx >= 0) {
        qt_argv.insert(qt_argv.end(), &argv[qt_args_start_idx], &argv[argc]);
    }

    // run the app
    auto qt_argc = static_cast<int>(std::size(qt_argv));
    auto rpc = RpcClient{};
    auto app = Application{
        prefs, rpc, std::move(startup_coordinator), minimized, config_dir, filenames, qt_argc, std::data(qt_argv)
    };

    if (app.configDirIsContended()) {
        return tr::interop::report_config_dir_busy(config_dir_str);
    }

    auto const ret = QApplication::exec();

    // A launch that starts its session from the connection dialog only finds out here that the dir is held.
    if (app.configDirIsContended()) {
        return tr::interop::report_config_dir_busy(config_dir_str);
    }

    // save prefs before exiting
    prefs.save(config_dir.toStdString(), app.embedded_session_settings());

    return ret;
}
