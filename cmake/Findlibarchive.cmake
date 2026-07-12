find_package(${CMAKE_FIND_PACKAGE_NAME} QUIET NO_MODULE)

include(FindPackageHandleStandardArgs)
if(${CMAKE_FIND_PACKAGE_NAME}_FOUND)
    find_package_handle_standard_args(${CMAKE_FIND_PACKAGE_NAME} CONFIG_MODE)
    return()
endif()

if(${CMAKE_FIND_PACKAGE_NAME}_PREFER_STATIC_LIB)
    set(${CMAKE_FIND_PACKAGE_NAME}_ORIG_CMAKE_FIND_LIBRARY_SUFFIXES ${CMAKE_FIND_LIBRARY_SUFFIXES})
    if(WIN32)
        set(CMAKE_FIND_LIBRARY_SUFFIXES .a .lib ${CMAKE_FIND_LIBRARY_SUFFIXES})
    else()
        set(CMAKE_FIND_LIBRARY_SUFFIXES .a ${CMAKE_FIND_LIBRARY_SUFFIXES})
    endif()
endif()

if(UNIX)
    find_package(PkgConfig QUIET)
    pkg_check_modules(_ARCHIVE QUIET libarchive)
endif()

if(APPLE)
    # macOS ships an ancient libarchive with no public headers, so Homebrew
    # installs its copy keg-only (off the default search path). Ask brew where
    # it is and add that to the search hints.
    find_program(_ARCHIVE_BREW NAMES brew)
    if(_ARCHIVE_BREW)
        execute_process(
            COMMAND ${_ARCHIVE_BREW} --prefix libarchive
            OUTPUT_VARIABLE _ARCHIVE_BREW_PREFIX
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET)
        if(_ARCHIVE_BREW_PREFIX)
            list(APPEND _ARCHIVE_INCLUDEDIR "${_ARCHIVE_BREW_PREFIX}/include")
            list(APPEND _ARCHIVE_LIBDIR "${_ARCHIVE_BREW_PREFIX}/lib")
        endif()
    endif()
endif()

find_path(${CMAKE_FIND_PACKAGE_NAME}_INCLUDE_DIR
    NAMES archive.h
    HINTS ${_ARCHIVE_INCLUDEDIR})
find_library(${CMAKE_FIND_PACKAGE_NAME}_LIBRARY
    NAMES archive
    HINTS ${_ARCHIVE_LIBDIR})

if(_ARCHIVE_VERSION)
    set(${CMAKE_FIND_PACKAGE_NAME}_VERSION ${_ARCHIVE_VERSION})
elseif(${CMAKE_FIND_PACKAGE_NAME}_INCLUDE_DIR)
    file(STRINGS "${${CMAKE_FIND_PACKAGE_NAME}_INCLUDE_DIR}/archive.h" ${CMAKE_FIND_PACKAGE_NAME}_VERSION_STR
        REGEX "^#define[\t ]+ARCHIVE_VERSION_ONLY_STRING[\t ]+\"[^\"]+\"")
    if(${CMAKE_FIND_PACKAGE_NAME}_VERSION_STR MATCHES "\"([^\"]+)\"")
        set(${CMAKE_FIND_PACKAGE_NAME}_VERSION "${CMAKE_MATCH_1}")
    endif()
endif()

find_package_handle_standard_args(${CMAKE_FIND_PACKAGE_NAME}
    REQUIRED_VARS
        ${CMAKE_FIND_PACKAGE_NAME}_INCLUDE_DIR
        ${CMAKE_FIND_PACKAGE_NAME}_LIBRARY
    VERSION_VAR ${CMAKE_FIND_PACKAGE_NAME}_VERSION)

if(${CMAKE_FIND_PACKAGE_NAME}_FOUND)
    set(${CMAKE_FIND_PACKAGE_NAME}_INCLUDE_DIRS ${${CMAKE_FIND_PACKAGE_NAME}_INCLUDE_DIR})
    set(${CMAKE_FIND_PACKAGE_NAME}_LIBRARIES ${${CMAKE_FIND_PACKAGE_NAME}_LIBRARY})

    if(NOT TARGET libarchive::libarchive)
        add_library(libarchive::libarchive INTERFACE IMPORTED)
        target_include_directories(libarchive::libarchive
            INTERFACE
                ${${CMAKE_FIND_PACKAGE_NAME}_INCLUDE_DIR})
        target_link_libraries(libarchive::libarchive
            INTERFACE
                ${${CMAKE_FIND_PACKAGE_NAME}_LIBRARY})
    endif()
endif()

mark_as_advanced(${CMAKE_FIND_PACKAGE_NAME}_INCLUDE_DIR ${CMAKE_FIND_PACKAGE_NAME}_LIBRARY)

if(${CMAKE_FIND_PACKAGE_NAME}_PREFER_STATIC_LIB)
    set(CMAKE_FIND_LIBRARY_SUFFIXES ${${CMAKE_FIND_PACKAGE_NAME}_ORIG_CMAKE_FIND_LIBRARY_SUFFIXES})
    unset(${CMAKE_FIND_PACKAGE_NAME}_ORIG_CMAKE_FIND_LIBRARY_SUFFIXES)
endif()
