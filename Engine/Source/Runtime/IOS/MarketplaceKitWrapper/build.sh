# Copyright Epic Games, Inc. All Rights Reserved.

rm -rf .build
mkdir .build
pushd .build
# TODO it shouldn't be needed to specify target twice, remove CMAKE_Swift_FLAGS_INIT in future cmake versions
cmake -G Ninja -DCMAKE_SYSTEM_NAME=iOS -DCMAKE_OSX_DEPLOYMENT_TARGET=15.0 -DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_Swift_FLAGS_INIT="-target arm64-apple-ios15.0" ..
ninja -v
popd
cat Notice.txt MarketplaceKitWrapper.h > MarketplaceKitWrapperTemp.h
mv MarketplaceKitWrapperTemp.h MarketplaceKitWrapper.h
cp .build/libMarketplaceKitWrapper.a .
