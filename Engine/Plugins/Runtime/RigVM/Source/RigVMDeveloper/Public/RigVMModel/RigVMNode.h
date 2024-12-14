// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "RigVMPin.h"
#include "RigVMTraitDefaultValueStruct.h"
#include "RigVMCore/RigVM.h"
#include "RigVMCore/RigVMStruct.h"
#include "RigVMCore/RigVMUserWorkflow.h"
#include "RigVMCore/RigVMGraphFunctionDefinition.h"
#include "UObject/StructOnScope.h"
#include "RigVMNode.generated.h"

class URigVMGraph;

/**
 * The Node represents a single statement within a Graph. 
 * Nodes can represent values such as Variables / Parameters,
 * they can represent Function Invocations or Control Flow
 * logic statements (such as If conditions of For loops).
 * Additionally Nodes are used to represent Comment statements.
 * Nodes contain Pins to represent parameters for Function
 * Invocations or Value access on Variables / Parameters.
 */
UCLASS(BlueprintType)
class RIGVMDEVELOPER_API URigVMNode : public UObject
{
	GENERATED_BODY()

public:

	// Default constructor
	URigVMNode();

	// Default destructor
	virtual ~URigVMNode();

	void Serialize(FArchive& Ar) override;

	// Returns the a . separated string containing all of the
	// names used to reach this Node within the Graph.
	// (for now this is the same as the Node's name)
	UFUNCTION(BlueprintCallable, Category = RigVMNode)
	FString GetNodePath(bool bRecursive = false) const;

	// Splits a NodePath at the start, so for example "CollapseNodeA|CollapseNodeB|CollapseNodeC" becomes "CollapseNodeA" and "CollapseNodeB|CollapseNodeC"
	static bool SplitNodePathAtStart(const FString& InNodePath, FString& LeftMost, FString& Right);

	// Splits a NodePath at the end, so for example "CollapseNodeA|CollapseNodeB|CollapseNodeC" becomes "CollapseNodeA|CollapseNodeB" and "CollapseNodeC"
	static bool SplitNodePathAtEnd(const FString& InNodePath, FString& Left, FString& RightMost);

	// Splits a NodePath into all segments, so for example "Node.Color.R" becomes ["Node", "Color", "R"]
	static bool SplitNodePath(const FString& InNodePath, TArray<FString>& Parts);

	// Joins a NodePath from to segments, so for example "CollapseNodeA" and "CollapseNodeB|CollapseNodeC" becomes "CollapseNodeA|CollapseNodeB|CollapseNodeC"
	static FString JoinNodePath(const FString& Left, const FString& Right);

	// Joins a NodePath from to segments, so for example ["CollapseNodeA", "CollapseNodeB", "CollapseNodeC"] becomes "CollapseNodeA|CollapseNodeB|CollapseNodeC"
	static FString JoinNodePath(const TArray<FString>& InParts);

	// Returns the current index of the Node within the Graph.
	UFUNCTION(BlueprintCallable, Category = RigVMNode)
	int32 GetNodeIndex() const;

	// Returns all of the top-level Pins of this Node.
	UFUNCTION(BlueprintCallable, Category = RigVMNode)
	virtual const TArray<URigVMPin*>& GetPins() const;

	// Returns all of the Pins of this Node (including SubPins).
	UFUNCTION(BlueprintCallable, Category = RigVMNode)
	TArray<URigVMPin*> GetAllPinsRecursively() const;

	// Returns all user defined categories on this node
	UFUNCTION(BlueprintCallable, Category = RigVMNode)
	virtual TArray<FString> GetPinCategories() const;

	// Returns the name of pin category
	UFUNCTION(BlueprintCallable, Category = RigVMNode)
	FString GetPinCategoryName(const FString InCategory) const;

	// Returns all sub user defined categories of a given parent category
	UFUNCTION(BlueprintCallable, Category = RigVMNode)
	TArray<FString> GetSubPinCategories(const FString InCategory, bool bOnlyExisting = false, bool bRecursive = false) const;

	// Returns the parent pin category of the given category (or an empty string in case there's no parent)
	UFUNCTION(BlueprintCallable, Category = RigVMNode)
	FString GetParentPinCategory(const FString InCategory, bool bOnlyExisting = false) const;

