// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IPropertyTypeCustomization.h"
#include "Widgets/Views/STreeView.h"
#include "Textures/SlateIcon.h"

struct FObjectKey;
struct FOptionalSize;

class FDetailWidgetRow;
class IDetailChildrenBuilder;
class IPropertyHandle;
class IPropertyUtilities;
class SComboButton;
class SWidget;
class SSearchBox;
class SWidgetSwitcher;
class SInlineEditableTextBlock;
class SBorder;
class SMenuAnchor;
class UStateTree;
class UStateTreeState;
class UStateTreeEditorData;
struct EVisibility;
struct FStateTreeEditorNode;
struct FStateTreePropertyPath;
enum class EStateTreeExpressionOperand : uint8;

/**
 * Type customization for nodes (Conditions, Evaluators and Tasks) in StateTreeState.
 */
class FStateTreeEditorNodeDetails : public IPropertyTypeCustomization
{
public:
	/** Makes a new instance of this detail layout class for a specific detail view requesting it */
	static TSharedRef<IPropertyTypeCustomization> MakeInstance();

	virtual ~FStateTreeEditorNodeDetails();
	
	/** IPropertyTypeCustomization interface */
	virtual void CustomizeHeader(TSharedRef<IPropertyHandle> StructPropertyHandle, FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& StructCustomizationUtils) override;
	virtual void CustomizeChildren(TSharedRef<IPropertyHandle> StructPropertyHandle, IDetailChildrenBuilder& StructBuilder, IPropertyTypeCustomizationUtils& StructCustomizationUtils) override;

private:

	bool ShouldResetToDefault(TSharedPtr<IPropertyHandle> PropertyHandle) const;
	void ResetToDefault(TSharedPtr<IPropertyHandle> PropertyHandle);
	void OnCopyNode();
	void OnPasteNode();
	TSharedPtr<IPropertyHandle> GetInstancedObjectValueHandle(TSharedPtr<IPropertyHandle> PropertyHandle);

	FOptionalSize GetIndentSize() const;
	FReply HandleIndentPlus();
	FReply HandleIndentMinus();
	
	int32 GetIndent() const;
	void SetIndent(const int32 Indent) const;
	bool IsIndent(const int32 Indent) const;
	
	FText GetOperandText() const;
	FSlateColor GetOperandColor() const;
	TSharedRef<SWidget> OnGetOperandContent() const;
	bool IsOperandEnabled() const;

	void SetOperand(const EStateTreeExpressionOperand Operand) const;
	bool IsOperand(const EStateTreeExpressionOperand Operand) const;

	bool IsFirstItem() const;
	int32 GetCurrIndent() const;
	int32 GetNextIndent() const;
	
	FText GetOpenParens() const;
	FText GetCloseParens() const;

	EVisibility IsConditionVisible() const;
	EVisibility IsConsiderationVisible() const;
	EVisibility IsOperandVisible() const;
	EVisibility AreIndentButtonsVisible() const;
	EVisibility AreParensVisible() const;
	
	EVisibility IsIconVisible() const;
	const FSlateBrush* GetIcon() const;
	FSlateColor GetIconColor() const;

	FReply OnDescriptionClicked(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) const;
	FText GetNodeDescription() const;
	EVisibility IsNodeDescriptionVisible() const;
	
	FText GetNodeTooltip() const;

	FReply OnRowMouseDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent);
	FReply OnRowMouseUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent);

	FText GetName() const;
	bool HandleVerifyNameChanged(const FText& InText, FText& OutErrorMessage) const;
	void HandleNameCommitted(const FText& NewLabel, ETextCommit::Type CommitType) const;

	FText GetNodePickerTooltip() const;
	void OnNodePicked(const UStruct* InStruct) const;

	void OnIdentifierChanged(const UStateTree& StateTree);
	void OnBindingChanged(const FStateTreePropertyPath& SourcePath, const FStateTreePropertyPath& TargetPath);
	void FindOuterObjects();

	FReply OnBrowseToNodeBlueprint() const;
	FReply OnEditNodeBlueprint() const;
	EVisibility IsBrowseToNodeBlueprintVisible() const;
	EVisibility IsEditNodeBlueprintVisible() const;

	TSharedRef<SWidget> GenerateOptionsMenu();
	void GeneratePickerMenu(class FMenuBuilder& InMenuBuilder);
	void OnDeleteNode() const;
	void OnDeleteAllNodes() const;
	void OnDuplicateNode() const;
	void OnRenameNode() const;

	UScriptStruct* BaseScriptStruct = nullptr;
	UClass* BaseClass = nullptr;
	TSharedPtr<SWidgetSwitcher> NameSwitcher;
	TSharedPtr<SInlineEditableTextBlock> NameEdit;
	TSharedPtr<SBorder> RowBorder; 

	UStateTreeEditorData* EditorData = nullptr;
	UStateTree* StateTree = nullptr;
	
	TSharedPtr<IPropertyUtilities> PropUtils;
	TSharedPtr<IPropertyHandle> StructProperty;
	TSharedPtr<IPropertyHandle> NodeProperty;
	TSharedPtr<IPropertyHandle> InstanceProperty;
	TSharedPtr<IPropertyHandle> InstanceObjectProperty;
	TSharedPtr<IPropertyHandle> IDProperty;

	TSharedPtr<IPropertyHandle> IndentProperty;
	TSharedPtr<IPropertyHandle> OperandProperty;

	FDelegateHandle OnBindingChangedHandle;
};
