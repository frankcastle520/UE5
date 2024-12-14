// Copyright Epic Games, Inc. All Rights Reserved.
#include "MetasoundAssetSubsystem.h"

#include "Algo/Sort.h"
#include "Algo/Transform.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Async/Async.h"
#include "Engine/AssetManager.h"
#include "HAL/CriticalSection.h"
#include "Metasound.h"
#include "MetasoundAssetBase.h"
#include "MetasoundBuilderSubsystem.h"
#include "MetasoundDocumentBuilderRegistry.h"
#include "MetasoundEngineAsset.h"
#include "MetasoundFrontendDocument.h"
#include "MetasoundFrontendDocumentBuilder.h"
#include "MetasoundFrontendRegistries.h"
#include "MetasoundFrontendSearchEngine.h"
#include "MetasoundFrontendTransform.h"
#include "MetasoundSettings.h"
#include "MetasoundSource.h"
#include "MetasoundTrace.h"
#include "MetasoundUObjectRegistry.h"
#include "Misc/CoreDelegates.h"
#include "Modules/ModuleManager.h"
#include "UObject/NoExportTypes.h"


#include UE_INLINE_GENERATED_CPP_BY_NAME(MetasoundAssetSubsystem)


namespace Metasound::Engine
{
	namespace AssetSubsystemPrivate
	{
		bool GetAssetClassInfo(const FAssetData& InAssetData, Frontend::FNodeClassInfo& OutInfo)
		{
			using namespace Metasound;
			using namespace Metasound::Frontend;

			bool bSuccess = true;

			OutInfo.Type = EMetasoundFrontendClassType::External;
			OutInfo.AssetPath = FTopLevelAssetPath(InAssetData.PackageName, InAssetData.AssetName);
			FString AssetClassID;
			bSuccess &= InAssetData.GetTagValue(AssetTags::AssetClassID, AssetClassID);
			OutInfo.AssetClassID = FGuid(AssetClassID);
			OutInfo.ClassName = FMetasoundFrontendClassName(FName(), *AssetClassID, FName());

#if WITH_EDITORONLY_DATA
			InAssetData.GetTagValue(AssetTags::IsPreset, OutInfo.bIsPreset);
#endif // WITH_EDITORONLY_DATA

			int32 RegistryVersionMajor = 0;
			bSuccess &= InAssetData.GetTagValue(AssetTags::RegistryVersionMajor, RegistryVersionMajor);
			OutInfo.Version.Major = RegistryVersionMajor;

			int32 RegistryVersionMinor = 0;
			bSuccess &= InAssetData.GetTagValue(AssetTags::RegistryVersionMinor, RegistryVersionMinor);
			OutInfo.Version.Minor = RegistryVersionMinor;

#if WITH_EDITORONLY_DATA
			auto ParseTypesString = [&](const FName AssetTag, TSet<FName>& OutTypes)
			{
				FString TypesString;
				if (InAssetData.GetTagValue(AssetTag, TypesString))
				{
					TArray<FString> DataTypeStrings;
					TypesString.ParseIntoArray(DataTypeStrings, *AssetTags::ArrayDelim);
					Algo::Transform(DataTypeStrings, OutTypes, [](const FString& DataType) { return *DataType; });
					return true;
				}

				return false;
			};

			// These values are optional and not necessary to return successfully as MetaSounds
			// don't require inputs or outputs for asset tags to be valid (ex. a new MetaSound,
			// non-source asset has no inputs or outputs)
			OutInfo.InputTypes.Reset();
			ParseTypesString(AssetTags::RegistryInputTypes, OutInfo.InputTypes);

			OutInfo.OutputTypes.Reset();
			ParseTypesString(AssetTags::RegistryOutputTypes, OutInfo.OutputTypes);
#endif // WITH_EDITORONLY_DATA

			return bSuccess;
		}

		bool RemovePath(FCriticalSection* MapCritSec, TMap<Frontend::FAssetKey, TArray<FTopLevelAssetPath>>& Map, const Frontend::FAssetKey& AssetKey, const FTopLevelAssetPath& AssetPath)
		{
			check(MapCritSec);
			FScopeLock Lock(MapCritSec);
			if (TArray<FTopLevelAssetPath>* MapAssetPaths = Map.Find(AssetKey))
			{
				auto ComparePaths = [&AssetPath](const FTopLevelAssetPath& Path) 
				{
					// Compare full paths if valid
					if (Path.IsValid() && AssetPath.IsValid())
					{
						return Path == AssetPath;
					}
					// Package names are stripped on destruction, so only asset name is reliable
					return Path.GetAssetName() == AssetPath.GetAssetName(); 
				};

				if (MapAssetPaths->RemoveAllSwap(ComparePaths, EAllowShrinking::No) > 0)
				{
					if (MapAssetPaths->IsEmpty())
					{
						Map.Remove(AssetKey);
					}
					return true;
				}
			}

			return false;
		}

		void AddPath(FCriticalSection* MapCritSec, TMap<Frontend::FAssetKey, TArray<FTopLevelAssetPath>>& Map, const Frontend::FAssetKey& AssetKey, const FTopLevelAssetPath& AssetPath)
		{
			check(MapCritSec);
			FScopeLock Lock(MapCritSec);
			TArray<FTopLevelAssetPath>& Paths = Map.FindOrAdd(AssetKey);
			Paths.AddUnique(AssetPath);
#if !NO_LOGGING
			if (Paths.Num() > 1)
			{
				TArray<FString> PathStrings;
				Algo::Transform(Paths, PathStrings, [](const FTopLevelAssetPath& Path) { return Path.ToString(); });
				UE_LOG(LogMetaSound, Warning,
					TEXT("MetaSoundAssetManager has registered multiple assets with key '%s':\n%s\n"),
					*AssetKey.ToString(),
					*FString::Join(PathStrings, TEXT("\n")));
			}
#endif // !NO_LOGGING
		}
	}

