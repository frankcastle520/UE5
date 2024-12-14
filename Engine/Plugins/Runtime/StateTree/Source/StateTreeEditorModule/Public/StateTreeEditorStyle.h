// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Styling/SlateStyle.h"

enum class EStateTreeStateSelectionBehavior : uint8;
enum class EStateTreeStateType : uint8;

class ISlateStyle;

class STATETREEEDITORMODULE_API FStateTreeEditorStyle
	: public FSlateStyleSet
{
public:
	static FStateTreeEditorStyle& Get();
	
	static const FSlateBrush* GetBrushForSelectionBehaviorType(EStateTreeStateSelectionBehavior InBehaviour, bool bHasChildren, EStateTreeStateType StateType);	

protected:
	friend class FStateTreeEditorModule;

	static void Register();
	static void Unregister();

private:
	FStateTreeEditorStyle();
};