	// Returns all parent categories of a given 
	UFUNCTION(BlueprintCallable, Category = RigVMNode)
	TArray<FString> GetParentPinCategories(const FString InCategory, bool bOnlyExisting = false, bool bIncludeSelf = false) const;

	// Returns the depth of the category (starting with 0 for "Foo" and 2 for "Foo|Bar|Waldo")
	static int32 GetPinCategoryDepth(const FString& InCategory);

	// Returns all pins for a given category
	UFUNCTION(BlueprintCallable, Category = RigVMNode)
	TArray<URigVMPin*> GetPinsForCategory(FString InCategory) const;

	// Returns all pins for a given category
	UFUNCTION(BlueprintCallable, Category = RigVMNode)
	bool IsPinCategoryExpanded(FString InCategory) const;

	const FString& GetLastAffectedPinCategory() const { return LastAffectedPinCategory; }

	// Returns the pin UI layout for this node
	UFUNCTION(BlueprintCallable, Category = RigVMNode)
	virtual FRigVMNodeLayout GetNodeLayout(bool bIncludeEmptyCategories = false) const;

	// Returns the default value for a given pin
	FString GetOriginalPinDefaultValue(const URigVMPin* InPin) const;

	// Returns a Pin given it's partial pin path below
	// this node (for example: "Color.R")
	UFUNCTION(BlueprintCallable, Category = RigVMNode)
	URigVMPin* FindPin(const FString& InPinPath) const;

	// Returns a root pin given its name
	UFUNCTION(BlueprintCallable, Category = RigVMNode)
	URigVMPin* FindRootPinByName(const FName& InPinName) const;

	// Returns all of the top-level orphaned Pins of this Node.
	UFUNCTION(BlueprintCallable, Category = RigVMNode)
    virtual const TArray<URigVMPin*>& GetOrphanedPins() const;

	// Returns true if the node has orphaned pins - which leads to a compiler error
	UFUNCTION(BlueprintPure, Category = RigVMNode)
    bool HasOrphanedPins() const { return GetOrphanedPins().Num() > 0; }

	// Returns the Graph of this Node
	UFUNCTION(BlueprintCallable, Category = RigVMNode)
	URigVMGraph* GetGraph() const;

	// Returns the top level / root Graph of this Node
	UFUNCTION(BlueprintCallable, Category = RigVMNode)
    URigVMGraph* GetRootGraph() const;

	// Returns the graph nesting depth of this node
	UFUNCTION(BlueprintCallable, Category = RigVMNode)
	int32 GetGraphDepth() const;

	// Returns the injection info of this Node (or nullptr)
	UFUNCTION(BlueprintCallable, Category = RigVMNode)
	URigVMInjectionInfo* GetInjectionInfo() const;

	// Returns the title of this Node - used for UI.
	UFUNCTION(BlueprintCallable, Category = RigVMNode)
	virtual FString GetNodeTitle() const;

	// Returns the 2d position of this node - used for UI.
	UFUNCTION(BlueprintCallable, Category = RigVMNode)
	FVector2D GetPosition() const;

	// Returns the 2d size of this node - used for UI.
	UFUNCTION(BlueprintCallable, Category = RigVMNode)
	FVector2D GetSize() const;

	// Returns the color of this node - used for UI.
	UFUNCTION(BlueprintCallable, Category = RigVMNode)
	virtual FLinearColor GetNodeColor() const;

	// Returns the tooltip of this node
	UFUNCTION(BlueprintCallable, Category = RigVMNode)
	virtual FText GetToolTipText() const;

	// Returns true if this Node is currently selected.
	UFUNCTION(BlueprintCallable, Category = RigVMNode)
	bool IsSelected() const;

	// Returns true if this is an injected node.
	// Injected nodes are managed by pins are are not visible to the user.
	UFUNCTION(BlueprintCallable, Category = RigVMNode)
	bool IsInjected() const;

	// Returns true if this should be visible in the UI
	UFUNCTION(BlueprintCallable, Category = RigVMNode)
	bool IsVisibleInUI() const;

	// Returns true if this Node has no side-effects
	// and no internal state.
	UFUNCTION(BlueprintCallable, Category = RigVMNode)
	virtual bool IsPure() const;

