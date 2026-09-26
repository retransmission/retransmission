set(MAC_QL_BUNDLE_NAME QuickLookExtension)

find_program(XCRUN_EXECUTABLE xcrun REQUIRED)

add_library(${TR_NAME}-mac-qlappex MODULE)

target_sources(${TR_NAME}-mac-qlappex
    PRIVATE
        PreviewProvider.mm)

set(LINGUAS
    af
    da
    de
    el
    en
    es
    eu
    fr
    he
    hu
    it
    ja
    nl
    pl
    pt-BR
    pt-PT
    ru
    sv
    tr
    uk
    zh-CN
    zh-TW)

if(ENABLE_NLS)
    set(ENABLED_LINGUAS ${LINGUAS})
else()
    set(ENABLED_LINGUAS en)
endif()

set(LANG_STRINGS_FILES
    Localizable.strings)

set(RESOURCES
    style.css)
source_group(Resources
    FILES ${RESOURCES})

set_source_files_properties(
    ${RESOURCES}
    PROPERTIES
        MACOSX_PACKAGE_LOCATION Resources)

set(XSTRINGS_SRC "${CMAKE_CURRENT_SOURCE_DIR}/Resources/Localizable.xcstrings")

if(CMAKE_GENERATOR STREQUAL Xcode)
    list(APPEND RESOURCES ${XSTRINGS_SRC})
    set_source_files_properties(${XSTRINGS_SRC}
        PROPERTIES
            MACOSX_PACKAGE_LOCATION Resources)
else()
    set(XSTRINGS_BUILD_DIR "${CMAKE_CURRENT_BINARY_DIR}/xcstrings_out")
    set(XSTRINGS_OUTPUTS)

    foreach(LANG ${ENABLED_LINGUAS})
        set(LOCALIZED_RESOURCE "${XSTRINGS_BUILD_DIR}/${LANG}.lproj/Localizable.strings")
        list(APPEND XSTRINGS_OUTPUTS "${LOCALIZED_RESOURCE}")
        set_source_files_properties("${LOCALIZED_RESOURCE}" PROPERTIES
            GENERATED TRUE
            MACOSX_PACKAGE_LOCATION "Resources/${LANG}.lproj")
    endforeach()

    add_custom_command(
        OUTPUT ${XSTRINGS_OUTPUTS}
        COMMAND ${XCRUN_EXECUTABLE} xcstringstool compile
                "${XSTRINGS_SRC}"
                --output-directory "${XSTRINGS_BUILD_DIR}"
        DEPENDS "${XSTRINGS_SRC}"
        COMMENT "Compiling String Catalog ${XSTRINGS_SRC}"
        VERBATIM
    )

    list(APPEND RESOURCES "${XSTRINGS_OUTPUTS}")
endif()

target_sources(${TR_NAME}-mac-qlappex
    PRIVATE
        ${RESOURCES})

set_target_properties(
    ${TR_NAME}-mac-qlappex
    PROPERTIES
        BUNDLE ON
        BUNDLE_EXTENSION appex
        INSTALL_RPATH "@loader_path/../../../../MacOS;@loader_path/../../../../Frameworks"
        XCODE_ATTRIBUTE_MACOSX_DEPLOYMENT_TARGET "12.0"
        MACOSX_BUNDLE ON
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/Info.plist.in"
        OUTPUT_NAME ${MAC_QL_BUNDLE_NAME})

target_compile_options(
    ${TR_NAME}-mac-qlappex
    PUBLIC
        "-mmacosx-version-min=12.0")

target_link_options(
    ${TR_NAME}-mac-qlappex
    PUBLIC
        "-mmacosx-version-min=12.0")

target_link_libraries(${TR_NAME}-mac-qlappex
    PRIVATE
        ${TR_NAME}
        "-framework Foundation"
        "-framework AppKit"
        "-framework CoreFoundation"
        "-framework CoreServices"
        "-framework Quartz"
        "-framework UniformTypeIdentifiers"
        "-framework QuickLook")

install(
    TARGETS ${TR_NAME}-mac-qlappex
    DESTINATION Applications/${MAC_BUNDLE_NAME}.app/Contents/PlugIns)

install(CODE
    "list(APPEND CMAKE_MODULE_PATH \"${PROJECT_SOURCE_DIR}/cmake\")
    include(TrMacros)
    include(GetPrerequisites)
    tr_fixup_bundle_item(
        \"\$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/Applications/${MAC_BUNDLE_NAME}.app\"
        \"Contents/PlugIns/${MAC_QL_BUNDLE_NAME}.appex/Contents/MacOS/${MAC_QL_BUNDLE_NAME}\" \"\")")
