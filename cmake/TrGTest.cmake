include(TrMacros)

option(USE_SYSTEM_GTEST "Use system GTest library" OFF)

tr_add_external_auto_library(GTEST GTest
    SUBPROJECT
    SOURCE_DIR googletest
    CMAKE_ARGS
        -DBUILD_GMOCK=OFF
        -DINSTALL_GTEST=OFF)
