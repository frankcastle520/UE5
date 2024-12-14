#!/bin/bash

set -x
set -eu

ToolChainVersion=v23
LLVM_VERSION_MAJOR=18
LLVM_VERSION=${LLVM_VERSION_MAJOR}.1.0
LLVM_BRANCH=release/${LLVM_VERSION_MAJOR}.x
LLVM_TAG=llvmorg-${LLVM_VERSION}

LLVM_URL=https://github.com/llvm/llvm-project/releases/download/llvmorg-${LLVM_VERSION}
ZLIB_PATH=/src/1.3

ToolChainVersionName="${ToolChainVersion}_clang-${LLVM_VERSION}-rockylinux8"

TARGETS="x86_64-unknown-linux-gnu aarch64-unknown-linux-gnueabi"

OutputDirLinux=/src/build/OUTPUT-linux
OutputDirWindows=/src/build/OUTPUT-windows
InstallClangDir=/src/build/install-clang

# Default permissions
umask 0022

# Get num of cores
CORES=$(getconf _NPROCESSORS_ONLN)
echo Using $CORES cores for building

echo "check_certificate=off" > "$HOME/.wgetrc"

if [ ! -f "/src/build/ct-ng-build.done" ]; then
	if [ ! -d "/src/build/crosstool-ng" ]; then
		# Get crosstool-ng
		git clone http://github.com/crosstool-ng/crosstool-ng -b crosstool-ng-1.26.0
	fi

	# Build crosstool-ng
	pushd crosstool-ng
	./bootstrap && ./configure --enable-local && make
	popd

	# Build linux toolchain to OUTPUT-linux
	for arch in $TARGETS; do
		mkdir -p build-linux-$arch
		pushd build-linux-$arch
		cp -f /src/$arch.linux.config .config
		../crosstool-ng/ct-ng build.$CORES
		popd
	done

	# Build windows toolchain to OUTPUT-windows
	for arch in $TARGETS; do
		mkdir -p build-windows-$arch
		pushd build-windows-$arch
		cp -f /src/$arch.windows.config .config
		../crosstool-ng/ct-ng build.$CORES
		popd
	done
	touch /src/build/ct-ng-build.done
fi

# since we are -u in the bash script and this ENV is not set it complains when source the devtoolset-7
export MANPATH=""

# need to unset this or crosstools complains
unset LD_LIBRARY_PATH

#
# Linux
#

echo "Cloning LLVM (tag $LLVM_TAG only)"
# clone -b can also accept tag names
if ! [ -d llvm-src ]; then
	git clone https://github.com/llvm/llvm-project llvm-src -b ${LLVM_TAG} --single-branch --depth 1 -c advice.detachedHead=false
	pushd llvm-src
	git -c advice.detachedHead=false checkout tags/${LLVM_TAG} -b ${LLVM_BRANCH}
	popd
fi


# Leaving these for now in case testing reveals that one or more of them are still needed
# this fixes an issue where AT_HWCAP2 is just not defined correctly in our sysroot. This is likely due to
# AT_HWCAP2 being around since glibc 2.18 offically, while we are still stuck on 2.17 glibc.
#patch -d llvm-src -p 1 < /src/patches/compiler-rt/manually-define-AT_HWCAP2.diff

# this fixes lack of HWCAP_CRC32 in the old glibc (similar issue)
#patch -d llvm-src -p 1 < /src/patches/compiler-rt/cpu_model_define_HWCAP_CRC32.diff

# move back to defaulting to dwarf 4, as if we leave to dwarf 5 libs built with dwarf5 will force everything to dwarf5
# even if you request dwarf 4. dwarf 5 currently causes issues with dump_syms and gdb/lldb earlier versions
#patch -d llvm-src -p 1 < /src/patches/clang/default-dwarf-4.patch

