// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "RigVMModel/RigVMGraph.h"
#include "RigVMCore/RigVMGraphFunctionHost.h"
#include "RigVMBlueprint.h"
#include "UncookedOnlyUtils.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "AnimNextRigVMAssetEditorData.generated.h"

enum class ERigVMGraphNotifType : uint8;
class UAnimNextRigVMAssetEntry;
class UAnimNextRigVMAssetEditorData;
class UAnimNextEdGraph;
class UAnimNextDataInterfaceEntry;

namespace UE::AnimNext::UncookedOnly
{
	struct FUtils;
	struct FUtilsPrivate;
}

namespace UE::AnimNext::Editor
{
	struct FUtils;
	class SRigVMAssetView;
	class SParameterPicker;
	class SRigVMAssetViewRow;
	class FVariableCustomization;
	class FAnimNextEditorModule;
	class FWorkspaceEditor;
	class FAnimNextAssetItemDetails;
	class FAnimNextGraphItemDetails;
	class FAnimNextFunctionItemDetails;
	struct FVariablesOutlinerEntryItem;
	class FVariablesOutlinerMode;
	class FVariablesOutlinerHierarchy;
	class SVariablesOutlinerValue;
	class SVariablesOutliner;
	class SAddVariablesDialog;
	class FVariableProxyCustomization;
}

namespace UE::AnimNext::Tests
{
	class FEditor_Graphs;
	class FEditor_Variables;
	class FVariables;
	class FDataInterfaceCompile;
}

enum class EAnimNextEditorDataNotifType : uint8
{
	PropertyChanged,	// An property was changed (Subject == UObject)
	EntryAdded,		// An entry has been added (Subject == UAnimNextRigVMAssetEntry)
	EntryRemoved,	// An entry has been removed (Subject == UAnimNextRigVMAssetEditorData)
	EntryRenamed,	// An entry has been renamed (Subject == UAnimNextRigVMAssetEntry)
	EntryAccessSpecifierChanged,	// An entry access specifier has been changed (Subject == UAnimNextRigVMAssetEntry)
	VariableTypeChanged,	// A variable entry type changed (Subject == UAnimNextVariableEntry)
	UndoRedo,		// Transaction was performed (Subject == UObject)
	VariableDefaultValueChanged,	// A variable entry default value changed (Subject == UAnimNextVariableEntry)
	VariableBindingChanged,	// A variable entry binding changed (Subject == UAnimNextVariableEntry)
};

namespace UE::AnimNext::UncookedOnly
{
	// A delegate for subscribing / reacting to editor data modifications.
	DECLARE_MULTICAST_DELEGATE_ThreeParams(FOnEditorDataModified, UAnimNextRigVMAssetEditorData* /* InEditorData */, EAnimNextEditorDataNotifType /* InType */, UObject* /* InSubject */);

	// An interaction bracket count reached 0
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnInteractionBracketFinished, UAnimNextRigVMAssetEditorData* /* InEditorData */);
}

