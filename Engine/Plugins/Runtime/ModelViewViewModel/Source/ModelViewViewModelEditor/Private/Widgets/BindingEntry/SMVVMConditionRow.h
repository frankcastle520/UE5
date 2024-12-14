// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Input/Reply.h"
#include "Types/MVVMBindingSource.h"
#include "Types/MVVMExecutionMode.h"
#include "Widgets/BindingEntry/SMVVMBaseRow.h"
#include "Widgets/SMVVMFieldSelectorMenu.h"


namespace UE::MVVM::BindingEntry
{

/**
 *
 */
class SConditionRow : public SBaseRow
{
public:
	SLATE_BEGIN_ARGS(SConditionRow) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& Args, const TSharedRef<STableViewBase>& OwnerTableView, const TSharedPtr<FWidgetBlueprintEditor>& InBlueprintEditor, UWidgetBlueprint* InBlueprint, const TSharedPtr<FBindingEntry>& InEntry);

protected:
	virtual TSharedRef<SWidget> BuildRowWidget() override;
	virtual const ANSICHAR* GetTableRowStyle() const override
	{
		return "BindingView.BindingRow";
	}

private:
	UMVVMBlueprintViewCondition* GetCondition() const;

	FSlateColor GetErrorBorderColor() const;

	EVisibility GetErrorButtonVisibility() const;

	FText GetErrorButtonToolTip() const;

	FReply OnErrorButtonClicked();

	TSharedRef<ITableRow> OnGenerateErrorRow(TSharedPtr<FText> Text, const TSharedRef<STableViewBase>& TableView) const;

	ECheckBoxState IsConditionEnabled() const;

	void OnIsConditionEnableChanged(ECheckBoxState NewState);

	ECheckBoxState IsConditionCompiled() const;

	void OnIsConditionCompileChanged(ECheckBoxState NewState);

	FMVVMLinkedPinValue GetFieldSelectedValue(bool bSource) const;

	void HandleFieldSelectionChanged(FMVVMLinkedPinValue Value, SFieldSelectorMenu::ESelectionType SelectionType, bool bSource);

	FFieldSelectionContext GetSelectedSelectionContext(bool bSource) const;

	FReply HandleFieldSelectorDrop(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent, bool bSource);

	void HandleFieldSelectorDragEnter(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent, bool bSource);

	TSharedRef<SWidget> HandleContextMenu() const;
	void HandleShowBlueprintGraph() const;

	void OnConditionOperationSelectionChanged(FName ValueName, ESelectInfo::Type);
	FText GetCurrentConditionOperationLabel() const;
	TSharedRef<SWidget> GenerateConditionOperationWidget(FName ValueName) const;
	FText GetConditionOperationLabel(EMVVMConditionOperation Operation) const;
	EMVVMConditionOperation GetConditionOperationFromValueName(FName ValueName);

	TOptional<float> GetValue() const;
	TOptional<float> GetMaxValue() const;
	void OnValueChanged(float InValue);
	void OnMaxValueChanged(float InValue);
	EVisibility GetMaxValueVisibilityFromOperation() const;
	FText GetValueTooltipFromOperation() const;

private:
	TArray<TSharedPtr<FText>> ErrorItems;
};

} // namespace UE::MVVM