# add a patch to disable auto-upgrade of debug info. It missed clang 16.x, so this patch shouldn't be needed for clang 17.x going forward
# See https://reviews.llvm.org/D143229 for context
#patch -d llvm-src -p 1 < /src/patches/llvm/disable-auto-upgrade-debug-info.patch

# LLVM has just failed to support stand-alone LLD build, cheat by moving a required header into a location it can be found easily
# if you fulling include this you end up breaking other things. https://github.com/llvm/llvm-project/issues/48572
#cp -rf llvm-src/libunwind/include/mach-o/ llvm-src/llvm/include

if [ ! -f build-clang.done ]; then
	mkdir -p build-clang
	pushd build-clang
		# CMake Error at cmake/modules/CheckCompilerVersion.cmake:40 (message):
		#   Host GCC version should be at least 5.1 because LLVM will soon use new C++
		#   features which your toolchain version doesn't support.  Your version is
		#   4.8.5.  You can temporarily opt out using
		#   LLVM_TEMPORARILY_ALLOW_OLD_TOOLCHAIN, but very soon your toolchain won't be
		#   supported.
		cmake3 -G "Unix Makefiles" ../llvm-src/llvm \
			-DLLVM_ENABLE_PROJECTS=llvm\;clang\;lld\;compiler-rt \
			-DCMAKE_BUILD_TYPE=Release \
			-DLLVM_ENABLE_TERMINFO=OFF \
			-DLLVM_ENABLE_LIBXML2=OFF \
			-DLLVM_ENABLE_ZLIB=FORCE_ON \
			-DZLIB_LIBRARY="$ZLIB_PATH/lib/Unix/x86_64-unknown-linux-gnu/Release/libz.a" \
			-DZLIB_INCLUDE_DIR="$ZLIB_PATH/include" \
			-DLLVM_ENABLE_LIBCXX=1 \
			-DCMAKE_INSTALL_PREFIX=${InstallClangDir} \
			-DLLVM_INCLUDE_BENCHMARKS=OFF \
			-DLLVM_TARGETS_TO_BUILD="AArch64;X86" \
			-DCLANG_REPOSITORY_STRING="github.com/llvm/llvm-project" \
	
		make -j$CORES && make install
	popd
	touch build-clang.done
fi


# Copy files
if [ ! -f copy-linux-toolchain.done ]; then
	for arch in $TARGETS; do
		echo "Copying ${arch} toolchain..."

		pushd ${OutputDirLinux}/$arch/
			chmod -R +w .

			if [ -d "$arch" ]; then
				# copy $arch/include/c++ to include/c++
				cp -r -L $arch/include .

				# copy usr lib64 and include dirs
				mkdir -p usr
				cp -r -L $arch/sysroot/usr/include usr
				cp -r -L $arch/sysroot/usr/lib64 usr
				cp -r -L $arch/sysroot/usr/lib usr

				cp -r -L $arch/sysroot/lib64 .
				cp -r -L $arch/sysroot/lib .

				[[ -f build.log.bz2 ]] && mv build.log.bz2 ../../build-linux-$arch.log.bz2

				rm -rf $arch
			fi
		popd

		echo "Copying clang..."
		cp -L ${InstallClangDir}/bin/clang           ${OutputDirLinux}/$arch/bin/
		cp -L ${InstallClangDir}/bin/clang++         ${OutputDirLinux}/$arch/bin/
		cp -L ${InstallClangDir}/bin/lld             ${OutputDirLinux}/$arch/bin/
		cp -L ${InstallClangDir}/bin/ld.lld          ${OutputDirLinux}/$arch/bin/
		cp -L ${InstallClangDir}/bin/llvm-ar         ${OutputDirLinux}/$arch/bin/
		cp -L ${InstallClangDir}/bin/llvm-profdata   ${OutputDirLinux}/$arch/bin/
		cp -L ${InstallClangDir}/bin/llvm-objcopy    ${OutputDirLinux}/$arch/bin/
		cp -L ${InstallClangDir}/bin/llvm-symbolizer ${OutputDirLinux}/$arch/bin/
		cp -L ${InstallClangDir}/bin/llvm-cov        ${OutputDirLinux}/$arch/bin/

		if [ "$arch" == "x86_64-unknown-linux-gnu" ]; then
			cp -r -L ${InstallClangDir}/lib/clang ${OutputDirLinux}/$arch/lib/
		fi
	done
	touch copy-linux-toolchain.done