	class FMetaSoundAssetManager :
		public Frontend::IMetaSoundAssetManager,
		public FGCObject
	{
	public:
		using FAssetInfo = Frontend::IMetaSoundAssetManager::FAssetInfo;
		using FAssetKey = Frontend::FAssetKey;

		FMetaSoundAssetManager() = default;
		virtual ~FMetaSoundAssetManager();

		static FMetaSoundAssetManager& GetChecked()
		{
			using namespace Frontend;
			return static_cast<FMetaSoundAssetManager&>(IMetaSoundAssetManager::GetChecked());
		}

		void RebuildDenyListCache(const UAssetManager& InAssetManager);
		void RegisterAssetClassesInDirectories(const TArray<FMetaSoundAssetDirectory>& Directories);

#if WITH_EDITOR
		bool ReplaceReferencesInDirectory(const TArray<FMetaSoundAssetDirectory>& InDirectories, const Metasound::Frontend::FNodeRegistryKey& OldClassKey, const Metasound::Frontend::FNodeRegistryKey& NewClassKey) const;
#endif // WITH_EDITOR
		void RequestAsyncLoadReferencedAssets(FMetasoundAssetBase& InAssetBase);
		void OnAssetScanComplete();
		void SearchAndIterateDirectoryAssets(const TArray<FDirectoryPath>& InDirectories, TFunctionRef<void(const FAssetData&)> InFunction) const;
		FMetasoundAssetBase* TryLoadAsset(const FSoftObjectPath& InObjectPath) const;
		void UnregisterAssetClassesInDirectories(const TArray<FMetaSoundAssetDirectory>& Directories);

		/* IMetaSoundAssetManager Implementation */
#if WITH_EDITORONLY_DATA
		virtual bool AddAssetReferences(FMetasoundAssetBase& InAssetBase) override;
#endif // WITH_EDITORONLY_DATA
		virtual FAssetKey AddOrUpdateAsset(const FAssetData& InAssetData) override;
		virtual FAssetKey AddOrUpdateAsset(const UObject& InObject) override;
		virtual bool CanAutoUpdate(const FMetasoundFrontendClassName& InClassName) const override;
		virtual bool ContainsKey(const FAssetKey& InKey) const override;
		virtual FMetasoundAssetBase* FindAsset(const FAssetKey& InKey) const override;
		virtual TScriptInterface<IMetaSoundDocumentInterface> FindAssetAsDocumentInterface(const Frontend::FAssetKey& InKey) const override;
		virtual FTopLevelAssetPath FindAssetPath(const FAssetKey& InKey) const override;
		virtual TArray<FTopLevelAssetPath> FindAssetPaths(const FAssetKey& InKey) const override;
		virtual FMetasoundAssetBase* GetAsAsset(UObject& InObject) const override;
		virtual const FMetasoundAssetBase* GetAsAsset(const UObject& InObject) const override;
#if WITH_EDITOR
		virtual TSet<FAssetInfo> GetReferencedAssetClasses(const FMetasoundAssetBase& InAssetBase) const override;
		virtual bool ReassignClassName(TScriptInterface<IMetaSoundDocumentInterface> DocInterface) override;
#endif // WITH_EDITOR
		virtual void IterateAssets(TFunctionRef<void(const FAssetKey, const TArray<FTopLevelAssetPath>&)> Iter) const override;
		virtual void ReloadMetaSoundAssets() const override;
		virtual void RemoveAsset(const UObject& InObject) override;
		virtual void RemoveAsset(const FAssetData& InAssetData) override;
		virtual void RenameAsset(const FAssetData& InAssetData, const FString& InOldObjectPath) override;
		virtual void SetLogActiveAssetsOnShutdown(bool bInLogActiveAssetsOnShutdown) override;
		virtual FMetasoundAssetBase* TryLoadAssetFromKey(const FAssetKey& InKey) const override;
		virtual bool TryGetAssetIDFromClassName(const FMetasoundFrontendClassName& InClassName, FGuid& OutGuid) const override;
		virtual bool TryLoadReferencedAssets(const FMetasoundAssetBase& InAssetBase, TArray<FMetasoundAssetBase*>& OutReferencedAssets) const override;
		virtual void WaitUntilAsyncLoadReferencedAssetsComplete(FMetasoundAssetBase& InAssetBase) override;

		/* FGCObject */
		virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
		virtual FString GetReferencerName() const override { return TEXT("FMetaSoundAssetManager"); }

	private:
		TArray<FMetaSoundAsyncAssetDependencies> LoadingDependencies;

		FMetaSoundAsyncAssetDependencies* FindLoadingDependencies(const UObject* InParentAsset);
		FMetaSoundAsyncAssetDependencies* FindLoadingDependencies(int32 InLoadID);
		void RemoveLoadingDependencies(int32 InLoadID);
		void OnAssetsLoaded(int32 InLoadID);

		FStreamableManager StreamableManager;
		int32 AsyncLoadIDCounter = 0;

		int32 AutoUpdateDenyListChangeID = INDEX_NONE;
		TSet<FName> AutoUpdateDenyListCache;
		std::atomic<bool> bIsInitialAssetScanComplete = false;
		TMap<FAssetKey, TArray<FTopLevelAssetPath>> PathMap;

		// Critical section primarily for allowing safe access of path map during async loading of MetaSound assets.
		mutable FCriticalSection PathMapCriticalSection;

		bool bLogActiveAssetsOnShutdown = true;
	};

	FMetaSoundAssetManager::~FMetaSoundAssetManager()
	{
#if !NO_LOGGING
		if (bLogActiveAssetsOnShutdown)
		{
			TMap<FAssetKey, TArray<FTopLevelAssetPath>> PathsOnShutdown;
			{
				FScopeLock Lock(&PathMapCriticalSection);
				PathsOnShutdown = MoveTemp(PathMap);
				PathMap.Reset();
			}

			if (!PathsOnShutdown.IsEmpty())
			{
				UE_LOG(LogMetaSound, Display, TEXT("AssetManager is shutting down with the following %i assets active:"), PathsOnShutdown.Num());
				for (const TPair<FAssetKey, TArray<FTopLevelAssetPath>>& Pair : PathsOnShutdown)
				{
					for (const FTopLevelAssetPath& Path : Pair.Value)
					{
						UE_LOG(LogMetaSound, Display, TEXT("- %s"), *Path.ToString());
					}
				}
			}
		}
#endif // !NO_LOGGING

	}

	void FMetaSoundAssetManager::AddReferencedObjects(FReferenceCollector& Collector)
	{
		for (FMetaSoundAsyncAssetDependencies& Dependencies : LoadingDependencies)
		{
			Collector.AddReferencedObject(Dependencies.MetaSound);
		}
	}

#if WITH_EDITORONLY_DATA
	bool FMetaSoundAssetManager::AddAssetReferences(FMetasoundAssetBase& InAssetBase)
	{
		using namespace Frontend;

		{
			const FMetasoundFrontendDocument& Document = InAssetBase.GetConstDocumentChecked();
			const FAssetKey AssetKey(Document.RootGraph.Metadata);
			if (!ContainsKey(AssetKey))
			{
				AddOrUpdateAsset(*InAssetBase.GetOwningAsset());
				UE_LOG(LogMetaSound, Verbose, TEXT("Adding asset '%s' to MetaSoundAsset registry."), *InAssetBase.GetOwningAssetName());
			}
		}

		bool bAddFromReferencedAssets = false;
		const TSet<FString>& ReferencedAssetClassKeys = InAssetBase.GetReferencedAssetClassKeys();
		for (const FString& KeyString : ReferencedAssetClassKeys)
		{
			FNodeRegistryKey RegistryKey;
			const bool bIsKey = FNodeRegistryKey::Parse(KeyString, RegistryKey);
			if (!bIsKey || !ContainsKey(RegistryKey))
			{
				UE_LOG(LogMetaSound, Verbose, TEXT("Missing referenced class '%s' asset entry."), *KeyString);
				bAddFromReferencedAssets = true;
			}
		}

		// All keys are loaded
		if (!bAddFromReferencedAssets)
		{
			return false;
		}

		UE_LOG(LogMetaSound, Verbose, TEXT("Attempting preemptive reference load..."));

		TArray<FMetasoundAssetBase*> ReferencedAssets = InAssetBase.GetReferencedAssets();
		for (FMetasoundAssetBase* Asset : ReferencedAssets)
		{
			if (Asset)
			{
				const FMetasoundFrontendDocument& RefDocument = Asset->GetConstDocumentChecked();
				const FAssetKey ClassKey = FAssetKey(RefDocument.RootGraph);
				if (!ContainsKey(ClassKey))
				{
					UE_LOG(LogMetaSound, Verbose,
						TEXT("Preemptive load of class '%s' due to early "
							"registration request (asset scan likely not complete)."),
						*ClassKey.ToString());

					UObject* MetaSoundObject = Asset->GetOwningAsset();
					if (ensureAlways(MetaSoundObject))
					{
						AddOrUpdateAsset(*MetaSoundObject);
					}
				}
			}
			else
			{
				UE_LOG(LogMetaSound, Warning, TEXT("Null referenced dependent asset in %s. Resaving asset in editor may fix the issue"), *InAssetBase.GetOwningAssetName());
			}
		}

		return true;
	}
#endif // WITH_EDITORONLY_DATA

