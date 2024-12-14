// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/StringFwd.h"
#include "HAL/IPlatformFileModule.h"
#include "HAL/PlatformFileManager.h"
#include "IStorageServerPlatformFile.h"
#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"

#if !UE_BUILD_SHIPPING

class IStorageServerClientModule : public IPlatformFileModule
{
public:
	static FORCEINLINE IStorageServerClientModule& Get()
	{
		return FModuleManager::LoadModuleChecked<IStorageServerClientModule>("StorageServerClient");
	}
	static FORCEINLINE IStorageServerPlatformFile* FindStorageServerPlatformFile()
	{
		return static_cast<IStorageServerPlatformFile*>(FPlatformFileManager::Get().FindPlatformFile(TEXT("StorageServer")));
	}

	virtual IStorageServerPlatformFile* TryCreateCustomPlatformFile(FStringView StoreDirectory, IPlatformFile* Inner) = 0;
};

#endif // !UE_BUILD_SHIPPING
