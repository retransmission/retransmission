find_program(DESKTOP_FILE_VALIDATE desktop-file-validate)

# Gettext builds the desktop file as part of the client, so `all-tests` must build it.
add_dependencies(all-tests ${TR_NAME}-gtk)

# The desktop file is data a desktop reads, so no client test can reach it.
add_test(
    NAME GTK.desktop-file
    COMMAND ${CMAKE_COMMAND}
        -Ddesktop_file=${CMAKE_BINARY_DIR}/gtk/${TR_NAME}-gtk.desktop
        -Dapp_id=${TR_GTK_APP_ID}
        -Dvalidator=${DESKTOP_FILE_VALIDATE}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/desktop-file-test.cmake)
