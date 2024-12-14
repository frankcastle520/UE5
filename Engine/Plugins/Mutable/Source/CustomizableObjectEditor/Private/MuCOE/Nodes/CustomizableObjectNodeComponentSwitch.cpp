// Copyright Epic Games, Inc. All Rights Reserved.

#include "MuCOE/Nodes/CustomizableObjectNodeComponentSwitch.h"

#include "MuCOE/EdGraphSchema_CustomizableObject.h"

#define LOCTEXT_NAMESPACE "CustomizableObjectEditor"


FName UCustomizableObjectNodeComponentSwitch::GetCategory() const
{
	return UEdGraphSchema_CustomizableObject::PC_Component;
}


#undef LOCTEXT_NAMESPACE

