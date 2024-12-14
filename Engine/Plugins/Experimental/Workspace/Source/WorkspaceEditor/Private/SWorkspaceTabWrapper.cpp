// Copyright Epic Games, Inc. All Rights Reserved.

#include "SWorkspaceTabWrapper.h"

#include "EditorModeManager.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Docking/SDockTab.h"
#include "Modules/ModuleManager.h"
#include "ToolMenus.h"

#define LOCTEXT_NAMESPACE "SWorkspaceTabWrapper"

void SWorkspaceTabWrapper::Construct( const FArguments& InArgs, TSharedPtr<class FTabInfo> InTabInfo, TSharedPtr<UE::Workspace::FWorkspaceEditor> InWorkspaceEditor, UObject* InDocumentID)
{
	Content = InArgs._Content.Widget;
	WeakWorkspaceEditor = InWorkspaceEditor;
	WeakDocumentObject = InDocumentID;

	if (InTabInfo->GetTab().IsValid())
	{
		InTabInfo->GetTab().Pin()->SetOnTabClosed(SDockTab::FOnTabClosedCallback::CreateLambda([WeakEditor=WeakWorkspaceEditor, WeakObject=WeakDocumentObject](TSharedRef<SDockTab>)
		{
			if (const TSharedPtr<UE::Workspace::FWorkspaceEditor> SharedWorkspaceEditor = WeakEditor.Pin())
			{
				SharedWorkspaceEditor->RemoveEditingObject(WeakObject.Get());
			}
		}));
	}

	// Set-up shared breadcrumb defaults JDB TODO figure out correct padding to align fake title with breadcrumbs
    const FMargin BreadcrumbTrailPadding = FMargin(4.f, 2.f);
    const FSlateBrush* BreadcrumbButtonImage = FAppStyle::GetBrush("BreadcrumbTrail.Delimiter");

	UToolMenus* ToolMenus = UToolMenus::Get();
	UToolMenu* ToolBarMenu = nullptr;
	if(ToolMenus)
	{
		const FName ToolbarName = "WorkspaceTabWrapperToolbar";		
		if (ToolMenus->IsMenuRegistered(ToolbarName))
		{
			ToolBarMenu = ToolMenus->ExtendMenu(ToolbarName);
		}
		else
		{
			ToolBarMenu = ToolMenus->RegisterMenu(ToolbarName, NAME_None, EMultiBoxType::SlimHorizontalToolBar);
			ToolBarMenu->StyleName = "AssetEditorToolbar";
		}
		
		FToolMenuSection& Section = ToolBarMenu->AddSection("AssetActions");
		Section.AddMenuEntry("Save", TAttribute<FText>(), LOCTEXT("SaveButtonTooltip", "Save Asset"), FSlateIcon(FAppStyle::Get().GetStyleSetName(), "AssetEditor.SaveAsset"),
			FUIAction(FExecuteAction::CreateSP(this, &SWorkspaceTabWrapper::ExecuteSave), FCanExecuteAction::CreateSP(this, &SWorkspaceTabWrapper::CanExecuteSave), FIsActionChecked(), FIsActionButtonVisible::CreateSP(this, &SWorkspaceTabWrapper::IsSaveButtonVisible)));
	
		const FToolMenuEntry FindInContentBrowserEntry = FToolMenuEntry::InitToolBarButton(
			"FindInContentBrowser",
			FUIAction(FExecuteAction::CreateSPLambda(this, [this]() 
				{
					if (UObject* Asset = WeakDocumentObject.Get())
					{
						GEditor->SyncBrowserToObject(Asset);
					}
				})),
			FText::GetEmpty(),
			LOCTEXT("FindInContentBrowserTooltip", "Finds this asset in the content browser"),
			FSlateIcon(FAppStyle::Get().GetStyleSetName(), "SystemWideCommands.FindInContentBrowser")
		);
		Section.AddEntry(FindInContentBrowserEntry);
	}
	
	ChildSlot
	[
		SNew(SVerticalBox)
		+SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				InTabInfo->CreateHistoryNavigationWidget()
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				SNew(SSeparator)
				.Orientation(Orient_Vertical)
			]
			// Title text/icon
			+SHorizontalBox::Slot()
			[
				SNew(SHorizontalBox)
				+SHorizontalBox::Slot()
				.FillWidth(1.f)
				[
					SNew(SHorizontalBox)
					+SHorizontalBox::Slot()
					.AutoWidth()
					.Padding( 10.0f,5.0f )
					.VAlign(VAlign_Center)
					[
						SNew(SImage)
						.DesiredSizeOverride(FVector2D{20.f})
						.Image( this, &SWorkspaceTabWrapper::GetTabIcon )
						.ColorAndOpacity(FSlateColor::UseForeground())
					]
					+SHorizontalBox::Slot()
					.FillWidth(1.f)
					[
						SAssignNew(BreadcrumbTrailScrollBox, SScrollBox)
						.Orientation(Orient_Horizontal)
						.ScrollBarVisibility(EVisibility::Collapsed)

						+SScrollBox::Slot()
						.Padding(0.f)
						.VAlign(VAlign_Center)
						[
							SNew(SHorizontalBox)
							// show fake 'root' breadcrumb for the title
							+SHorizontalBox::Slot()
							.AutoWidth()
							.VAlign(VAlign_Center)
							.Padding(BreadcrumbTrailPadding)
							[
								SNew(SVerticalBox)
								+SVerticalBox::Slot()
								.FillHeight(1.f)
								[
									SNew(STextBlock)
									.Text(this, &SWorkspaceTabWrapper::GetWorkspaceName)
									.TextStyle( FAppStyle::Get(), TEXT("GraphBreadcrumbButtonText") )
									.Visibility( this, &SWorkspaceTabWrapper::IsWorkspaceNameVisible )
								]
								
							]
							+SHorizontalBox::Slot()
							.AutoWidth()
							.VAlign(VAlign_Center)
							.Padding(BreadcrumbTrailPadding)
							[
								SNew(SImage)
								.Image( BreadcrumbButtonImage )
								.Visibility( this, &SWorkspaceTabWrapper::IsWorkspaceNameVisible )
								.ColorAndOpacity(FSlateColor::UseForeground())
							]

							// New style breadcrumb
							+SHorizontalBox::Slot()
							.AutoWidth()
							.VAlign(VAlign_Center)
							[
								SAssignNew(BreadcrumbTrail, SBreadcrumbTrail<TSharedPtr<UE::Workspace::FWorkspaceBreadcrumb>>)
								.ButtonStyle(FAppStyle::Get(), "GraphBreadcrumbButton")
								.TextStyle(FAppStyle::Get(), "GraphBreadcrumbButtonText")
								.ButtonContentPadding(BreadcrumbTrailPadding)
								.DelimiterImage(BreadcrumbButtonImage)
								.OnCrumbClicked_Lambda([](const TSharedPtr<UE::Workspace::FWorkspaceBreadcrumb> InBreadcrumb){ InBreadcrumb->OnClicked.ExecuteIfBound(); })
								.GetCrumbButtonContent_Lambda([](const TSharedPtr<UE::Workspace::FWorkspaceBreadcrumb> InBreadcrumb, const FTextBlockStyle* InTextStyle) -> TSharedRef<SWidget>
								{
									FText Text = InBreadcrumb->OnGetLabel.Execute().Get();
									return SNew(SHorizontalBox)
										+SHorizontalBox::Slot()
										.AutoWidth()
										[
											SNew(STextBlock)
											.Text(Text)
											.TextStyle(InTextStyle)
										]
										+SHorizontalBox::Slot()
										.AutoWidth()
										.Padding(3.0f, 0.f, 0.f, 0.f)
										[
											SNew(SBox)
										   .HAlign(HAlign_Center)
										   .VAlign(VAlign_Center)
										   .HeightOverride(20.0f)		
										   [
											   SNew(SImage)
											   .Image_Lambda([InBreadcrumb]() -> const FSlateBrush*
											   {											
												   if (InBreadcrumb->CanSave.IsBound() && InBreadcrumb->CanSave.Execute())
												   {
													   return FAppStyle::GetBrush("Icons.DirtyBadge");
												   }
										   		
												   return nullptr;
											   })
										   ]
										];
								})
							]
						]
					]
				]
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				SNew(SSeparator)
				.Orientation(Orient_Vertical)
			]
			+SHorizontalBox::Slot()
			.AutoWidth()
			[
				ToolMenus && ToolBarMenu ? ToolMenus->GenerateWidget(ToolBarMenu) : SNullWidget::NullWidget	
			]
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SSeparator)
			.Orientation(Orient_Horizontal)
		]
		+SVerticalBox::Slot()
		.FillHeight(1.0f)
		[
			GetContent()
		]
	];

	RebuildBreadcrumbTrail();
} 

