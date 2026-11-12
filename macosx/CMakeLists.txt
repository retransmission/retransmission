# Settings shared by the app in App/ and its QuickLook extension.

# minimum macOS target support
if(CMAKE_OSX_DEPLOYMENT_TARGET VERSION_LESS MACOS_SUPPORT_MINIMUM)
    message(FATAL_ERROR "Targeting macOS versions before ${MACOS_SUPPORT_MINIMUM} is not supported for the macOS project, please adjust CMAKE_OSX_DEPLOYMENT_TARGET (${CMAKE_OSX_DEPLOYMENT_TARGET})")
endif()

find_program(ACTOOL_EXECUTABLE actool REQUIRED)
find_program(CODESIGN_EXECUTABLE codesign REQUIRED)

set(MAC_BUNDLE_NAME ${TR_PROJ_APPNAME_CAPITALIZED})

add_compile_options(
    $<$<COMPILE_LANGUAGE:C>:-fmodules>

    # https://github.com/llvm/llvm-project/issues/57432
    "$<$<COMPILE_LANG_AND_ID:CXX,AppleClang,Clang>:-fno-modules;-Xclang;-fno-cxx-modules>"

    # equivalent of XCODE_ATTRIBUTE_CLANG_ENABLE_OBJC_ARC YES for this directory
    $<$<COMPILE_LANGUAGE:C,CXX>:-fobjc-arc>

    # equivalent of XCODE_ATTRIBUTE_CLANG_ENABLE_OBJC_WEAK YES for this directory
    $<$<COMPILE_LANGUAGE:C,CXX>:-fobjc-weak>

    # GNU extensions are good practice in Objective-C
    $<$<COMPILE_LANGUAGE:C,CXX>:-Wno-gnu>

    # Our #warnings are good practice in IDE development, not in command-line compilation
    $<$<AND:$<NOT:$<BOOL:${XCODE}>>,$<COMPILE_LANGUAGE:C,CXX>>:-Wno-\#warnings>)

add_link_options(
    # equivalent of XCODE_ATTRIBUTE_DEAD_CODE_STRIPPING YES for this directory
    $<$<LINK_LANGUAGE:C,CXX>:-dead_strip>)

file(GLOB MAC_TOP_LEVEL_ENTRIES
    LIST_DIRECTORIES true
    RELATIVE ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/*)
list(FILTER MAC_TOP_LEVEL_ENTRIES EXCLUDE REGEX "^\\.")
list(REMOVE_ITEM MAC_TOP_LEVEL_ENTRIES App CMakeLists.txt Configs QuickLookExtension)
if(MAC_TOP_LEVEL_ENTRIES)
    message(FATAL_ERROR "Move these into macosx/App/ or macosx/QuickLookExtension/: ${MAC_TOP_LEVEL_ENTRIES}")
endif()

add_subdirectory(QuickLookExtension)
add_subdirectory(App)
