// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#include <array>
#include <string_view>
#include <vector>

#include <fmt/format.h>

#include "libtransmission/macros.h"
#include "libtransmission/tr-getopt.h"
#include "libtransmission/utils.h" // tr_main
#include "libtransmission/version.h"

#include <libtransmission-app/app.h>

#include "Application.h"
#include "InteropHelper.h"
#include "Prefs.h"
#include "RpcClient.h"

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

bool tryDelegate(QStringList const& filenames)
{
    InteropHelper const interop_client;
    if (!interop_client.isConnected()) {
        return false;
    }

    bool delegated = false;

    for (auto const& filename : filenames) {
        auto const add_data = AddData(filename);
        QString metainfo;

        switch (add_data.type) {
        case AddData::URL:
            metainfo = add_data.url.toString();
            break;

        case AddData::MAGNET:
            metainfo = add_data.magnet;
            break;

        case AddData::FILENAME:
        case AddData::METAINFO:
            metainfo = QString::fromUtf8(add_data.toBase64());
            break;

        default:
            break;
        }

        if (!metainfo.isEmpty() && interop_client.addMetainfo(metainfo)) {
            delegated = true;
        }
    }

    return delegated;
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

    InteropHelper::initialize();

    // if there's another copy of the app running, delegate work to it and exit
    if (tryDelegate(filenames)) {
        return 0;
    }

    // set the fallback config dir
    if (config_dir.isNull()) {
        config_dir = QString::fromStdString(tr::platform::get_default_config_dir(TR_PROJ_APPNAME));
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
    auto const app = Application{ prefs, rpc, minimized, config_dir, filenames, qt_argc, std::data(qt_argv) };
    auto const ret = QApplication::exec();

    // save prefs before exiting
    prefs.save(config_dir.toStdString(), app.local_session_settings());

    return ret;
}