// Script-callable editor API hoisted onto UAnimNextRigVMAsset
UCLASS()
class UAnimNextRigVMAssetLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** Finds an entry in an AnimNext asset */
	UFUNCTION(BlueprintCallable, Category = "AnimNext|Entries", meta=(ScriptMethod))
	static ANIMNEXTUNCOOKEDONLY_API UAnimNextRigVMAssetEntry* FindEntry(UAnimNextRigVMAsset* InAsset, FName InName);

	/** Removes an entry from an AnimNext asset */
	UFUNCTION(BlueprintCallable, Category = "AnimNext|Entries", meta=(ScriptMethod))
	static ANIMNEXTUNCOOKEDONLY_API bool RemoveEntry(UAnimNextRigVMAsset* InAsset, UAnimNextRigVMAssetEntry* InEntry, bool bSetupUndoRedo = true, bool bPrintPythonCommand = true);

	/** Removes multiple entries from an AnimNext asset */
	UFUNCTION(BlueprintCallable, Category = "AnimNext|Entries", meta=(ScriptMethod))
	static ANIMNEXTUNCOOKEDONLY_API bool RemoveEntries(UAnimNextRigVMAsset* InAsset, const TArray<UAnimNextRigVMAssetEntry*>& InEntries, bool bSetupUndoRedo = true, bool bPrintPythonCommand = true);

	/** Removes all entries from an AnimNext asset */
	UFUNCTION(BlueprintCallable, Category = "AnimNext|Entries", meta=(ScriptMethod))
	static ANIMNEXTUNCOOKEDONLY_API bool RemoveAllEntries(UAnimNextRigVMAsset* InAsset, bool bSetupUndoRedo = true, bool bPrintPythonCommand = true);

	/** Adds an animation graph to an AnimNext asset */
	UFUNCTION(BlueprintCallable, Category = "AnimNext|Entries", meta=(ScriptMethod))
	static ANIMNEXTUNCOOKEDONLY_API UAnimNextAnimationGraphEntry* AddAnimationGraph(UAnimNextRigVMAsset* InAsset, FName InName, bool bSetupUndoRedo = true, bool bPrintPythonCommand = true);

	/** Adds a parameter to an AnimNext asset */
	UFUNCTION(BlueprintCallable, Category = "AnimNext|Entries", meta=(ScriptMethod))
	static ANIMNEXTUNCOOKEDONLY_API UAnimNextVariableEntry* AddVariable(UAnimNextRigVMAsset* InAsset, FName InName, EPropertyBagPropertyType InValueType, EPropertyBagContainerType InContainerType = EPropertyBagContainerType::None, const UObject* InValueTypeObject = nullptr, const FString& InDefaultValue = TEXT(""), bool bSetupUndoRedo = true, bool bPrintPythonCommand = true);

	/** Adds an event graph to an AnimNext asset */
	UFUNCTION(BlueprintCallable, Category = "AnimNext|Entries", meta=(ScriptMethod))
	static ANIMNEXTUNCOOKEDONLY_API UAnimNextEventGraphEntry* AddEventGraph(UAnimNextRigVMAsset* InAsset, FName InName, UScriptStruct* InEventStruct, bool bSetupUndoRedo = true, bool bPrintPythonCommand = true);

	/** Adds a data interface to an AnimNext asset */
	UFUNCTION(BlueprintCallable, Category = "AnimNext|Entries", meta=(ScriptMethod))
	static ANIMNEXTUNCOOKEDONLY_API UAnimNextDataInterfaceEntry* AddDataInterface(UAnimNextRigVMAsset* InAsset, UAnimNextDataInterface* InDataInterface, bool bSetupUndoRedo = true, bool bPrintPythonCommand = true);
};

/* Base class for all AnimNext editor data objects that use RigVM */
UCLASS(Abstract)
class ANIMNEXTUNCOOKEDONLY_API UAnimNextRigVMAssetEditorData : public UObject, public IRigVMClientHost, public IRigVMGraphFunctionHost, public IRigVMClientExternalModelHost
{
	GENERATED_BODY()

public:
	/** Adds an animation graph to this asset */
	UAnimNextAnimationGraphEntry* AddAnimationGraph(FName InName, bool bSetupUndoRedo = true, bool bPrintPythonCommand = true);

	/** Adds a parameter to this asset */
	UAnimNextVariableEntry* AddVariable(FName InName, FAnimNextParamType InType, const FString& InDefaultValue = TEXT(""), bool bSetupUndoRedo = true, bool bPrintPythonCommand = true);

	/** Adds an event graph to this asset */
	UAnimNextEventGraphEntry* AddEventGraph(FName InName, UScriptStruct* InEventStruct, bool bSetupUndoRedo = true, bool bPrintPythonCommand = true);

	/** Adds a data interface to this asset */
	UAnimNextDataInterfaceEntry* AddDataInterface(UAnimNextDataInterface* InDataInterface, bool bSetupUndoRedo = true, bool bPrintPythonCommand = true);