fi

# Build compiler-rt
if [ ! -f build-runtime.done ]; then
for arch in $TARGETS; do
	if [ "$arch" == "x86_64-unknown-linux-gnu" ]; then
		# We already built it with clang
		continue
	fi

	mkdir -p ${OutputDirLinux}/$arch/lib/clang/${LLVM_VERSION_MAJOR}/{lib,share,include}

	# copy share + include files (same as x86_64)
	cp -r ${OutputDirLinux}/x86_64-unknown-linux-gnu/lib/clang/${LLVM_VERSION_MAJOR}/share/* ${OutputDirLinux}/$arch/lib/clang/${LLVM_VERSION_MAJOR}/share/
	cp -r ${OutputDirLinux}/x86_64-unknown-linux-gnu/lib/clang/${LLVM_VERSION_MAJOR}/include/* ${OutputDirLinux}/$arch/lib/clang/${LLVM_VERSION_MAJOR}/include/

	mkdir -p build-rt-$arch
	pushd build-rt-$arch

		cmake3 -G "Unix Makefiles" ../llvm-src/compiler-rt \
			-DCMAKE_BUILD_TYPE=Release \
			-DCMAKE_SYSTEM_NAME="Linux" \
			-DCOMPILER_RT_DEFAULT_TARGET_ONLY=ON \
			-DCMAKE_C_COMPILER_TARGET="$arch" \
			-DCMAKE_C_COMPILER=${InstallClangDir}/bin/clang \
			-DCMAKE_CXX_COMPILER=${InstallClangDir}/bin/clang++ \
			-DCMAKE_LD=${InstallClangDir}/bin/lld \
			-DCMAKE_AR=${InstallClangDir}/bin/llvm-ar \
			-DCMAKE_NM=${InstallClangDir}/bin/llvm-nm \
			-DCMAKE_RANLIB=${InstallClangDir}/bin/llvm-ranlib \
			-DLLVM_ENABLE_ZLIB=FORCE_ON \
			-DZLIB_LIBRARY="$ZLIB_PATH/lib/Unix/x86_64-unknown-linux-gnu/Release/libz.a" \
			-DZLIB_INCLUDE_DIR="$ZLIB_PATH/include" \
			-DCMAKE_EXE_LINKER_FLAGS="--target=$arch -L${OutputDirLinux}/$arch/lib64 --sysroot=${OutputDirLinux}/$arch -fuse-ld=lld" \
			-DCMAKE_C_FLAGS="--target=$arch --sysroot=${OutputDirLinux}/$arch" \
			-DCMAKE_CXX_FLAGS="--target=$arch --sysroot=${OutputDirLinux}/$arch" \
			-DCMAKE_ASM_FLAGS="--target=$arch --sysroot=${OutputDirLinux}/$arch" \
			-DCOMPILER_RT_BUILD_ORC=OFF \
			-DCOMPILER_RT_BUILD_LIBFUZZER=OFF \
			-DCMAKE_INSTALL_PREFIX=../install-rt-$arch \
			-DSANITIZER_COMMON_LINK_FLAGS="-fuse-ld=lld" \
			-DSCUDO_LINK_FLAGS="-fuse-ld=lld" \
			-DLLVM_LIBC_INCLUDE_SCUDO=ON \
			-DCOMPILER_RT_BUILD_XRAY=OFF \
			-DLLVM_CONFIG_PATH=${InstallClangDir}/bin/llvm-config \

		make -j$CORES && make install

	popd

	echo "Copying compiler rt..."
	cp -r install-rt-$arch/lib/* ${OutputDirLinux}/$arch/lib/clang/${LLVM_VERSION_MAJOR}/lib/
done

touch build-runtime.done
fi

# Create version file
echo "${ToolChainVersionName}" > ${OutputDirLinux}/ToolchainVersion.txt

#
# Windows
#

if [ ! -f copy-windows-toolchain.done ]; then
	for arch in $TARGETS; do
		echo "Copying Windows $arch toolchain..."

		pushd ${OutputDirWindows}/$arch/
			chmod -R +w .

			# copy $arch/include/c++ to include/c++
			cp -r -L $arch/include .

			# copy usr lib64 and include dirs
			mkdir -p usr
			cp -r -L $arch/sysroot/usr/include usr
			cp -r -L $arch/sysroot/usr/lib64 usr
			cp -r -L $arch/sysroot/usr/lib usr

			cp -r -L $arch/sysroot/lib64 .
			cp -r -L $arch/sysroot/lib .

			# Copy compiler-rt
			cp -r -L ${OutputDirLinux}/$arch/lib/clang lib/

			# Copy linux llvm-symbolizer so that we can resolve ASan hits
			cp -r -L ${OutputDirLinux}/$arch/bin/llvm-symbolizer bin

			[[ -f build.log.bz2 ]] && mv build.log.bz2 ../../build-windows-$arch.log.bz2

			rm -rf $arch
		popd
	done
	touch copy-windows-toolchain.done
fi

# Pack Linux files
pushd ${OutputDirLinux}
	mkdir -p build/{src,scripts}
	cp /src/build/build-linux-x86_64-unknown-linux-gnu/.build/tarballs/* build/src
	cp /src/build/build-linux-aarch64-unknown-linux-gnueabi/.build/tarballs/* build/src
	tar czfvh /src/build/llvm-${LLVM_VERSION}-github-snapshot.src.tar.gz --hard-dereference /src/build/llvm-src
	cp /src/build/*.src.tar.gz build/src
	cp -f /src/*.{config,sh,nsi,bat} build/scripts

	# copy the toolchain in the directory named its version as per convention
	mkdir -p ${OutputDirLinux}/${ToolChainVersionName}
	cp -rf x86_64-unknown-linux-gnu ${OutputDirLinux}/${ToolChainVersionName}
	cp -rf build ${OutputDirLinux}/${ToolChainVersionName}
	cp -rf aarch64-unknown-linux-gnueabi ${OutputDirLinux}/${ToolChainVersionName}
	cp -rf ToolchainVersion.txt ${OutputDirLinux}/${ToolChainVersionName}

	# delete libraries in x86_64's lib folder or bundled binares with crash
	find ${OutputDirLinux}/${ToolChainVersionName}/x86_64-unknown-linux-gnu/lib/ -maxdepth 1 -type f -delete

	# remove broken links before tar otherwise it will fail
	# currently, {sysroot}/lib/bfd-plugins/liblto_plugin points to a non-existent liblto_plugin in {sysroot}/libexec
	find ${OutputDirLinux} -xtype l -delete

	tar czfhv /src/build/native-linux-${ToolChainVersionName}.tar.gz --hard-dereference ${ToolChainVersionName}
popd

# Pack Windows files
pushd ${OutputDirWindows}
	mkdir -p build/{src,scripts}
	cp -f /src/build/build-windows-x86_64-unknown-linux-gnu/.build/tarballs/* build/src
	cp -f /src/build/build-windows-aarch64-unknown-linux-gnueabi/.build/tarballs/* build/src
	zip -r /src/build/llvm-${LLVM_VERSION}-github-snapshot.src.zip /src/build/llvm-src
	cp -f /src/build/*.src.zip build/src
	cp -f /src/*.{config,sh,nsi,bat} build/scripts

	zip -r /src/build/${ToolChainVersionName}-windows.zip *
popd

echo done.
