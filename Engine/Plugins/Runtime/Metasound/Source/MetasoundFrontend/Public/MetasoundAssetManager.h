// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "MetasoundFrontendDocument.h"
#include "MetasoundFrontendRegistryKey.h"
#include "MetasoundVertex.h"
#include "UObject/SoftObjectPath.h"
#include "UObject/WeakObjectPtrTemplates.h"

// Forward Declarations
class FMetasoundAssetBase;
class UEdGraph;

struct FAssetData;
struct FMetasoundFrontendClassName;
struct FMetaSoundFrontendDocumentBuilder;
class IMetaSoundDocumentInterface;

namespace Metasound::Frontend
{
	namespace AssetTags
	{
		extern const FString METASOUNDFRONTEND_API ArrayDelim;

#if WITH_EDITORONLY_DATA
		extern const FName METASOUNDFRONTEND_API IsPreset;
#endif // WITH_EDITORONLY_DATA

		extern const FName METASOUNDFRONTEND_API AssetClassID;
		extern const FName METASOUNDFRONTEND_API RegistryVersionMajor;
		extern const FName METASOUNDFRONTEND_API RegistryVersionMinor;

#if WITH_EDITORONLY_DATA
		extern const FName METASOUNDFRONTEND_API RegistryInputTypes;
		extern const FName METASOUNDFRONTEND_API RegistryOutputTypes;
#endif // WITH_EDITORONLY_DATA
	} // namespace AssetTags

	struct METASOUNDFRONTEND_API FMetaSoundAssetRegistrationOptions
	{
		// If true, forces a re-register of this class (and all class dependencies
		// if the following option 'bRegisterDependencies' is enabled).
		bool bForceReregister = true;

		// If true, forces flag to resync all view (editor) data pertaining to the given asset(s) being registered.
		bool bForceViewSynchronization = true;

		// If true, recursively attempts to register dependencies. (TODO: Determine if this option should be removed.
		// Must validate that failed dependency updates due to auto-update for ex. being disabled is handled gracefully
		// at runtime.)
		bool bRegisterDependencies = true;

		// Attempt to auto-update (Only runs if class not registered or set to force re-register.
		// Will not respect being set to true if project-level MetaSoundSettings specify to not run auto-update.)
		bool bAutoUpdate = true;

		// If true, warnings will be logged if updating a node results in existing connections being discarded.
		bool bAutoUpdateLogWarningOnDroppedConnection = false;

#if WITH_EDITOR
		// Soft deprecated. Preprocessing now handled contextually if cooking or serializing.
		bool bPreprocessDocument = true;

		// Attempt to rebuild referenced classes (only run if class not registered or set to force re-register)
		bool bRebuildReferencedAssetClasses = true;

		// No longer used. Memory management of document (i.e. copying or using object's version) inferred internally
		bool bRegisterCopyIfAsync = false;
#endif // WITH_EDITOR
	};

	struct METASOUNDFRONTEND_API FAssetKey
	{
		FMetasoundFrontendClassName ClassName;
		FMetasoundFrontendVersionNumber Version;

		FAssetKey() = default;
		FAssetKey(const FMetasoundFrontendClassName& InClassName, const FMetasoundFrontendVersionNumber& InVersion);
		FAssetKey(const FNodeRegistryKey& RegKey);
		FAssetKey(const FMetasoundFrontendClassMetadata& InMetadata);

		FORCEINLINE friend bool operator==(const FAssetKey& InLHS, const FAssetKey& InRHS)
		{
			return (InLHS.ClassName == InRHS.ClassName) && (InLHS.Version == InRHS.Version);
		}

		FORCEINLINE friend bool operator<(const FAssetKey& InLHS, const FAssetKey& InRHS)
		{
			if (InLHS.ClassName == InRHS.ClassName)
			{
				return InLHS.Version < InRHS.Version;
			}

			return InLHS.ClassName < InRHS.ClassName;
		}

		friend FORCEINLINE uint32 GetTypeHash(const FAssetKey& InKey)
		{
			return HashCombineFast(GetTypeHash(InKey.ClassName), GetTypeHash(InKey.Version));
		}

		static const FAssetKey& GetInvalid()
		{
			static FAssetKey Invalid;
			return Invalid;
		}

		bool IsValid() const
		{
			return ClassName.IsValid() && Version.IsValid();
		}

		FString ToString() const;
	};


	class METASOUNDFRONTEND_API IMetaSoundAssetManager
	{
	public:
		virtual ~IMetaSoundAssetManager() = default;

		static IMetaSoundAssetManager* Get();
		static IMetaSoundAssetManager& GetChecked();
		static void Deinitialize();
		static void Initialize(TUniquePtr<IMetaSoundAssetManager>&& InInterface);

		UE_DEPRECATED(5.5, "Use Initialize/Deinitialize instead")
		virtual bool IsTesting() const;

		UE_DEPRECATED(5.5, "Use Initialize/Deinitialize instead")
		static void Set(IMetaSoundAssetManager& InInterface) { checkNoEntry(); }

		struct FAssetInfo
		{
			FNodeRegistryKey RegistryKey;
			FSoftObjectPath AssetPath;

			FORCEINLINE friend bool operator==(const FAssetInfo& InLHS, const FAssetInfo& InRHS)
			{
				return (InLHS.RegistryKey == InRHS.RegistryKey) && (InLHS.AssetPath == InRHS.AssetPath);
			}