	// Report an error to the user, typically used for scripting APIs
	static void ReportError(const TCHAR* InMessage);

protected:
	friend class UE::AnimNext::Editor::SRigVMAssetView;
	friend class UE::AnimNext::Editor::SRigVMAssetViewRow;
	friend struct UE::AnimNext::Editor::FUtils;
	friend struct UE::AnimNext::UncookedOnly::FUtils;
	friend class UE::AnimNext::Editor::FAnimNextEditorModule;
	friend class UE::AnimNext::Editor::FWorkspaceEditor;
	friend class UAnimNextRigVMAssetEntry;
	friend class UAnimNextRigVMAssetLibrary;
	friend class UAnimNextEdGraph;
	friend class UE::AnimNext::Tests::FEditor_Graphs;
	friend class UE::AnimNext::Tests::FEditor_Variables;
	friend class UE::AnimNext::Tests::FVariables;
	friend class UE::AnimNext::Tests::FDataInterfaceCompile;
	friend class UE::AnimNext::Editor::FVariableCustomization;
	friend class UE::AnimNext::Editor::FAnimNextAssetItemDetails;
	friend class UE::AnimNext::Editor::FAnimNextGraphItemDetails;
	friend class UE::AnimNext::Editor::FAnimNextFunctionItemDetails;
	friend struct UE::AnimNext::Editor::FVariablesOutlinerEntryItem;
	friend class UE::AnimNext::Editor::FVariablesOutlinerMode;
	friend class UE::AnimNext::Editor::FVariablesOutlinerHierarchy;
	friend class UE::AnimNext::Editor::SVariablesOutlinerValue;
	friend class UE::AnimNext::Editor::SVariablesOutliner;
	friend class UAnimNextModuleWorkspaceAssetUserData;
	friend class UE::AnimNext::Editor::SAddVariablesDialog;
	friend class UE::AnimNext::Editor::FVariableProxyCustomization;
	friend class UAnimNextDataInterfaceEntry;

	// UObject interface
	virtual void Serialize(FArchive& Ar) override;
	virtual void PostLoad() override;
	virtual void PostTransacted(const FTransactionObjectEvent& TransactionEvent) override;
	virtual void PostDuplicate(EDuplicateMode::Type DuplicateMode) override;
	virtual void GetAssetRegistryTags(FAssetRegistryTagsContext Context) const override;
	virtual bool IsEditorOnly() const override { return true; }
	virtual bool Rename(const TCHAR* NewName = nullptr, UObject* NewOuter = nullptr, ERenameFlags Flags = REN_None) override;	
	virtual void PreDuplicate(FObjectDuplicationParameters& DupParams) override;

	void HandlePackageDone(const FEndLoadPackageContext& Context);
	void HandlePackageDone();

