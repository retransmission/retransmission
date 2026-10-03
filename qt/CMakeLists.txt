set_property(GLOBAL PROPERTY AUTOGEN_SOURCE_GROUP "Generated Files")

add_library(${TR_NAME}-qt-lib STATIC)

target_sources(${TR_NAME}-qt-lib
    PRIVATE
        AboutDialog.cc
        AboutDialog.h
        AccessibleSqueezeLabel.cc
        AccessibleSqueezeLabel.h
        AddData.cc
        AddData.h
        Application.cc
        Application.h
        BaseDialog.h
        ColumnResizer.cc
        ColumnResizer.h
        ComTransport.cc
        DBusTransport.cc
        DetailsDialog.cc
        DetailsDialog.h
        FaviconCache.cc
        FileTreeDelegate.cc
        FileTreeDelegate.h
        FileTreeItem.cc
        FileTreeItem.h
        FileTreeModel.cc
        FileTreeModel.h
        FileTreeView.cc
        FileTreeView.h
        FilterBar.cc
        FilterBar.h
        FilterBarComboBox.cc
        FilterBarComboBox.h
        FilterBarComboBoxDelegate.cc
        FilterBarComboBoxDelegate.h
        Filters.cc
        Filters.h
        Formatter.cc
        Formatter.h
        FreeSpaceLabel.cc
        FreeSpaceLabel.h
        IconCache.cc
        IconCache.h
        IconToolButton.cc
        IconToolButton.h
        InteropObject.cc
        InteropObject.h
        MainWindow.cc
        MainWindow.h
        MakeDialog.cc
        MakeDialog.h
        NativeIcon.cc
        $<$<BOOL:${APPLE}>:NativeIconMac.mm>
        NativeIcon.h
        OptionsDialog.cc
        OptionsDialog.h
        PathButton.cc
        PathButton.h
        Prefs.h
        PrefsDialog.cc
        PrefsDialog.h
        QtCompat.h
        RelocateDialog.cc
        RelocateDialog.h
        RpcClient.cc
        RpcClient.h
        Session.cc
        Session.h
        SessionDialog.cc
        SessionDialog.h
        Speed.cc
        Speed.h
        SqueezeLabel.cc
        SqueezeLabel.h
        StatsDialog.cc
        StatsDialog.h
        StyleHelper.cc
        StyleHelper.h
        Torrent.cc
        Torrent.h
        TorrentDelegate.cc
        TorrentDelegate.h
        TorrentDelegateMin.cc
        TorrentDelegateMin.h
        TorrentFilter.cc
        TorrentFilter.h
        TorrentModel.cc
        TorrentModel.h
        TorrentView.cc
        TorrentView.h
        TrackerDelegate.cc
        TrackerDelegate.h
        TrackerModel.cc
        TrackerModel.h
        TrackerModelFilter.cc
        TrackerModelFilter.h
        Transports.h
        TrFormat.cc
        TrFormat.h
        TrQtInit.cc
        TrQtInit.h
        Typedefs.h
        UserMetaType.h
        Utils.cc
        Utils.h
        VariantHelpers.cc
        VariantHelpers.h
        WatchDir.cc
        WatchDir.h)

tr_allow_compile_if(
    [=[[ENABLE_QT_COM_INTEROP]]=]
        ComTransport.cc
    [=[[ENABLE_QT_DBUS_INTEROP]]=]
        DBusTransport.cc)

target_sources(${TR_NAME}-qt-lib
    PRIVATE
        AboutDialog.ui
        DetailsDialog.ui
        MainWindow.ui
        MakeDialog.ui
        MakeProgressDialog.ui
        OptionsDialog.ui
        PrefsDialog.ui
        RelocateDialog.ui
        SessionDialog.ui
        StatsDialog.ui
        TrackersDialog.ui)

source_group(Ui
    REGULAR_EXPRESSION [[.*\.ui$]])

source_group(Resources
    REGULAR_EXPRESSION [[.*\.qrc$]])

