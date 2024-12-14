// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "RigVMGraph.h"
#include "RigVMSchema.h"
#include "RigVMFunctionLibrary.h"
#include "RigVMController.h"
#include "UObject/WeakInterfacePtr.h"
#include "RigVMClient.generated.h"

struct FRigVMClient;

UINTERFACE()
class RIGVMDEVELOPER_API URigVMClientHost : public UInterface
{
	GENERATED_BODY()
};

enum class ERigVMLoadType : uint8
{
	PostLoad,
	CheckUserDefinedStructs
};

DECLARE_DELEGATE_RetVal(URigVMGraph*, FRigVMGetFocusedGraph);

// Interface that allows an object to host a rig VM client. Used by graph edting code to interact with the controller.
class RIGVMDEVELOPER_API IRigVMClientHost
{
	GENERATED_BODY()

public:

	/** Returns the expected schema class to use for this blueprint */
	virtual FString GetAssetName() const = 0;

	/** Returns the expected schema class to use for this blueprint */
	virtual UClass* GetRigVMSchemaClass() const = 0;

	/** Returns the expected execute context struct to use for this blueprint */
	virtual UScriptStruct* GetRigVMExecuteContextStruct() const = 0;

	/** Returns the expected ed graph class to use for this blueprint */
	virtual UClass* GetRigVMEdGraphClass() const = 0;

	/** Returns the expected ed graph node class to use for this blueprint */
	virtual UClass* GetRigVMEdGraphNodeClass() const = 0;

	/** Returns the expected ed graph schema class to use for this blueprint */
	virtual UClass* GetRigVMEdGraphSchemaClass() const = 0;

	/** Returns the class of the settings to use */
	virtual UClass* GetRigVMEditorSettingsClass() const = 0;

	// Returns the rigvm client for this host
	virtual FRigVMClient* GetRigVMClient() = 0;

	// Returns the rigvm client for this host
	virtual const FRigVMClient* GetRigVMClient() const = 0;

	// Returns the rigvm function host
	virtual IRigVMGraphFunctionHost* GetRigVMGraphFunctionHost() = 0;

	// Returns the rigvm function host
	virtual const IRigVMGraphFunctionHost* GetRigVMGraphFunctionHost() const = 0;

	// Returns the editor object corresponding with the supplied RigVM graph
	virtual UObject* GetEditorObjectForRigVMGraph(URigVMGraph* InVMGraph) const = 0;

	// Returns the RigVM graph corresponding with the supplied editor object
	virtual URigVMGraph* GetRigVMGraphForEditorObject(UObject* InObject) const = 0;

	// Reacts to adding a graph
	virtual void HandleRigVMGraphAdded(const FRigVMClient* InClient, const FString& InNodePathOrName) = 0;

	// Reacts to removing a graph
	virtual void HandleRigVMGraphRemoved(const FRigVMClient* InClient, const FString& InNodePathOrName) = 0;

	// Reacts to renaming a graph
	virtual void HandleRigVMGraphRenamed(const FRigVMClient* InClient, const FString& InOldNodePath, const FString& InNewNodePath) = 0;

	// Reacts to a request to configure a controller
	virtual void HandleConfigureRigVMController(const FRigVMClient* InClient, URigVMController* InControllerToConfigure) = 0;

	// Given a type name for a user-defined type, either struct or an enum, returns a pointer to the object definition
	// or nullptr if the client host has no knowledge of such a type.
	virtual UObject* ResolveUserDefinedTypeById(const FString& InTypeName) const;

	// Recompiles the VM if not already being compiled
	virtual void RecompileVM() = 0;

	// Recompiles VM if flagged for recompiling is set
	virtual void RecompileVMIfRequired() = 0;

	// Flags VM requires recompile, and if auto recompile is enabled and no compile bracket is active, requests a recompilation
	virtual void RequestAutoVMRecompilation() = 0;

	// Sets flag for automatic recompile on model changes
	virtual void SetAutoVMRecompile(bool bAutoRecompile) = 0;

