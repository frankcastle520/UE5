// Copyright Epic Games, Inc. All Rights Reserved.

#include "StorageServerClientModule.h"

#include "Misc/CommandLine.h"
#include "Misc/Paths.h"
#include "StorageServerPlatformFile.h"

#if !UE_BUILD_SHIPPING

class FStorageServerClientModule : public IStorageServerClientModule
{
public:
	FStorageServerClientModule() = default;
	~FStorageServerClientModule() = default;

	// ~Begin IModuleInterface
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
	// ~End IModuleInterface

	// ~Begin IPlatformFileModule
	virtual IPlatformFile* GetPlatformFile() override;
	// ~End IPlatformFileModule

	// ~Begin IStorageServerClientModule
	virtual IStorageServerPlatformFile* TryCreateCustomPlatformFile(FStringView StoreDirectory, IPlatformFile* Inner) override;
	// ~End IStorageServerClientModule

	IPlatformFile* CachedDefaultPlatformFileInstance = nullptr;
};

void FStorageServerClientModule::StartupModule()
{
}

void FStorageServerClientModule::ShutdownModule()
{
}

IPlatformFile* FStorageServerClientModule::GetPlatformFile()
{
	static TUniquePtr<IPlatformFile> DefaultPlatformFileInstance = MakeUnique<FStorageServerPlatformFile>();
	if (!CachedDefaultPlatformFileInstance)
	{
		CachedDefaultPlatformFileInstance = DefaultPlatformFileInstance.Get();
	}
	return DefaultPlatformFileInstance.Get();
}

IStorageServerPlatformFile* FStorageServerClientModule::TryCreateCustomPlatformFile(FStringView StoreDirectory, IPlatformFile* Inner)
{
	TUniquePtr<FStorageServerPlatformFile> StorageServerPlatformFile = MakeUnique<FStorageServerPlatformFile>();
	StorageServerPlatformFile->SetCustomProjectStorePath(StoreDirectory);
	StorageServerPlatformFile->SetAllowPackageIo(false);
	StorageServerPlatformFile->SetAbortOnConnectionFailure(false);
	const TCHAR* CmdLine = FCommandLine::Get();
	if (StorageServerPlatformFile->ShouldBeUsed(Inner, CmdLine) && StorageServerPlatformFile->Initialize(Inner, CmdLine))
	{
		if (FPaths::IsProjectFilePathSet())
		{
			StorageServerPlatformFile->InitializeAfterProjectFilePath();
		}
		return StorageServerPlatformFile.Release();
	}
	return nullptr;
}

IMPLEMENT_MODULE(FStorageServerClientModule, StorageServerClient)

#endif // !UE_BUILD_SHIPPING
