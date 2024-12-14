// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Modules/ModuleManager.h"

class UNDIMediaSettings;
struct NDIlib_v5;

class FNDIMediaRuntimeLibrary
{
public:
	FNDIMediaRuntimeLibrary(const FString& InLibraryPath);
	~FNDIMediaRuntimeLibrary();

	bool IsLoaded() const
	{
		return Lib != nullptr;
	}

	/** Dynamically loaded function pointers for the NDI lib API.*/
	const NDIlib_v5* Lib = nullptr;
	
	/** Handle to the NDI runtime dll. */
	void* LibHandle = nullptr;

	FString LibraryPath;
};

class FNDIMediaModule : public IModuleInterface
{
public:
	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	/**
	 * Returns a handle to the currently loaded NDI runtime library.
	 * Objects holding runtime resources should also keep a ref on the library.
	 */
	static TSharedPtr<FNDIMediaRuntimeLibrary> GetNDIRuntimeLibrary()
	{
		FNDIMediaModule* NDIMediaModule = FModuleManager::GetModulePtr<FNDIMediaModule>(TEXT("NDIMedia"));
		if (NDIMediaModule != nullptr)
		{
			return NDIMediaModule->NDILib;
		}
		return nullptr;
	}
	
private:
	bool LoadModuleDependencies();

#if UE_EDITOR
	void OnNDIMediaSettingsChanged(UObject* InSettings, struct FPropertyChangedEvent& InPropertyChangedEvent);
	void OnRuntimeLibrarySettingsChanged(const UNDIMediaSettings* InSettings);
#endif
	
	TSharedPtr<FNDIMediaRuntimeLibrary> NDILib;
};
