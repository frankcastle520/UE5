// Copyright Epic Games, Inc. All Rights Reserved.

#include "NDIMediaModule.h"

#include "GenericPlatform/GenericPlatformMisc.h"
#include "HAL/PlatformProcess.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/MessageDialog.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "NDIMediaAPI.h"
#include "NDIMediaCapture.h"
#include "NDIMediaSettings.h"

#define LOCTEXT_NAMESPACE "NDIMediaModule"

FNDIMediaRuntimeLibrary::FNDIMediaRuntimeLibrary(const FString& InLibraryPath)
{
	LibraryPath = InLibraryPath;

	if (LibraryPath.IsEmpty())
	{
		UE_LOG(LogNDIMedia, Error, TEXT("Unable to load NDI runtime library: Specified Path is empty."));
		return;
	}

	const FString LibraryDirectory = FPaths::GetPath(LibraryPath);
	FPlatformProcess::PushDllDirectory(*LibraryDirectory);
	LibHandle = FPlatformProcess::GetDllHandle(*LibraryPath);
	FPlatformProcess::PopDllDirectory(*LibraryDirectory);

	if (LibHandle)
	{
		typedef const NDIlib_v5* (*NDIlib_v5_load_ptr)(void);
		if (const NDIlib_v5_load_ptr NDILib_v5_load = static_cast<NDIlib_v5_load_ptr>(FPlatformProcess::GetDllExport(LibHandle, TEXT("NDIlib_v5_load"))))
		{
			Lib = NDILib_v5_load();
			if (Lib != nullptr)
			{
				// Not required, but "correct" (see the SDK documentation)
				if (Lib->initialize())
				{
					UE_LOG(LogNDIMedia, Log, TEXT("NDI runtime library loaded and initialized: \"%s\"."), *LibraryPath);
				}
				else
				{
					Lib = nullptr;
					UE_LOG(LogNDIMedia, Error, TEXT("Unable to initialize NDI library from \"%s\"."), *LibraryPath);
				}
			}
			else
			{
				UE_LOG(LogNDIMedia, Error, TEXT("Unable to load NDI runtime library interface via \"NDIlib_v5_load\" from \"%s\"."), *LibraryPath);
			}
		}
		else
		{
			UE_LOG(LogNDIMedia, Error, TEXT("Unable to load NDI runtime library entry point: \"NDIlib_v5_load\" from \"%s\"."), *LibraryPath);
		}
	}
	else
	{
		UE_LOG(LogNDIMedia, Error, TEXT("Unable to load NDI runtime library: \"%s\"."), *LibraryPath);
	}

	if (Lib == nullptr && LibHandle != nullptr)
	{
		FPlatformProcess::FreeDllHandle(LibHandle);
		LibHandle = nullptr;
	}
}

FNDIMediaRuntimeLibrary::~FNDIMediaRuntimeLibrary()
{
	if (Lib != nullptr)
	{
		// Not required, but nice.
		Lib->destroy();
	}

	// Free the dll handle
	if (LibHandle != nullptr)
	{
		FPlatformProcess::FreeDllHandle(LibHandle);
	}
}

void FNDIMediaModule::StartupModule()
{
#if UE_EDITOR
	if (UNDIMediaSettings* Settings = GetMutableDefault<UNDIMediaSettings>())
	{
		Settings->OnSettingChanged().AddRaw(this, &FNDIMediaModule::OnNDIMediaSettingsChanged);
	}
#endif

	if (!LoadModuleDependencies())
	{
		UE_LOG(LogNDIMedia, Error, TEXT("Unable to load \"" NDILIB_LIBRARY_NAME "\" from the specified location(s)."));
	}
}

void FNDIMediaModule::ShutdownModule()
{
#if UE_EDITOR
	if (UObjectInitialized())
	{
		UNDIMediaSettings* Settings = GetMutableDefault<UNDIMediaSettings>();
		Settings->OnSettingChanged().RemoveAll(this);
	}
#endif	

	NDILib.Reset();
}

namespace UE::NDIMedia::Private
{
	static const TCHAR* DefaultLibraryName = TEXT(NDILIB_LIBRARY_NAME);
	static const TCHAR* DefaultVariableName = TEXT(NDILIB_REDIST_FOLDER);
	