	// Returns current state of automatic recompile flag
	virtual bool GetAutoVMRecompile() const = 0;

	// Helper to increase recompile bracket on nested requests
	virtual void IncrementVMRecompileBracket() = 0;

	// Helper to decrease recompile bracket on nested requests. When value == 1, if autorecompile is enabled, it triggers a VM recompilation
	virtual void DecrementVMRecompileBracket() = 0;

	// Regenerates model pins if data has changed while the RigVM Graph is not opened (i.e. user defined struct is changed)
	virtual void RefreshAllModels(ERigVMLoadType InLoadType) = 0;

	virtual void OnRigVMRegistryChanged() = 0;

	virtual void RequestRigVMInit() = 0;

	virtual URigVMGraph* GetModel(const UEdGraph* InEdGraph = nullptr) const = 0;
	virtual URigVMGraph* GetModel(const FString& InNodePath) const = 0;

	virtual URigVMGraph* GetDefaultModel() const = 0;

	virtual TArray<URigVMGraph*> GetAllModels() const = 0;

	virtual URigVMFunctionLibrary* GetLocalFunctionLibrary() const = 0;

	virtual URigVMFunctionLibrary* GetOrCreateLocalFunctionLibrary(bool bSetupUndoRedo = true) = 0;

	virtual URigVMGraph* AddModel(FString InName = TEXT("Rig Graph"), bool bSetupUndoRedo = true, bool bPrintPythonCommand = true) = 0;

	virtual bool RemoveModel(FString InName = TEXT("Rig Graph"), bool bSetupUndoRedo = true, bool bPrintPythonCommand = true) = 0;

	virtual FRigVMGetFocusedGraph& OnGetFocusedGraph() = 0;
	virtual const FRigVMGetFocusedGraph& OnGetFocusedGraph() const = 0;

	virtual URigVMGraph* GetFocusedModel() const = 0;

	virtual URigVMController* GetController(const URigVMGraph* InGraph = nullptr) const = 0;

	virtual URigVMController* GetControllerByName(const FString InGraphName = TEXT("")) const = 0;

	virtual URigVMController* GetOrCreateController(URigVMGraph* InGraph = nullptr) = 0;

	virtual URigVMController* GetController(const UEdGraph* InEdGraph) const = 0;
	virtual URigVMController* GetOrCreateController(const UEdGraph* InGraph) = 0;

	virtual TArray<FString> GeneratePythonCommands(const FString InNewBlueprintName)  = 0;

	virtual void SetupPinRedirectorsForBackwardsCompatibility() = 0;

	virtual FRigVMGraphModifiedEvent& OnModified() = 0;

	virtual bool IsFunctionPublic(const FName& InFunctionName) const = 0;
	virtual void MarkFunctionPublic(const FName& InFunctionName, bool bIsPublic = true) = 0;

	virtual void RenameGraph(const FString& InNodePath, const FName& InNewName) = 0;
};

UINTERFACE()
class RIGVMDEVELOPER_API URigVMEditorSideObject : public UInterface
{
	GENERATED_BODY()
};

// Interface that allows a UI graph to identify itself against a model graph
class RIGVMDEVELOPER_API IRigVMEditorSideObject
{
	GENERATED_BODY()

public:

	// Returns the corresponding VM graph
	virtual FRigVMClient* GetRigVMClient() const = 0;

	// Returns the nodepath for this UI graph
	virtual FString GetRigVMNodePath() const = 0;

	// Reacts to renaming the model
	virtual void HandleRigVMGraphRenamed(const FString& InOldNodePath, const FString& InNewNodePath) = 0;
};

UINTERFACE()
class RIGVMDEVELOPER_API URigVMClientExternalModelHost : public UInterface
{
	GENERATED_BODY()
};

// Interface that allows access to externally-hosted models
class RIGVMDEVELOPER_API IRigVMClientExternalModelHost
{
	GENERATED_BODY()

public:

