#!/bin/bash

# Configuration parameters
readonly MPCDI_ARCH_NAME=mpcdi.zip
readonly MPCDI_SOURCES_DIR=mpcdi

readonly BUILD_CONFIGURATION=Release
readonly ARCH_NAME=x86_64-unknown-linux-gnu
readonly UE_ENGINE_LOCATION=`cd $(pwd)/../../../../../../../..; pwd`
readonly UE_THIRD_PARTY_LOCATION=$UE_ENGINE_LOCATION/Source/ThirdParty
readonly SOURCE_LOCATION=$(pwd)/../$MPCDI_SOURCES_DIR

# Run Engine/Build/BatchFiles/Linux/SetupToolchain.sh first to ensure
# that the toolchain is setup and verify that this name matches.
# Also, toolchain version is listed in Engine/Config/Linux/Linux_SDK.json
readonly TOOLCHAIN_NAME=v22_clang-16.0.6-centos7

readonly UE_TOOLCHAIN_LOCATION="$UE_ENGINE_LOCATION/Extras/ThirdPartyNotUE/SDKs/HostLinux/Linux_x64/$TOOLCHAIN_NAME/$ARCH_NAME"

C_COMPILER="$UE_TOOLCHAIN_LOCATION/bin/clang"
CXX_COMPILER="$UE_TOOLCHAIN_LOCATION/bin/clang++"

CXX_FLAGS="-std=c++11 -stdlib=libc++ -pthread -fPIC -I$UE_THIRD_PARTY_LOCATION/Unix/LibCxx/include/c++/v1"
CXX_LINKER="-L$UE_THIRD_PARTY_LOCATION/Unix/LibCxx/lib/Unix/$ARCH_NAME/ -lc++"
CMAKE_ARGS=(
    -DCMAKE_POLICY_DEFAULT_CMP0056=NEW
    -DCMAKE_C_COMPILER="$C_COMPILER" 
    -DCMAKE_CXX_COMPILER="$CXX_COMPILER" 
    -DCMAKE_CXX_FLAGS="$CXX_FLAGS" 
    -DCMAKE_EXE_LINKER_FLAGS="$CXX_LINKER" 
    -DCMAKE_MODULE_LINKER_FLAGS="$CXX_LINKER" 
    -DCMAKE_SHARED_LINKER_FLAGS="$CXX_LINKER")

# Debugging options
set -e

# Remove local sources
cleanSources() {
  if [[ -d $MPCDI_SOURCES_DIR ]]; then
    echo "Cleaning MPCDI sources..."
    rm -rf $MPCDI_SOURCES_DIR
  fi
}

# Unpack sources
unpackSources() {
  if [[ ! -d $MPCDI_SOURCES_DIR ]]; then
    echo "Unpacking MPCDI sources..."
    unzip $MPCDI_ARCH_NAME
  fi
}

# Build library
buildLib() {
  if [[ -d $MPCDI_SOURCES_DIR ]]; then
    pushd $MPCDI_SOURCES_DIR

    echo "Configuring MPCDI for (64-bit, $BUILD_CONFIGURATION)..."
    cmake -G "Unix Makefiles" . -DCMAKE_BUILD_TYPE=$BUILD_CONFIGURATION "${CMAKE_ARGS[@]}"

    echo "Building MPCDI for (64-bit, $BUILD_CONFIGURATION)..."
    cmake --build ./ --target mpcdi

    popd
  fi
}

# Deploy library
deployLib() {
  if [[ -d $MPCDI_SOURCES_DIR ]]; then
    echo "Deploying MPCDI..."

    mkdir -p ../lib/Linux/$ARCH_NAME/$BUILD_CONFIGURATION
    cp -f $MPCDI_SOURCES_DIR/libmpcdi.a ../lib/Linux/$ARCH_NAME/$BUILD_CONFIGURATION

    mkdir -p ../include
    rsync -a --prune-empty-dirs --include '*/' --include '*.h' --exclude '*' $MPCDI_SOURCES_DIR/* ../include
  fi
}


# Build pipeline
pushd ..
cleanSources
unpackSources
buildLib
deployLib
cleanSources
popd

