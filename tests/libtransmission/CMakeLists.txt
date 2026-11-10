include(GoogleTest)

add_executable(libtransmission-test)

target_sources(libtransmission-test
    PRIVATE
        announce-list-test.cc
        announcer-test.cc
        announcer-udp-test.cc
        api-compat-test.cc
        benc-test.cc
        bep55-holepunch-test.cc
        bep55-introducer-store-test.cc
        bep55-test-utils.h
        bitfield-test.cc
        block-info-test.cc
        blocklist-download-test.cc
        blocklist-test.cc
        buffer-test.cc
        clients-test.cc
        completion-test.cc
        config-dir-lock-test.cc
        copy-test.cc
        crypto-test.cc
        dht-test.cc
        error-test.cc
        env-test.cc
        fake-timer.h
        file-piece-map-test.cc
        file-utils-test.cc
        file-test.cc
        getopt-test.cc
        handshake-test.cc
        history-test.cc
        inout-test.cc
        ip-cache-test.cc
        json-test.cc
        local-data-test.cc
        lpd-test.cc
        magnet-metainfo-test.cc
        makemeta-test.cc
        move-test.cc
        net-test.cc
        open-files-test.cc
        peer-mgr-wishlist-test.cc
        peer-msgs-test.cc
        platform-test.cc
        quark-test.cc
        remove-test.cc
        rename-test.cc
        resume-test.cc
        rpc-test.cc
        serializer-tests.cc
        session-alt-speeds-test.cc
        session-test.cc
        settings-fixtures.h
        settings-test.cc
        shared-string-test.cc
        strbuf-test.cc
        string-utils-test.cc
        subprocess-test-script.cmd
        subprocess-test.cc
        test-fixtures.h
        timer-test.cc
        torrent-builder-test.cc
        torrent-disk-io-test.cc
        torrent-files-test.cc
        torrent-magnet-test.cc
        torrent-metainfo-test.cc
        torrent-queue-test.cc
        torrent-test.cc
        torrents-test.cc
        tr-peer-info-test.cc
        types-test.cc
        utils-test.cc
        values-test.cc
        variant-test.cc
        watchdir-test.cc
        web-test.cc
        web-utils-test.cc
        wildmat-test.cc)

if(APPLE)
    target_sources(libtransmission-test
        PRIVATE
            utils-apple-test.mm)
endif()

set_property(
    TARGET libtransmission-test
    PROPERTY FOLDER "tests")

# Where the build installs the web client, relative to the data dir.
# The platform test lays out a data dir the same way and expects the lookup to find it.
get_filename_component(WEB_CLIENT_DIR_NAME ${TR_WEB_ASSETS} NAME)
file(RELATIVE_PATH WEB_CLIENT_INSTALL_SUBDIR ${CMAKE_INSTALL_FULL_DATADIR} ${TR_DATA_INSTALL_FULL_DIR}/${WEB_CLIENT_DIR_NAME})

target_compile_definitions(libtransmission-test
    PRIVATE
        -DLIBTRANSMISSION_TEST_ASSETS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets"
        LIBTRANSMISSION_TEST_WEB_CLIENT_INSTALL_SUBDIR="${WEB_CLIENT_INSTALL_SUBDIR}"
        __TRANSMISSION__)

target_compile_options(libtransmission-test
    PRIVATE $<$<OR:$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>>:-Wno-sign-compare>) # patches welcomed

target_link_libraries(libtransmission-test
    PRIVATE
        ${TR_NAME}
        GTest::gtest_main
        dht::dht
        libarchive::libarchive
        transmission::fmt-header-only
        libevent::core
        libevent::extra
        wildmat)

tr_setup_gtest_target(libtransmission-test "LT.")

# Re-run the disk-IO tests on tr::LocalData's threaded backend.
#
# A suite belongs here only if it drives a tr::LocalData op through a
# tr_session. A suite that issues no such op re-runs byte-identical, and
# the LocalData unit tests pick their backend themselves, so neither would
# gain anything from being listed.
if(NOT CMAKE_CROSSCOMPILING OR CMAKE_CROSSCOMPILING_EMULATOR)
    set(DISK_IO_SUITES *IncompleteDirTest.*:MoveTest.*:RenameTest.*:RpcTest.*:UploadTest.*)

    # Without threads: the session thread does the workers' and the admin
    # lanes' work in an order that TR_LOCAL_DATA_SHUFFLE seeds. Completions
    # arrive late and out of order, as the ordering contract allows, and a
    # failure replays under its seed. Each seed is a different schedule.
    foreach(SEED 1 2 3)
        if(SEED EQUAL 1)
            set(TEST_NAME LT.ShuffledDiskIo)
        else()
            set(TEST_NAME LT.ShuffledDiskIo.seed${SEED})
        endif()

        add_test(
            NAME ${TEST_NAME}
            COMMAND libtransmission-test --gtest_filter=${DISK_IO_SUITES})

        set_tests_properties(${TEST_NAME}
            PROPERTIES
                ENVIRONMENT TR_LOCAL_DATA_SHUFFLE=${SEED})
    endforeach()

    # ...and once more on real threads.
    add_test(
        NAME LT.ThreadedDiskIo
        COMMAND libtransmission-test --gtest_filter=${DISK_IO_SUITES})

    set_tests_properties(LT.ThreadedDiskIo
        PROPERTIES
            ENVIRONMENT TR_LOCAL_DATA_WORKERS=2)
endif()

add_custom_command(
    TARGET libtransmission-test
    PRE_BUILD
    COMMAND
        ${CMAKE_COMMAND}
        -E copy_if_different
        ${CMAKE_CURRENT_SOURCE_DIR}/subprocess-test-script.cmd
        $<TARGET_FILE_DIR:libtransmission-test>/subprocess-test.cmd)

add_executable(subprocess-test)

target_sources(subprocess-test
    PRIVATE
        subprocess-test-program.cc)

set_property(
    TARGET subprocess-test
    PROPERTY FOLDER "tests")

target_link_libraries(subprocess-test
    PRIVATE
        ${TR_NAME})

add_dependencies(libtransmission-test
    subprocess-test)