	// Returns the externally-held models for a client
	virtual const TArray<TObjectPtr<URigVMGraph>>& GetExternalModels() const = 0;

	// Sets a new created model as 
	virtual TObjectPtr<URigVMGraph> CreateContainedGraphModel(URigVMCollapseNode* CollapseNode, const FName& Name) = 0;
};


// A management struct containing graphs and controllers.
USTRUCT()
struct RIGVMDEVELOPER_API FRigVMClient
{
public:

	GENERATED_BODY()

	FRigVMClient()
		: DefaultSchemaClass(nullptr)
		, ControllerClass(URigVMController::StaticClass())
		, FunctionLibrary(nullptr)
		, ActionStack(nullptr)
		, bSuspendNotifications(false)
		, bIgnoreModelNotifications(false)
		, bDefaultModelCanBeRemoved(false)
		, bSuspendModelNotificationsForOthers(false)
		, OuterClientHost(nullptr)
		, OuterClientPropertyName(NAME_None)	
		, ExternalModelHost(nullptr)
	{
	}

	UE_DEPRECATED(5.5, "Please use SetDefaultSchemaClass or set a schema per controller/graph.")
	void SetSchemaClass(TSubclassOf<URigVMSchema> InSchemaClass) { SetDefaultSchemaClass(InSchemaClass); }
	void SetDefaultSchemaClass(TSubclassOf<URigVMSchema> InSchemaClass);
	void SetControllerClass(TSubclassOf<URigVMController> InControllerClass);
	void SetOuterClientHost(UObject* InOuterClientHost, const FName& InOuterClientHostPropertyName);
	void SetFromDeprecatedData(URigVMGraph* InDefaultGraph, URigVMFunctionLibrary* InFunctionLibrary);
	void SetExternalModelHost(IRigVMClientExternalModelHost* InExternalModelHost);

	void Reset();
	int32 Num() const { return GetModels().Num(); }
	UE_DEPRECATED(5.5, "Please use GetDefaultSchema or get a schema per controller/graph.")
	URigVMSchema* GetSchema() { return GetDefaultSchema(); }
	URigVMSchema* GetDefaultSchema() const;
	TSubclassOf<URigVMSchema> GetDefaultSchemaClass() const { return DefaultSchemaClass; }
	UE_DEPRECATED(5.5, "Please use GetSchema or get a schema per controller/graph.")
	URigVMSchema* GetOrCreateSchema() { return GetDefaultSchema(); }
	URigVMGraph* GetDefaultModel() const;
	URigVMGraph* GetModel(int32 InIndex) const;
	URigVMGraph* GetModel(const UEdGraph* InEdGraph = nullptr) const;
	URigVMGraph* GetModel(const FString& InNodePathOrName) const;
	URigVMGraph* GetModel(const UObject* InEditorSideObject) const;
	void RefreshAllModels(ERigVMLoadType InLoadType, bool bEnablePostLoadHashing, bool& bIsCompiling);
	const TArray<TObjectPtr<URigVMGraph>>& GetModels() const;
	TArray<URigVMGraph*> GetAllModels(bool bIncludeFunctionLibrary, bool bRecursive) const;
	TArray<URigVMGraph*> GetAllModelsLeavesFirst(bool bIncludeFunctionLibrary) const;
	URigVMController* GetController(int32 InIndex) const;
	URigVMController* GetController(const FString& InNodePathOrName) const;
	URigVMController* GetController(const URigVMGraph* InModel) const;
	URigVMController* GetController(const UObject* InEditorSideObject) const;
	URigVMController* GetOrCreateController(int32 InIndex);
	URigVMController* GetOrCreateController(const FString& InNodePathOrName);
	URigVMController* GetOrCreateController(const URigVMGraph* InModel);
	URigVMController* GetOrCreateController(const UObject* InEditorSideObject);
	URigVMController* GetControllerByName(const FString InGraphName) const;
	bool RemoveController(const URigVMGraph* InModel);
	URigVMFunctionLibrary* GetFunctionLibrary() const { return FunctionLibrary; }
	URigVMFunctionLibrary* GetOrCreateFunctionLibrary(bool bSetupUndoRedo, const FObjectInitializer* ObjectInitializer = nullptr, bool bCreateController = true);
	URigVMFunctionLibrary* GetOrCreateFunctionLibrary(TSubclassOf<URigVMSchema> InSchemaClass, bool bSetupUndoRedo, const FObjectInitializer* ObjectInitializer = nullptr, bool bCreateController = true);
	TArray<FName> GetEntryNames() const;
	UE_DEPRECATED(5.5, "Please use GetDefaultExecuteContextStruct or get an execute context from a graph/controller schema.")
	UScriptStruct* GetExecuteContextStruct() const { return GetDefaultExecuteContextStruct(); }
	UScriptStruct* GetDefaultExecuteContextStruct() const;
	UE_DEPRECATED(5.5, "Please use SetDefaultExecuteContextStruct or set an execute context on a graph/controller schema.")
	void SetExecuteContextStruct(UScriptStruct* InExecuteContextStruct) { SetDefaultExecuteContextStruct(InExecuteContextStruct); }
	void SetDefaultExecuteContextStruct(UScriptStruct* InExecuteContextStruct);