	// IRigVMClientHost interface
	virtual FString GetAssetName() const override { return GetName(); }
	virtual UClass* GetRigVMSchemaClass() const override;
	virtual UScriptStruct* GetRigVMExecuteContextStruct() const override;
	virtual UClass* GetRigVMEdGraphClass() const override;
	virtual UClass* GetRigVMEdGraphNodeClass() const override;
	virtual UClass* GetRigVMEdGraphSchemaClass() const override;
	virtual UClass* GetRigVMEditorSettingsClass() const override;
	virtual FRigVMClient* GetRigVMClient() override;
	virtual const FRigVMClient* GetRigVMClient() const override;
	virtual IRigVMGraphFunctionHost* GetRigVMGraphFunctionHost() override;
	virtual const IRigVMGraphFunctionHost* GetRigVMGraphFunctionHost() const override;
	virtual void HandleRigVMGraphAdded(const FRigVMClient* InClient, const FString& InNodePath) override;
	virtual void HandleRigVMGraphRemoved(const FRigVMClient* InClient, const FString& InNodePath) override;
	virtual void HandleRigVMGraphRenamed(const FRigVMClient* InClient, const FString& InOldNodePath, const FString& InNewNodePath) override;
	virtual void HandleConfigureRigVMController(const FRigVMClient* InClient, URigVMController* InControllerToConfigure) override;
	virtual UObject* GetEditorObjectForRigVMGraph(URigVMGraph* InVMGraph) const override;
	virtual URigVMGraph* GetRigVMGraphForEditorObject(UObject* InObject) const override;
	virtual void RecompileVM() override;
	virtual void RecompileVMIfRequired() override;
	virtual void RequestAutoVMRecompilation() override;
	virtual void SetAutoVMRecompile(bool bAutoRecompile) override;
	virtual bool GetAutoVMRecompile() const override;
	virtual void IncrementVMRecompileBracket() override;
	virtual void DecrementVMRecompileBracket() override;
	virtual void RefreshAllModels(ERigVMLoadType InLoadType) override;
	virtual void OnRigVMRegistryChanged() override;
	virtual void RequestRigVMInit() override;
	virtual URigVMGraph* GetModel(const UEdGraph* InEdGraph = nullptr) const override;
	virtual URigVMGraph* GetModel(const FString& InNodePath) const override;
	virtual URigVMGraph* GetDefaultModel() const override;
	virtual TArray<URigVMGraph*> GetAllModels() const override;
	virtual URigVMFunctionLibrary* GetLocalFunctionLibrary() const override;
	virtual URigVMFunctionLibrary* GetOrCreateLocalFunctionLibrary(bool bSetupUndoRedo =  true) override;
	virtual URigVMGraph* AddModel(FString InName = TEXT("Rig Graph"), bool bSetupUndoRedo = true, bool bPrintPythonCommand = true) override;
	virtual bool RemoveModel(FString InName = TEXT("Rig Graph"), bool bSetupUndoRedo = true, bool bPrintPythonCommand = true) override;
	virtual FRigVMGetFocusedGraph& OnGetFocusedGraph() override;
	virtual const FRigVMGetFocusedGraph& OnGetFocusedGraph() const override;
	virtual URigVMGraph* GetFocusedModel() const override;
	virtual URigVMController* GetController(const URigVMGraph* InGraph = nullptr) const override;
	virtual URigVMController* GetControllerByName(const FString InGraphName = TEXT("")) const override;
	virtual URigVMController* GetOrCreateController(URigVMGraph* InGraph = nullptr) override;
	virtual URigVMController* GetController(const UEdGraph* InEdGraph) const override;
	virtual URigVMController* GetOrCreateController(const UEdGraph* InGraph) override;
	virtual TArray<FString> GeneratePythonCommands(const FString InNewBlueprintName) override;
	virtual void SetupPinRedirectorsForBackwardsCompatibility() override;
	virtual FRigVMGraphModifiedEvent& OnModified() override;
	virtual bool IsFunctionPublic(const FName& InFunctionName) const override;
	virtual void MarkFunctionPublic(const FName& InFunctionName, bool bIsPublic = true) override;
	virtual void RenameGraph(const FString& InNodePath, const FName& InNewName) override;


	// IRigVMGraphFunctionHost interface
	virtual FRigVMGraphFunctionStore* GetRigVMGraphFunctionStore() override;
	virtual const FRigVMGraphFunctionStore* GetRigVMGraphFunctionStore() const override;

	// IRigVMClientExternalModelHost interface
	virtual const TArray<TObjectPtr<URigVMGraph>>& GetExternalModels() const override { return GraphModels; }
	virtual TObjectPtr<URigVMGraph> CreateContainedGraphModel(URigVMCollapseNode* CollapseNode, const FName& Name) override;

	// Override called during initialization to determine what RigVM controller class is used
	virtual TSubclassOf<URigVMController> GetControllerClass() const { return URigVMController::StaticClass(); }

	// Override called during initialization to determine what RigVM execute struct is used
	virtual UScriptStruct* GetExecuteContextStruct() const PURE_VIRTUAL(UAnimNextRigVMAssetEditorData::GetExecuteContextStruct, return nullptr;)

	// Create and store a UEdGraph that corresponds to a URigVMGraph
	virtual UEdGraph* CreateEdGraph(URigVMGraph* InRigVMGraph, bool bForce);

	// Create and store a UEdGraph that corresponds to a URigVMCollapseNode
	virtual void CreateEdGraphForCollapseNode(URigVMCollapseNode* InNode, bool bForce);

	// Destroy a UEdGraph that corresponds to a URigVMCollapseNode
	virtual void RemoveEdGraphForCollapseNode(URigVMCollapseNode* InNode, bool bNotify);

	// Remove the UEdGraph that corresponds to a URigVMGraph
	virtual bool RemoveEdGraph(URigVMGraph* InModel);

	// Initialize the asset for use
	virtual void Initialize(bool bRecompileVM);

