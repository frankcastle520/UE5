// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Delegates/Delegate.h"
#include "Modules/ModuleInterface.h"
#include "UObject/PackageReload.h"

namespace Metasound::Engine
{
#if WITH_EDITOR
	// Status of initial asset scan when editor loads up.
	enum class EAssetScanStatus : uint8
	{
		NotRequested = 0,
		InProgress = 2,
		Complete = 3
	};

	// Node class primes status of MetaSound assets.  Priming an asset
	// loads the asset asynchronously (if not already loaded)
	// & registers it with the MetaSound Node Class Registry.
	enum class ENodeClassRegistryPrimeStatus : uint8
	{
		NotRequested = 0,
		Requested = 1,
		InProgress = 2,
		Complete = 3,
		Canceled = 4
	};

	enum class ERegistrationAssetContext
	{
		None, // No special asset context associated with this graph registration action
		Removing, // Graph registration during asset removal
		Renaming, // Graph registration during asset rename
		Reloading, // Graph registration during asset reload
	};

	DECLARE_DELEGATE_TwoParams(FOnMetasoundGraphRegister, UObject&, ERegistrationAssetContext)
	DECLARE_DELEGATE_TwoParams(FOnMetasoundGraphUnregister, UObject&, ERegistrationAssetContext)
#endif // WITH_EDITOR

	class IMetasoundEngineModule : public IModuleInterface
	{
	public:
		virtual void StartupModule() override = 0;
		virtual void ShutdownModule() override = 0;

#if WITH_EDITOR
		// Primes MetaSound assets, registering them with the asset manager
		// and loads assets asynchronously (if not already loaded) 
		// & registers them if not already registered with the MetaSound Node Class Registry.
		virtual void PrimeAssetRegistryAsync() = 0;

		// Adds assets to the MetaSound asset manager.
		// This is a subset of PrimeAssetRegistryAsync (does not load/register assets)
		virtual void PrimeAssetManager() = 0;

		virtual ENodeClassRegistryPrimeStatus GetNodeClassRegistryPrimeStatus() const = 0;
		virtual EAssetScanStatus GetAssetRegistryScanStatus() const = 0;
		// Bool rather than enum because this does not require async asset loading like priming the node class registry
		virtual bool IsAssetManagerPrimed() const = 0;

		// Asset registry delegates for calling MetaSound editor module register/unregister with frontend 
		virtual FOnMetasoundGraphRegister& GetOnGraphRegisteredDelegate() = 0;
		virtual FOnMetasoundGraphUnregister& GetOnGraphUnregisteredDelegate() = 0;
#endif // WITH_EDITOR
	};
} // namespace Metasound::Engine
