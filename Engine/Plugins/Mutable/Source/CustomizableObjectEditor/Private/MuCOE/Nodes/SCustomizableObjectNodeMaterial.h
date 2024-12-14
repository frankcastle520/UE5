// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "SCustomizableObjectNode.h"
#include "SGraphNode.h"

class UCustomizableObjectNodeMaterial;
class SGraphPin;
class UEdGraphPin;


/** Custom widget for the Material node. */
class SCustomizableObjectNodeMaterial : public SCustomizableObjectNode
{
public:
	SLATE_BEGIN_ARGS(SCustomizableObjectNodeMaterial) {}
	SLATE_END_ARGS();

	void Construct(const FArguments& InArgs, UCustomizableObjectNodeMaterial* InGraphNode);

	// SGraphNode interface
	virtual TSharedPtr<SGraphPin> CreatePinWidget(UEdGraphPin* Pin) const override;
};
