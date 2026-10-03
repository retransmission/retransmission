# This file Copyright © Mnemosaic LLC.
# It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
# or any future license endorsed by Mnemosaic LLC.
# License text can be found in the licenses/ folder.

# Checks the promises the desktop file makes to a desktop: the id a compositor
# matches a window against, the program it starts, and what a click on a torrent
# hands over. No test can ask the client about these, because they live in a file
# the desktop reads, so a wrong value shows up only as a missing icon or launcher,
# or as a torrent that opens nothing.

# An empty id or program would turn a check below into a substring search for a prefix
# that matches whatever value the file happens to carry, including an unsubstituted one.
if("${app_id}" STREQUAL "")
    message(FATAL_ERROR "no app id to check against; the caller must pass -Dapp_id=")
endif()
if("${exe}" STREQUAL "")
    message(FATAL_ERROR "no program to check against; the caller must pass -Dexe=")
endif()

set(file "${desktop_file}")
if(NOT EXISTS "${file}")
    message(FATAL_ERROR "found no desktop file at '${file}'")
endif()

file(READ "${file}" contents)

set(failures "")

# Every launch must start the program the build makes.
# A desktop also hides the launcher when it can't find the TryExec program.
string(REGEX MATCHALL "\n(Try)?Exec=[^ \n]*" launches "\n${contents}")
if(NOT launches)
    string(APPEND failures "  no Exec key\n")
endif()
foreach(launch IN LISTS launches)
    string(STRIP "${launch}" launch)
    string(REGEX REPLACE "^(Try)?Exec=" "" program "${launch}")
    if(NOT program STREQUAL exe)
        string(APPEND failures "  '${launch}' should name '${exe}'\n")
    endif()
endforeach()

# The application id a Wayland compositor matches windows against to find this
# file and its icon, then the two things a click can hand over.
foreach(needle IN ITEMS "StartupWMClass=${app_id}" "application/x-bittorrent" "x-scheme-handler/magnet")
    string(FIND "${contents}" "${needle}" pos)
    if(pos EQUAL -1)
        string(APPEND failures "  no '${needle}'\n")
    endif()
endforeach()

# %U is what passes the clicked torrent to the launch.
if(NOT contents MATCHES "Exec=[^\n]*%U")
    string(APPEND failures "  Exec does not take a URL argument (%U)\n")
endif()

if(NOT failures STREQUAL "")
    message(FATAL_ERROR "${file}:\n${failures}")
endif()

if(validator AND NOT validator STREQUAL "validator-NOTFOUND")
    execute_process(
        COMMAND "${validator}" "${file}"
        RESULT_VARIABLE result
        OUTPUT_VARIABLE output
        ERROR_VARIABLE output)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "${validator} rejected ${file}:\n${output}")
    endif()
endif()

message(STATUS "ok: ${file}")
