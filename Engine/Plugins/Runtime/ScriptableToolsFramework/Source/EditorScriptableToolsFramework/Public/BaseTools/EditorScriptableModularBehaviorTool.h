// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "BaseTools/ScriptableModularBehaviorTool.h"
#include "EditorScriptableModularBehaviorTool.generated.h"

/**
 * Editor-Only variant of UScriptableModularBehaviorTool, which gives access to Editor-Only BP functions
 */
UCLASS(Transient, Blueprintable)
class EDITORSCRIPTABLETOOLSFRAMEWORK_API UEditorScriptableModularBehaviorTool : public UScriptableModularBehaviorTool
{
	GENERATED_BODY()
public:

};