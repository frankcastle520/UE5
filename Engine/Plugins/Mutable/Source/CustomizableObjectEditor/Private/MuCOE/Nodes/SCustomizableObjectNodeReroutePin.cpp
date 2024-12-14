// Copyright Epic Games, Inc. All Rights Reserved.

#include "SCustomizableObjectNodeReroutePin.h"

#include "MuCOE/EdGraphSchema_CustomizableObject.h"
#include "MuCOE/UnrealEditorPortabilityHelpers.h"


void SCustomizableObjectNodeReroutePin::Construct(const FArguments& InArgs, UEdGraphPin* InPin)
{
	SGraphPinKnot::Construct({}, InPin);

	// Cache pin icons.
	PassThroughImageConnected = UE_MUTABLE_GET_BRUSH(TEXT("Graph.ExecPin.Connected"));
	PassThroughImageDisconnected = UE_MUTABLE_GET_BRUSH(TEXT("Graph.ExecPin.Disconnected"));	
}


const FSlateBrush* SCustomizableObjectNodeReroutePin::GetPinIcon() const
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
