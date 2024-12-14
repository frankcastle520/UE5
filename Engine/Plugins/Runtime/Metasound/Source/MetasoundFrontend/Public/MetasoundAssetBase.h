// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "MetasoundAccessPtr.h"
#include "MetasoundAssetManager.h"
#include "MetasoundFrontend.h"
#include "MetasoundFrontendController.h"
#include "MetasoundFrontendDocument.h"
#include "MetasoundFrontendDocumentAccessPtr.h"
#include "MetasoundFrontendRegistryKey.h"
#include "MetasoundGraph.h"
#include "MetasoundLog.h"
#include "MetasoundParameterTransmitter.h"
#include "MetasoundVertex.h"
#include "Templates/SharedPointer.h"
#include "UObject/SoftObjectPath.h"
#include "UObject/WeakObjectPtrTemplates.h"


// Forward Declarations
class UEdGraph;
struct FMetaSoundFrontendDocumentBuilder;
class IConsoleVariable;
typedef TMulticastDelegate<void(IConsoleVariable*), FDefaultDelegateUserPolicy> FConsoleVariableMulticastDelegate;

namespace Metasound
{
	class IGraph;
	class FGraph;
	namespace Frontend
	{
		// Forward Declarations
		class IInterfaceRegistryEntry;

		METASOUNDFRONTEND_API TRange<float> GetBlockRateClampRange();
		METASOUNDFRONTEND_API float GetBlockRateOverride();
		METASOUNDFRONTEND_API FConsoleVariableMulticastDelegate& GetBlockRateOverrideChangedDelegate();

		METASOUNDFRONTEND_API TRange<int32> GetSampleRateClampRange();
		METASOUNDFRONTEND_API int32 GetSampleRateOverride();
		METASOUNDFRONTEND_API FConsoleVariableMulticastDelegate& GetSampleRateOverrideChangedDelegate();
		class FProxyDataCache;

	} // namespace Frontend
} // namespace Metasound

/** FMetasoundAssetBase is intended to be a mix-in subclass for UObjects which utilize
 * Metasound assets.  It provides consistent access to FMetasoundFrontendDocuments, control
 * over the FMetasoundFrontendClassInterface of the FMetasoundFrontendDocument.  It also enables the UObject
 * to be utilized by a host of other engine tools built to support MetaSounds.
 */
class METASOUNDFRONTEND_API FMetasoundAssetBase : public IAudioProxyDataFactory
{
public:
	virtual TSharedPtr<Audio::IProxyData> CreateProxyData(const Audio::FProxyDataInitParams& InitParams) override;

	static const FString FileExtension;

	FMetasoundAssetBase() = default;
	virtual ~FMetasoundAssetBase() = default;

#if WITH_EDITORONLY_DATA
	virtual FText GetDisplayName() const = 0;

	// Returns the graph associated with this Metasound. Graph is required to be referenced on
	// Metasound UObject for editor serialization purposes.
	// @return Editor graph associated with this metasound uobject.
	virtual UEdGraph* GetGraph() const = 0;
	virtual UEdGraph& GetGraphChecked() const = 0;
	virtual void MigrateEditorGraph(FMetaSoundFrontendDocumentBuilder& OutBuilder) = 0;

	// Sets the graph associated with this Metasound. Graph is required to be referenced on
	// Metasound UObject for editor serialization purposes.
	// @param Editor graph associated with this metasound object.
	virtual void SetGraph(UEdGraph* InGraph) = 0;

	// Only required for editor builds. Adds metadata to properties available when the object is
	// not loaded for use by the Asset Registry.
	virtual void SetRegistryAssetClassInfo(const Metasound::Frontend::FNodeClassInfo& InClassInfo) = 0;
#endif // WITH_EDITORONLY_DATA

	UE_DEPRECATED(5.5, "Moved to IMetaSoundDocumentInterface::ConformObjectToDocument")
	virtual bool ConformObjectDataToInterfaces();

