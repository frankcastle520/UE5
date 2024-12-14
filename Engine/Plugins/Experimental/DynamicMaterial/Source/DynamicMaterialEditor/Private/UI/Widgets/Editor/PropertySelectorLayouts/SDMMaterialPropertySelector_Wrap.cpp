// Copyright Epic Games, Inc. All Rights Reserved.

#include "UI/Widgets/Editor/PropertySelectorLayouts/SDMMaterialPropertySelector_Wrap.h"

#define LOCTEXT_NAMESPACE "SDMMaterialPropertySelector_Wrap"

void SDMMaterialPropertySelector_Wrap::Construct(const FArguments& InArgs, const TSharedRef<SDMMaterialEditor> InEditorWidget)
{
	SDMMaterialPropertySelector_WrapBase::Construct(
		SDMMaterialPropertySelector_WrapBase::FArguments(),
		InEditorWidget
	);
}

#undef LOCTEXT_NAMESPACE
