// Copyright Epic Games, Inc. All Rights Reserved.

#include "SSmartObjectViewport.h"

#include "PreviewProfileController.h"
#include "SmartObjectAssetEditorViewportClient.h"
#include "SSmartObjectViewportToolbar.h"
#include "SmartObjectAssetToolkit.h"
#include "Framework/Application/SlateApplication.h"
#include "ViewportToolbar/UnrealEdViewportToolbar.h"
#include "ToolMenus.h"

#define LOCTEXT_NAMESPACE "SmartObjectViewport"

void SSmartObjectViewport::Construct(const FArguments& InArgs)
{
	ViewportClient = InArgs._EditorViewportClient;
	PreviewScene = InArgs._PreviewScene;
	AssetEditorToolkitPtr = InArgs._AssetEditorToolkit;

	SEditorViewport::Construct(
		SEditorViewport::FArguments().IsEnabled(FSlateApplication::Get().GetNormalExecutionAttribute())
	);
}

void SSmartObjectViewport::BindCommands()
{
	SEditorViewport::BindCommands();
}

TSharedRef<FEditorViewportClient> SSmartObjectViewport::MakeEditorViewportClient()
{
	return ViewportClient.ToSharedRef();
}

TSharedPtr<SWidget> SSmartObjectViewport::MakeViewportToolbar()
{
	return SAssignNew(ViewportToolbar, SSmartObjectViewportToolBar, SharedThis(this))
		.Visibility_Lambda([]()
			{
				return UE::UnrealEd::ShowOldViewportToolbars() ? EVisibility::Visible : EVisibility::Collapsed;
			}
		);
}

