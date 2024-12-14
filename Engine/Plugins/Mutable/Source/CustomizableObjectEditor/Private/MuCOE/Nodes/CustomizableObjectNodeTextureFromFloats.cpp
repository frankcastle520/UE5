// Copyright Epic Games, Inc. All Rights Reserved.

#include "MuCOE/Nodes/CustomizableObjectNodeTextureFromFloats.h"

#include "MuCO/CustomizableObjectCustomVersion.h"
#include "MuCOE/EdGraphSchema_CustomizableObject.h"

class UCustomizableObjectNodeRemapPins;

#define LOCTEXT_NAMESPACE "CustomizableObjectEditor"


void UCustomizableObjectNodeTextureFromFloats::AllocateDefaultPins(UCustomizableObjectNodeRemapPins* RemapPins)
{
	const UEdGraphSchema_CustomizableObject* Schema = GetDefault<UEdGraphSchema_CustomizableObject>();

	FString PinName = TEXT("Texture");
	UEdGraphPin* OutputPin = CustomCreatePin(EGPD_Output, Schema->PC_Image, FName(*PinName));
	OutputPin->bDefaultValueIsIgnored = true;

	PinName = TEXT("R");
	UEdGraphPin* RPin = CustomCreatePin(EGPD_Input, Schema->PC_Float, FName(*PinName));
	RPin->bDefaultValueIsIgnored = true;

	PinName = TEXT("G");
	UEdGraphPin* GPin = CustomCreatePin(EGPD_Input, Schema->PC_Float, FName(*PinName));
	GPin->bDefaultValueIsIgnored = true;

	PinName = TEXT("B");
	UEdGraphPin* BPin = CustomCreatePin(EGPD_Input, Schema->PC_Float, FName(*PinName));
	BPin->bDefaultValueIsIgnored = true;

	PinName = TEXT("A");
	UEdGraphPin* APin = CustomCreatePin(EGPD_Input, Schema->PC_Float, FName(*PinName));
	APin->bDefaultValueIsIgnored = true;
}


FText UCustomizableObjectNodeTextureFromFloats::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	return LOCTEXT("Texture_From_Floats", "Texture From Float Channels");
}


FLinearColor UCustomizableObjectNodeTextureFromFloats::GetNodeTitleColor() const
{
	const UEdGraphSchema_CustomizableObject* Schema = GetDefault<UEdGraphSchema_CustomizableObject>();
	return Schema->GetPinTypeColor(Schema->PC_Image);
}


FText UCustomizableObjectNodeTextureFromFloats::GetTooltipText() const
{
	return LOCTEXT("Texture_From_Floats_Tooltip", "Creates a flat color texture from the float channels provided.");
}

#undef LOCTEXT_NAMESPACE
