// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Engine/Blueprint.h"
#include "EditorOnlyVCamModifierBlueprint.generated.h"

/** This Blueprint class allows UEditorOnlyVCamModifier Blueprints to use editor-only functions.  */
UCLASS()
class UEditorOnlyVCamModifierBlueprint : public UBlueprint
{
	GENERATED_BODY()
public:

	//~ Begin UObject Interface
	virtual bool IsEditorOnly() const override
	{
		// By returning true, ::IsEditorOnlyObject will consider this to be an editor-only object.
		return true;
	}
	//~ End UObject Interface
	
	// UBlueprint interface
	virtual bool SupportedByDefaultBlueprintFactory() const override { return true; }
	virtual bool AlwaysCompileOnLoad() const override { return true; }
	// End of UBlueprint interface
};
