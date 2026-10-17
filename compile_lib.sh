#!/bin/bash
set -e

# Xcode launched from the Dock gives build tools launchd's PATH, which omits Homebrew.
export PATH="/opt/homebrew/bin:/usr/local/bin:$PATH"

# Change to the repository root directory
ROOT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$ROOT_DIR"

# Fallback to Xcode environment variables or use defaults
CONFIGURATION="${CONFIGURATION:-Debug}"
if [ "$CONFIGURATION" = "Release+Asserts" ]; then
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
CMAKE_LIB_BUILD_DIRECTORY="${CMAKE_LIB_BUILD_DIRECTORY:-build/$CMAKE_BUILD_TYPE/cmake}"

# Locate the Xcode project directory dynamically or fallback to root/macosx
XCODE_PROJECT_DIR="${PROJECT_DIR:-$ROOT_DIR}/macosx"
DERIVED_FRAMEWORKS_DIRECTORY="$XCODE_PROJECT_DIR/DerivedFrameworks"

XCFRAMEWORK_FILE="$DERIVED_FRAMEWORKS_DIRECTORY/LibTransmission.xcframework"

if [ "$ACTION" = "clean" ]; then
    echo "🧹 Cleaning build artifacts"
    rm -rf "$CMAKE_LIB_BUILD_DIRECTORY" "$XCFRAMEWORK_FILE"
    exit 0
fi

echo "📌 Configuring LibTransmission version"
sh "$ROOT_DIR/update-version-h.sh"

echo "🛠 Configuring CMake ($CONFIGURATION, BUILD_TYPE: $CMAKE_BUILD_TYPE, ARCHS: $CMAKE_ARCHS, SDK: $SDK_NAME)..."

mkdir -p "$CMAKE_LIB_BUILD_DIRECTORY"
cd "$CMAKE_LIB_BUILD_DIRECTORY"

# Configure CMake with variables passed down from Xcode
cmake -G "Ninja" -S "$ROOT_DIR" -B . \
  -DCMAKE_BUILD_TYPE="$CMAKE_BUILD_TYPE" \
  -DCMAKE_CXX_FLAGS_RELWITHDEBINFO="-O2 -g" \
  -DCMAKE_OSX_ARCHITECTURES="$CMAKE_ARCHS" \
  -DCMAKE_OSX_SYSROOT="$SDK_NAME" \
  -DENABLE_IPO=OFF \
  -DCMAKE_CXX_FLAGS=-DBUILD_MAC_CLIENT \
  -DUSE_SYSTEM_DEFAULT=OFF \
  -DENABLE_NLS=OFF \
  -DINSTALL_LIB=ON \
  -DENABLE_DAEMON=OFF -DENABLE_GTK=OFF -DENABLE_QT=OFF -DENABLE_MAC=OFF \
  -DENABLE_UTILS=OFF -DENABLE_CLI=OFF -DENABLE_TESTS=OFF -DREBUILD_WEB=OFF

echo "🏗 Compiling and natively installing core..."
cmake --build . --config "$CMAKE_BUILD_TYPE"

cd "$ROOT_DIR"

echo "Checking native core library and external third-party dependencies"
THIRD_PARTY_LIBS=$(find "$CMAKE_LIB_BUILD_DIRECTORY"/third-party -name "*.a" -not -path "*/src/_*-build/*")
if [ -d "$XCFRAMEWORK_FILE" ] && [ -z "$(find "$CMAKE_LIB_BUILD_DIRECTORY"/libtransmission/libtransmission.a $THIRD_PARTY_LIBS -newer "$XCFRAMEWORK_FILE")" ]; then
    echo "LibTransmission.xcframework is up to date"
    exit 0
fi

echo "📦 Merging native core library with external third-party dependencies..."
mkdir -p "$CMAKE_LIB_BUILD_DIRECTORY"/monolith
libtool -static -o "$CMAKE_LIB_BUILD_DIRECTORY"/monolith/libtransmission_combined.a "$CMAKE_LIB_BUILD_DIRECTORY"/libtransmission/libtransmission.a $THIRD_PARTY_LIBS

echo "🚀 Packaging everything into XCFramework at $XCFRAMEWORK_FILE..."
mkdir -p "$DERIVED_FRAMEWORKS_DIRECTORY"

rm -rf "$XCFRAMEWORK_FILE"

# Package the monolith archive as a static library framework slice
xcodebuild -create-xcframework \
  -library "$CMAKE_LIB_BUILD_DIRECTORY"/monolith/libtransmission_combined.a \
  -output "$XCFRAMEWORK_FILE"

echo "🎉 PERFECT! Look at fresh $XCFRAMEWORK_FILE"
