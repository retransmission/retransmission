include(TrGTest)

# Everything a `ctest` run needs, for builds that skip the main `all` target.
# Defined before the subdirectories so each can add what its tests run.
add_custom_target(all-tests)

set_property(
    TARGET all-tests
    PROPERTY FOLDER "tests")

add_subdirectory(libtransmission)
add_subdirectory(libtransmission-app)
if(ENABLE_GTK)
    add_subdirectory(gtk)
endif()
if(ENABLE_QT)
    add_subdirectory(qt)
endif()
add_subdirectory(interop)
if(ENABLE_UTILS)
    add_subdirectory(utils)
endif()

if(TARGET libtransmission-test)
    add_dependencies(all-tests libtransmission-test)
endif()

add_dependencies(all-tests libtransmission-app-test)

if(TARGET qt-tests)
    add_dependencies(all-tests qt-tests)
endif()

if(TARGET transmission-show)
    add_dependencies(all-tests transmission-show)
endif()

if(TARGET transmission-remote-test)
    add_dependencies(all-tests transmission-remote-test)
endif()
