include(GoogleTest)

add_executable(libtransmission-app-test)

target_sources(libtransmission-app-test
	PRIVATE
		converter-tests.cc
		dbus-peer-record-test.cc
		interop-test.cc
		prefs-test.cc
		rpc-client-test.cc
		rpc-queue-test.cc
		session-test.cc
		test-fixtures.h)

set_property(
	TARGET libtransmission-app-test
	PROPERTY FOLDER "tests")

# These tests reach libtransmission internals: the heavier sandbox fixture from
# tests/libtransmission/, and headers such as net.h that guard on this macro.
target_compile_definitions(libtransmission-app-test
	PRIVATE
		-DLIBTRANSMISSION_TEST_ASSETS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../libtransmission/assets"
		__TRANSMISSION__)

target_link_libraries(libtransmission-app-test
	PRIVATE
		${TR_NAME}-app
		${TR_NAME}
		GTest::gtest_main
		transmission::fmt-header-only
		dht::dht
		libevent::core
		libevent::extra)

tr_setup_gtest_target(libtransmission-app-test "LTA.")