	Frontend::FAssetKey FMetaSoundAssetManager::AddOrUpdateAsset(const UObject& InObject)
	{
		using namespace AssetSubsystemPrivate;
		using namespace Frontend;

		METASOUND_TRACE_CPUPROFILER_EVENT_SCOPE(FMetaSoundAssetManager::AddOrUpdateAsset_UObject);

		const FMetasoundAssetBase* MetaSoundAsset = Metasound::IMetasoundUObjectRegistry::Get().GetObjectAsAssetBase(&InObject);
		check(MetaSoundAsset);

		const FMetasoundFrontendDocument& Document = MetaSoundAsset->GetConstDocumentChecked();
		const FAssetKey AssetKey = FAssetKey(Document.RootGraph);

		if (AssetKey.IsValid())
		{
			AssetSubsystemPrivate::AddPath(&PathMapCriticalSection, PathMap, AssetKey, FTopLevelAssetPath(&InObject));
		}

		return AssetKey;
	}

	Frontend::FAssetKey FMetaSoundAssetManager::AddOrUpdateAsset(const FAssetData& InAssetData)
	{
		using namespace Frontend;

		METASOUND_TRACE_CPUPROFILER_EVENT_SCOPE(UMetaSoundAssetSubsystem::AddOrUpdateAsset_AssetData);

		// Invalid ClassID means the node could not be registered.
		// Let caller report or ensure as necessary.
		FAssetKey AssetKey = FAssetKey::GetInvalid();

		// Don't add temporary assets used for diffing
		if (InAssetData.HasAnyPackageFlags(PKG_ForDiffing))
		{
			return AssetKey;
		}

		FNodeClassInfo ClassInfo;
		bool bClassInfoFound = AssetSubsystemPrivate::GetAssetClassInfo(InAssetData, ClassInfo);
		if (!bClassInfoFound)
		{
			UObject* Object = nullptr;

			FSoftObjectPath Path = InAssetData.ToSoftObjectPath();
			if (!FPackageName::GetPackageMountPoint(InAssetData.GetObjectPathString()).IsNone())
			{
				if (InAssetData.IsAssetLoaded())
				{
					Object = Path.ResolveObject();
					UE_LOG(LogMetaSound, Verbose, TEXT("Adding loaded asset '%s' to MetaSoundAsset registry."), *Object->GetName());
				}
				else
				{
					Object = Path.TryLoad();
					UE_LOG(LogMetaSound, Verbose, TEXT("Loaded asset '%s' and adding to MetaSoundAsset registry."), *Object->GetName());
				}
			}

			if (Object)
			{
				return AddOrUpdateAsset(*Object);
			}
		}

		if (ClassInfo.AssetClassID.IsValid())
		{
			AssetKey = FAssetKey(ClassInfo.ClassName, ClassInfo.Version);
			if (AssetKey.IsValid())
			{
				AssetSubsystemPrivate::AddPath(&PathMapCriticalSection, PathMap, AssetKey, ClassInfo.AssetPath);
			}
		}
		return AssetKey;
	}

	bool FMetaSoundAssetManager::CanAutoUpdate(const FMetasoundFrontendClassName& InClassName) const
	{
		const UMetaSoundSettings* Settings = GetDefault<UMetaSoundSettings>();
		if (!Settings->bAutoUpdateEnabled)
		{
			return false;
		}

		return !AutoUpdateDenyListCache.Contains(InClassName.GetFullName());
	}

	bool FMetaSoundAssetManager::ContainsKey(const Metasound::Frontend::FAssetKey& InKey) const
	{
		FScopeLock Lock(&PathMapCriticalSection);
		return PathMap.Contains(InKey);
	}

	FMetaSoundAsyncAssetDependencies* FMetaSoundAssetManager::FindLoadingDependencies(const UObject* InParentAsset)
	{
		auto IsEqualMetaSoundUObject = [InParentAsset](const FMetaSoundAsyncAssetDependencies& InDependencies) -> bool
		{
			return (InDependencies.MetaSound == InParentAsset);
		};

		return LoadingDependencies.FindByPredicate(IsEqualMetaSoundUObject);
	}

	FMetaSoundAsyncAssetDependencies* FMetaSoundAssetManager::FindLoadingDependencies(int32 InLoadID)
	{
		auto IsEqualID = [InLoadID](const FMetaSoundAsyncAssetDependencies& InDependencies) -> bool
		{
			return (InDependencies.LoadID == InLoadID);
		};

		return LoadingDependencies.FindByPredicate(IsEqualID);
	}

	FMetasoundAssetBase* FMetaSoundAssetManager::FindAsset(const Metasound::Frontend::FAssetKey& InKey) const
	{
		FTopLevelAssetPath AssetPath = FindAssetPath(InKey);
		if (AssetPath.IsValid())
		{
			if (UObject* Object = FSoftObjectPath(AssetPath, { }).ResolveObject())
			{
				return GetAsAsset(*Object);
			}
		}

		return nullptr;
	}

	TScriptInterface<IMetaSoundDocumentInterface> FMetaSoundAssetManager::FindAssetAsDocumentInterface(const Metasound::Frontend::FAssetKey& InKey) const
	{
		const FTopLevelAssetPath AssetPath = FindAssetPath(InKey);
		if (AssetPath.IsValid())
		{
			if (UObject* Object = FSoftObjectPath(AssetPath, { }).ResolveObject())
			{
				return TScriptInterface<IMetaSoundDocumentInterface>(Object);
			}
		}

		return nullptr;
	}

	FTopLevelAssetPath FMetaSoundAssetManager::FindAssetPath(const Metasound::Frontend::FAssetKey& InKey) const
	{
		FScopeLock Lock(&PathMapCriticalSection);
		if (const TArray<FTopLevelAssetPath>* Paths = PathMap.Find(InKey))
		{
			if (!Paths->IsEmpty())
			{
				return Paths->Last();
			}
		}

		return nullptr;
	}