			FORCEINLINE friend uint32 GetTypeHash(const IMetaSoundAssetManager::FAssetInfo& InInfo)
			{
				return HashCombineFast(GetTypeHash(InInfo.RegistryKey), GetTypeHash(InInfo.AssetPath));
			}
		};

#if WITH_EDITORONLY_DATA
		// Adds missing assets using the provided asset's local reference class cache. Used
		// to prime system from asset attempting to register prior to asset scan being complete.
		// Returns true if references were added, false if it they are already found.
		virtual bool AddAssetReferences(FMetasoundAssetBase& InAssetBase) = 0;
#endif

		// Add or Update a MetaSound Asset's entry data
		virtual FAssetKey AddOrUpdateAsset(const UObject& InObject) = 0;
		virtual FAssetKey AddOrUpdateAsset(const FAssetData& InAssetData) = 0;

		// Whether or not the class is eligible for auto-update
		virtual bool CanAutoUpdate(const FMetasoundFrontendClassName& InClassName) const = 0;

		// Whether or not the asset manager has loaded the given asset
		virtual bool ContainsKey(const Metasound::Frontend::FAssetKey& InAssetKey) const = 0;

		// Returns object (if loaded) associated with the given key (null if key not registered with the AssetManager)
		// If multiple assets are associated with the given key, the last one is returned. 
		virtual FMetasoundAssetBase* FindAsset(const Metasound::Frontend::FAssetKey& InAssetKey) const = 0;

		// Returns object (if loaded) associated with the given key as a Document Interface (null if key not registered with the AssetManager)
		virtual TScriptInterface<IMetaSoundDocumentInterface> FindAssetAsDocumentInterface(const Frontend::FAssetKey& InKey) const = 0;

		// Returns path associated with the given key (returns invalid asset path if key not registered with the AssetManager or was not loaded from asset)
		// If multiple assets are associated with the given key, the last one is returned.
		virtual FTopLevelAssetPath FindAssetPath(const Metasound::Frontend::FAssetKey& InAssetKey) const = 0;
		
		// Returns all paths associated with the given key (returns empty array if key not registered with the AssetManager or was not loaded from asset)
		virtual TArray<FTopLevelAssetPath> FindAssetPaths(const Metasound::Frontend::FAssetKey& InAssetKey) const = 0;

		// Converts an object to an AssetBase if its a registered asset
		virtual FMetasoundAssetBase* GetAsAsset(UObject& InObject) const = 0;
		virtual const FMetasoundAssetBase* GetAsAsset(const UObject& InObject) const = 0;

#if WITH_EDITOR
		// Generates all asset info associated with registered assets that are referenced by the provided asset's graph.
		virtual TSet<FAssetInfo> GetReferencedAssetClasses(const FMetasoundAssetBase& InAssetBase) const = 0;
#endif // WITH_EDITOR

		// Iterate all known MetaSound asset paths
		virtual void IterateAssets(TFunctionRef<void(const FAssetKey, const TArray<FTopLevelAssetPath>&)> Iter) const = 0;

		UE_DEPRECATED(5.5, "Rescan no longer supported nor required by Frontend")
		virtual void RescanAutoUpdateDenyList() { }

		// Set flag for logging active assets on shutdown. In certain cases (ex. validation), it is expected that assets are active at shutdown
		virtual void SetLogActiveAssetsOnShutdown(bool bLogActiveAssetsOnShutdown) = 0;

		// Attempts to retrieve the AssetID from the given ClassName if the ClassName is from a valid asset.
		virtual bool TryGetAssetIDFromClassName(const FMetasoundFrontendClassName& InClassName, FGuid& OutGuid) const = 0;

		// Attempts to load an FMetasoundAssetBase from the given path, or returns it if its already loaded
		virtual FMetasoundAssetBase* TryLoadAsset(const FSoftObjectPath& InObjectPath) const = 0;

		// Returns asset associated with the given key (null if key not registered with the AssetManager or was not loaded from asset)
		virtual FMetasoundAssetBase* TryLoadAssetFromKey(const Metasound::Frontend::FAssetKey& InAssetKey) const = 0;

		// Try to load referenced assets of the given asset or return them if they are already loaded (non-recursive).
		// @return - True if all referenced assets successfully loaded, false if not.
		virtual bool TryLoadReferencedAssets(const FMetasoundAssetBase& InAssetBase, TArray<FMetasoundAssetBase*>& OutReferencedAssets) const = 0;

#if WITH_EDITOR
		// Assigns a new arbitrary class name to the given document, which can cause references to be invalidated.
		// (See
		virtual bool ReassignClassName(TScriptInterface<IMetaSoundDocumentInterface> DocInterface) = 0;
#endif // WITH_EDITOR

		// Requests an async load of all async referenced assets of the input asset.
		virtual void RequestAsyncLoadReferencedAssets(FMetasoundAssetBase& InAssetBase) = 0;

		// Synchronously requests unregister and reregister of all loaded MetaSound assets node class entries.
		virtual void ReloadMetaSoundAssets() const = 0;

		// Removes object from MetaSound asset manager
		virtual void RemoveAsset(const UObject& InObject) = 0;

		// Removes object from MetaSound asset manager
		virtual void RemoveAsset(const FAssetData& InAssetData) = 0;

		// Updates the given MetaSound's asset record with the new name and optionally reregisters it with the Frontend Node Class Registry.
		virtual void RenameAsset(const FAssetData& InAssetData, const FString& InOldObjectPath) = 0;

		// Waits until all async load requests related to this asset are complete.
 		virtual void WaitUntilAsyncLoadReferencedAssetsComplete(FMetasoundAssetBase& InAssetBase) = 0;
	};
} // namespace Metasound::Frontend