	FString GetRuntimeLibraryFullPath(bool bInUseBundled = true, const FString& InPathOverride = FString())
	{
		FString LibraryPath;
	
		if (bInUseBundled)
		{
			LibraryPath = FPaths::Combine(IPluginManager::Get().FindPlugin(TEXT("NDIMedia"))->GetBaseDir(), TEXT("/Binaries/ThirdParty/Win64"));
		}
		else
		{
			LibraryPath = !InPathOverride.IsEmpty() ? InPathOverride : FPlatformMisc::GetEnvironmentVariable(DefaultVariableName);
		}

		return FPaths::Combine(LibraryPath, DefaultLibraryName);
	}

	void UpdateLibraryFullPath(UNDIMediaSettings* InSettings, const TSharedPtr<FNDIMediaRuntimeLibrary>& InNDILib)
	{
		if (InSettings)
		{
			if (InNDILib.IsValid() && InNDILib->IsLoaded())
			{
				InSettings->LibraryFullPath = InNDILib->LibraryPath;
			}
			else
			{
				InSettings->LibraryFullPath.Reset();
			}
		}
	}
}

bool FNDIMediaModule::LoadModuleDependencies()
{
	UNDIMediaSettings* Settings = GetMutableDefault<UNDIMediaSettings>();
	
	using namespace UE::NDIMedia::Private;
	FString LibraryPath = GetRuntimeLibraryFullPath(Settings->bUseBundledLibrary, Settings->LibraryDirectoryOverride);

	NDILib = MakeShared<FNDIMediaRuntimeLibrary>(LibraryPath);
	bool bIsLoaded = NDILib.IsValid() && NDILib->IsLoaded();

	// Fallback to bundled library if something was wrong with system one.
	if(!bIsLoaded && !Settings->bUseBundledLibrary)
	{
		LibraryPath = GetRuntimeLibraryFullPath();
		UE_LOG(LogNDIMedia, Warning, TEXT("Falling back to bundled NDI runtime library: \"%s\"."), *LibraryPath);
		NDILib = MakeShared<FNDIMediaRuntimeLibrary>(LibraryPath);
		bIsLoaded = NDILib.IsValid() && NDILib->IsLoaded();
	}

	UpdateLibraryFullPath(Settings, NDILib);

	return bIsLoaded;
}

#if UE_EDITOR
void FNDIMediaModule::OnNDIMediaSettingsChanged(UObject* InSettings, struct FPropertyChangedEvent& InPropertyChangedEvent)
{
	const UNDIMediaSettings* NDIMediaSettings = Cast<UNDIMediaSettings>(InSettings);
	if (!NDIMediaSettings)
	{
		return;
	}

	const FName Name = InPropertyChangedEvent.GetPropertyName();
	
	static const FName bUseBundledLibraryName = GET_MEMBER_NAME_CHECKED(UNDIMediaSettings, bUseBundledLibrary);
	static const FName LibraryDirectoryOverrideName = GET_MEMBER_NAME_CHECKED(UNDIMediaSettings, LibraryDirectoryOverride);

	if (Name == bUseBundledLibraryName || Name == LibraryDirectoryOverrideName)
	{
		OnRuntimeLibrarySettingsChanged(NDIMediaSettings);
	}
}

void FNDIMediaModule::OnRuntimeLibrarySettingsChanged(const UNDIMediaSettings* InSettings)
{
	using namespace UE::NDIMedia::Private;
	const FString NewLibraryPath = GetRuntimeLibraryFullPath(InSettings->bUseBundledLibrary, InSettings->LibraryDirectoryOverride);

	if (!NDILib || NDILib->LibraryPath != NewLibraryPath)
	{
		const TSharedPtr<FNDIMediaRuntimeLibrary> NewNDILib = MakeShared<FNDIMediaRuntimeLibrary>(NewLibraryPath);
		if (NewNDILib && NewNDILib->IsLoaded())
		{
			NDILib = NewNDILib;
			UpdateLibraryFullPath(GetMutableDefault<UNDIMediaSettings>(), NDILib);
		}
		else if (NDILib && NDILib->IsLoaded())
		{
			UE_LOG(LogNDIMedia, Log, TEXT("Keeping current NDI runtime library: \"%s\"."), *NDILib->LibraryPath);
		}
		else
		{
			UE_LOG(LogNDIMedia, Error, TEXT("No NDI runtime library could be loaded."));
			UpdateLibraryFullPath(GetMutableDefault<UNDIMediaSettings>(), NDILib);
		}
	}
	else
	{
		UE_LOG(LogNDIMedia, Log, TEXT("NDI runtime library already loaded: \"%s\"."), *NewLibraryPath);
	}
}
#endif

#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(FNDIMediaModule, NDIMedia)
