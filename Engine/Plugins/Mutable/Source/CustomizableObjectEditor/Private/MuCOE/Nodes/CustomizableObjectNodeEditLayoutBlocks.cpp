// Copyright Epic Games, Inc. All Rights Reserved.

#include "MuCOE/Nodes/CustomizableObjectNodeModifierEditLayoutBlocks.h"


#define LOCTEXT_NAMESPACE "CustomizableObjectEditor"


UCustomizableObjectNodeModifierEditLayoutBlocks::UCustomizableObjectNodeModifierEditLayoutBlocks()
	: Super()
{
	Layout = CreateDefaultSubobject<UCustomizableObjectLayout>(FName("CustomizableObjectLayout"));
	Layout->Blocks.Empty();
}

#undef LOCTEXT_NAMESPACE

