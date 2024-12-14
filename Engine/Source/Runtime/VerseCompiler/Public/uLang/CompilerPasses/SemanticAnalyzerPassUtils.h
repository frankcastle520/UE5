// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

namespace uLang
{
enum class ESemanticPass
{
    SemanticPass_Invalid = 0,

    SemanticPass_Types,
    SemanticPass_Attributes,
    SemanticPass_Code,

    SemanticPass__MinValid = SemanticPass_Types,
    SemanticPass__MaxValid = SemanticPass_Code
};
}