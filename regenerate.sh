# : << DOC
# HOW TO USE

# Build everything you want.
# However, the most promising variant is with SPM and static library in XCFramework.
# But it doesn't work for QuickLook extension.

# 1. SPM and static library with all libraries inside in SPM as XCFramework.
# - Pros: Simple and out of the box
# - Cons: Does not work QuickLook extension.

# 2. Static library with all libraries.
# - Pros: Simple and works everywhere. Not a blocker for QuickLook extension.
# - Cons: You need to add Header Search Paths and you need to add library search paths.

# 3. Generated xcodeproj for LibTransmission.
# - Pros: Automated. Easy to modify. Easy to add files.
# - Cons: Not easy to integrate. You have to add project as nearby project into XCWorkspace and also set proper paths.

# 4. Split project into LibTransmission.xcodeproj and Transmission.xcodeproj. Put one into another.
# - Pros: Not Automated but easy to modify. Average to add files/remove files.
# - Cons: The same amount of work to maintain for libtransmission contributors. Still requires paths for libraries.

# However, `4. option` was(is?) used in large codebases. As build from sources option.
# `XCFramework` is an option for C/C++ libraries to be integrated into Swift project.
# Fat static library is something that people wants to see (?) on `macOS`. Nobody wants to install other libraries.
# Generated (CMake) xcodeproj is something very tasty and jealous. Zero maintenance.

# DOC

function install_as_spm {
#!/bin/bash
set -e

# Change to the repository root directory
cd "$(dirname "$0")"

echo "🧹 Cleaning up old build..."
rm -rf build_cmake
rm -rf transmission-spm
rm -rf build_install

mkdir -p build_cmake
cd build_cmake

echo "🛠 Configuring CMake (Debug, enabling INSTALL_LIB)..."
# Enable INSTALL_LIB and explicitly set the installation directory to the temporary build_install folder"

cmake -G "Ninja" .. \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" \
  -DENABLE_IPO=OFF \
  -DINSTALL_LIB=ON \
  -DCMAKE_INSTALL_PREFIX=../build_install \
  -DENABLE_DAEMON=OFF -DENABLE_GTK=OFF -DENABLE_QT=OFF -DENABLE_MAC=OFF \
  -DENABLE_UTILS=OFF -DENABLE_CLI=OFF -DENABLE_TESTS=OFF -DREBUILD_WEB=OFF

echo "🏗 Compiling and natively installing core..."

# Build the project
cmake --build . --config Debug

# Perform native install — CMake will automatically collect the library and headers into the build_install folder
cmake --install .

cd ..

echo "📦 Merging native core library with external third-party dependencies..."

# The main core library is now guaranteed to be in build_install/lib/libtransmission.a# We just need to mix in the third-party libraries (dht, libevent, etc.) with it
THIRD_PARTY_LIBS=$(find build_cmake/third-party -name "*.a")

mkdir -p build_install/monolith
libtool -static -o build_install/monolith/libtransmission_combined.a build_install/lib/libtransmission.a $THIRD_PARTY_LIBS

echo "🚀 Packaging everything into XCFramework for SPM..."
mkdir -p transmission-spm

xcodebuild -create-xcframework \
  -library build_install/monolith/libtransmission_combined.a \
  -headers build_install/include/transmission \
  -output TransmissionSPM/libtransmission.xcframework

echo "✍️ Generating Package.swift..."
cat << 'EOF' > TransmissionSPM/Package.swift
// swift-tools-version:5.9
import PackageDescription

let package = Package(
    name: "LibTransmission",
    platforms: [.macOS(.v12)],
    products: [
        .library(name: "LibTransmission", targets: ["LibTransmission"])
    ],
    targets: [
        .binaryTarget(
            name: "LibTransmission",
            path: "libtransmission.xcframework"
        )
    ]
)
EOF

## Clean up heavy build artifacts, leaving only the ready SPM package
rm -rf build_cmake
rm -rf build_install
echo "🎉 PERFECT! Native local SPM package is ready in the transmission-spm/ folder"
}

function build_static_library {

#!/bin/bash
set -e

cd "$(dirname "$0")"

rm -rf build_cmake
rm -rf build_output

mkdir -p build_cmake
cd build_cmake

echo "🛠 CMake Configuration..."

cmake -G "Ninja" .. \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" \
  -DENABLE_IPO=OFF \
  -DINSTALL_LIB=ON \
  -DCMAKE_INSTALL_PREFIX=../build_output \
  -DENABLE_DAEMON=OFF -DENABLE_GTK=OFF -DENABLE_QT=OFF -DENABLE_MAC=OFF \
  -DENABLE_UTILS=OFF -DENABLE_CLI=OFF -DENABLE_TESTS=OFF

echo "🏗 Build and Install..."

cmake --build . --config Debug
cmake --install .

cd ..

echo "📦 Merging everything into ONE monolithic libtransmission_final.a file..."

THIRD_PARTY_LIBS=$(find build_cmake/third-party -name "*.a")

mkdir -p build_output/final_lib

## Merge the native library and all third-party junk into ONE file
libtool -static -o build_output/final_lib/libtransmission_final.a build_output/lib/libtransmission.a $THIRD_PARTY_LIBS

## clean up leftovers
rm -rf build_cmake

echo "🎉 Done! Clean library and headers are located in the build_output/ folder"
}

install_as_spm
