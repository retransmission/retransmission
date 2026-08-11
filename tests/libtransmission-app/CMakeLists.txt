include(GoogleTest)

add_executable(libtransmission-app-test)

target_sources(libtransmission-app-test
	PRIVATE
		converter-tests.cc
		prefs-test.cc
		rpc-client-test.cc
		rpc-queue-test.cc
		session-test.cc
		test-fixtures.h)

set_property(
	TARGET libtransmission-app-test
	PROPERTY FOLDER "tests")

target_link_libraries(libtransmission-app-test
	PRIVATE
		${TR_NAME}-app
		${TR_NAME}
		GTest::gtest_main
		transmission::fmt-header-only
		libevent::core
		libevent::extra)

tr_setup_gtest_target(libtransmission-app-test "LTA.")
