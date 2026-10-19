find_program(APPSTREAM appstreamcli)

# Everything but main() is a library so that tests/gtk can link the client's
# code without launching it.
add_library(${TR_NAME}-gtk-lib STATIC)

target_sources(${TR_NAME}-gtk-lib
    PRIVATE
        AboutDialog.cc
        AboutDialog.h
        Actions.cc
        Actions.h
        Application.cc
        Application.h
        DBusTransport.cc
        DetailsDialog.cc
        DetailsDialog.h
        Dialogs.cc
        Dialogs.h
        DynamicPropertyStore.h
        FaviconCache.cc
        FileList.cc
        FileList.h
        FilterBar.cc
        FilterBar.h
        FilterBase.h
        FilterBase.hh
        FilterListModel.h
        FilterListModel.hh
        Flags.h
        FreeSpaceLabel.cc
        FreeSpaceLabel.h
        GtkCompat.h
        HigWorkarea.h
        IconCache.cc
        IconCache.h
        ListModelAdapter.cc
        ListModelAdapter.h
        Macros.h
        MainWindow.cc
        MainWindow.h
        MakeDialog.cc
        MakeDialog.h
        MessageLogWindow.cc
        MessageLogWindow.h
        Notify.cc
        Notify.h
        OptionsDialog.cc
        OptionsDialog.h
        PathButton.cc
        PathButton.h
        Percents.cc
        Percents.h
        Prefs.cc
        Prefs.h
        PrefsDialog.cc
        PrefsDialog.h
        RelocateDialog.cc
        RelocateDialog.h
        Session.cc
        Session.h
        SorterBase.h
        SorterBase.hh
        SortListModel.h
        SortListModel.hh
        StatsDialog.cc
        StatsDialog.h
        SystemTrayIcon.cc
        SystemTrayIcon.h
        Torrent.cc
        Torrent.h
        TorrentCellRenderer.cc
        TorrentCellRenderer.h
        TorrentFilter.cc
        TorrentFilter.h
        TorrentSorter.cc
        TorrentSorter.h
        Transports.h
        Utils.cc
        Utils.h)

tr_allow_compile_if(
    [=[[GTK_VERSION EQUAL 3]]=]
        TorrentCellRenderer.cc)

target_sources(${TR_NAME}-gtk-lib
    PRIVATE
        ui/gtk3/AboutDialog.ui
        ui/gtk3/AddTrackerDialog.ui
        ui/gtk3/DetailsDialog.ui
        ui/gtk3/EditTrackersDialog.ui
        ui/gtk3/FilterBar.ui
        ui/gtk3/MainWindow.ui
        ui/gtk3/MakeDialog.ui
        ui/gtk3/MakeProgressDialog.ui
        ui/gtk3/MessageLogWindow.ui
        ui/gtk3/OptionsDialog.ui
        ui/gtk3/PrefsDialog.ui
        ui/gtk3/RelocateDialog.ui
        ui/gtk3/StatsDialog.ui
        ui/gtk3/TorrentUrlChooserDialog.ui)

source_group(Ui/GTK3
    REGULAR_EXPRESSION [[ui/gtk3/.*\.ui$]])

target_sources(${TR_NAME}-gtk-lib
    PRIVATE
        ui/gtk4/AboutDialog.ui
        ui/gtk4/AddTrackerDialog.ui
        ui/gtk4/DetailsDialog.ui
        ui/gtk4/EditTrackersDialog.ui
        ui/gtk4/FilterBar.ui
        ui/gtk4/MainWindow.ui
        ui/gtk4/MakeDialog.ui
        ui/gtk4/MakeProgressDialog.ui
        ui/gtk4/MessageLogWindow.ui
        ui/gtk4/OptionsDialog.ui
        ui/gtk4/PrefsDialog.ui
        ui/gtk4/RelocateDialog.ui
        ui/gtk4/StatsDialog.ui
        ui/gtk4/TorrentListItemCompact.ui
        ui/gtk4/TorrentListItemFull.ui
        ui/gtk4/TorrentUrlChooserDialog.ui)

source_group(Ui/GTK4
    REGULAR_EXPRESSION [[ui/gtk4/.*\.ui$]])

if(WITH_APPINDICATOR)
    set_property(
        SOURCE SystemTrayIcon.cc
        APPEND
        PROPERTY COMPILE_DEFINITIONS
            HAVE_APPINDICATOR)
    if(APPINDICATOR_IS_AYATANA)
        set_property(
            SOURCE SystemTrayIcon.cc
            APPEND
            PROPERTY COMPILE_DEFINITIONS
                APPINDICATOR_IS_AYATANA)
    endif()
endif()

if(GTK_VERSION EQUAL 4)
    set(UI_SUBDIR ui/gtk4)
else()
    set(UI_SUBDIR ui/gtk3)
