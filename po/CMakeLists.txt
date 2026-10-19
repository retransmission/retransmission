set(LINGUAS
    da
    es
    eu
    fi
    fr
    he
    hu
    is
    ja
    nl
    pl
    ru
    sv
    tr
    uk
    zh_CN
    zh_TW)

if(ENABLE_NLS)
    set(ENABLED_LINGUAS ${LINGUAS})
else()
    set(ENABLED_LINGUAS)
endif()

set(MO_FILES)
foreach(LANG ${ENABLED_LINGUAS})
    set(msgfmt_INPUT_FILE ${LANG}.po)
    set(msgfmt_OUTPUT_FILE ${CMAKE_CURRENT_BINARY_DIR}/${TR_GETTEXT_DOMAIN}-${LANG}.mo)

    add_custom_command(
        OUTPUT ${msgfmt_OUTPUT_FILE}
        COMMAND ${GETTEXT_MSGFMT_EXECUTABLE} --output-file=${msgfmt_OUTPUT_FILE} ${msgfmt_INPUT_FILE}
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        DEPENDS ${msgfmt_INPUT_FILE})

    list(APPEND MO_FILES ${msgfmt_OUTPUT_FILE})

    install(
        FILES ${msgfmt_OUTPUT_FILE}
        DESTINATION ${CMAKE_INSTALL_LOCALEDIR}/${LANG}/LC_MESSAGES/
        RENAME ${TR_GETTEXT_DOMAIN}.mo)
endforeach()

if(MO_FILES)
    add_custom_target(${TR_GETTEXT_DOMAIN}-po
        ALL
        DEPENDS ${MO_FILES})
endif()
