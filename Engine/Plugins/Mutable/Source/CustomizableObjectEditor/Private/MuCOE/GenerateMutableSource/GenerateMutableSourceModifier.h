// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MuT/NodeModifier.h"

class UEdGraphPin;
struct FMutableGraphGenerationContext;

mu::Ptr<mu::NodeModifier> GenerateMutableSourceModifier(const UEdGraphPin* Pin, FMutableGraphGenerationContext& GenerationContext);