void SWorkspaceTabWrapper::RebuildBreadcrumbTrail() const
{
	BreadcrumbTrail->ClearCrumbs(false);

	TArray<TSharedPtr<UE::Workspace::FWorkspaceBreadcrumb>> Breadcrumbs;
	if (UObject* DocumentID = WeakDocumentObject.Get())
	{
		if (const TSharedPtr<UE::Workspace::FWorkspaceEditor> SharedWorkspaceEditor = WeakWorkspaceEditor.Pin())
		{
			const UE::Workspace::FWorkspaceEditorModule& WorkspaceEditorModule = FModuleManager::LoadModuleChecked<UE::Workspace::FWorkspaceEditorModule>("WorkspaceEditor");
			if(const UE::Workspace::FObjectDocumentArgs* DocumentArgs = WorkspaceEditorModule.FindObjectDocumentType(DocumentID))
			{
				if(DocumentArgs->OnGetDocumentBreadcrumbTrail.IsBound())
				{
					DocumentArgs->OnGetDocumentBreadcrumbTrail.Execute(UE::Workspace::FWorkspaceEditorContext(SharedWorkspaceEditor.ToSharedRef(), DocumentID), Breadcrumbs);
				}
			}
		}
	}

	// Widgets have to be added in reverse order
	for (int32 Index = Breadcrumbs.Num() - 1; Index >= 0; --Index)
	{
		TSharedPtr<UE::Workspace::FWorkspaceBreadcrumb> Breadcrumb = Breadcrumbs[Index];
		BreadcrumbTrail->PushCrumb(	TAttribute<FText>::CreateLambda([Breadcrumb]() { return Breadcrumb->OnGetLabel.IsBound() ? Breadcrumb->OnGetLabel.Execute().Get() : FText::GetEmpty(); }), Breadcrumb);
	}
}

