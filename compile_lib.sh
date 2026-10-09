#!/bin/bash
set -e

# Change to the repository root directory
ROOT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$ROOT_DIR"

# Fallback to Xcode environment variables or use defaults
CONFIGURATION="${CONFIGURATION:-Debug}"

# Fixing CMAKE_BUILD_TYPE ( change it in Xcode )
if [ "$CONFIGURATION" = "Release - Debug" ]; then
    CMAKE_BUILD_TYPE="RelWithDebInfo"
else
    CMAKE_BUILD_TYPE="$CONFIGURATION"
fi

# Xcode provides ARCHS separated by spaces, but CMake expects them separated by semicolons
ARCHS="${ARCHS:-arm64}"
CMAKE_ARCHS=$(echo "$ARCHS" | tr ' ' ';')

# Target SDK (macosx, iphoneos, etc.) required for CMake to locate correct system headers
SDK_NAME="${SDK_NAME:-macosx}"

# Define directories relative to the repository root
CMAKE_LIB_BUILD_DIRECTORY="${CMAKE_LIB_BUILD_DIRECTORY:-build_cmake}"
CMAKE_LIB_INSTALL_DIRECTORY="${CMAKE_LIB_INSTALL_DIRECTORY:-build_install}"

# Locate the Xcode project directory dynamically or fallback to root/macosx
XCODE_PROJECT_DIR="${PROJECT_DIR:-$ROOT_DIR}/macosx"
DERIVED_FRAMEWORKS_DIRECTORY="$XCODE_PROJECT_DIR/DerivedFrameworks"

XCFRAMEWORK_FILE="$DERIVED_FRAMEWORKS_DIRECTORY/LibTransmission.xcframework"

echo "📌 Configuring LibTransmission version"
sh "$ROOT_DIR/update-version-h.sh"

echo "🛠 Configuring CMake ($CONFIGURATION, BUILD_TYPE: $CMAKE_BUILD_TYPE, ARCHS: $CMAKE_ARCHS, SDK: $SDK_NAME)..."

mkdir -p "$CMAKE_LIB_BUILD_DIRECTORY"
cd "$CMAKE_LIB_BUILD_DIRECTORY"

# Configure CMake with variables passed down from Xcode
cmake -G "Ninja" .. \
  -DCMAKE_BUILD_TYPE="$CMAKE_BUILD_TYPE" \
  -DCMAKE_OSX_ARCHITECTURES="$CMAKE_ARCHS" \
  -DCMAKE_OSX_SYSROOT="$SDK_NAME" \
  -DENABLE_IPO=OFF \
  -DUSE_SYSTEM_DEFAULT=OFF \
  -DENABLE_NLS=OFF \
  -DINSTALL_LIB=ON \
  -DCMAKE_INSTALL_PREFIX="../$CMAKE_LIB_INSTALL_DIRECTORY" \
  -DENABLE_DAEMON=OFF -DENABLE_GTK=OFF -DENABLE_QT=OFF -DENABLE_MAC=OFF \
  -DENABLE_UTILS=OFF -DENABLE_CLI=OFF -DENABLE_TESTS=OFF -DREBUILD_WEB=OFF

echo "🏗 Compiling and natively installing core..."
cmake --build . --config "$CMAKE_BUILD_TYPE"
cmake --install .

cd "$ROOT_DIR"

echo "📦 Merging native core library with external third-party dependencies..."
THIRD_PARTY_LIBS=$(find "$CMAKE_LIB_BUILD_DIRECTORY"/third-party -name "*.a" -not -path "*/src/_*-build/*")

mkdir -p "$CMAKE_LIB_INSTALL_DIRECTORY"/monolith
libtool -static -o "$CMAKE_LIB_INSTALL_DIRECTORY"/monolith/libtransmission_combined.a "$CMAKE_LIB_INSTALL_DIRECTORY"/lib/libtransmission.a $THIRD_PARTY_LIBS

echo "🚀 Packaging everything into XCFramework at $XCFRAMEWORK_FILE..."
mkdir -p "$DERIVED_FRAMEWORKS_DIRECTORY"

# Clean up the old XCFramework using standard rm to avoid dependency on the 'trash' utility
if [[ -d "$XCFRAMEWORK_FILE" ]]; then
    echo "Cleaning up existing XCFramework..."
    rm -rf "$XCFRAMEWORK_FILE"
fi

# Package the monolith archive as a static library framework slice
xcodebuild -create-xcframework \
  -library "$CMAKE_LIB_INSTALL_DIRECTORY"/monolith/libtransmission_combined.a \
  -headers "$CMAKE_LIB_INSTALL_DIRECTORY"/include/transmission \
  -output "$XCFRAMEWORK_FILE"

echo "🎉 PERFECT! Look at fresh $XCFRAMEWORK_FILE"
