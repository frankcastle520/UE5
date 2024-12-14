// Copyright Epic Games, Inc. All Rights Reserved.

#include "SStaticMeshEditorViewportToolBar.h"
#include "SStaticMeshEditorViewport.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "UObject/Package.h"
#include "Components/StaticMeshComponent.h"
#include "Styling/AppStyle.h"
#include "Engine/StaticMesh.h"
#include "IStaticMeshEditor.h"
#include "StaticMeshEditorActions.h"
#include "Slate/SceneViewport.h"
#include "ComponentReregisterContext.h"
#include "AnalyticsEventAttribute.h"
#include "Interfaces/IAnalyticsProvider.h"
#include "EngineAnalytics.h"
#include "Widgets/Docking/SDockTab.h"
#include "Engine/StaticMeshSocket.h"
#include "SEditorViewportToolBarMenu.h"
#include "StaticMeshViewportLODCommands.h"
#include "PreviewProfileController.h"
#include "StaticMeshEditorViewportToolbarSections.h"

#define LOCTEXT_NAMESPACE "StaticMeshEditorViewportToolbar"

///////////////////////////////////////////////////////////
// SStaticMeshEditorViewportToolbar


void SStaticMeshEditorViewportToolbar::Construct(const FArguments& InArgs, TSharedPtr<class ICommonEditorViewportToolbarInfoProvider> InInfoProvider)
{
	SCommonEditorViewportToolbarBase::Construct(SCommonEditorViewportToolbarBase::FArguments().PreviewProfileController(MakeShared<FPreviewProfileController>()), InInfoProvider);
}

// SCommonEditorViewportToolbarBase interface
TSharedRef<SWidget> SStaticMeshEditorViewportToolbar::GenerateShowMenu() const
{
	TSharedRef<SEditorViewport> BaseViewportRef = GetInfoProvider().GetViewportWidget();
	TSharedRef<SStaticMeshEditorViewport> ViewportRef = StaticCastSharedRef<SStaticMeshEditorViewport, SEditorViewport>(BaseViewportRef);

	return UE::StaticMeshEditor::GenerateShowMenuWidget(ViewportRef);
}

// SCommonEditorViewportToolbarBase interface
void SStaticMeshEditorViewportToolbar::ExtendLeftAlignedToolbarSlots(TSharedPtr<SHorizontalBox> MainBoxPtr, TSharedPtr<SViewportToolBar> ParentToolBarPtr) const 
{
	const FMargin ToolbarSlotPadding(2.0f, 2.0f);

	if (!MainBoxPtr.IsValid())
	{
		return;
	}

	MainBoxPtr->AddSlot()
		.AutoWidth()
		.Padding(ToolbarSlotPadding)
		[
			SNew(SEditorViewportToolbarMenu)
			.Label(this, &SStaticMeshEditorViewportToolbar::GetLODMenuLabel)
			.OnGetMenuContent(this, &SStaticMeshEditorViewportToolbar::GenerateLODMenu)
			.Cursor(EMouseCursor::Default)
			.ParentToolBar(ParentToolBarPtr)
		];
}

FText SStaticMeshEditorViewportToolbar::GetLODMenuLabel() const
{
	TSharedRef<SEditorViewport> BaseViewportRef = GetInfoProvider().GetViewportWidget();
	TSharedRef<SStaticMeshEditorViewport> ViewportRef = StaticCastSharedRef<SStaticMeshEditorViewport, SEditorViewport>(BaseViewportRef);

	return UE::StaticMeshEditor::GetLODMenuLabel(ViewportRef);
}

TSharedRef<SWidget> SStaticMeshEditorViewportToolbar::GenerateLODMenu() const
{
	TSharedRef<SEditorViewport> BaseViewportRef = GetInfoProvider().GetViewportWidget();
	TSharedRef<SStaticMeshEditorViewport> ViewportRef = StaticCastSharedRef<SStaticMeshEditorViewport, SEditorViewport>(BaseViewportRef);

	return UE::StaticMeshEditor::GenerateLODMenuWidget(ViewportRef);
}

#undef LOCTEXT_NAMESPACE
