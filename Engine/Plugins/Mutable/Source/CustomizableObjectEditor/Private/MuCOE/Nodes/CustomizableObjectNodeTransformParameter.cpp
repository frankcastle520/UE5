// Copyright Epic Games, Inc. All Rights Reserved.

#include "MuCOE/Nodes/CustomizableObjectNodeTransformParameter.h"

#include "MuCOE/EdGraphSchema_CustomizableObject.h"


#define LOCTEXT_NAMESPACE "CustomizableObjectEditor"


FText UCustomizableObjectNodeTransformParameter::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	if (TitleType == ENodeTitleType::ListView || ParameterName.IsEmpty())
	{
		return LOCTEXT("Transform_Parameter", "Transform Parameter");
	}
	else if (TitleType == ENodeTitleType::EditableTitle)
	{
		return FText::Format(LOCTEXT("Transform_Parameter_EditableTitle", "{0}"), FText::FromString(ParameterName));
	}
	else
	{
		return FText::Format(LOCTEXT("Transform_Parameter_Title", "{0}\nTransform Parameter"), FText::FromString(ParameterName));
	}
}

FLinearColor UCustomizableObjectNodeTransformParameter::GetNodeTitleColor() const
{
	const UEdGraphSchema_CustomizableObject* Schema = GetDefault<UEdGraphSchema_CustomizableObject>();
	return Schema->GetPinTypeColor(Schema->PC_Transform);
}

FText UCustomizableObjectNodeTransformParameter::GetTooltipText() const
{
	return LOCTEXT("Transform_Parameter_Tooltip", "Expose a runtime modifiable transform parameter from the Customizable Object.");
}

void UCustomizableObjectNodeTransformParameter::OnRenameNode(const FString& NewName)
{
	if (!NewName.IsEmpty())
	{
		ParameterName = NewName;
	}
}

void UCustomizableObjectNodeTransformParameter::AllocateDefaultPins(UCustomizableObjectNodeRemapPins* RemapPins)
{
	const UEdGraphSchema_CustomizableObject* Schema = GetDefault<UEdGraphSchema_CustomizableObject>();

	UEdGraphPin* ValuePin = CustomCreatePin(EGPD_Output, Schema->PC_Transform, FName("Value"));
	ValuePin->bDefaultValueIsIgnored = true;
}

#undef LOCTEXT_NAMESPACE
