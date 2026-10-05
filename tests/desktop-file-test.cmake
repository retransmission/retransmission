# This file Copyright © Mnemosaic LLC.
# It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
# or any future license endorsed by Mnemosaic LLC.
# License text can be found in the licenses/ folder.

# Checks the promises a client's desktop entry makes to a desktop: the name and icon it shows,
# the id a compositor matches a window against, the program it starts,
# and what a click on a torrent hands over.
# No test can ask the client about these, because they live in a file the desktop reads,
# so a wrong value shows up only as a missing icon or launcher, or as a torrent that opens nothing.
#
# -Ddesktop_file=  the generated desktop entry
# -Dapp_name=      the app's name, which the entry's Name must start with
# -Dicon_name=     the name the client installs its icon under
# -Dexe=           the program the build makes, which every Exec and TryExec must name
# -Dapp_id=        the StartupWMClass the entry must carry; leave it out to skip that check
# -Dvalidator=     desktop-file-validate, if found

# An empty value would let an unsubstituted or missing key pass a check below.
foreach(arg IN ITEMS app_name icon_name exe)
    if("${${arg}}" STREQUAL "")
        message(FATAL_ERROR "nothing to check against; the caller must pass -D${arg}=")
    endif()
endforeach()
if(DEFINED app_id AND "${app_id}" STREQUAL "")
    message(FATAL_ERROR "-Dapp_id= is empty; pass a value or leave the argument out")
endif()

set(file "${desktop_file}")
if(NOT EXISTS "${file}")
    message(FATAL_ERROR "found no desktop file at '${file}'")
endif()

file(READ "${file}" contents)

set(failures "")

# The entry spells out the app's name for gettext to translate,
# so a rename in macros.h has to reach this file too.
if(NOT "\n${contents}" MATCHES "\nName=([^\n]*)")
    string(APPEND failures "  no Name key\n")
else()
    string(FIND "${CMAKE_MATCH_1}" "${app_name}" pos)
    if(NOT pos EQUAL 0)
        string(APPEND failures "  Name is '${CMAKE_MATCH_1}', which doesn't start with '${app_name}'\n")
    endif()
endif()

# A desktop finds the icon by this name in the installed icon themes.
# desktop-file-validate accepts any value here, even an empty one.
if(NOT "\n${contents}" MATCHES "\nIcon=([^\n]*)")
    string(APPEND failures "  no Icon key\n")
elseif(NOT CMAKE_MATCH_1 STREQUAL "${icon_name}")
    string(APPEND failures "  Icon is '${CMAKE_MATCH_1}', not '${icon_name}'\n")
endif()

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

# The two things a click can hand over, then the application id
# a Wayland compositor matches windows against to find this file and its icon.
set(needles "application/x-bittorrent" "x-scheme-handler/magnet")
if(DEFINED app_id)
    list(APPEND needles "StartupWMClass=${app_id}")
endif()
foreach(needle IN LISTS needles)
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