	TArray<FTopLevelAssetPath> FMetaSoundAssetManager::FindAssetPaths(const Metasound::Frontend::FAssetKey& InKey) const
	{
		FScopeLock Lock(&PathMapCriticalSection);
		if (const TArray<FTopLevelAssetPath>* Paths = PathMap.Find(InKey))
		{
			return *Paths;
		}

		return { };
	}

	FMetasoundAssetBase* FMetaSoundAssetManager::GetAsAsset(UObject& InObject) const
	{
		return IMetasoundUObjectRegistry::Get().GetObjectAsAssetBase(&InObject);
	}

	const FMetasoundAssetBase* FMetaSoundAssetManager::GetAsAsset(const UObject& InObject) const
	{
		return IMetasoundUObjectRegistry::Get().GetObjectAsAssetBase(&InObject);
	}

	void FMetaSoundAssetManager::OnAssetsLoaded(int32 InLoadID)
	{
		FMetaSoundAsyncAssetDependencies* LoadedDependencies = FindLoadingDependencies(InLoadID);
		if (ensureMsgf(LoadedDependencies, TEXT("Call to async asset load complete with invalid IDs %d"), InLoadID))
		{
			if (LoadedDependencies->StreamableHandle.IsValid())
			{
				if (LoadedDependencies->MetaSound)
				{
					Metasound::IMetasoundUObjectRegistry& UObjectRegistry = Metasound::IMetasoundUObjectRegistry::Get();
					FMetasoundAssetBase* ParentAssetBase = UObjectRegistry.GetObjectAsAssetBase(LoadedDependencies->MetaSound);
					if (ensureMsgf(ParentAssetBase, TEXT("UClass of Parent MetaSound asset %s is not registered in metasound UObject Registery"), *LoadedDependencies->MetaSound->GetPathName()))
					{
						// Get all async loaded assets
						TArray<UObject*> LoadedAssets;
						LoadedDependencies->StreamableHandle->GetLoadedAssets(LoadedAssets);

						// Cast UObjects to FMetaSoundAssetBase
						TArray<FMetasoundAssetBase*> LoadedAssetBases;
						for (UObject* AssetDependency : LoadedAssets)
						{
							if (AssetDependency)
							{
								FMetasoundAssetBase* AssetDependencyBase = UObjectRegistry.GetObjectAsAssetBase(AssetDependency);
								if (ensure(AssetDependencyBase))
								{
									LoadedAssetBases.Add(AssetDependencyBase);
								}
							}
						}

						// Update parent asset with loaded assets. 
						ParentAssetBase->OnAsyncReferencedAssetsLoaded(LoadedAssetBases);
					}
				}
			}

			// Remove from active array of loading dependencies.
			RemoveLoadingDependencies(InLoadID);
		}
	}

	void FMetaSoundAssetManager::OnAssetScanComplete()
	{
		bIsInitialAssetScanComplete = true;
	}

#if WITH_EDITOR
	TSet<UMetaSoundAssetSubsystem::FAssetInfo> FMetaSoundAssetManager::GetReferencedAssetClasses(const FMetasoundAssetBase& InAssetBase) const
	{
		METASOUND_TRACE_CPUPROFILER_EVENT_SCOPE(FMetaSoundAssetManager::GetReferencedAssetClasses);
		using namespace Metasound::Frontend;

		TSet<FAssetInfo> OutAssetInfos;
		const FMetasoundFrontendDocument& Document = InAssetBase.GetConstDocumentChecked();
		for (const FMetasoundFrontendClass& Class : Document.Dependencies)
		{
			if (Class.Metadata.GetType() != EMetasoundFrontendClassType::External)
			{
				continue;
			}

			const FAssetKey AssetKey(Class.Metadata);
			FTopLevelAssetPath ObjectPath = FindAssetPath(AssetKey);
			if (ObjectPath.IsValid())
			{
				FAssetInfo AssetInfo { FNodeRegistryKey(Class.Metadata), FSoftObjectPath(ObjectPath) };
				OutAssetInfos.Add(MoveTemp(AssetInfo));
			}
			else
			{
				FNodeRegistryKey RegistryKey(Class.Metadata);
				const FMetasoundFrontendRegistryContainer* Registry = FMetasoundFrontendRegistryContainer::Get();
				check(Registry);
				const bool bIsRegistered = Registry->IsNodeRegistered(RegistryKey);

				bool bReportFail = false;
				if (bIsRegistered)
				{
					if (!Registry->IsNodeNative(RegistryKey))
					{
						bReportFail = true;
					}
				}
				else
				{
					// Don't report failure if a matching class with a matching major version and higher minor version exists (it will be autoupdated) 
					FMetasoundFrontendClass FrontendClass;
					const bool bDidFindClassWithName = ISearchEngine::Get().FindClassWithHighestVersion(AssetKey.ClassName.ToNodeClassName(), FrontendClass);
					if (!(bDidFindClassWithName &&
						AssetKey.Version.Major == FrontendClass.Metadata.GetVersion().Major &&
						AssetKey.Version.Minor < FrontendClass.Metadata.GetVersion().Minor))
					{
						bReportFail = true;
					}
				}

				if (bReportFail)
				{
					if (bIsInitialAssetScanComplete)
					{
						UE_LOG(LogMetaSound, Warning, TEXT("MetaSound Node Class with registry key '%s' not registered when gathering referenced asset classes from '%s': Retrieving all asset classes may not be comprehensive."), *AssetKey.ToString(), *InAssetBase.GetOwningAssetName());
					}
					else
					{
						UE_LOG(LogMetaSound, Warning, TEXT("Attempt to get registered dependent asset with key '%s' from MetaSound asset '%s' before asset scan has completed: Asset class cannot be provided"), *AssetKey.ToString(), *InAssetBase.GetOwningAssetName());
					}
				}
			}
		}
		return MoveTemp(OutAssetInfos);
	}

	bool FMetaSoundAssetManager::ReassignClassName(TScriptInterface<IMetaSoundDocumentInterface> DocInterface)
	{
#if WITH_EDITORONLY_DATA
		UObject* MetaSoundObject = DocInterface.GetObject();
		if (!MetaSoundObject)
		{
			return false;
		}

		FMetasoundAssetBase* AssetBase = GetAsAsset(*MetaSoundObject);
		if (!AssetBase)
		{
			return false;
		}

		FMetaSoundFrontendDocumentBuilder& Builder = FDocumentBuilderRegistry::GetChecked().FindOrBeginBuilding(DocInterface);

		const FMetasoundFrontendClassMetadata& ClassMetadata = Builder.GetConstDocumentChecked().RootGraph.Metadata;
		const FTopLevelAssetPath Path(MetaSoundObject);

		AssetBase->UnregisterGraphWithFrontend();

		{
			const FAssetKey OldAssetKey(ClassMetadata.GetClassName(), ClassMetadata.GetVersion());
			AssetSubsystemPrivate::RemovePath(&PathMapCriticalSection, PathMap, OldAssetKey, Path);
		}

		Builder.GenerateNewClassName();

		{
			const FAssetKey NewAssetKey(ClassMetadata.GetClassName(), ClassMetadata.GetVersion());
			AssetSubsystemPrivate::AddPath(&PathMapCriticalSection, PathMap, NewAssetKey, Path);
		}

		AssetBase->UpdateAndRegisterForExecution();
		return true;

#else // !WITH_EDITORONLY_DATA
		return false;
#endif // !WITH_EDITORONLY_DATA
	}
#endif // WITH_EDITOR

