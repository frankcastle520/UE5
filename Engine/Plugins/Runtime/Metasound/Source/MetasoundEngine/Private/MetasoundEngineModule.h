// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IMetasoundEngineModule.h"
#include "MetasoundFrontendRegistryContainer.h"
#include "UObject/GCObject.h"

#if WITH_EDITOR
#include "HAL/IConsoleManager.h"
#endif // WITH_EDITOR

namespace Metasound::Engine
{
#if WITH_EDITOR
	namespace MetasoundEngineModulePrivate
	{
		extern int32 EnableMetaSoundEditorAssetValidation;
		extern FAutoConsoleVariableRef CVarEnableMetaSoundEditorAssetValidation;
	} // namespace MetasoundEngineModulePrivate
#endif // WITH_EDITOR

	class FModule : public IMetasoundEngineModule
	{
		// Supplies GC referencing in the MetaSound Frontend node registry for doing
		// async work on UObjets
		class FObjectReferencer
			: public FMetasoundFrontendRegistryContainer::IObjectReferencer
			, public FGCObject
		{
		public:
			virtual void AddObject(UObject* InObject) override
			{
				FScopeLock LockObjectArray(&ObjectArrayCriticalSection);
				ObjectArray.Add(InObject);
			}

			virtual void RemoveObject(UObject* InObject) override
			{
				FScopeLock LockObjectArray(&ObjectArrayCriticalSection);
				ObjectArray.Remove(InObject);
			}

			virtual void AddReferencedObjects(FReferenceCollector& Collector) override
			{
				FScopeLock LockObjectArray(&ObjectArrayCriticalSection);
				Collector.AddReferencedObjects(ObjectArray);
			}

			virtual FString GetReferencerName() const override
			{
				return TEXT("FMetasoundEngineModule::FObjectReferencer");
			}

		private:
			mutable FCriticalSection ObjectArrayCriticalSection;
			TArray<TObjectPtr<UObject>> ObjectArray;
		};

	public:
		virtual void StartupModule() override;
		virtual void ShutdownModule() override;

#if WITH_EDITOR
		virtual void PrimeAssetRegistryAsync() override;
		virtual void PrimeAssetManager() override;

		virtual ENodeClassRegistryPrimeStatus GetNodeClassRegistryPrimeStatus() const override;
		virtual EAssetScanStatus GetAssetRegistryScanStatus() const override;
		virtual bool IsAssetManagerPrimed() const override;

		virtual FOnMetasoundGraphRegister& GetOnGraphRegisteredDelegate() override;
		virtual FOnMetasoundGraphUnregister& GetOnGraphUnregisteredDelegate() override;
#endif // WITH_EDITOR

	private:
#if WITH_EDITOR
		virtual void AddClassRegistryAsset(const FAssetData& InAssetData);
		virtual void LoadAndRegisterAsset(const FAssetData& InAssetData);
		virtual void UpdateClassRegistryAsset(const FAssetData& InAssetData);
		virtual void RemoveAssetFromClassRegistry(const FAssetData& InAssetData);
		virtual void RenameAssetInClassRegistry(const FAssetData& InAssetData, const FString& InOldObjectPath);
		virtual void ShutdownAssetClassRegistry();

		virtual void OnPackageReloaded(const EPackageReloadPhase InPackageReloadPhase, FPackageReloadedEvent* InPackageReloadedEvent);
		virtual void OnAssetScanFinished();

		// Asset registry delegates for calling MetaSound editor module 
		FOnMetasoundGraphRegister OnGraphRegister;
		FOnMetasoundGraphUnregister OnGraphUnregister;

		// Asset registry state
		ENodeClassRegistryPrimeStatus NodeClassRegistryPrimeStatus = ENodeClassRegistryPrimeStatus::NotRequested;
		EAssetScanStatus AssetScanStatus = EAssetScanStatus::NotRequested;
		bool bAssetManagerPrimed = false;
		int32 ActiveAsyncAssetLoadRequests = 0;
#endif // WITH_EDITOR
	};
} // namespace Metasound::Engine
