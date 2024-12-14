// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "StateTreeTypes.h"
#include "StateTreeNodeBase.generated.h"

struct FStateTreeLinker;
struct FStateTreeEditorPropertyPath;
struct FStateTreePropertyPath;
struct IStateTreeBindingLookup;

/**
 * Enum describing in what format a text is expected to be returned.
 *
 * - Normal text should be used for values
 * - Bold text should generally be used for actions, like name a of a task "<b>Play Animation</> {AnimName}".
 * - Subdued should be generally used for secondary/structural information, like "{Left} <s>equals</> {Right}".
 */
UENUM(BlueprintType)
enum EStateTreeNodeFormatting : uint8
{
	/**
	 * The returned text can contain following right text formatting (no nesting)
	 *	- <b>Bold</> (bolder font is used)
	 *	- <s>Subdued</> (normal font with lighter color) */
	RichText,
	
	/** The text should be unformatted */
	Text,
};

/**
 * Base struct of StateTree Conditions, Considerations, Evaluators, and Tasks.
 */
USTRUCT()
struct STATETREEMODULE_API FStateTreeNodeBase
{
	GENERATED_BODY()

PRAGMA_DISABLE_DEPRECATION_WARNINGS
	FStateTreeNodeBase() = default;
	FStateTreeNodeBase(const FStateTreeNodeBase&) = default;
	FStateTreeNodeBase(FStateTreeNodeBase&&) = default;
	FStateTreeNodeBase& operator=(const FStateTreeNodeBase&) = default;
	FStateTreeNodeBase& operator=(FStateTreeNodeBase&&) = default;
PRAGMA_ENABLE_DEPRECATION_WARNINGS

	virtual ~FStateTreeNodeBase() {}

	/** @return Struct that represents the runtime data of the node. */
	virtual const UStruct* GetInstanceDataType() const { return nullptr; };

	/**
	 * Called when the StateTree asset is linked. Allows to resolve references to other StateTree data.
	 * @see TStateTreeExternalDataHandle
	 * @param Linker Reference to the linker
	 * @return true if linking succeeded. 
	 */
	[[nodiscard]] virtual bool Link(FStateTreeLinker& Linker) { return true; }

	/**
	 * Called during State Tree compilation, allows to modify and validate the node and instance data.
	 * The method is called with node and instance that is duplicated during compilation and used at runtime (it's different than the data used in editor).  
	 * @param InstanceDataView Pointer to the instance data.
	 * @param ValidationMessages Any messages to report during validation. Displayed as errors if the validation result is Invalid, else as warnings.
	 * @return Validation result based on if the validation succeeded or not. Returning Invalid will fail compilation and messages will be displayed as errors.
	 */
	virtual EDataValidationResult Compile(FStateTreeDataView InstanceDataView, TArray<FText>& ValidationMessages) { return EDataValidationResult::NotValidated; }
	
#if WITH_EDITOR
	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	UE_DEPRECATED(5.3, "Use version with FStateTreePropertyPath instead.")
	virtual void OnBindingChanged(const FGuid& ID, FStateTreeDataView InstanceData, const FStateTreeEditorPropertyPath& SourcePath, const FStateTreeEditorPropertyPath& TargetPath, const IStateTreeBindingLookup& BindingLookup) final {}
	PRAGMA_ENABLE_DEPRECATION_WARNINGS

	/**
	 * Returns description for the node, use in the UI.
	 * The UI description is selected as follows: 
	 * - Node Name, if not empty
	 * - Description if not empty
	 * - Display name of the node struct
	 * @param ID ID of the item, can be used make property paths to this item.
	 * @param InstanceDataView View to the instance data, can be struct or class.
	 * @param BindingLookup Reference to binding lookup which can be used to reason about property paths.
	 * @param Formatting Requested formatting (whether rich or plain text should be returned).
	 */
	virtual FText GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting = EStateTreeNodeFormatting::Text) const
	{
		return FText::GetEmpty();
	}

	/**
	 * @returns name of the icon in format:
	 *		StyleSetName | StyleName [ | SmallStyleName | StatusOverlayStyleName]
	 *		SmallStyleName and StatusOverlayStyleName are optional.
	 *		Example: "StateTreeEditorStyle|Node.Animation" 
	 */
	virtual FName GetIconName() const
	{
		return FName();
	}

	/** @return the color to be used with the icon. */
	virtual FColor GetIconColor() const
	{
		return UE::StateTree::Colors::DarkGrey;
	}

	/**
	 * Called when binding of any of the properties in the node changes.
	 * @param ID ID of the item, can be used make property paths to this item.
	 * @param InstanceDataView view to the instance data, can be struct or class.
	 * @param SourcePath Source path of the new binding.
	 * @param TargetPath Target path of the new binding (the property in the condition).
	 * @param BindingLookup Reference to binding lookup which can be used to reason about property paths.
	 */
	virtual void OnBindingChanged(const FGuid& ID, FStateTreeDataView InstanceDataView, const FStateTreePropertyPath& SourcePath, const FStateTreePropertyPath& TargetPath, const IStateTreeBindingLookup& BindingLookup) {}

	/**
	 * Called when a property of the node has been modified externally
	 * @param PropertyChangedEvent The event for the changed property. PropertyChain's active properties are set relative to node.
	 * @param InstanceData view to the instance data, can be struct or class.
	 */
	virtual void PostEditNodeChangeChainProperty(const FPropertyChangedChainEvent& PropertyChangedEvent, FStateTreeDataView InstanceDataView) {}

	/**
	 * Called when a property of node's instance data has been modified externally
	 * @param PropertyChangedEvent The event for the changed property. PropertyChain's active properties are set relative to instance data.
	 * @param InstanceData view to the instance data, can be struct or class.
	 */
	virtual void PostEditInstanceDataChangeChainProperty(const FPropertyChangedChainEvent& PropertyChangedEvent, FStateTreeDataView InstanceDataView) {}
#endif

	/**
	* Called after the state tree asset that contains this node is loaded from disk.
	* @param InstanceDataView view to the instance data, can be struct or class.
	*/
	virtual void PostLoad(FStateTreeDataView InstanceDataView) {}

	/** Name of the node. */
	UPROPERTY(EditDefaultsOnly, Category = "", meta=(EditCondition = "false", EditConditionHides))
	FName Name;

	/** Property binding copy batch handle. */
	UPROPERTY()
	FStateTreeIndex16 BindingsBatch = FStateTreeIndex16::Invalid;

	/** Index of template instance data for the node. Can point to Shared or Default instance data in StateTree depending on node type. */
	UPROPERTY()
	FStateTreeIndex16 InstanceTemplateIndex = FStateTreeIndex16::Invalid;

	/** Data handle to access the instance data. */
	UPROPERTY()
	FStateTreeDataHandle InstanceDataHandle = FStateTreeDataHandle::Invalid; 

#if WITH_EDITORONLY_DATA
	UE_DEPRECATED(5.4, "InstanceDataHandle is used instead to reference to the instance data.")
	UPROPERTY()
	FStateTreeIndex16 DataViewIndex_DEPRECATED = FStateTreeIndex16::Invalid;

	UE_DEPRECATED(5.4, "InstanceDataHandle is used instead to reference to the instance data.")
	UPROPERTY()
	FStateTreeIndex16 InstanceIndex_DEPRECATED = FStateTreeIndex16::Invalid;
	
	UE_DEPRECATED(5.4, "InstanceDataHandle is used to determine if the node has object data.")
	UPROPERTY()
	uint8 bInstanceIsObject_DEPRECATED : 1;
#endif
};
