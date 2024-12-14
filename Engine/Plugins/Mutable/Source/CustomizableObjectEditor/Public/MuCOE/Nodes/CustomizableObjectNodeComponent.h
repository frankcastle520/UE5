// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MuCOE/Nodes/CustomizableObjectNode.h"

#include "CustomizableObjectNodeComponent.generated.h"

class UEdGraphPin;


UCLASS(abstract)
class CUSTOMIZABLEOBJECTEDITOR_API UCustomizableObjectNodeComponent : public UCustomizableObjectNode
{
public:
	GENERATED_BODY()

	// UCustomizableObjectNode interface
	virtual bool IsAffectedByLOD() const override;
};

