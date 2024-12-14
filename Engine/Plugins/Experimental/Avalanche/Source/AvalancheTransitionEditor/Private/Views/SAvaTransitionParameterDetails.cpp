// Copyright Epic Games, Inc. All Rights Reserved.

#include "SAvaTransitionParameterDetails.h"
#include "SAvaTransitionTreeDetails.h"

void SAvaTransitionParameterDetails::Construct(const FArguments& InArgs, const TSharedRef<FAvaTransitionEditorViewModel>& InEditorViewModel)
{
	ChildSlot
	[
		SNew(SAvaTransitionTreeDetails, InEditorViewModel)
	];
}
