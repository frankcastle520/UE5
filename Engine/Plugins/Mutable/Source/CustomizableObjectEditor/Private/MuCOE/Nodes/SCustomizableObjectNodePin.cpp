// Copyright Epic Games, Inc. All Rights Reserved.

#include "SCustomizableObjectNodePin.h"

#include "MuCOE/EdGraphSchema_CustomizableObject.h"
#include "MuCOE/UnrealEditorPortabilityHelpers.h"


void SCustomizableObjectNodePin::Construct(const FArguments& InArgs, UEdGraphPin* InGraphPinObj)
{
	SGraphPin::Construct(SGraphPin::FArguments(), InGraphPinObj);

	// Cache pin icons.
	PassThroughImageConnected = UE_MUTABLE_GET_BRUSH(TEXT("Graph.ExecPin.Connected"));
	PassThroughImageDisconnected = UE_MUTABLE_GET_BRUSH(TEXT("Graph.ExecPin.Disconnected"));
}


const FSlateBrush* SCustomizableObjectNodePin::GetPinIcon() const
{
	if (GraphPinObj->PinType.PinCategory == UEdGraphSchema_CustomizableObject::PC_PassThroughImage)
	{
		return GraphPinObj->LinkedTo.Num() ?
			PassThroughImageConnected :
			PassThroughImageDisconnected;
	}
	else
	{
		return SGraphPin::GetPinIcon();
	}
}