	// Handle RigVM modification events
	virtual void HandleModifiedEvent(ERigVMGraphNotifType InNotifType, URigVMGraph* InGraph, UObject* InSubject);

	// Class to use when instantiating AssetUserData for the EditorData instance
	virtual TSubclassOf<UAssetUserData> GetAssetUserDataClass() const;

	// Get all the kinds of entry for this asset
	virtual TConstArrayView<TSubclassOf<UAnimNextRigVMAssetEntry>> GetEntryClasses() const PURE_VIRTUAL(UAnimNextRigVMAssetEditorData::GetEntryClasses, return {};)

	// Override to allow assets to prevent certain entries being created
	virtual bool CanAddNewEntry(TSubclassOf<UAnimNextRigVMAssetEntry> InClass) const { return true; }

	// Allows this asset to generate graphs to be injected at compilation time
	virtual void GetProgrammaticGraphs(const FRigVMCompileSettings& InSettings, TArray<URigVMGraph*>& OutGraphs) {}

	// Customization point for derived types to transform new asset entries
	virtual void CustomizeNewAssetEntry(UAnimNextRigVMAssetEntry* InNewEntry) const {}

	// Helper for creating new sub-entries. Sets package flags and outers appropriately 
	static UObject* CreateNewSubEntry(UAnimNextRigVMAssetEditorData* InEditorData, TSubclassOf<UObject> InClass);

	// Helper for creating new sub-entries. Sets package flags and outers appropriately
	template<typename EntryClassType>
	static EntryClassType* CreateNewSubEntry(UAnimNextRigVMAssetEditorData* InEditorData)
	{
		return CastChecked<EntryClassType>(CreateNewSubEntry(InEditorData, EntryClassType::StaticClass()));
	}

	// Get all the entries for this asset
	TConstArrayView<TObjectPtr<UAnimNextRigVMAssetEntry>> GetAllEntries() const { return Entries; } 

	// Access all the UEdGraphs in this asset
	TArray<UEdGraph*> GetAllEdGraphs() const;

	// Iterate over all entries of the specified type
	// If predicate returns false, iteration is stopped
	template<typename EntryType, typename PredicateType>
	void ForEachEntryOfType(PredicateType InPredicate) const
	{
		for(UAnimNextRigVMAssetEntry* Entry : Entries)
		{
			if(EntryType* TypedEntry = Cast<EntryType>(Entry))
			{
				if(!InPredicate(TypedEntry))
				{
					return;
				}
			}
		}
	}

	// Returns all nodes in all graphs of the specified class
	template<class T>
	void GetAllNodesOfClass(TArray<T*>& OutNodes) const
	{
		ForEachEntryOfType<IAnimNextRigVMGraphInterface>([&OutNodes](IAnimNextRigVMGraphInterface* InGraphInterface)
		{
			URigVMEdGraph* RigVMEdGraph = InGraphInterface->GetEdGraph();
			check(RigVMEdGraph)

			TArray<T*> GraphNodes;
			RigVMEdGraph->GetNodesOfClass<T>(GraphNodes);

			TArray<UEdGraph*> SubGraphs;
			RigVMEdGraph->GetAllChildrenGraphs(SubGraphs);
			for (const UEdGraph* SubGraph : SubGraphs)
			{
				if (SubGraph)
				{
					SubGraph->GetNodesOfClass<T>(GraphNodes);
				}
			}

			OutNodes.Append(GraphNodes);

			return true;
		});

		for (URigVMEdGraph* RigVMEdGraph : FunctionEdGraphs)
		{
			if (RigVMEdGraph)
			{
				RigVMEdGraph->GetNodesOfClass<T>(OutNodes);

				TArray<UEdGraph*> SubGraphs;
				RigVMEdGraph->GetAllChildrenGraphs(SubGraphs);
				for (const UEdGraph* SubGraph : SubGraphs)
				{
					if (SubGraph)
					{
						SubGraph->GetNodesOfClass<T>(OutNodes);
					}
				}
			}
		}
	}
	
	// Find an entry by name
	UAnimNextRigVMAssetEntry* FindEntry(FName InName) const;

	// Remove an entry from the asset
	// @return true if the item was removed
	bool RemoveEntry(UAnimNextRigVMAssetEntry* InEntry, bool bSetupUndoRedo = true, bool bPrintPythonCommand = true);

