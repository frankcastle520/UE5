// Copyright Epic Games, Inc. All Rights Reserved.

#include "SRetargetSources.h"
#include "AssetRegistry/AssetData.h"
#include "Misc/MessageDialog.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Styling/AppStyle.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Input/SButton.h"
#include "Animation/DebugSkelMeshComponent.h"
#include "Widgets/SToolTip.h"
#include "IDocumentation.h"
#include "ScopedTransaction.h"
#include "SRetargetSourceWindow.h"
#include "AnimPreviewInstance.h"
#include "IEditableSkeleton.h"
#include "PropertyCustomizationHelpers.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"


#define LOCTEXT_NAMESPACE "SRetargetSources"

void SRetargetSources::Construct(
	const FArguments& InArgs,
	const TSharedRef<IEditableSkeleton>& InEditableSkeleton,
	FSimpleMulticastDelegate& InOnPostUndo)
{
	const FString DocLink = TEXT("Shared/Editors/Persona");
	ChildSlot
	[
		SNew (SVerticalBox)
		
		+ SVerticalBox::Slot()
		.Padding(5, 5)
		.AutoHeight()
		[
			SNew(STextBlock)
			.TextStyle(FAppStyle::Get(), "Persona.RetargetManager.ImportantText")
			.Text(LOCTEXT("RetargetSource_Title", "Manage Retarget Sources"))
		]
		
		+ SVerticalBox::Slot()
		.Padding(5, 5)
		.FillHeight(0.5)
		[
			// construct retarget source UI
			SNew(SRetargetSourceWindow, InEditableSkeleton, InOnPostUndo)
		]

		+SVerticalBox::Slot()
		.Padding(5, 5)
		.AutoHeight()
		[
			SNew(SSeparator)
			.Orientation(Orient_Horizontal)
		]

		+ SVerticalBox::Slot()
		.Padding(5, 5)
		.AutoHeight()
		[
			SNew(STextBlock)
			.TextStyle(FAppStyle::Get(), "Persona.RetargetManager.ImportantText")
			.Text(LOCTEXT("CompatibleSkeletons_Title", "Manage Compatible Skeletons"))
		]

		+ SVerticalBox::Slot()
		.Padding(5, 5)
		.FillHeight(0.5)
		[
			// construct compatible skeletons UI
			SNew(SCompatibleSkeletons, InEditableSkeleton, InOnPostUndo)
		]
	];
}


#undef LOCTEXT_NAMESPACE
