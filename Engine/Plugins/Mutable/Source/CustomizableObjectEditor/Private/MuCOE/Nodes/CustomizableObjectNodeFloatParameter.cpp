// Copyright Epic Games, Inc. All Rights Reserved.

#include "MuCOE/Nodes/CustomizableObjectNodeFloatParameter.h"

#include "MuCO/CustomizableObjectCustomVersion.h"
#include "MuCOE/EdGraphSchema_CustomizableObject.h"
#include "MuCOE/UnrealEditorPortabilityHelpers.h"

class UCustomizableObjectNodeRemapPins;

#define LOCTEXT_NAMESPACE "CustomizableObjectEditor"


void UCustomizableObjectNodeFloatParameter::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	FProperty* PropertyThatChanged = PropertyChangedEvent.Property;
	if ( PropertyThatChanged && (PropertyThatChanged->GetName() == TEXT("DescriptionImage") || PropertyThatChanged->GetName() == TEXT("Name")) )
	{
		ReconstructNode();
	}

	Super::PostEditChangeProperty(PropertyChangedEvent);
}


void UCustomizableObjectNodeFloatParameter::AllocateDefaultPins(UCustomizableObjectNodeRemapPins* RemapPins)
{
	const UEdGraphSchema_CustomizableObject* Schema = GetDefault<UEdGraphSchema_CustomizableObject>();

	UEdGraphPin* ValuePin = CustomCreatePin(EGPD_Output, Schema->PC_Float, FName("Value"));
	ValuePin->bDefaultValueIsIgnored = true;
}


bool UCustomizableObjectNodeFloatParameter::IsAffectedByLOD() const
{
	return false;
}


FText UCustomizableObjectNodeFloatParameter::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	if (TitleType == ENodeTitleType::ListView || ParameterName.IsEmpty())
	{
		return LOCTEXT("Float_Parameter", "Float Parameter");
	}
	else if (TitleType == ENodeTitleType::EditableTitle)
	{
		return FText::Format(LOCTEXT("Float_Parameter_EditableTitle", "{0}"), FText::FromString(ParameterName));
	}
	else
	{
		return FText::Format(LOCTEXT("Float_Parameter_Title", "{0}\nFloat Parameter"), FText::FromString(ParameterName));
	}
}


FLinearColor UCustomizableObjectNodeFloatParameter::GetNodeTitleColor() const
{
	const UEdGraphSchema_CustomizableObject* Schema = GetDefault<UEdGraphSchema_CustomizableObject>();
	return Schema->GetPinTypeColor(Schema->PC_Float);
}


FText UCustomizableObjectNodeFloatParameter::GetTooltipText() const
{
	return LOCTEXT("Float_Parameter_Tooltip", "Expose a numeric parameter from the Customizable Object that can be modified at runtime.");
}


void UCustomizableObjectNodeFloatParameter::OnRenameNode(const FString& NewName)
{
	if (!NewName.IsEmpty())
	{
		ParameterName = NewName;
	}
}


void UCustomizableObjectNodeFloatParameter::BackwardsCompatibleFixup(int32 CustomizableObjectCustomVersion)
{
	Super::BackwardsCompatibleFixup(CustomizableObjectCustomVersion);

	if (CustomizableObjectCustomVersion == FCustomizableObjectCustomVersion::RemovedParameterDecorations)
	{
		ReconstructNode();
	}
}


#undef LOCTEXT_NAMESPACE