endif()

add_executable(${TR_NAME}-gtk WIN32)

target_sources(${TR_NAME}-gtk
    PRIVATE
        main.cc)

# The generated resource file registers itself from a static constructor,
# which a static library would drop as unreferenced. A test that loads the
# client's UI adds the same resources to its own executable.
tr_target_glib_resources(${TR_NAME}-gtk
    transmission.gresource.xml
    ${UI_SUBDIR}/transmission-ui.gresource.xml)

# The program the desktop entry and the magnet handler launch.
set(TR_GTK_EXECUTABLE_NAME ${TR_NAME}-gtk)

# The desktop file ID, which the metainfo and users' default apps refer to.
# The finished file has this name whether or not it went through gettext,
# so the install and the test both have one path to name.
set(DESKTOP_FILE_ID ${TR_NAME}-gtk.desktop)
set(DESKTOP_FILE "${CMAKE_CURRENT_BINARY_DIR}/${DESKTOP_FILE_ID}")
set(METAINFO_FILE "${CMAKE_CURRENT_BINARY_DIR}/${TR_NAME}-gtk.metainfo.xml")

# Gettext merges the translations into the configured copies.
if(ENABLE_NLS)
    configure_file(retransmission-gtk.desktop.in retransmission-gtk.desktop.in @ONLY)
    configure_file(retransmission-gtk.metainfo.xml.in retransmission-gtk.metainfo.xml.in @ONLY)
    tr_gettext_msgfmt(${TR_NAME}-gtk
        ${DESKTOP_FILE}
        ${CMAKE_CURRENT_BINARY_DIR}/retransmission-gtk.desktop.in)
    tr_gettext_msgfmt(${TR_NAME}-gtk
        ${METAINFO_FILE}
        ${CMAKE_CURRENT_BINARY_DIR}/retransmission-gtk.metainfo.xml.in)
else()
    configure_file(retransmission-gtk.desktop.in ${DESKTOP_FILE} @ONLY)
endif()

target_compile_definitions(${TR_NAME}-gtk-lib
    PUBLIC
        "TR_GTK_APP_ID=\"${TR_GTK_APP_ID}\""
        "TR_GTK_EXECUTABLE_NAME=\"${TR_GTK_EXECUTABLE_NAME}\""
        "TR_GTK_ICON_NAME=\"${TR_GTK_ICON_NAME}\""
        TRANSMISSIONLOCALEDIR="${CMAKE_INSTALL_FULL_LOCALEDIR}"
        GETTEXT_PACKAGE="${TR_GETTEXT_DOMAIN}"
        $<$<BOOL:${ENABLE_UTP}>:WITH_UTP>)

foreach(TGT ${TR_NAME}-gtk-lib ${TR_NAME}-gtk)
    target_compile_options(${TGT}
        PRIVATE
            $<$<BOOL:${ENABLE_WERROR}>:$<IF:$<CXX_COMPILER_ID:MSVC>,/WX,-Werror>>
            $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wno-exit-time-destructors>)
endforeach()

target_include_directories(${TR_NAME}-gtk-lib
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_BINARY_DIR})

target_include_directories(${TR_NAME}-gtk-lib SYSTEM
    PRIVATE
        $<$<BOOL:${WITH_APPINDICATOR}>:${APPINDICATOR_INCLUDE_DIRS}>)

target_link_libraries(${TR_NAME}-gtk-lib
    PUBLIC
        ${TR_NAME}-app
        ${TR_NAME}
        woke::woke
        transmission::gtk_impl
        transmission::fmt-header-only
        $<$<BOOL:${WITH_APPINDICATOR}>:${APPINDICATOR_LIBRARIES}>)

target_link_libraries(${TR_NAME}-gtk
    PRIVATE
        ${TR_NAME}-gtk-lib)

if(MSVC)
    tr_append_target_property(${TR_NAME}-gtk LINK_FLAGS "/ENTRY:mainCRTStartup")
endif()

tr_win32_app_info(${TR_NAME}-gtk
    "GTK+ Client"
    "${TR_NAME}-gtk"
    "${TR_NAME}-gtk.exe"
    "${TR_NAME}.ico")

install(
    TARGETS ${TR_NAME}-gtk
    DESTINATION ${CMAKE_INSTALL_BINDIR})

tr_install_app_icons(${TR_GTK_ICON_NAME})

if(INSTALL_DOC)
    install(
        FILES ${TR_NAME}-gtk.1
        DESTINATION ${CMAKE_INSTALL_MANDIR}/man1)
endif()

install(
    FILES ${DESKTOP_FILE}
    DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)

if(ENABLE_NLS)
    install(
        FILES ${METAINFO_FILE}
        DESTINATION ${CMAKE_INSTALL_DATADIR}/metainfo)
endif()
