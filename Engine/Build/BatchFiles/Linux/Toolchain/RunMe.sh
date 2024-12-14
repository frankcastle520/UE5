#!/bin/bash

set -eu

SCRIPT_DIR=$(cd "$(dirname "$BASH_SOURCE")" ; pwd)
SCRIPT_NAME=$(basename "$BASH_SOURCE")

Image=rockylinux:8

DOCKER_BUILD_DIR=/src/build

# https://stackoverflow.com/questions/23513045/how-to-check-if-a-process-is-running-inside-docker-container
# https://unix.stackexchange.com/questions/607695/how-to-check-if-its-docker-or-host-machine-inside-bash-script
# cgroups 2 busted the first way, attempting a new way here but may break again in the future
if ! [ -f "/.dockerenv" ]; then

  ##############################################################################
  # host commands
  ##############################################################################

  ZLIB_PATH=../../../../../Engine/Source/ThirdParty/zlib/1.3/

  # Need to static link zlib for being able to compress debug files
  cp -rpvf $ZLIB_PATH ./

  ImageName=build_linux_toolchain

  echo docker run -t --name ${ImageName} -v "${SCRIPT_DIR}:/src" ${Image} /src/${SCRIPT_NAME}
  #docker run -t --name ${ImageName} -v "${SCRIPT_DIR}:/src" ${Image} /src/${SCRIPT_NAME}

  # Use if you want a shell when a command fails in the script
  docker run -it --name ${ImageName} -v "${SCRIPT_DIR}:/src" ${Image} bash -c "/src/${SCRIPT_NAME}; bash"

  #echo Removing ${ImageName}...
  docker rm ${ImageName}

else


  if [ $UID -eq 0 ]; then
    ##############################################################################
    # docker root commands
    ##############################################################################



  if [ "${Image}" == "centos:7" ]; then
##### CentOs 7 stuff #####
    echo "Running with Centos 7"
    sleep 5
    yum install -y epel-release centos-release-scl dnf dnf-plugins-core

    # needed for mingw due to https://pagure.io/fesco/issue/2333
    dnf -y copr enable alonid/mingw-epel7

    yum install -y ncurses-devel patch make tree zip \
        git wget which gcc-c++ gperf bison flex texinfo bzip2 help2man file unzip autoconf libtool \
        glibc-static libstdc++-devel libstdc++-static mingw64-gcc mingw64-gcc-c++ mingw64-winpthreads-static \
        devtoolset-8-gcc.x86_64 devtoolset-8-gcc-c++.x86_64 libisl-devel openssl openssl-devel python3 zlib-static

    # build a proper cmake
    pushd ${DOCKER_BUILD_DIR}
    if ! [ -f "cmake.done" ]; then
        git clone http://github.com/Kitware/CMake.git

        cd CMake
        ./bootstrap --parallel=128 && make -j128 && make install && ln -s /usr/local/bin/cmake /usr/bin/cmake3
        popd
    fi

##### End CentOs 7 stuff #####
  else
    echo "Running with Rockylinux 8"
    sleep 5
##### Rocky Linux 8 stuff #####
	dnf install -y 'dnf-command(config-manager)'
	dnf config-manager --enable powertools
	yum install -y ncurses-devel patch make cmake3 zip git wget which gcc-c++ bzip2 file unzip autoconf libtool glibc-devel libstdc++-devel xz flex bison file unzip autoconf libtool diffutils openssl openssl-devel rsync perl-libintl perl-Text-Unidecode mingw32-gcc mingw32-gcc-c++ mingw64-gcc mingw64-gcc-c++ glibc-static mingw32-winpthreads-static.noarch mingw64-winpthreads-static.noarch libstdc++-static texinfo help2man perl-Unicode-Normalize gettext python3

##### End Rocky Linux 8 stuff #####
  fi

    # Create non-privileged user and workspace
    adduser buildmaster || echo "User exists"
    mkdir -p ${DOCKER_BUILD_DIR}
    chown buildmaster:nobody -R ${DOCKER_BUILD_DIR}

    exec su buildmaster "$0"
  fi

  ##############################################################################
  # docker user level commands
  ##############################################################################
  cd ${DOCKER_BUILD_DIR}
  /src/build_linux_toolchain.sh

fi
