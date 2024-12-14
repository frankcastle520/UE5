// Copyright Epic Games, Inc. All Rights Reserved.

#include "MuCOE/Nodes/SCustomizableObjectNodeMaterialPinImage.h"

#include "MuCOE/EdGraphSchema_CustomizableObject.h"
#include "MuCOE/GraphTraversal.h"
#include "MuCOE/Nodes/CustomizableObjectNodeMaterial.h"
#include "MuCOE/UnrealEditorPortabilityHelpers.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/SToolTip.h"

class IToolTip;
class SWidget;
struct FSlateBrush;

#define LOCTEXT_NAMESPACE "CustomizableObjectEditor"


void SCustomizableObjectNodeMaterialPinImage::Construct(const FArguments& InArgs, UEdGraphPin* InGraphPinObj)
{
	SCustomizableObjectNodePin::Construct(SCustomizableObjectNodePin::FArguments(), InGraphPinObj);

	// Override previously defined tool tip.
	TSharedPtr<IToolTip> TooltipWidget = SNew(SToolTip)
		.Text(this, &SCustomizableObjectNodeMaterialPinImage::GetPinTooltipText);

	SetToolTip(TooltipWidget);
}


TSharedRef<SWidget>	SCustomizableObjectNodeMaterialPinImage::GetDefaultValueWidget()
{
	LabelAndValue->SetWrapSize(TNumericLimits<float>::Max()); // Remove warping.

	return SNew(SEditableTextBox)
		.Style(FAppStyle::Get(), "Graph.EditableTextBox")
		.Text(this, &SCustomizableObjectNodeMaterialPinImage::GetDefaultValueText)
		.SelectAllTextWhenFocused(false)
		.Visibility(this, &SCustomizableObjectNodeMaterialPinImage::GetDefaultValueVisibility)
		.IsReadOnly(true)
		.ForegroundColor(FSlateColor::UseForeground());
}


FText SCustomizableObjectNodeMaterialPinImage::GetPinTooltipText() const
{
	if (GraphPinObj->bOrphanedPin)
	{
		return LOCTEXT("PinModeMutableOrpahan", "Pin not disapearing due to being connected or having a property modified.");
	}
	
	if (GraphPinObj->PinType.PinCategory == UEdGraphSchema_CustomizableObject::PC_Image)
	{
		return LOCTEXT("PinModeMutableTooltip", "Texture Parameter goes through Mutable.");
	}
	else
	{
		return LOCTEXT("PinModePassthroughTooltip", "Texture Parameter is ignored by Mutable.");
	}
}


FText SCustomizableObjectNodeMaterialPinImage::GetDefaultValueText() const
{
	if (GraphPinObj->PinType.PinCategory == UEdGraphSchema_CustomizableObject::PC_Image)
	{
		return LOCTEXT("PinModeMutable", "mutable");
	}
	else 
	{
		return LOCTEXT("PinModePassthrough", "passthrough");
	}
}


EVisibility SCustomizableObjectNodeMaterialPinImage::GetDefaultValueVisibility() const
{
	return EVisibility::Visible;
}

#undef LOCTEXT_NAMESPACE