	void FMetaSoundAssetManager::IterateAssets(TFunctionRef<void(const FAssetKey, const TArray<FTopLevelAssetPath>&)> Iter) const
	{
		for (const TPair<FAssetKey, TArray<FTopLevelAssetPath>>& Pair : PathMap)
		{
			Iter(Pair.Key, Pair.Value);
		}
	}

	void FMetaSoundAssetManager::RebuildDenyListCache(const UAssetManager& InAssetManager)
	{
		using namespace Metasound::Frontend;

		const UMetaSoundSettings* Settings = GetDefault<UMetaSoundSettings>();
		if (Settings->DenyListCacheChangeID == AutoUpdateDenyListChangeID)
		{
			return;
		}

		AutoUpdateDenyListCache.Reset();

		for (const FMetasoundFrontendClassName& ClassName : Settings->AutoUpdateDenylist)
		{
			AutoUpdateDenyListCache.Add(ClassName.GetFullName());
		}

		check(UAssetManager::IsInitialized());
		UAssetManager& AssetManager = UAssetManager::Get();
		for (const FDefaultMetaSoundAssetAutoUpdateSettings& UpdateSettings : Settings->AutoUpdateAssetDenylist)
		{
			FAssetData AssetData;
			if (AssetManager.GetAssetDataForPath(UpdateSettings.MetaSound, AssetData))
			{
				FString AssetClassID;
				if (AssetData.GetTagValue(AssetTags::AssetClassID, AssetClassID))
				{
					const FMetasoundFrontendClassName ClassName = { FName(), *AssetClassID, FName() };
					AutoUpdateDenyListCache.Add(ClassName.GetFullName());
				}
			}
		}

		AutoUpdateDenyListChangeID = Settings->DenyListCacheChangeID;
	}

	void FMetaSoundAssetManager::RegisterAssetClassesInDirectories(const TArray<FMetaSoundAssetDirectory>& InDirectories)
	{
		TArray<FDirectoryPath> Directories;
		Algo::Transform(InDirectories, Directories, [](const FMetaSoundAssetDirectory& AssetDir) { return AssetDir.Directory; });

		SearchAndIterateDirectoryAssets(Directories, [this](const FAssetData& AssetData)
		{
			AddOrUpdateAsset(AssetData);
			FMetasoundAssetBase* MetaSoundAsset = Metasound::IMetasoundUObjectRegistry::Get().GetObjectAsAssetBase(AssetData.GetAsset());
			check(MetaSoundAsset);

			Metasound::Frontend::FMetaSoundAssetRegistrationOptions RegOptions;
			if (const UMetaSoundSettings* Settings = GetDefault<UMetaSoundSettings>())
			{
				RegOptions.bAutoUpdateLogWarningOnDroppedConnection = Settings->bAutoUpdateLogWarningOnDroppedConnection;
			}
			MetaSoundAsset->UpdateAndRegisterForExecution(RegOptions);
		});
	}

	void FMetaSoundAssetManager::RemoveAsset(const UObject& InObject)
	{
		using namespace Frontend;

		TScriptInterface<const IMetaSoundDocumentInterface> DocInterface(&InObject);
		check(DocInterface.GetObject());
		const FMetasoundFrontendDocument& Document = DocInterface->GetConstDocument();
		const FMetasoundFrontendClassMetadata& Metadata = Document.RootGraph.Metadata;

		const FTopLevelAssetPath AssetPath(&InObject);
		if (IDocumentBuilderRegistry* BuilderRegistry = IDocumentBuilderRegistry::Get())
		{
			constexpr bool bForceUnregister = true;
			BuilderRegistry->FinishBuilding(Metadata.GetClassName(), AssetPath, bForceUnregister);
		}

		const FAssetKey AssetKey(Metadata.GetClassName(), Metadata.GetVersion());
		AssetSubsystemPrivate::RemovePath(&PathMapCriticalSection, PathMap, AssetKey, AssetPath);
	}

	void FMetaSoundAssetManager::RemoveAsset(const FAssetData& InAssetData)
	{
		using namespace Frontend;

		FNodeClassInfo ClassInfo;
		const FTopLevelAssetPath AssetPath(InAssetData.PackageName, InAssetData.AssetName);
		if (ensureAlways(AssetSubsystemPrivate::GetAssetClassInfo(InAssetData, ClassInfo)))
		{
			if (IDocumentBuilderRegistry* BuilderRegistry = IDocumentBuilderRegistry::Get())
			{
				constexpr bool bForceUnregister = true;
				BuilderRegistry->FinishBuilding(ClassInfo.ClassName, AssetPath, bForceUnregister);
			}

			const FAssetKey AssetKey(ClassInfo.ClassName, ClassInfo.Version);
			AssetSubsystemPrivate::RemovePath(&PathMapCriticalSection, PathMap, AssetKey, AssetPath);
		}
	}

	void FMetaSoundAssetManager::RemoveLoadingDependencies(int32 InLoadID)
	{
		auto IsEqualID = [InLoadID](const FMetaSoundAsyncAssetDependencies& InDependencies) -> bool
		{
			return (InDependencies.LoadID == InLoadID);
		};
		LoadingDependencies.RemoveAllSwap(IsEqualID);
	}

