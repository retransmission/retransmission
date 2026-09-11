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

# ---

include(GoogleTest)

# One gtest executable per source file, linked against the client library.
# A test that loads the client's UI must also add the resources the
# executable in gtk/ registers, with tr_target_glib_resources().
function(add_trgtk_test source)
    get_filename_component(name_no_ext ${source} NAME_WE)
    string(REGEX REPLACE "-test$" "" test_name ${name_no_ext})
    set(target gtk-test-${test_name})

    add_executable(${target} ${source} test-fixtures.h)
    target_link_libraries(${target}
        PRIVATE
            ${TR_NAME}-gtk-lib
            GTest::gtest_main)
    set_property(TARGET ${target} PROPERTY FOLDER "tests")

    tr_setup_gtest_target(${target} "GTK.")
    add_dependencies(all-tests ${target})
endfunction()

add_trgtk_test(serializer-test.cc)