set(LINGUAS
    af
    ca
    da
    de
    el
    en
    es
    eu
    fi
    fr
    he
    hu
    id
    is
    it
    ja
    ka
    kk
    ko
    lt
    nb
    nl
    pl
    pt
    pt_BR
    pt_PT
    ru
    sl
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

# The client loads <name>_<locale>.qm, which is built from translations/<name>_<locale>.ts.
set(TRANSLATIONS_NAME transmission)

set(TS_FILES)
foreach(LANG ${ENABLED_LINGUAS})
    list(APPEND TS_FILES translations/${TRANSLATIONS_NAME}_${LANG}.ts)
endforeach()

if(TS_FILES)
    tr_qt_add_translation(QM_FILES ${TS_FILES})
endif()

target_sources(${TR_NAME}-qt-lib
    PRIVATE
        ${QM_FILES})

target_include_directories(${TR_NAME}-qt-lib
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(${TR_NAME}-qt-lib
    PUBLIC
        ${TR_NAME}-app
        ${TR_NAME}
        transmission::qt_impl)

target_compile_definitions(${TR_NAME}-qt-lib
    PUBLIC
        $<$<BOOL:${ENABLE_QT_COM_INTEROP}>:ENABLE_COM_INTEROP>
        $<$<BOOL:${ENABLE_QT_DBUS_INTEROP}>:ENABLE_DBUS_INTEROP>
        QT_NO_CAST_FROM_ASCII
    PRIVATE
        "TRANSLATIONS_DIR=\"${TR_DATA_INSTALL_FULL_DIR}/translations\""
        "TRANSLATIONS_NAME=\"${TRANSLATIONS_NAME}\""
        "TR_QT_ICON_NAME=\"${TR_QT_ICON_NAME}\""
        "TR_GETTEXT_DOMAIN=\"${TR_GETTEXT_DOMAIN}\""
        "TR_LOCALE_DIR=\"${CMAKE_INSTALL_FULL_LOCALEDIR}\"")

# Designer files translate their text with the shared catalog, as the code does.
set_target_properties(
    ${TR_NAME}-qt-lib
    PROPERTIES
        AUTOMOC ON
        AUTORCC ON
        AUTOUIC ON
        AUTOUIC_OPTIONS "--tr;trqt::uiText;--include;TrFormat.h")

add_executable(${TR_NAME}-qt WIN32)

target_sources(${TR_NAME}-qt
    PRIVATE
        main.cc
        application.qrc)

target_link_libraries(${TR_NAME}-qt
    PRIVATE
        ${TR_NAME}-qt-lib)

set_target_properties(
    ${TR_NAME}-qt
    PROPERTIES
        AUTOMOC ON
        AUTORCC ON
        AUTOUIC ON)

if(MSVC)
    tr_append_target_property(${TR_NAME}-qt LINK_FLAGS "/ENTRY:mainCRTStartup")
endif()

if(ENABLE_QT_COM_INTEROP)
    # The type library rides in as a resource, and a resource in a static
    # library does not reach the executable that links it. A caller whose
    # QAxBase finds no interface information cannot resolve a method by name.
    tr_target_idl_files(${TR_NAME}-qt
        transmission-qt.idl)
endif()

tr_win32_app_info(${TR_NAME}-qt
    "Qt Client"
    "${TR_NAME}-qt"
    "${TR_NAME}-qt.exe"
    "${SOURCE_ICONS_DIR}/${TR_NAME}.ico")

install(
    TARGETS ${TR_NAME}-qt
    DESTINATION ${CMAKE_INSTALL_BINDIR})

tr_install_app_icons(${TR_QT_ICON_NAME})

if(INSTALL_DOC)
    install(
        FILES ${TR_NAME}-qt.1
        DESTINATION ${CMAKE_INSTALL_MANDIR}/man1)
endif()

set(DESKTOP_FILE "${CMAKE_CURRENT_BINARY_DIR}/${TR_NAME}-qt.desktop")

# Gettext merges the translations into the configured copy.
if(ENABLE_NLS AND GETTEXT_FOUND)
    configure_file(retransmission-qt.desktop.in retransmission-qt.desktop.in @ONLY)
    tr_gettext_msgfmt(${TR_NAME}-qt
        ${DESKTOP_FILE}
        ${CMAKE_CURRENT_BINARY_DIR}/retransmission-qt.desktop.in)
else()
    configure_file(retransmission-qt.desktop.in ${DESKTOP_FILE} @ONLY)
endif()

install(
    FILES ${DESKTOP_FILE}
    DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)

if(ENABLE_NLS)
    install(
        FILES ${QM_FILES}
        DESTINATION ${TR_DATA_INSTALL_DIR}/translations)
endif()

if(WIN32)
    file(GENERATE
        OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/dist-qt.conf"
        CONTENT "")
    install(
        FILES "${CMAKE_CURRENT_BINARY_DIR}/dist-qt.conf"
        DESTINATION ${CMAKE_INSTALL_SYSCONFDIR}
        RENAME qt.conf)
endif()
