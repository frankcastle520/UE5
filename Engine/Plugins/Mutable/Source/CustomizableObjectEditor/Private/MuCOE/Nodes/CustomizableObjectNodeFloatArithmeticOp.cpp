// Copyright Epic Games, Inc. All Rights Reserved.

#include "MuCOE/Nodes/CustomizableObjectNodeFloatArithmeticOp.h"

#include "MuCOE/EdGraphSchema_CustomizableObject.h"

class UCustomizableObjectNodeRemapPins;
struct FPropertyChangedEvent;

#define LOCTEXT_NAMESPACE "CustomizableObjectEditor"


UCustomizableObjectNodeFloatArithmeticOp::UCustomizableObjectNodeFloatArithmeticOp()
	: Super()
{
	Operation = EFloatArithmeticOperation::E_Add;
}


void UCustomizableObjectNodeFloatArithmeticOp::AllocateDefaultPins(UCustomizableObjectNodeRemapPins* RemapPins)
{
	const UEdGraphSchema_CustomizableObject* Schema = GetDefault<UEdGraphSchema_CustomizableObject>();

	UEdGraphPin* XPin = CustomCreatePin(EGPD_Input, Schema->PC_Float, FName("A"));
	XPin->bDefaultValueIsIgnored = true;
	XPin->PinFriendlyName = FText();

	FName PinName = TEXT("B");
	UEdGraphPin* YPin = CustomCreatePin(EGPD_Input, Schema->PC_Float, PinName);
	YPin->bDefaultValueIsIgnored = true;
	YPin->PinFriendlyName = FText();

	PinName = TEXT("Result");
	UEdGraphPin* ResultPin = CustomCreatePin(EGPD_Output, Schema->PC_Float, PinName);
	ResultPin->bDefaultValueIsIgnored = true;
	ResultPin->PinFriendlyName = FText();
}


FText UCustomizableObjectNodeFloatArithmeticOp::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	if (TitleType == ENodeTitleType::ListView)
	{
		return LOCTEXT("Float_Arithmetic_Operation", "Float Arithmetic Operation");
	}

	const UEnum* EnumPtr = FindObject<UEnum>(nullptr, TEXT("/Script/CustomizableObjectEditor.EFloatArithmeticOperation"), true);

	if (!EnumPtr)
	{
		return FText::FromString(FString("Float Operation"));
	}

	const int32 index = EnumPtr->GetIndexByValue((int32)Operation);
	return EnumPtr->GetDisplayNameTextByIndex(index);
}


FLinearColor UCustomizableObjectNodeFloatArithmeticOp::GetNodeTitleColor() const
{
	const UEdGraphSchema_CustomizableObject* Schema = GetDefault<UEdGraphSchema_CustomizableObject>();
	return Schema->GetPinTypeColor(Schema->PC_Float);
}


FText UCustomizableObjectNodeFloatArithmeticOp::GetTooltipText() const
{
	return LOCTEXT("Float_Arithmetic_Tooltip", "Perform an arithmetic operation between two floats.");
}


#undef LOCTEXT_NAMESPACE