	// Returns true if the node is defined as non-varying
	UFUNCTION(BlueprintCallable, Category = RigVMNode)
	virtual bool IsDefinedAsConstant() const { return false; }

	// Returns true if the node is defined as non-varying
	UFUNCTION(BlueprintCallable, Category = RigVMNode)
	virtual bool IsDefinedAsVarying() const { return false; }

	// Returns true if this Node has side effects or
	// internal state.
	UFUNCTION(BlueprintCallable, Category = RigVMNode)
	virtual bool IsMutable() const;

	// Returns true if this node has an unknown type pin
	bool HasWildCardPin() const;

	virtual bool ContributesToResult() const { return IsMutable(); }

	// Returns true if this Node is the beginning of a scope
	UFUNCTION(BlueprintCallable, Category = RigVMNode)
	virtual bool IsEvent() const;

	// Returns the name of the event
	UFUNCTION(BlueprintCallable, Category = RigVMNode)
	virtual FName GetEventName() const;

	// Returns true if this node can only exist once in a graph
	UFUNCTION(BlueprintCallable, Category = RigVMNode)
	virtual bool CanOnlyExistOnce() const;

	// Returns true if the node has any input pins
	UFUNCTION(BlueprintCallable, Category = RigVMNode)
	virtual bool HasInputPin(bool bIncludeIO = true) const;

	// Returns true if the node has any io pins
	UFUNCTION(BlueprintCallable, Category = RigVMNode)
	virtual bool HasIOPin() const;

	// Returns true if the node has any lazily evaluating pins
	UFUNCTION(BlueprintCallable, Category = RigVMNode)
	virtual bool HasLazyPin(bool bOnlyConsiderPinsWithLinks = false) const;

	// Returns true if the node has any output pins
	UFUNCTION(BlueprintCallable, Category = RigVMNode)
	virtual bool HasOutputPin(bool bIncludeIO = true) const;

	// Returns true if the node has any pins of the provided direction
	UFUNCTION(BlueprintCallable, Category = RigVMNode)
	virtual bool HasPinOfDirection(ERigVMPinDirection InDirection) const;

	// Returns true if this Node is linked to another 
	// given node through any of the Nodes' Pins.
	UFUNCTION(BlueprintCallable, Category = RigVMNode)
	bool IsLinkedTo(URigVMNode* InNode) const;

	// Returns all links to any pin on this node
	UFUNCTION(BlueprintCallable, Category = RigVMNode)
	TArray<URigVMLink*> GetLinks() const;

	// Returns a list of Nodes connected as sources to
	// this Node as the target.
	UFUNCTION(BlueprintCallable, Category = RigVMNode)
	TArray<URigVMNode*> GetLinkedSourceNodes() const;

	// Returns a list of Nodes connected as targets to
	// this Node as the source.
	UFUNCTION(BlueprintCallable, Category = RigVMNode)
	TArray<URigVMNode*> GetLinkedTargetNodes() const;

	// Returns the name of the node prior to the renaming
	UFUNCTION(BlueprintCallable, Category = RigVMNode)
	FName GetPreviousFName() const { return PreviousName; }

	UE_DEPRECATED(5.3, "Please, use GetInstructionsForVM with Context param")
	const TArray<int32>& GetInstructionsForVM(URigVM* InVM, const FRigVMASTProxy& InProxy = FRigVMASTProxy()) const	{ static TArray<int32> Dummy; return Dummy;	}

	// Returns the indices of associated instructions for this node
	const TArray<int32>& GetInstructionsForVM(const FRigVMExtendedExecuteContext& Context, URigVM* InVM, const FRigVMASTProxy& InProxy = FRigVMASTProxy()) const;

	UE_DEPRECATED(5.3, "Please, use GetInstructionVisitedCount with Context param")
	virtual int32 GetInstructionVisitedCount(URigVM* InVM, const FRigVMASTProxy& InProxy = FRigVMASTProxy()) const { return 0; }

	// Returns the number of visited / run instructions for this node
	virtual int32 GetInstructionVisitedCount(const FRigVMExtendedExecuteContext& Context, URigVM* InVM, const FRigVMASTProxy& InProxy = FRigVMASTProxy()) const;

	UE_DEPRECATED(5.3, "Please, use GetInstructionMicroSeconds with Context param")
	double GetInstructionMicroSeconds(URigVM* InVM, const FRigVMASTProxy& InProxy = FRigVMASTProxy()) const { return 0.0; }

