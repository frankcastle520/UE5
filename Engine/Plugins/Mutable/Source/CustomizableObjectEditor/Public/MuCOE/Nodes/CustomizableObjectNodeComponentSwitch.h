// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MuCOE/Nodes/CustomizableObjectNodeSwitchBase.h"

#include "CustomizableObjectNodeComponentSwitch.generated.h"


UCLASS()
class CUSTOMIZABLEOBJECTEDITOR_API UCustomizableObjectNodeComponentSwitch : public UCustomizableObjectNodeSwitchBase
{
public:
	GENERATED_BODY()
	
	// UCustomizableObjectNodeSwitchBase interface
	virtual FName GetCategory() const override;
};

