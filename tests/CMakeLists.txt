include(TrGTest)

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

add_custom_target(all-tests)

set_property(
    TARGET all-tests
    PROPERTY FOLDER "tests")

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