	// Registers the root graph of the given asset with the MetaSound Frontend. Unlike 'UpdateAndRegisterForSerialization", this call
	// generates all necessary runtime data to execute the given graph (i.e. INodes).
	virtual void UpdateAndRegisterForExecution(Metasound::Frontend::FMetaSoundAssetRegistrationOptions InRegistrationOptions = Metasound::Frontend::FMetaSoundAssetRegistrationOptions());

	UE_DEPRECATED(5.5, "Moved to UpdateAndRegisterForExecution.")
	virtual void RegisterGraphWithFrontend(Metasound::Frontend::FMetaSoundAssetRegistrationOptions InRegistrationOptions = Metasound::Frontend::FMetaSoundAssetRegistrationOptions());

	// Unregisters the root graph of the given asset with the MetaSound Frontend.
	void UnregisterGraphWithFrontend();

	UE_DEPRECATED(5.5, "Moved to UpdateAndRegisterForSerialization instead, which is only in builds set to load editor-only data.")
	void CookMetaSound();

#if WITH_EDITORONLY_DATA
	// Updates and registers this and referenced MetaSound document objects with the NodeClass Registry. AutoUpdates and
	// optimizes aforementioned documents for serialization. Unlike 'UpdateAndRegisterForRuntime', does not generate required
	// runtime data for graph execution. If CookPlatformName is set, used to strip data not required for the provided platform.
	void UpdateAndRegisterForSerialization(FName CookPlatformName = { });
#endif // WITH_EDITORONLY_DATA

#if WITH_EDITOR
	// Rebuild dependent asset classes
	void RebuildReferencedAssetClasses();
#endif // WITH_EDITOR

	// Returns whether an interface with the given version is declared by the given asset's document.
	bool IsInterfaceDeclared(const FMetasoundFrontendVersion& InVersion) const;

	// Gets the asset class info.
	UE_DEPRECATED(5.4, "NodeClassInfo can be constructed directly from document's root graph & asset's path and requires no specialized virtual getter.")
	virtual Metasound::Frontend::FNodeClassInfo GetAssetClassInfo() const;

	// Returns all the class keys of this asset's referenced assets
	virtual const TSet<FString>& GetReferencedAssetClassKeys() const = 0;

	// Returns set of class references set call to serialize in the editor
	// Used at runtime load register referenced classes.
	virtual TArray<FMetasoundAssetBase*> GetReferencedAssets() = 0;

	// Return all dependent asset paths to load asynchronously
	virtual const TSet<FSoftObjectPath>& GetAsyncReferencedAssetClassPaths() const = 0;

	// Called when async assets have finished loading.
	virtual void OnAsyncReferencedAssetsLoaded(const TArray<FMetasoundAssetBase*>& InAsyncReferences) = 0;

	bool AddingReferenceCausesLoop(const FMetasoundAssetBase& InMetaSound) const;

	UE_DEPRECATED(5.5, "Use overload that is provided an AssetBase")
	bool AddingReferenceCausesLoop(const FSoftObjectPath& InReferencePath) const;

	bool IsReferencedAsset(const FMetasoundAssetBase& InAssetToCheck) const;

	bool IsRegistered() const;

	// Imports data from a JSON string directly
	bool ImportFromJSON(const FString& InJSON);

	// Imports the asset from a JSON file at provided path
	bool ImportFromJSONAsset(const FString& InAbsolutePath);

	// Soft Deprecated in favor of DocumentBuilder API. Returns handle for the root metasound graph of this asset.
	Metasound::Frontend::FDocumentHandle GetDocumentHandle();
	Metasound::Frontend::FConstDocumentHandle GetDocumentHandle() const;

	// Soft Deprecated in favor of DocumentBuilder API. Returns handle for the root metasound graph of this asset.
	Metasound::Frontend::FGraphHandle GetRootGraphHandle();
	Metasound::Frontend::FConstGraphHandle GetRootGraphHandle() const;

	UE_DEPRECATED(5.5, "Direct mutation of the document is no longer supported via AssetBase.")
	void SetDocument(FMetasoundFrontendDocument InDocument, bool bMarkDirty = true);

	virtual const FMetasoundFrontendDocument& GetConstDocumentChecked() const;