	void FMetaSoundAssetManager::RenameAsset(const FAssetData& InAssetData, const FString& InOldObjectPath)
	{
		using namespace Frontend;

		FMetasoundAssetBase* MetaSoundAsset = GetAsAsset(*InAssetData.GetAsset());
		check(MetaSoundAsset);

		FNodeClassInfo ClassInfo;
		if (ensureAlways(AssetSubsystemPrivate::GetAssetClassInfo(InAssetData, ClassInfo)))
		{
			const FAssetKey AssetKey(ClassInfo.ClassName, ClassInfo.Version);
			const FTopLevelAssetPath OldPath(InOldObjectPath);
			AssetSubsystemPrivate::RemovePath(&PathMapCriticalSection, PathMap, AssetKey, OldPath);

			if (ClassInfo.AssetClassID.IsValid())
			{
				if (AssetKey.IsValid())
				{
					AssetSubsystemPrivate::AddPath(&PathMapCriticalSection, PathMap, AssetKey, ClassInfo.AssetPath);
				}
			}
		}
	}

#if WITH_EDITOR
	bool FMetaSoundAssetManager::ReplaceReferencesInDirectory(const TArray<FMetaSoundAssetDirectory>& InDirectories, const Metasound::Frontend::FNodeRegistryKey& OldClassKey, const Metasound::Frontend::FNodeRegistryKey& NewClassKey) const
	{
		using namespace Frontend;

		bool bReferencesReplaced = false;

#if WITH_EDITORONLY_DATA
		if (!NewClassKey.IsValid())
		{
			return bReferencesReplaced;
		}

		FMetasoundFrontendClass NewClass;
		const bool bNewClassExists = ISearchEngine::Get().FindClassWithHighestVersion(NewClassKey.ClassName, NewClass);
		if (bNewClassExists)
		{
			TArray<FDirectoryPath> Directories;
			Algo::Transform(InDirectories, Directories, [](const FMetaSoundAssetDirectory& AssetDir) { return AssetDir.Directory; });

			TMap<FNodeRegistryKey, FNodeRegistryKey> OldToNewReferenceKeys = { { OldClassKey, NewClassKey } };
			SearchAndIterateDirectoryAssets(Directories, [this, &bReferencesReplaced, &OldToNewReferenceKeys](const FAssetData& AssetData)
			{
				if (UObject* MetaSoundObject = AssetData.GetAsset())
				{
					MetaSoundObject->Modify();
					FMetaSoundFrontendDocumentBuilder& Builder = FDocumentBuilderRegistry::GetChecked().FindOrBeginBuilding(MetaSoundObject);
					const bool bDependencyUpdated = Builder.UpdateDependencyRegistryData(OldToNewReferenceKeys);
					if (bDependencyUpdated)
					{
						bReferencesReplaced = true;
						Builder.RemoveUnusedDependencies();
						if (FMetasoundAssetBase* AssetBase = GetAsAsset(*MetaSoundObject); ensure(AssetBase))
						{
							AssetBase->RebuildReferencedAssetClasses();
						}
					}
				}
			});
		}
		else
		{
			UE_LOG(LogMetaSound, Display, TEXT("Cannot replace references in MetaSound assets found in given directory/directories: NewClass '%s' does not exist"), *NewClassKey.ToString());
		}
#endif // WITH_EDITORONLY_DATA

		return bReferencesReplaced;
	}
#endif // WITH_EDITOR

	void FMetaSoundAssetManager::RequestAsyncLoadReferencedAssets(FMetasoundAssetBase& InAssetBase)
	{
		const TSet<FSoftObjectPath>& AsyncReferences = InAssetBase.GetAsyncReferencedAssetClassPaths();
		if (AsyncReferences.Num() > 0)
		{
			if (UObject* OwningAsset = InAssetBase.GetOwningAsset())
			{
				TArray<FSoftObjectPath> PathsToLoad = AsyncReferences.Array();

				// Protect against duplicate calls to async load assets. 
				if (FMetaSoundAsyncAssetDependencies* ExistingAsyncLoad = FindLoadingDependencies(OwningAsset))
				{
					if (ExistingAsyncLoad->Dependencies == PathsToLoad)
					{
						// early out since these are already actively being loaded.
						return;
					}
				}

				int32 AsyncLoadID = AsyncLoadIDCounter++;

				auto AssetsLoadedDelegate = [this, AsyncLoadID]()
				{
					this->OnAssetsLoaded(AsyncLoadID);
				};

				// Store async loading data for use when async load is complete. 
				FMetaSoundAsyncAssetDependencies& AsyncDependencies = LoadingDependencies.AddDefaulted_GetRef();

				AsyncDependencies.LoadID = AsyncLoadID;
				AsyncDependencies.MetaSound = OwningAsset;
				AsyncDependencies.Dependencies = PathsToLoad;
				AsyncDependencies.StreamableHandle = StreamableManager.RequestAsyncLoad(PathsToLoad, AssetsLoadedDelegate);
			}
			else
			{
				UE_LOG(LogMetaSound, Error, TEXT("Cannot load async asset as FMetasoundAssetBase null owning UObject"), *InAssetBase.GetOwningAssetName());
			}
		}

	}

	void FMetaSoundAssetManager::ReloadMetaSoundAssets() const
	{
		TSet<FMetasoundAssetBase*> ToReregister;
		IterateAssets([&ToReregister](const FAssetKey& AssetKey, const TArray<FTopLevelAssetPath>& Paths)
		{
			if (FMetasoundAssetBase* Asset = IMetaSoundAssetManager::GetChecked().FindAsset(AssetKey))
			{
				if (Asset->IsRegistered())
				{
					ToReregister.Add(Asset);
					Asset->UnregisterGraphWithFrontend();
				}
			}
		});

		// Handled in second loop to avoid re-registering referenced graphs more than once
		IterateAssets([&ToReregister](const FAssetKey& AssetKey, const TArray<FTopLevelAssetPath>& Paths)
		{
			if (FMetasoundAssetBase* Asset = IMetaSoundAssetManager::GetChecked().FindAsset(AssetKey))
			{
				if (ToReregister.Contains(Asset))
				{
					Asset->UpdateAndRegisterForExecution();
				}
			}
		});
	}

	void FMetaSoundAssetManager::SearchAndIterateDirectoryAssets(const TArray<FDirectoryPath>& InDirectories, TFunctionRef<void(const FAssetData&)> InFunction) const
	{
		if (InDirectories.IsEmpty())
		{
			return;
		}

		UAssetManager& AssetManager = UAssetManager::Get();

		FAssetManagerSearchRules Rules;
		for (const FDirectoryPath& Path : InDirectories)
		{
			Rules.AssetScanPaths.Add(*Path.Path);
		}

		Metasound::IMetasoundUObjectRegistry::Get().IterateRegisteredUClasses([&](UClass& RegisteredClass)
		{
			Rules.AssetBaseClass = &RegisteredClass;
			TArray<FAssetData> MetaSoundAssets;
			AssetManager.SearchAssetRegistryPaths(MetaSoundAssets, Rules);
			for (const FAssetData& AssetData : MetaSoundAssets)
			{
				InFunction(AssetData);
			}
		});
	}
	
	void FMetaSoundAssetManager::SetLogActiveAssetsOnShutdown(bool bInLogActiveAssetsOnShutdown)
	{
		bLogActiveAssetsOnShutdown = bInLogActiveAssetsOnShutdown;
	}

	FMetasoundAssetBase* FMetaSoundAssetManager::TryLoadAssetFromKey(const Metasound::Frontend::FAssetKey& InAssetKey) const
	{
		FTopLevelAssetPath ObjectPath = FindAssetPath(InAssetKey);
		if (ObjectPath.IsValid())
		{
			const FSoftObjectPath SoftPath(ObjectPath);
			return TryLoadAsset(SoftPath);
		}

		return nullptr;
	}

	bool FMetaSoundAssetManager::TryGetAssetIDFromClassName(const FMetasoundFrontendClassName& InClassName, FGuid& OutGuid) const
	{
		return FGuid::Parse(InClassName.Name.ToString(), OutGuid);
	}

	FMetasoundAssetBase* FMetaSoundAssetManager::TryLoadAsset(const FSoftObjectPath& InObjectPath) const
	{
		return Metasound::IMetasoundUObjectRegistry::Get().GetObjectAsAssetBase(InObjectPath.TryLoad());
	}