	FRigVMGetFocusedGraph& OnGetFocusedGraph() { return OnGetFocusedGraphDelegate;}
	const FRigVMGetFocusedGraph& OnGetFocusedGraph() const { return OnGetFocusedGraphDelegate; }
	URigVMGraph* GetFocusedModel() const;
	URigVMGraph* AddModel(const FString InName, bool bSetupUndoRedo, bool bPrintPythonCommand);
	URigVMGraph* AddModel(const FName& InName, bool bSetupUndoRedo, const FObjectInitializer* ObjectInitializer = nullptr, bool bCreateController = true);
	URigVMGraph* AddModel(const FName& InName, TSubclassOf<URigVMSchema> InSchemaClass, bool bSetupUndoRedo, const FObjectInitializer* ObjectInitializer = nullptr, bool bCreateController = true);

	URigVMGraph* CreateModel(const FName& InName, TSubclassOf<URigVMSchema> InSchemaClass, bool bSetupUndoRedo, UObject* InOuter, const FObjectInitializer* ObjectInitializer = nullptr, bool bCreateController = true);
	TObjectPtr<URigVMGraph> CreateContainedGraphModel(URigVMCollapseNode* CollapseNode, const FName& Name);
	
	void AddModel(URigVMGraph* InModel, bool bCreateController);
	bool RemoveModel(FString InName, bool bSetupUndoRedo, bool bPrintPythonCommand);
	bool RemoveModel(const FString& InNodePathOrName, bool bSetupUndoRedo);
	FName RenameModel(const FString& InNodePathOrName, const FName& InNewName, bool bSetupUndoRedo);
	void PostTransacted(const FTransactionObjectEvent& TransactionEvent);
	void OnCollapseNodeRenamed(const URigVMCollapseNode* InCollapseNode);
	void OnCollapseNodeRemoved(const URigVMCollapseNode* InCollapseNode);

	URigVMNode* FindNode(const FString& InNodePathOrName) const;
	URigVMPin* FindPin(const FString& InPinPath) const;
	
	TArray<TObjectPtr<URigVMGraph>>::RangedForIteratorType      begin() { return const_cast<TArray<TObjectPtr<URigVMGraph>>&>(GetModels()).begin(); }
	TArray<TObjectPtr<URigVMGraph>>::RangedForConstIteratorType begin() const { return GetModels().begin(); }
	TArray<TObjectPtr<URigVMGraph>>::RangedForIteratorType      end() { return const_cast<TArray<TObjectPtr<URigVMGraph>>&>(GetModels()).end(); }
	TArray<TObjectPtr<URigVMGraph>>::RangedForConstIteratorType end() const { return GetModels().end(); }

