// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MuT/NodeLayout.h"
#include "MuCOE/CustomizableObjectLayout.h"

class UEdGraphPin;
struct FMutableGraphGenerationContext;


extern mu::Ptr<mu::NodeLayout> CreateDefaultLayout();

extern mu::Ptr<mu::NodeLayout> CreateMutableLayoutNode(FMutableGraphGenerationContext& GenerationContext, const UCustomizableObjectLayout*, bool bIgnoreLayoutWarnings, bool& bWasEmpty);

extern mu::FSourceLayoutBlock ToMutable(FMutableGraphGenerationContext& GenerationContext, const FCustomizableObjectLayoutBlock&);
