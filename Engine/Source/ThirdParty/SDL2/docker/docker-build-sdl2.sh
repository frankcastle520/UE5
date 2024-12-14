#!/bin/bash

## Unreal Engine 4 Build script for SDL2
## Copyright Epic Games, Inc. All Rights Reserved.

# Should be run in docker image, launched something like this (see RunMe.sh script):
#   docker run --name ${ImageName} --platform linux/arm64 -v ${SCRIPT_DIR}/../../Vulkan:/Vulkan -v ${SDL_DIR}:/SDL-gui-backend -v ${SCRIPT_DIR}:/src ${Image} /src/docker-build-sdl2.sh
#
# Expects these mapped directories:
#   /Vulkan: vulkan sdk
#   /SDL-gui-backend: SDL2 source
#
# Built libSDL libraries are in /build directory

DISTRO=${1:-Rocky8}

if [ $UID -eq 0 ]; then

	if [ ${DISTRO} == "CentOS7" ]; then
		# Centos 7

		# first we need to fix up the yum repos since CentOS 7 is EOL and mirrorlist.centos.org is now offline
		sed -i s/mirror.centos.org/vault.centos.org/g /etc/yum.repos.d/*.repo
		sed -i s/^#.*baseurl=http/baseurl=http/g /etc/yum.repos.d/*.repo
		sed -i s/^mirrorlist=http/#mirrorlist=http/g /etc/yum.repos.d/*.repo

		# now install stuff
		yum install -y epel-release
		yum install -y cmake3 make gcc-c++
		yum install -y libXcursor-devel libXinerama-devel libxi-dev libXrandr-devel libXScrnSaver-devel libXi-devel mesa-libGL-devel mesa-libEGL-devel pulseaudio-libs-devel wayland-protocols-devel wayland-devel libxkbcommon-devel mesa-libwayland-egl-devel alsa-lib-devel libudev-devel
	elif [ ${DISTRO} == "Rocky8" ]; then
		yum install -y epel-release
		yum install -y gcc gcc-c++ git-core make cmake \
			alsa-lib-devel pulseaudio-libs-devel pipewire-devel libX11-devel \
			libXext-devel libXrandr-devel libXcursor-devel libXfixes-devel \
			libXi-devel libXScrnSaver-devel dbus-devel systemd-devel \
			mesa-libGL-devel libxkbcommon-devel mesa-libGLES-devel \
			mesa-libEGL-devel vulkan-devel wayland-devel wayland-protocols-devel libdrm-devel 

	else
		echo Unsupported distro ${DISTRO}
		exit 1
	fi

	# Create non-privileged user and workspace
	adduser buildmaster
	mkdir -p /build
	chown buildmaster:nobody -R /build
	cd /build

	exec su buildmaster "$0"
fi

# This will be run from user buildmaster

export VULKAN_SDK=/Vulkan
export SDL_DIR=/SDL-gui-backend

export ARCH=$(uname -m)

# Get num of cores
export CORES=$(getconf _NPROCESSORS_ONLN)
echo Using ${CORES} cores for building

BuildWithOptions()
{
	local BuildDir=$1
	local StaticLibName=$2
	local SdlLibName=$3
	shift 3
	local Options="$@"

	rm -rf $BuildDir
	mkdir -p $BuildDir
	pushd $BuildDir

	# Building with OGL breaks SDL_CreateWindow() on embedded devices w/o proper GL libraries
	#   http://lists.libsdl.org/pipermail/commits-libsdl.org/2017-September/001967.html
	if [[ ${ARCH} == 'aarch64' ]]; then
		Options+=' -DSDL_VIDEO_OPENGL=OFF'
	fi

	set -x
	cmake3 $Options ${SDL_DIR}
	set +x

	make -j${CORES}

	mv $StaticLibName ../$SdlLibName
	popd
}

set -e

OPTS=()
OPTS+=(-DSDL_SHARED_ENABLED_BY_DEFAULT=OFF)
OPTS+=(-DSDL_STATIC_ENABLED_BY_DEFAULT=ON)
OPTS+=(-DSDL_KMSDRM=OFF)
OPTS+=(-DCMAKE_C_FLAGS=-gdwarf-4)

# build Debug with -fPIC so it's usable in any type of build
BuildWithOptions Debug      libSDL2d.a libSDL2_fPIC_Debug.a -DCMAKE_BUILD_TYPE=Debug   -DSDL_STATIC_PIC=ON   "${OPTS[@]}"
BuildWithOptions Release    libSDL2.a  libSDL2.a            -DCMAKE_BUILD_TYPE=Release                       "${OPTS[@]}"
BuildWithOptions ReleasePIC libSDL2.a  libSDL2_fPIC.a       -DCMAKE_BUILD_TYPE=Release -DSDL_STATIC_PIC=ON   "${OPTS[@]}"

set +e