const FSlateBrush* SWorkspaceTabWrapper::GetTabIcon() const
{
	if (UObject* DocumentID = WeakDocumentObject.Get())
	{
		if (const TSharedPtr<UE::Workspace::FWorkspaceEditor> SharedWorkspaceEditor = WeakWorkspaceEditor.Pin())
		{
			const UE::Workspace::FWorkspaceEditorModule& WorkspaceEditorModule = FModuleManager::LoadModuleChecked<UE::Workspace::FWorkspaceEditorModule>("WorkspaceEditor");
			if(const UE::Workspace::FObjectDocumentArgs* DocumentArgs = WorkspaceEditorModule.FindObjectDocumentType(DocumentID))
			{
				if(DocumentArgs->OnGetTabIcon.IsBound())
				{
					return DocumentArgs->OnGetTabIcon.Execute(UE::Workspace::FWorkspaceEditorContext(SharedWorkspaceEditor.ToSharedRef(), DocumentID));
				}
			}
		}			
	}
	
	return nullptr;
}

EVisibility SWorkspaceTabWrapper::IsWorkspaceNameVisible() const
{
	if (const TSharedPtr<UE::Workspace::FWorkspaceEditor> SharedWorkspaceEditor = WeakWorkspaceEditor.Pin())
	{
		if (SharedWorkspaceEditor->Workspace)
		{
			return EVisibility::Visible;
		}
	}

	return EVisibility::Collapsed;
}

FText  SWorkspaceTabWrapper::GetWorkspaceName() const
{
	if (const TSharedPtr<UE::Workspace::FWorkspaceEditor> SharedWorkspaceEditor = WeakWorkspaceEditor.Pin())
	{
		if (SharedWorkspaceEditor->Workspace)
		{
			return FText::FromName(SharedWorkspaceEditor->Workspace->GetFName());
		}
	}

	return FText::GetEmpty();
}

void SWorkspaceTabWrapper::ExecuteSave() const
{
	if(BreadcrumbTrail.IsValid() && BreadcrumbTrail->HasCrumbs())
	{
		TSharedPtr<UE::Workspace::FWorkspaceBreadcrumb> LastBreadcrumb = BreadcrumbTrail->PeekCrumb();
		 LastBreadcrumb->OnSave.ExecuteIfBound();
	}
}

bool SWorkspaceTabWrapper::CanExecuteSave() const
{
	if(BreadcrumbTrail.IsValid() && BreadcrumbTrail->HasCrumbs())
	{
		TSharedPtr<UE::Workspace::FWorkspaceBreadcrumb> LastBreadcrumb = BreadcrumbTrail->PeekCrumb();
		return LastBreadcrumb->CanSave.IsBound() ? LastBreadcrumb->CanSave.Execute() : false;
	}

	return false;
}

bool SWorkspaceTabWrapper::IsSaveButtonVisible() const
{
	if(BreadcrumbTrail.IsValid() && BreadcrumbTrail->HasCrumbs())
	{
		TSharedPtr<UE::Workspace::FWorkspaceBreadcrumb> LastBreadcrumb = BreadcrumbTrail->PeekCrumb();
		return LastBreadcrumb->CanSave.IsBound();
	}

	return false;
}

#undef LOCTEXT_NAMESPACE // "SWorkspaceTabWrapper"