	// Soft deprecated.  Document layer should not be directly mutated via asset base in anticipation
	// of moving all mutable document calls to the Frontend/Subsystem Document Builder API.
	FMetasoundFrontendDocument& GetDocumentChecked();

	UE_DEPRECATED(5.5, "Use GetConstDocumentChecked instead.")
	const FMetasoundFrontendDocument& GetDocumentChecked() const;

	const Metasound::Frontend::FGraphRegistryKey& GetGraphRegistryKey() const;

	UE_DEPRECATED(5.4, "Use GetGraphRegistryKey instead.")
	const Metasound::Frontend::FNodeRegistryKey& GetRegistryKey() const;

#if WITH_EDITORONLY_DATA
	bool VersionAsset(FMetaSoundFrontendDocumentBuilder& Builder);
#endif // WITH_EDITORONLY_DATA

#if WITH_EDITOR
	/*
	 * Caches transient metadata (class & vertex) found in the registry
	 * that is not necessary for serialization or core graph generation.
	 *
	 * @return - Whether class was found in the registry & data was cached successfully.
	 */
	void CacheRegistryMetadata();

	FMetasoundFrontendDocumentModifyContext& GetModifyContext();

	UE_DEPRECATED(5.5, "Use GetConstModifyContext")
	const FMetasoundFrontendDocumentModifyContext& GetModifyContext() const;

	const FMetasoundFrontendDocumentModifyContext& GetConstModifyContext() const;
#endif // WITH_EDITOR

	// Calls the outermost package and marks it dirty.
	bool MarkMetasoundDocumentDirty() const;

	struct FSendInfoAndVertexName
	{
		Metasound::FMetaSoundParameterTransmitter::FSendInfo SendInfo;
		Metasound::FVertexName VertexName;
	};

	// Returns the owning asset responsible for transactions applied to MetaSound
	virtual UObject* GetOwningAsset() = 0;

	// Returns the owning asset responsible for transactions applied to MetaSound
	virtual const UObject* GetOwningAsset() const = 0;

	FString GetOwningAssetName() const;

#if WITH_EDITORONLY_DATA
	void ClearVersionedOnLoad();
	bool GetVersionedOnLoad() const;
	void SetVersionedOnLoad();
#endif // WITH_EDITORONLY_DATA

	UE_DEPRECATED(5.5, "Use IMetaSoundDocumentInterface 'IsActivelyBuilding' instead")
	virtual bool IsBuilderActive() const { checkNoEntry(); return false; }

protected:
	void OnNotifyBeginDestroy();

#if WITH_EDITOR
	virtual void SetReferencedAssetClasses(TSet<Metasound::Frontend::IMetaSoundAssetManager::FAssetInfo>&& InAssetClasses) = 0;
#endif

	// Get information for communicating asynchronously with MetaSound running instance.
	UE_DEPRECATED(5.3, "MetaSounds no longer communicate using FSendInfo.")
	TArray<FSendInfoAndVertexName> GetSendInfos(uint64 InInstanceID) const;

#if WITH_EDITORONLY_DATA
	FText GetDisplayName(FString&& InTypeName) const;
#endif // WITH_EDITORONLY_DATA

	// Returns an access pointer to the document.
	virtual Metasound::Frontend::FDocumentAccessPtr GetDocumentAccessPtr() = 0;

	// Returns an access pointer to the document.
	virtual Metasound::Frontend::FConstDocumentAccessPtr GetDocumentConstAccessPtr() const = 0;

	// Waits for a graph to be registered in the scenario where a graph is registered on an async task.
	//
	// When a graph is registered, the underlying IMetaSoundDocumentInterface may be accessed on an
	// async tasks. If modifications need to be made to the IMetaSoundDocumentInterface, callers should
	// wait for the inflight graph registration to complete by calling this method.
	
	// Container for runtime data of MetaSound graph.
	struct FRuntimeData_DEPRECATED
	{
		// Current ID of graph.
		FGuid ChangeID;

		// Array of inputs which can be set for construction. 
		TArray<FMetasoundFrontendClassInput> PublicInputs;

