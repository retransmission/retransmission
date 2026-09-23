# umbrella file for Transmission App.
# Contains instructions for Transmission App and QuickLookExtension.

# minimum macOS target support
if(CMAKE_OSX_DEPLOYMENT_TARGET VERSION_LESS MACOS_SUPPORT_MINIMUM)
    message(FATAL_ERROR "Targeting macOS versions before ${MACOS_SUPPORT_MINIMUM} is not supported for the macOS project, please adjust CMAKE_OSX_DEPLOYMENT_TARGET (${CMAKE_OSX_DEPLOYMENT_TARGET})")
endif()

add_subdirectory(QuickLookExtension)
add_subdirectory(Transmission)

add_dependencies(${TR_NAME}-mac ${TR_NAME}-mac-qlappex)