	// Returns the accumulated duration of all of instructions for this node 
	double GetInstructionMicroSeconds(const FRigVMExtendedExecuteContext& Context, URigVM* InVM, const FRigVMASTProxy& InProxy = FRigVMASTProxy()) const;

	// return true if this node is a loop node
	UFUNCTION(BlueprintPure, Category = RigVMNode)
	bool IsLoopNode() const;

	// return true if this node is a control flow node
	UFUNCTION(BlueprintPure, Category = RigVMNode)
	bool IsControlFlowNode() const;

	// returns the names of the control flow blocks of this node
	virtual const TArray<FName>& GetControlFlowBlocks() const;

	// returns true if a control flow block requires slicing
	virtual const bool IsControlFlowBlockSliced(const FName& InBlockName) const;

	// returns true if the node is within a loop
	virtual bool IsWithinLoop() const;

	// returns true if the node can be upgraded
	UFUNCTION(BlueprintPure, Category = RigVMNode)
	virtual bool CanBeUpgraded() const { return GetUpgradeInfo().IsValid(); }

	// returns all supported workflows of the node
	UFUNCTION(BlueprintCallable, Category = RigVMNode)
	virtual TArray<FRigVMUserWorkflow> GetSupportedWorkflows(ERigVMUserWorkflowType InType, const UObject* InSubject) const;

	UFUNCTION(BlueprintCallable, Category = RigVMNode)
	bool HasBreakpoint() const { return bHasBreakpoint; }

	UFUNCTION(BlueprintCallable, Category = RigVMNode)
	void SetHasBreakpoint(const bool bValue) { bHasBreakpoint = bValue; }

	UFUNCTION(BlueprintCallable, Category = RigVMNode)
	bool ExecutionIsHaltedAtThisNode() const { return bHaltedAtThisNode; }

	UFUNCTION(BlueprintCallable, Category = RigVMNode)
	void SetExecutionIsHaltedAtThisNode(const bool bValue) { bHaltedAtThisNode = bValue; }

	UFUNCTION(BlueprintPure, Category = RigVMNode)
	virtual bool IsAggregate() const;

	UFUNCTION(BlueprintPure, Category = RigVMNode)
	virtual URigVMPin* GetFirstAggregatePin() const;

	UFUNCTION(BlueprintPure, Category = RigVMNode)
	virtual URigVMPin* GetSecondAggregatePin() const;

	UFUNCTION(BlueprintPure, Category = RigVMNode)
	virtual URigVMPin* GetOppositeAggregatePin() const;

	UFUNCTION(BlueprintPure, Category = RigVMNode)
	virtual bool IsInputAggregate() const;

	UFUNCTION(BlueprintPure, Category = RigVMNode)
	virtual TArray<URigVMPin*> GetAggregateInputs() const { return {}; }

	UFUNCTION(BlueprintPure, Category = RigVMNode)
	virtual TArray<URigVMPin*> GetAggregateOutputs() const { return {}; }

	UFUNCTION(BlueprintPure, Category = RigVMNode)
	virtual FName GetNextAggregateName(const FName& InLastAggregatePinName) const { return NAME_None; }

	UFUNCTION(BlueprintCallable, Category = RigVMNode)
	virtual URigVMLibraryNode* FindFunctionForNode() const;

	virtual FRigVMStructUpgradeInfo GetUpgradeInfo() const { return FRigVMStructUpgradeInfo(); }

	virtual uint32 GetStructureHash() const;

	// allows the node to support non-native pins
	virtual bool HasNonNativePins() const { return !TraitRootPinNames.IsEmpty(); }

	UFUNCTION(BlueprintPure, Category = RigVMNode)
	TArray<URigVMPin*> GetTraitPins() const;

	const TArray<FString>& GetTraitNames() const { return TraitRootPinNames; }

	UFUNCTION(BlueprintPure, Category = RigVMNode)
	bool IsTraitPin(FName InName) const;

	bool IsTraitPin(const URigVMPin* InTraitPin) const;

	URigVMPin* FindTrait(const FName& InName, const FString& InSubPinPath = FString()) const;

	URigVMPin* FindTrait(const URigVMPin* InTraitPin) const;

