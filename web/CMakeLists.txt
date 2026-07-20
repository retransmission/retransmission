if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    # standalone mode: `cmake -S web -B <build-dir>` builds just the webapp
    # without configuring the rest of Transmission. CI uses this to check
    # that the bundle can be rebuilt with nothing but cmake and esbuild.
    cmake_minimum_required(VERSION 3.18 FATAL_ERROR)
    project(transmission-web NONE)
    set(TR_NAME transmission)
endif()

set(ESBUILD_MINIMUM 0.25.0) # 0.25.0 fixed the lowering of native CSS nesting
find_program(ESBUILD esbuild REQUIRED)
execute_process(COMMAND "${ESBUILD}" --version
    OUTPUT_STRIP_TRAILING_WHITESPACE
    OUTPUT_VARIABLE ESBUILD_VERSION_STRING)
if("${ESBUILD_VERSION_STRING}" VERSION_LESS "${ESBUILD_MINIMUM}")
    message(FATAL_ERROR "Found esbuild: ${ESBUILD} Found unsuitable version \"${ESBUILD_VERSION_STRING}\", but required is at least \"${ESBUILD_MINIMUM}\"")
endif()

set(WEB_SOURCES
    src/about-dialog.js
    src/action-manager.js
    src/alert-dialog.js
    src/appearance-settings.js
    src/context-menu.js
    src/file-row.js
    src/formatter.js
    src/inspector.js
    src/labels-dialog.js
    src/main.js
    src/move-dialog.js
    src/notifications.js
    src/open-dialog.js
    src/overflow-menu.js
    src/prefs-dialog.js
    src/prefs.js
    src/remote.js
    src/remove-dialog.js
    src/rename-dialog.js
    src/shortcuts-dialog.js
    src/statistics-dialog.js
    src/torrent.js
    src/torrent-row.js
    src/transmission.js
    src/utils.js
    src/vendor/clusterize.js
    src/vendor/fast-deep-equal.js)

set(WEB_STYLES
    assets/css/transmission-app.css)

set(WEB_IMAGES
    assets/img/box.svg
    assets/img/chevron-down.svg
    assets/img/chevron-up.svg
    assets/img/file-text.svg
    assets/img/film.svg
    assets/img/folder.svg
    assets/img/horizontal-rule.svg
    assets/img/image.svg
    assets/img/lock-fill.svg
    assets/img/magnet.svg
    assets/img/music.svg
    assets/img/package.svg
    assets/img/README.md
    assets/img/router.svg
    assets/img/type.svg
    assets/img/turtle.svg)

add_custom_command(
    OUTPUT
        "${CMAKE_CURRENT_BINARY_DIR}/public_html/transmission-app.css"
        "${CMAKE_CURRENT_BINARY_DIR}/public_html/transmission-app.css.LEGAL.txt"
        "${CMAKE_CURRENT_BINARY_DIR}/public_html/transmission-app.css.map"
        "${CMAKE_CURRENT_BINARY_DIR}/public_html/transmission-app.js"
        "${CMAKE_CURRENT_BINARY_DIR}/public_html/transmission-app.js.LEGAL.txt"
        "${CMAKE_CURRENT_BINARY_DIR}/public_html/transmission-app.js.map"
    WORKING_DIRECTORY
        "${CMAKE_CURRENT_BINARY_DIR}"
    # building from a copy of the sources keeps the paths that esbuild
    # writes into the .map files relative, so the generated bundle is
    # identical to `npm run build`'s no matter where the build dir lives
    COMMAND "${CMAKE_COMMAND}" -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/assets" "assets"
    COMMAND "${CMAKE_COMMAND}" -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/public_html" "public_html"
    COMMAND "${CMAKE_COMMAND}" -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/src" "src"
    # these flags mirror esbuild.mjs (the `npm run build` recipe) and the
    # packager recipe in README.md -- keep the three in sync
    COMMAND "${ESBUILD}"
        --bundle
        --external:*/favicon.svg
        --legal-comments=external
        --loader:.png=dataurl
        --loader:.svg=dataurl
        --minify
        --outfile=public_html/transmission-app.js
        --sourcemap
        --target=chrome104,firefox115,safari16.4
        src/main.js
    DEPENDS
        ${WEB_SOURCES}
        ${WEB_STYLES}
        ${WEB_IMAGES}
    VERBATIM)

add_custom_target("${TR_NAME}-web"
    ALL
    DEPENDS
        "${CMAKE_CURRENT_BINARY_DIR}/public_html/transmission-app.css"
        "${CMAKE_CURRENT_BINARY_DIR}/public_html/transmission-app.css.LEGAL.txt"
        "${CMAKE_CURRENT_BINARY_DIR}/public_html/transmission-app.css.map"
        "${CMAKE_CURRENT_BINARY_DIR}/public_html/transmission-app.js"
        "${CMAKE_CURRENT_BINARY_DIR}/public_html/transmission-app.js.LEGAL.txt"
        "${CMAKE_CURRENT_BINARY_DIR}/public_html/transmission-app.js.map"
    SOURCES
        ${WEB_SOURCES})

if(NOT CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(TR_WEB_ASSETS ${CMAKE_CURRENT_BINARY_DIR}/public_html PARENT_SCOPE)
endif()