	bool FMetaSoundAssetManager::TryLoadReferencedAssets(const FMetasoundAssetBase& InAssetBase, TArray<FMetasoundAssetBase*>& OutReferencedAssets) const
	{
		using namespace Metasound::Frontend;

		bool bSucceeded = true;
		OutReferencedAssets.Reset();

		TArray<FMetasoundAssetBase*> ReferencedAssets;
		const TSet<FString>& AssetClassKeys = InAssetBase.GetReferencedAssetClassKeys();
		for (const FString& KeyString : AssetClassKeys)
		{
			FNodeRegistryKey Key;
			FNodeRegistryKey::Parse(KeyString, Key);
			if (FMetasoundAssetBase* MetaSound = TryLoadAssetFromKey(Key))
			{
				OutReferencedAssets.Add(MetaSound);
			}
			else
			{
				UE_LOG(LogMetaSound, Error, TEXT("Failed to find or load referenced MetaSound asset with key '%s'"), *KeyString);
				bSucceeded = false;
			}
		}

		return bSucceeded;
	}

	void FMetaSoundAssetManager::UnregisterAssetClassesInDirectories(const TArray<FMetaSoundAssetDirectory>& InDirectories)
	{
		TArray<FDirectoryPath> Directories;
		Algo::Transform(InDirectories, Directories, [](const FMetaSoundAssetDirectory& AssetDir) { return AssetDir.Directory; });

		SearchAndIterateDirectoryAssets(Directories, [this](const FAssetData& AssetData)
		{
			using namespace Metasound;
			using namespace Metasound::Frontend;

			if (AssetData.IsAssetLoaded())
			{
				FMetasoundAssetBase* MetaSoundAsset = Metasound::IMetasoundUObjectRegistry::Get().GetObjectAsAssetBase(AssetData.GetAsset());
				check(MetaSoundAsset);
				MetaSoundAsset->UnregisterGraphWithFrontend();

				RemoveAsset(AssetData);
			}
			else
			{
				FNodeClassInfo AssetClassInfo;
				if (ensureAlways(AssetSubsystemPrivate::GetAssetClassInfo(AssetData, AssetClassInfo)))
				{
					const FNodeRegistryKey RegistryKey = FNodeRegistryKey(AssetClassInfo);
					const bool bIsRegistered = FMetasoundFrontendRegistryContainer::Get()->IsNodeRegistered(RegistryKey);
					if (bIsRegistered)
					{
						FMetasoundFrontendRegistryContainer::Get()->UnregisterNode(RegistryKey);
						const FTopLevelAssetPath AssetPath(AssetData.PackageName, AssetData.AssetName);
						const FAssetKey AssetKey(AssetClassInfo.ClassName, AssetClassInfo.Version);
						AssetSubsystemPrivate::RemovePath(&PathMapCriticalSection, PathMap, AssetKey, AssetPath);
					}
				}
			}
		});
	}

	void FMetaSoundAssetManager::WaitUntilAsyncLoadReferencedAssetsComplete(FMetasoundAssetBase& InAssetBase)
	{
		TSet<FMetasoundAssetBase*> TransitiveReferences;
		TArray<FMetasoundAssetBase*> TransitiveReferencesQueue;
		TransitiveReferences.Add(&InAssetBase);
		TransitiveReferencesQueue.Add(&InAssetBase);
		while (!TransitiveReferencesQueue.IsEmpty())
		{
			FMetasoundAssetBase* Reference = TransitiveReferencesQueue.Pop();
			UObject* OwningAsset = Reference->GetOwningAsset();
			if (!OwningAsset)
			{
				continue;
			}
			while (FMetaSoundAsyncAssetDependencies* LoadingDependency = FindLoadingDependencies(OwningAsset))
			{
				// Grab shared ptr to handle as LoadingDependencies may be deleted and have it's shared pointer removed. 
				TSharedPtr<FStreamableHandle> StreamableHandle = LoadingDependency->StreamableHandle;
				if (StreamableHandle.IsValid())
				{
					UE_LOG(LogMetaSound, Verbose, TEXT("Waiting on async load (id: %d) from asset %s"), LoadingDependency->LoadID, *InAssetBase.GetOwningAssetName());

					EAsyncPackageState::Type LoadState = StreamableHandle->WaitUntilComplete();
					if (EAsyncPackageState::Complete != LoadState)
					{
						UE_LOG(LogMetaSound, Error, TEXT("Failed to complete loading of async dependent assets from parent asset %s"), *InAssetBase.GetOwningAssetName());
						RemoveLoadingDependencies(LoadingDependency->LoadID);
					}
					else
					{
						// This will remove the loading dependencies from internal storage
						OnAssetsLoaded(LoadingDependency->LoadID);
					}

					// This will prevent OnAssetsLoaded from being called via the streamables
					// internal delegate complete callback.
					StreamableHandle->CancelHandle();
				}
			}

			for (FMetasoundAssetBase* NextReference : Reference->GetReferencedAssets())
			{
				bool bAlreadyInSet;
				TransitiveReferences.Add(NextReference, &bAlreadyInSet);
				if (!bAlreadyInSet)
				{
					TransitiveReferencesQueue.Add(NextReference);
				}
			}
		}
	}

	void DeinitializeAssetManager()
	{
		Frontend::IMetaSoundAssetManager::Deinitialize();
	}

	void InitializeAssetManager()
	{
		Frontend::IMetaSoundAssetManager::Initialize(MakeUnique<FMetaSoundAssetManager>());
	}
} // namespace Metasound::Engine

void UMetaSoundAssetSubsystem::Initialize(FSubsystemCollectionBase& InCollection)
{
	FCoreDelegates::OnPostEngineInit.AddUObject(this, &UMetaSoundAssetSubsystem::PostEngineInitInternal);
}

void UMetaSoundAssetSubsystem::PostEngineInitInternal()
{
	using namespace Metasound::Engine;

	check(UAssetManager::IsInitialized());
	UAssetManager& AssetManager = UAssetManager::Get();
	AssetManager.CallOrRegister_OnCompletedInitialScan(FSimpleMulticastDelegate::FDelegate::CreateUObject(this, &UMetaSoundAssetSubsystem::PostInitAssetScanInternal));
	FMetaSoundAssetManager::GetChecked().RebuildDenyListCache(AssetManager);
}

void UMetaSoundAssetSubsystem::PostInitAssetScanInternal()
{
	using namespace Metasound::Engine;

	METASOUND_TRACE_CPUPROFILER_EVENT_SCOPE(UMetaSoundAssetSubsystem::PostInitAssetScanInternal);

	const UMetaSoundSettings* Settings = GetDefault<UMetaSoundSettings>();
	if (ensureAlways(Settings))
	{
		FMetaSoundAssetManager& Manager = FMetaSoundAssetManager::GetChecked();
		Manager.SearchAndIterateDirectoryAssets(Settings->DirectoriesToRegister, [&Manager](const FAssetData& AssetData)
		{
			Manager.AddOrUpdateAsset(AssetData);
		});
		Manager.OnAssetScanComplete();
	}
}