	// Remove a number of entries from the asset
	// @return true if any items were removed
	bool RemoveEntries(TConstArrayView<UAnimNextRigVMAssetEntry*> InEntries, bool bSetupUndoRedo = true, bool bPrintPythonCommand = true);

	// Remove all entries from the asset
	// @return true if any items were removed
	bool RemoveAllEntries(bool bSetupUndoRedo = true, bool bPrintPythonCommand = true);

	void BroadcastModified(EAnimNextEditorDataNotifType InType, UObject* InSubject);

	void ReconstructAllNodes();

	// Called from PostLoad to load external packages
	void PostLoadExternalPackages();

	// Find an entry that corresponds to the specified RigVMGraph. This uses the name of the graph to match the entry 
	UAnimNextRigVMAssetEntry* FindEntryForRigVMGraph(const URigVMGraph* InRigVMGraph) const;

	// Find an entry that corresponds to the specified RigVMGraph. This uses the name of the graph to match the entry 
	UAnimNextRigVMAssetEntry* FindEntryForRigVMEdGraph(const URigVMEdGraph* InRigVMEdGraph) const;

	// Checks all entries to see if any are public variables
	bool HasPublicVariables() const;

	// Refresh the 'external' models for the RigVM client to reference
	void RefreshExternalModels();

	// Handle compiler reporting
	void HandleReportFromCompiler(EMessageSeverity::Type InSeverity, UObject* InSubject, const FString& InMessage);

	/** All entries in this asset - not saved, either serialized or discovered at load time */
	UPROPERTY(transient)
	TArray<TObjectPtr<UAnimNextRigVMAssetEntry>> Entries;

	UPROPERTY()
	FRigVMClient RigVMClient;

	UPROPERTY()
	FRigVMGraphFunctionStore GraphFunctionStore;

	UPROPERTY(EditAnywhere, Category = "User Interface")
	FRigVMEdGraphDisplaySettings RigGraphDisplaySettings;

	UPROPERTY(EditAnywhere, Category = "VM")
	FRigVMRuntimeSettings VMRuntimeSettings;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VM", meta = (AllowPrivateAccess = "true"))
	FRigVMCompileSettings VMCompileSettings;

	UPROPERTY(transient, DuplicateTransient)
	TMap<FString, FRigVMOperand> PinToOperandMap;

	UPROPERTY()
	TArray<FEditedDocumentInfo> LastEditedDocuments;

	UPROPERTY(transient, DuplicateTransient)
	int32 VMRecompilationBracket = 0;

	UPROPERTY(transient, DuplicateTransient)
	bool bVMRecompilationRequired = false;

	UPROPERTY(transient, DuplicateTransient)
	bool bIsCompiling = false;

	FOnRigVMCompiledEvent RigVMCompiledEvent;

	FRigVMGraphModifiedEvent RigVMGraphModifiedEvent;

	// Delegate to subscribe to modifications to this editor data
	UE::AnimNext::UncookedOnly::FOnEditorDataModified ModifiedDelegate;

	// Delegate to get notified when an interaction bracket reaches 0
	UE::AnimNext::UncookedOnly::FOnInteractionBracketFinished InteractionBracketFinished;

	// Cached exports, generated lazily or on compilation
	mutable TOptional<FAnimNextAssetRegistryExports> CachedExports;
	
	// Collection of models gleaned from graphs
	TArray<TObjectPtr<URigVMGraph>> GraphModels;

	// Set of functions implemented for this graph
	UPROPERTY()
	TArray<TObjectPtr<URigVMEdGraph>> FunctionEdGraphs;

	// Default FunctionLibrary EdGraph
	UPROPERTY()
	TObjectPtr<UAnimNextEdGraph> FunctionLibraryEdGraph;

	bool bAutoRecompileVM = true;
	bool bErrorsDuringCompilation = false;
	bool bSuspendModelNotificationsForSelf = false;
	bool bSuspendAllNotifications = false;
	bool bCompileInDebugMode = false;
	bool bSuspendPythonMessagesForRigVMClient = true;
	bool bSuspendEditorDataNotifications = false;
	
};
