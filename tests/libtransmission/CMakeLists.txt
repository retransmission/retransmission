include(GoogleTest)

add_executable(libtransmission-test)

target_sources(libtransmission-test
    PRIVATE
        announce-list-test.cc
        announcer-test.cc
        announcer-udp-test.cc
        api-compat-test.cc
        benc-test.cc
        bitfield-test.cc
        block-info-test.cc
        blocklist-download-test.cc
        blocklist-test.cc
        buffer-test.cc
        clients-test.cc
        completion-test.cc
        copy-test.cc
        crypto-test.cc
        dht-test.cc
        error-test.cc
        env-test.cc
        file-piece-map-test.cc
        file-utils-test.cc
        file-test.cc
        getopt-test.cc
        handshake-test.cc
        history-test.cc
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

target_compile_definitions(libtransmission-test
    PRIVATE
        -DLIBTRANSMISSION_TEST_ASSETS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets"
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
        WideInteger::WideInteger
        wildmat)

tr_setup_gtest_target(libtransmission-test "LT.")

# Re-run the disk-IO tests with tr::LocalData delivering its completions
# late and out of order, which its ordering contract allows.
#
# A suite belongs here only if it drives a tr::LocalData op through a
# tr_session. A suite that issues no such op re-runs byte-identical, and
# the LocalData unit tests select the mode themselves, so neither would
# gain anything from being listed.
if(NOT CMAKE_CROSSCOMPILING OR CMAKE_CROSSCOMPILING_EMULATOR)
    add_test(
        NAME LT.ShuffledDiskIo
        COMMAND libtransmission-test --gtest_filter=*IncompleteDirTest.*)

    set_tests_properties(LT.ShuffledDiskIo
        PROPERTIES
            ENVIRONMENT TR_LOCAL_DATA_SHUFFLE=1)
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