	UObject* GetOuter() const;
	FProperty* GetOuterClientProperty() const;
	void NotifyOuterOfPropertyChange(EPropertyChangeType::Type ChangeType = EPropertyChangeType::Interactive) const;
	FName GetUniqueName(const FName& InDesiredName) const;
	static FName GetUniqueName(UObject* InOuter, const FName& InDesiredName);
	static void DestroyObject(UObject* InObject);

	uint32 GetStructureHash() const;
	uint32 GetSerializedStructureHash() const;

	// backwards compatibility
	FRigVMClientPatchResult PatchModelsOnLoad();
	void PatchFunctionReferencesOnLoad();
	void PatchFunctionsOnLoad(IRigVMGraphFunctionHost* FunctionHost, TArray<FName>& BackwardsCompatiblePublicFunctions, TMap<URigVMLibraryNode*, FRigVMGraphFunctionHeader>& OldHeaders);
	FRigVMClientPatchResult PatchPinDefaultValues();

	// try to reattach detached links and delete remaining ones
	void ProcessDetachedLinks();

	// work to be done before saving
	void PreSave(FObjectPreSaveContext ObjectSaveContext);

	void HandleGraphModifiedEvent(ERigVMGraphNotifType InNotifType, URigVMGraph* InGraph, UObject* InSubject);

	FRigVMGraphFunctionStore* FindFunctionStore(const URigVMLibraryNode* InLibraryNode);
	bool UpdateGraphFunctionData(const URigVMLibraryNode* InLibraryNode);
	bool UpdateExternalVariablesForFunction(const URigVMLibraryNode* InLibraryNode);
	bool UpdateDependenciesForFunction(const URigVMLibraryNode* InLibraryNode);
	bool UpdateFunctionReferences(const FRigVMGraphFunctionHeader& InHeader, bool bUpdateDependencies, bool bUpdateExternalVariables);
	bool DirtyGraphFunctionCompilationData(URigVMLibraryNode* InLibraryNode);
	bool UpdateGraphFunctionSerializedGraph(URigVMLibraryNode* InLibraryNode);
	bool IsFunctionPublic(URigVMLibraryNode* InLibraryNode);

	static constexpr TCHAR RigVMModelPrefix[] = TEXT("RigVMModel");

private:

	enum ERigVMClientAction
	{
		ERigVMClientAction_AddModel,
		ERigVMClientAction_RemoveModel,
		ERigVMClientAction_RenameModel
	};

	struct FRigVMClientAction
	{
		ERigVMClientAction Type;
		FString NodePath;
		FString OtherNodePath;
	};

	URigVMController* CreateController(const URigVMGraph* InModel);
	URigVMActionStack* GetOrCreateActionStack();
	void ResetActionStack();

	FRigVMGetFocusedGraph OnGetFocusedGraphDelegate;

	UPROPERTY(transient)
	TSubclassOf<URigVMSchema> DefaultSchemaClass;

	UPROPERTY(transient)
	TSubclassOf<URigVMController> ControllerClass;

	UPROPERTY()
	TArray<TObjectPtr<URigVMGraph>> Models;

	UPROPERTY()
	TObjectPtr<URigVMFunctionLibrary> FunctionLibrary;

	UPROPERTY(transient)
	TMap<FSoftObjectPath, TObjectPtr<URigVMController>> Controllers;

	UPROPERTY(transient)
	TObjectPtr<URigVMActionStack> ActionStack;

	UPROPERTY(transient)
	int32 UndoRedoIndex = 0;
	
	TArray<FRigVMClientAction> UndoStack;
	TArray<FRigVMClientAction> RedoStack;

public:
	bool bSuspendNotifications;
	bool bIgnoreModelNotifications;
	bool bDefaultModelCanBeRemoved;
	bool bSuspendModelNotificationsForOthers;
private:
	TWeakObjectPtr<UObject> OuterClientHost;
	FName OuterClientPropertyName;

	TWeakInterfacePtr<IRigVMClientExternalModelHost> ExternalModelHost;

	friend class UEngineTestClientHost;
	friend class URigVMBlueprint;
};