		// Array of inputs which can be transmitted to.
		TArray<FMetasoundFrontendClassInput> TransmittableInputs;

		// Core graph.
		TSharedPtr<Metasound::FGraph, ESPMode::ThreadSafe> Graph;
	};

	struct UE_DEPRECATED(5.4, "FRuntimeData is no longer used to store runtime graphs and inputs. Runtime graphs are stored in the node registry. Runtime inputs are stored on the UMetaSoundSoruce") FRuntimeData : FRuntimeData_DEPRECATED
	{
	};

	// Returns the cached runtime data.
	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	UE_DEPRECATED(5.4, "Access to graph and public inputs has moved. Use the node registry to access the graph and GetPublicClassInputs() to access public inputs")
	const FRuntimeData& GetRuntimeData() const;
	PRAGMA_ENABLE_DEPRECATION_WARNINGS

	UE_DEPRECATED(5.5, "AutoUpdate implementation now private and implemented within 'Version Dependencies'")
	bool AutoUpdate(bool bInLogWarningsOnDroppedConnection);

	UE_DEPRECATED(5.5, "Moved to private, non-cook specific implementation")
	void CookReferencedMetaSounds();

	// Ensures all referenced graph classes are registered (or re-registers depending on options).
	void RegisterAssetDependencies(const Metasound::Frontend::FMetaSoundAssetRegistrationOptions & InRegistrationOptions);

	UE_DEPRECATED(5.4, "Template node transformation moved to private implementation. A MetaSound asset will likely never have the function Process(...). Without a Process function, you cannot have preprocessing.")
	TSharedPtr<FMetasoundFrontendDocument> PreprocessDocument();

private:
#if WITH_EDITORONLY_DATA
	void UpdateAssetRegistry();
	void UpdateAndRegisterReferencesForSerialization(FName CookPlatformName);
#endif // WITH_EDITORONLY_DATA

	// Checks if version is up-to-date. If so, returns true. If false, updates the interfaces within the given asset's document to the most recent version.
	bool TryUpdateInterfaceFromVersion(const FMetasoundFrontendVersion& Version);

	// Versions dependencies to most recent version where applicable. If asset is a preset, MetaSound is rebuilt to accommodate any referenced node class interface changes.
	// Otherwise, automatically updates any nodes and respective dependent classes to accommodate changes to interfaces therein preserving edges/connections where possible.
	bool VersionDependencies(FMetaSoundFrontendDocumentBuilder& Builder, bool bInLogWarningsOnDroppedConnection);

	// Returns new interface to be versioned to from the given version. If no interface versioning is
	// required, returns invalid interface (interface with no name and invalid version number).
	FMetasoundFrontendInterface GetInterfaceToVersion(const FMetasoundFrontendVersion& InterfaceVersion) const;

#if WITH_EDITORONLY_DATA
	bool bVersionedOnLoad = false;
#endif // WITH_EDITORONLY_DATA

	Metasound::Frontend::FGraphRegistryKey GraphRegistryKey;
};

class METASOUNDFRONTEND_API FMetasoundAssetProxy final : public Audio::TProxyData<FMetasoundAssetProxy>
{
public:
	IMPL_AUDIOPROXY_CLASS(FMetasoundAssetProxy);

	struct FParameters
	{
		TSet<FMetasoundFrontendVersion> Interfaces;
		TSharedPtr<const Metasound::IGraph> Graph;

	};

	explicit FMetasoundAssetProxy(const FParameters& InParams);
	
	FMetasoundAssetProxy(const FMetasoundAssetProxy& Other);

	const Metasound::IGraph* GetGraph() const
	{
		return Graph.Get();
	}

	const TSet<FMetasoundFrontendVersion>& GetInterfaces() const
	{
		return Interfaces;
	}

private:
	
	TSet<FMetasoundFrontendVersion> Interfaces;
	TSharedPtr<const Metasound::IGraph> Graph;
};
using FMetasoundAssetProxyPtr = TSharedPtr<FMetasoundAssetProxy, ESPMode::ThreadSafe>;
