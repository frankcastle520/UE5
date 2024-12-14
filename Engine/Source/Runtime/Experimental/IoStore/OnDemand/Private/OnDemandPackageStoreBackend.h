// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IO/IoStatus.h"
#include "IO/PackageStore.h"
#include "Templates/SharedPointer.h"

struct FIoContainerHeader;
using FSharedContainerHeader = TSharedPtr<FIoContainerHeader>;

namespace UE::IoStore
{

class IOnDemandPackageStoreBackend
	: public IPackageStoreBackend
{
public:
	virtual ~IOnDemandPackageStoreBackend() = default;

	virtual FIoStatus Mount(FString ContainerName, FSharedContainerHeader ContainerHeader) = 0;
	virtual FIoStatus Unmount(const FString& ContainerName) = 0;
	virtual FIoStatus UnmountAll() = 0;
};

TSharedPtr<IOnDemandPackageStoreBackend> MakeOnDemandPackageStoreBackend();

} // namespace UE