TSharedPtr<SWidget> SSmartObjectViewport::BuildViewportToolbar()
{
	// Register the viewport toolbar if another viewport hasn't already (it's shared).
	const FName ViewportToolbarName = "SmartObjectEditor.ViewportToolbar";

	if (!UToolMenus::Get()->IsMenuRegistered(ViewportToolbarName))
	{
		UToolMenu* const ViewportToolbarMenu = UToolMenus::Get()->RegisterMenu(
			ViewportToolbarName, NAME_None /* parent */, EMultiBoxType::SlimHorizontalToolBar
		);

		ViewportToolbarMenu->StyleName = "ViewportToolbar";

		// Add the left-aligned part of the viewport toolbar.
		{
			FToolMenuSection& LeftSection = ViewportToolbarMenu->FindOrAddSection("Left");
		}

		// Add the right-aligned part of the viewport toolbar.
		{
			// Add the submenus of this section as EToolMenuInsertType::Last to sort them after any
			// default-positioned submenus external code might add.
			FToolMenuSection& RightSection = ViewportToolbarMenu->FindOrAddSection("Right");
			RightSection.Alignment = EToolMenuSectionAlign::Last;

			// Add the "Camera" submenu.
			{
				const FName GrandParentSubmenuName = "UnrealEd.ViewportToolbar.Camera";
				const FName ParentSubmenuName = "SmartObjectEditor.ViewportToolbar.Camera";
				const FName SubmenuName = "SmartObjectEditor.ViewportToolbar.CameraOptions";

				// Create our grandparent menu.
				if (!UToolMenus::Get()->IsMenuRegistered(GrandParentSubmenuName))
				{
					UToolMenus::Get()->RegisterMenu(GrandParentSubmenuName);
				}

				// Create our parent menu.
				if (!UToolMenus::Get()->IsMenuRegistered(ParentSubmenuName))
				{
					UToolMenus::Get()->RegisterMenu(ParentSubmenuName, GrandParentSubmenuName);
				}

				// Create our menu.
				UToolMenus::Get()->RegisterMenu(SubmenuName, ParentSubmenuName);

				UE::UnrealEd::ExtendCameraSubmenu(SubmenuName);

				FToolMenuEntry CameraSubmenu = UE::UnrealEd::CreateViewportToolbarCameraSubmenu();
				CameraSubmenu.InsertPosition.Position = EToolMenuInsertType::First;
				RightSection.AddEntry(CameraSubmenu);
			}

			// Add the "View Modes" sub menu.
			{
				// Stay backward-compatible with the old viewport toolbar.
				{
					const FName ParentSubmenuName = "UnrealEd.ViewportToolbar.View";
					// Create our parent menu.
					if (!UToolMenus::Get()->IsMenuRegistered(ParentSubmenuName))
					{
						UToolMenus::Get()->RegisterMenu(ParentSubmenuName);
					}

					// Register our ToolMenu here first, before we create the submenu, so we can set our parent.
					UToolMenus::Get()->RegisterMenu("SmartObjectEditor.ViewportToolbar.ViewModes", ParentSubmenuName);
				}

				FToolMenuEntry ViewModesSubmenu = UE::UnrealEd::CreateViewportToolbarViewModesSubmenu();
				ViewModesSubmenu.InsertPosition.Position = EToolMenuInsertType::First;
				RightSection.AddEntry(ViewModesSubmenu);

				FToolMenuEntry PerformanceAndScalabilitySubmenu = UE::UnrealEd::CreatePerformanceAndScalabilitySubmenu();
				PerformanceAndScalabilitySubmenu.InsertPosition.Position = EToolMenuInsertType::First;
				RightSection.AddEntry(PerformanceAndScalabilitySubmenu);
			}

			// Add the Show submenu.
			{
				FToolMenuEntry ShowSubmenu = UE::UnrealEd::CreateDefaultShowSubmenu();
				ShowSubmenu.InsertPosition.Position = EToolMenuInsertType::First;
				RightSection.AddEntry(ShowSubmenu);
			}

			// Add the Performance and Scalability submenu.
			{
				FToolMenuEntry PerformanceAndScalabilitySubmenu = UE::UnrealEd::CreatePerformanceAndScalabilitySubmenu();
				PerformanceAndScalabilitySubmenu.InsertPosition.Position = EToolMenuInsertType::First;
				RightSection.AddEntry(PerformanceAndScalabilitySubmenu);
			}

			// Add the "Preview Profile" sub menu.
			{
				PreviewProfileController = MakeShared<FPreviewProfileController>();
				FToolMenuEntry PreviewProfileSubmenu =
					UE::UnrealEd::CreateViewportToolbarAssetViewerProfileSubmenu(PreviewProfileController);
				PreviewProfileSubmenu.InsertPosition.Position = EToolMenuInsertType::Last;
				RightSection.AddEntry(PreviewProfileSubmenu);
			}
		}
	}

	FToolMenuContext ViewportToolbarContext;
	{
		ViewportToolbarContext.AppendCommandList(GetCommandList());

		// Add the UnrealEd viewport toolbar context.
		{
			UUnrealEdViewportToolbarContext* const ContextObject = NewObject<UUnrealEdViewportToolbarContext>();
			ContextObject->Viewport = SharedThis(this);

			// Hook up our toolbar's filter for supported view modes.
			ContextObject->IsViewModeSupported = UE::UnrealEd::IsViewModeSupportedDelegate::CreateLambda(
				[](EViewModeIndex ViewModeIndex) -> bool
				{
					// This code is taken from SViewportToolBar::IsViewModeSupported
					// SSCSEditorViewportToolBar does not override it, so we just take it as-is
					// TODO: maybe create a private function for it, or move IsViewModeSupported to SEditorViewport

					switch (ViewModeIndex)
					{
					case VMI_PrimitiveDistanceAccuracy:
					case VMI_MaterialTextureScaleAccuracy:
					case VMI_RequiredTextureResolution:
						return false;
					default:
						return true;
					}
				}
			);

			ViewportToolbarContext.AddObject(ContextObject);
		}
	}

	// clang-format off
	const TSharedRef<SWidget> NewViewportToolbar = SNew(SBox)
		.Visibility_Lambda(
		[]() -> EVisibility
		{
			return  UE::UnrealEd::ShowNewViewportToolbars() ? EVisibility::Visible: EVisibility::Collapsed;
		}
	)
	[
		UToolMenus::Get()->GenerateWidget(ViewportToolbarName, ViewportToolbarContext)
	];
	// clang-format on

	return NewViewportToolbar;
}

TSharedRef<SEditorViewport> SSmartObjectViewport::GetViewportWidget()
{
	return SharedThis(this);
}

TSharedPtr<FExtender> SSmartObjectViewport::GetExtenders() const
{
	TSharedPtr<FExtender> Result(MakeShareable(new FExtender));
	return Result;
}

void SSmartObjectViewport::OnFloatingButtonClicked()
{
}

#undef LOCTEXT_NAMESPACE