#if WITH_EDITORONLY_DATA
void UMetaSoundAssetSubsystem::AddAssetReferences(FMetasoundAssetBase& InAssetBase)
{
	using namespace Metasound::Frontend;
	IMetaSoundAssetManager::GetChecked().AddAssetReferences(InAssetBase);
}
#endif // WITH_EDITORONLY_DATA

Metasound::Frontend::FNodeRegistryKey UMetaSoundAssetSubsystem::AddOrUpdateAsset(const UObject& InObject)
{
	using namespace Metasound::Frontend;
	return IMetaSoundAssetManager::GetChecked().AddOrUpdateAsset(InObject);
}

Metasound::Frontend::FNodeRegistryKey UMetaSoundAssetSubsystem::AddOrUpdateAsset(const FAssetData& InAssetData)
{
	using namespace Metasound::Frontend;
	return IMetaSoundAssetManager::GetChecked().AddOrUpdateAsset(InAssetData);
}

bool UMetaSoundAssetSubsystem::CanAutoUpdate(const FMetasoundFrontendClassName& InClassName) const
{
	using namespace Metasound::Frontend;
	return IMetaSoundAssetManager::GetChecked().CanAutoUpdate(InClassName);
}

bool UMetaSoundAssetSubsystem::ContainsKey(const Metasound::Frontend::FNodeRegistryKey& InRegistryKey) const
{
	using namespace Metasound::Frontend;
	return IMetaSoundAssetManager::GetChecked().ContainsKey(FAssetKey(InRegistryKey));
}

FMetasoundAssetBase* UMetaSoundAssetSubsystem::GetAsAsset(UObject& InObject) const
{
	using namespace Metasound::Frontend;
	return IMetaSoundAssetManager::GetChecked().GetAsAsset(InObject);
}

const FMetasoundAssetBase* UMetaSoundAssetSubsystem::GetAsAsset(const UObject& InObject) const
{
	using namespace Metasound::Frontend;
	return IMetaSoundAssetManager::GetChecked().GetAsAsset(InObject);
}

#if WITH_EDITOR
TSet<UMetaSoundAssetSubsystem::FAssetInfo> UMetaSoundAssetSubsystem::GetReferencedAssetClasses(const FMetasoundAssetBase& InAssetBase) const
{
	using namespace Metasound::Frontend;
	return IMetaSoundAssetManager::GetChecked().GetReferencedAssetClasses(InAssetBase);
}
#endif // WITH_EDITOR

FMetasoundAssetBase* UMetaSoundAssetSubsystem::TryLoadAssetFromKey(const Metasound::Frontend::FNodeRegistryKey& RegistryKey) const
{
	using namespace Metasound::Frontend;
	return IMetaSoundAssetManager::GetChecked().TryLoadAssetFromKey(RegistryKey);
}

bool UMetaSoundAssetSubsystem::TryLoadReferencedAssets(const FMetasoundAssetBase& InAssetBase, TArray<FMetasoundAssetBase*>& OutReferencedAssets) const
{
	using namespace Metasound::Frontend;
	return IMetaSoundAssetManager::GetChecked().TryLoadReferencedAssets(InAssetBase, OutReferencedAssets);
}

const FSoftObjectPath* UMetaSoundAssetSubsystem::FindObjectPathFromKey(const Metasound::Frontend::FNodeRegistryKey& InRegistryKey) const
{
	using namespace Metasound::Frontend;
	static FSoftObjectPath TempPath;
	TempPath.Reset();
	const FTopLevelAssetPath Path = IMetaSoundAssetManager::GetChecked().FindAssetPath(InRegistryKey);
	if (Path.IsValid())
	{
		TempPath = FSoftObjectPath(Path);
	}
	return &TempPath;
}

FMetasoundAssetBase* UMetaSoundAssetSubsystem::TryLoadAsset(const FSoftObjectPath& InObjectPath) const
{
	using namespace Metasound::Frontend;
	return IMetaSoundAssetManager::GetChecked().TryLoadAsset(InObjectPath);
}

void UMetaSoundAssetSubsystem::RemoveAsset(const UObject& InObject)
{
	using namespace Metasound::Frontend;
	IMetaSoundAssetManager::GetChecked().RemoveAsset(InObject);
}

void UMetaSoundAssetSubsystem::RemoveAsset(const FAssetData& InAssetData)
{
	using namespace Metasound::Frontend;
	IMetaSoundAssetManager::GetChecked().RemoveAsset(InAssetData);
}

void UMetaSoundAssetSubsystem::RenameAsset(const FAssetData& InAssetData, bool /* bInReregisterWithFrontend */)
{
	using namespace Metasound::Frontend;
	IMetaSoundAssetManager::GetChecked().RenameAsset(InAssetData, { });
}

UMetaSoundAssetSubsystem& UMetaSoundAssetSubsystem::GetChecked()
{
	check(GEngine);
	UMetaSoundAssetSubsystem* Subsystem = GEngine->GetEngineSubsystem<UMetaSoundAssetSubsystem>();
	check(Subsystem);
	return *Subsystem;
}

#if WITH_EDITOR
bool UMetaSoundAssetSubsystem::ReassignClassName(TScriptInterface<IMetaSoundDocumentInterface> DocInterface)
{
	using namespace Metasound::Engine;
	return FMetaSoundAssetManager::GetChecked().ReassignClassName(DocInterface);
}
#endif // WITH_EDITOR

void UMetaSoundAssetSubsystem::RegisterAssetClassesInDirectories(const TArray<FMetaSoundAssetDirectory>& InDirectories)
{
	using namespace Metasound::Engine;
	FMetaSoundAssetManager::GetChecked().RegisterAssetClassesInDirectories(InDirectories);
}

#if WITH_EDITOR
bool UMetaSoundAssetSubsystem::ReplaceReferencesInDirectory(
	const TArray<FMetaSoundAssetDirectory>& InDirectories,
	const FMetasoundFrontendClassName& OldClassName,
	const FMetasoundFrontendClassName& NewClassName,
	const FMetasoundFrontendVersionNumber OldVersion,
	const FMetasoundFrontendVersionNumber NewVersion)
{
	using namespace Metasound::Engine;
	using namespace Metasound::Frontend;

	return FMetaSoundAssetManager::GetChecked().ReplaceReferencesInDirectory(
		InDirectories,
		FNodeRegistryKey(EMetasoundFrontendClassType::External, OldClassName, OldVersion),
		FNodeRegistryKey(EMetasoundFrontendClassType::External, NewClassName, NewVersion)
	);
}
#endif // WITH_EDITOR

void UMetaSoundAssetSubsystem::UnregisterAssetClassesInDirectories(const TArray<FMetaSoundAssetDirectory>& InDirectories)
{
	using namespace Metasound::Engine;
	FMetaSoundAssetManager::GetChecked().UnregisterAssetClassesInDirectories(InDirectories);
}
