// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MVVMBlueprintPin.h"
#include "MVVMPropertyPath.h"
#include "Types/MVVMLinkedPinValue.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/SMVVMFieldSelectorMenu.h"

class SGraphPin;
class UK2Node_CallFunction;
class UWidgetBlueprint;

class UMVVMBlueprintViewCondition;

enum class ECheckBoxState : uint8;

namespace UE::MVVM
{

class SFieldSelector;

class SConditionParameter : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SConditionParameter) {}
		SLATE_ARGUMENT_DEFAULT(UMVVMBlueprintViewCondition*, Condition) =  nullptr;
		SLATE_ARGUMENT(FMVVMBlueprintPinId, ParameterId)
		SLATE_ARGUMENT_DEFAULT(bool, AllowDefault) = true;
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, UWidgetBlueprint* WidgetBlueprint);

private:
	ECheckBoxState OnGetIsBindArgumentChecked() const;
	void OnBindArgumentChecked(ECheckBoxState Checked);

	FMVVMLinkedPinValue OnGetSelectedField() const;
	void SetSelectedField(const FMVVMLinkedPinValue& Path);

	void HandleFieldSelectionChanged(FMVVMLinkedPinValue Value, SFieldSelectorMenu::ESelectionType SelectionType);
	FFieldSelectionContext GetSelectedSelectionContext() const;

	int32 GetCurrentWidgetIndex() const;

private:
	TWeakObjectPtr<UWidgetBlueprint> WidgetBlueprint;
	TWeakObjectPtr<UMVVMBlueprintViewCondition> ViewCondition;
	FMVVMBlueprintPinId ParameterId;
	/** This reference is just to keep the default value widget alive. */
	TSharedPtr<SGraphPin> GraphPin;

	FMVVMLinkedPinValue PreviousSelectedField;

	bool bAllowDefault = true;
	bool bDefaultValueVisible = true;
};

}