	TSharedPtr<FStructOnScope> GetTraitInstance(const FName& InName, bool bUseDefaultValueFromPin = true) const;

	TSharedPtr<FStructOnScope> GetTraitInstance(const URigVMPin* InTraitPin, bool bUseDefaultValueFromPin = true) const;

	UScriptStruct* GetTraitScriptStruct(const FName& InName) const;

	UScriptStruct* GetTraitScriptStruct(const URigVMPin* InTraitPin) const;
	
	virtual bool IsOutDated() const
	{
		return false;
	}
	
	virtual FString GetDeprecatedMetadata() const
	{
		return FString();
	}

	// returns the display name for a pin
	virtual FName GetDisplayNameForPin(const FString& InPinPath) const;

	// returns the display name for a struct member
	static FName GetDisplayNameForStructMember(const UStruct* InStruct, const FString& InPath);

	// returns the display name within a property
	static FName GetDisplayNameForProperty(const FProperty* InProperty, const FString& InRemainingPath);

	// returns the category for a pin
	virtual FString GetCategoryForPin(const FString& InPinPath) const;

	// returns the index of a pin within a category
	virtual int32 GetIndexInCategoryForPin(const FString& InPinPath) const;

private:

	static const inline TCHAR* NodeColorName = TEXT("NodeColor");

	bool IsLinkedToRecursive(URigVMPin* InPin, URigVMNode* InNode) const;
	void GetLinkedNodesRecursive(URigVMPin* InPin, bool bLookForSources, TArray<URigVMNode*>& OutNodes) const;

protected:

	virtual void InvalidateCache();
	virtual TArray<int32> GetInstructionsForVMImpl(const FRigVMExtendedExecuteContext& Context, URigVM* InVM, const FRigVMASTProxy& InProxy = FRigVMASTProxy()) const; 
	virtual FText GetToolTipTextForPin(const URigVMPin* InPin) const;
	virtual bool AllowsLinksOn(const URigVMPin* InPin) const { return true; }
	virtual bool ShouldInputPinComputeLazily(const URigVMPin* InPin) const { return false; }
	virtual FString GetOriginalDefaultValueForRootPin(const URigVMPin* InRootPin) const;

	void UpdateTraitRootPinNames();

	UPROPERTY()
	FString NodeTitle;

	UPROPERTY()
	FVector2D Position;

	UPROPERTY()
	FVector2D Size;

	UPROPERTY()
	FLinearColor NodeColor;

	UPROPERTY(transient)
	FName PreviousName;

	UPROPERTY(transient)
	bool bHasBreakpoint;

	UPROPERTY(transient)
	bool bHaltedAtThisNode;

	UPROPERTY()
	TArray<FString> TraitRootPinNames;

	// root trait pin store their default value in a separate property bag so that
	// things like soft object ptr can be used and tracked in a uproperty 
	UPROPERTY()
	TMap<FString, FRigVMTraitDefaultValueStruct> TraitDefaultValues;
	
private:

	UPROPERTY()
	TArray<TObjectPtr<URigVMPin>> Pins;

	UPROPERTY()
	TArray<TObjectPtr<URigVMPin>> OrphanedPins;

protected:
	
	UPROPERTY()
	TArray<FString> PinCategories;

	UPROPERTY()
	TMap<FString, bool> PinCategoryExpansion;

	FString LastAffectedPinCategory;
	
private:

#if WITH_EDITOR
	struct FProfilingCache
	{
		mutable int32 VisitedCount;
		mutable double MicroSeconds;
		mutable TArray<int32> Instructions;
	};
	const FProfilingCache* UpdateProfilingCacheIfNeeded(const FRigVMExtendedExecuteContext& Context, URigVM* InVM, const FRigVMASTProxy& InProxy) const;
	mutable uint32 ProfilingHash;
	mutable TMap<uint32, TSharedPtr<FProfilingCache>> ProfilingCache;
	static TArray<int32> EmptyInstructionArray;
#endif

	mutable TMap<FString, FString> CachedOriginalPinDefaultValues;
	
	friend class URigVMController;
	friend class URigVMGraph;
	friend class URigVMPin;
	friend class URigVMCompiler;
	friend class FRigVMLexer;
	friend class URigVMSchema;
};
