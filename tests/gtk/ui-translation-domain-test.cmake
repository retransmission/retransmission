# This file Copyright © Mnemosaic LLC.
# It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
# or any future license endorsed by Mnemosaic LLC.
# License text can be found in the licenses/ folder.

# Checks that no GTK .ui file names its own translation domain.
# A domain attribute overrides the domain the client binds at startup,
# so when the two names differ, that file's strings stay untranslated.
# GUI builders add the attribute whenever a project sets a domain.

if(NOT IS_DIRECTORY "${ui_dir}")
    message(FATAL_ERROR "found no directory at '${ui_dir}'; the caller must pass -Dui_dir=")
endif()

file(GLOB_RECURSE ui_files "${ui_dir}/*.ui")
if(NOT ui_files)
    message(FATAL_ERROR "found no .ui files under '${ui_dir}'")
endif()

set(failures "")
foreach(file IN LISTS ui_files)
    file(READ "${file}" contents)
    # Any element, because GtkBuilder also takes a domain on <menu>.
    if(contents MATCHES "<[^>]*[ \t\r\n]domain=")
        string(APPEND failures "  ${file}\n")
    endif()
endforeach()

if(NOT failures STREQUAL "")
    message(FATAL_ERROR "these .ui files name a translation domain instead of using the client's:\n${failures}")
endif()

list(LENGTH ui_files count)
message(STATUS "ok: ${count} .ui files use the client's translation domain")
