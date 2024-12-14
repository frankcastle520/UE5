// Copyright Epic Games, Inc. All Rights Reserved.

#include "SCustomizableObjectNodeReroute.h"

#include "SCustomizableObjectNodeReroutePin.h"


void SCustomizableObjectNodeReroute::Construct(const FArguments& InArgs, UEdGraphNode* InGraphNode)
{
	SGraphNodeKnot::Construct({}, InGraphNode);
}


TSharedPtr<SGraphPin> SCustomizableObjectNodeReroute::CreatePinWidget(UEdGraphPin* Pin) const
{
	return SNew(SCustomizableObjectNodeReroutePin, Pin);
}

