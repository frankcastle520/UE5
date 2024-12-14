// Copyright Epic Games, Inc. All Rights Reserved.

#include "SDMXControlConsoleCompactEditorView.h"

#include "Commands/DMXControlConsoleEditorCommands.h"
#include "DMXControlConsole.h"
#include "DMXControlConsoleCompactEditorMenuContext.h"
#include "DMXControlConsoleEditorModule.h"
#include "DMXControlConsoleEditorSelection.h"
#include "Editor.h"
#include "FileHelpers.h"
#include "Models/DMXControlConsoleCueStackModel.h"
#include "Models/DMXControlConsoleCompactEditorModel.h"
#include "Models/DMXControlConsoleEditorModel.h"
#include "Models/DMXControlConsoleEditorPlayMenuModel.h"
#include "ToolMenu.h"
#include "ToolMenus.h"
#include "UObject/Package.h"
#include "Views/SDMXControlConsoleEditorCueStackView.h"
#include "Views/SDMXControlConsoleEditorLayoutView.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SDMXControlConsoleEditorCueStackComboBox.h"
#include "Widgets/Text/STextBlock.h"


#define LOCTEXT_NAMESPACE "SDMXControlConsoleCompactEditorView"

namespace UE::DMX::Private
{
	const FName SDMXControlConsoleCompactEditorView::ToolbarMenuName = "DMX.ControlConsole.CompactEditorToolbar";

	SDMXControlConsoleCompactEditorView::~SDMXControlConsoleCompactEditorView()
	{
		UDMXControlConsoleData* ControlConsoleData = ControlConsole ? ControlConsole->GetControlConsoleData() : nullptr;
		if (ControlConsoleData && ControlConsoleData->IsSendingDMX() && bStopSendingDMXOnDestruct)
		{
			ControlConsoleData->StopSendingDMX();
		}
	}

	void SDMXControlConsoleCompactEditorView::Construct(const FArguments& InArgs)
	{
		UDMXControlConsoleCompactEditorModel* CompactEditorModel = GetMutableDefault<UDMXControlConsoleCompactEditorModel>();
		ControlConsole = CompactEditorModel->LoadControlConsoleSynchronous();
		const UDMXControlConsoleData* ControlConsoleData = ControlConsole ? ControlConsole->GetControlConsoleData() : nullptr;
		
		if (ControlConsole && ControlConsoleData)
		{
			bStopSendingDMXOnDestruct = !ControlConsoleData->IsSendingDMX();

			EditorModel = NewObject<UDMXControlConsoleEditorModel>(GetTransientPackage(), NAME_None, RF_Transient | RF_Transactional);
			EditorModel->Initialize(ControlConsole);

			// Setup commands now, it relies on EditorModel and is required for PlayMenuModel
			SetupCommands();

			PlayMenuModel = NewObject<UDMXControlConsoleEditorPlayMenuModel>(GetTransientPackage(), NAME_None, RF_Transient | RF_Transactional);
			PlayMenuModel->Initialize(ControlConsole, CommandList.ToSharedRef());

			CueStackModel = MakeShared<FDMXControlConsoleCueStackModel>(ControlConsole);

			ChildSlot
			[
				SNew(SVerticalBox)

				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(SHorizontalBox)
				
					// Compact Editor toolbar section
					+ SHorizontalBox::Slot()
					.FillWidth(1.0)
					[
						CreateToolbar()
					]

					+ SHorizontalBox::Slot()
					.AutoWidth()
					[
						SNew(SBorder)
						.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
						.VAlign(VAlign_Center)
						.Padding(FMargin(4.f, 0.f, 20.f, 0.f))
						[
							SNew(SHorizontalBox)

							// Cue Stack toolbar section
							+ SHorizontalBox::Slot()
							.AutoWidth()
							.Padding(20.f, 0.f)
							[
								GenerateCueStackToolbarWidget()
							]

							// Asset name label section
							+ SHorizontalBox::Slot()
							[
								SNew(SBox)
								.VAlign(VAlign_Center)
								[
									SNew(STextBlock)
									.Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
									.Text(this, &SDMXControlConsoleCompactEditorView::GetAssetNameText)
								]
							]
						]
					]
				]

				+ SVerticalBox::Slot()
				.FillHeight(1.f)
				[
					SNew(SSplitter)
					.Orientation(Orient_Horizontal)

					// Layout View section
					+ SSplitter::Slot()
					.Value(.8f)
					[
						SNew(SDMXControlConsoleEditorLayoutView, EditorModel)
					]

					// Cue Stack View section
					+ SSplitter::Slot()
					.Value(.2f)
					[
						SNew(SDMXControlConsoleEditorCueStackView, CueStackModel)
						.Visibility(this, &SDMXControlConsoleCompactEditorView::GetCueStackViewVisibility)
					]
				]
			];
		}
		else
		{
			ChildSlot
			[
				SNullWidget::NullWidget
			];
		}
	}

	FReply SDMXControlConsoleCompactEditorView::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
	{
		if (CommandList->ProcessCommandBindings(InKeyEvent))
		{
			return FReply::Handled();
		}

		return FReply::Unhandled();
	}

	void SDMXControlConsoleCompactEditorView::AddReferencedObjects(FReferenceCollector& Collector)
	{
		Collector.AddReferencedObject(ControlConsole);
		Collector.AddReferencedObject(EditorModel);
		Collector.AddReferencedObject(PlayMenuModel);
	}

	FString SDMXControlConsoleCompactEditorView::GetReferencerName() const
	{
		return TEXT("UE::DMX::Private::SDMXControlConsoleCompactEditorView");
	}

	void SDMXControlConsoleCompactEditorView::SetupCommands()
	{
		CommandList = MakeShared<FUICommandList>();

		if (ensureMsgf(EditorModel, TEXT("Invalid Editor Model, cannot setup commands for Control Console Compact Editor")))
		{
			const TSharedRef<FDMXControlConsoleEditorSelection> SelectionHandler = EditorModel->GetSelectionHandler();
			constexpr bool bSelectOnlyVisible = true;
			CommandList->MapAction
			(
				FDMXControlConsoleEditorCommands::Get().SelectAll,
				FExecuteAction::CreateSP(SelectionHandler, &FDMXControlConsoleEditorSelection::SelectAll, bSelectOnlyVisible)
			);
		}
	}

	TSharedRef<SWidget> SDMXControlConsoleCompactEditorView::CreateToolbar()
	{
		// Using the same pattern as SSequencer to present the menu depending on the current asset
		UToolMenus* ToolMenus = UToolMenus::Get();
		if (!ToolMenus->IsMenuRegistered(ToolbarMenuName))
		{
			UToolMenu* Toolbar = ToolMenus->RegisterMenu(ToolbarMenuName, NAME_None, EMultiBoxType::SlimHorizontalToolBar);
			Toolbar->AddDynamicSection("PopulateToolBar", FNewToolMenuDelegate::CreateStatic(&SDMXControlConsoleCompactEditorView::PopulateToolbar));
		}

		UDMXControlConsoleCompactEditorMenuContext* ContextObject = NewObject<UDMXControlConsoleCompactEditorMenuContext>();
		ContextObject->WeakCompactEditorView = SharedThis(this);

		const FToolMenuContext MenuContext(CommandList, TSharedPtr<FExtender>(), ContextObject);
		UToolMenu* ToolMenu = UToolMenus::Get()->GenerateMenu(ToolbarMenuName, MenuContext);
		return UToolMenus::Get()->GenerateWidget(ToolbarMenuName, MenuContext);
	}

	TSharedRef<SWidget> SDMXControlConsoleCompactEditorView::GenerateCueStackToolbarWidget()
	{
		const bool bInvalidCueStack = !CueStackModel.IsValid();
		if (bInvalidCueStack)
		{
			return SNullWidget::NullWidget;
		}

		const TSharedRef<SWidget> CueStackToolbarWidget =
			SNew(SHorizontalBox)

			// Cue Stack Combo Box section
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(2.f, 0.f)
			[
				SNew(SDMXControlConsoleEditorCueStackComboBox, CueStackModel)
			]

			// 'Show cue stack' Check Box section
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(4.f, 0.f)
			[
				SNew(SHorizontalBox)

				+ SHorizontalBox::Slot()
				.AutoWidth()
				[
					SAssignNew(CueStackCheckBox, SCheckBox)
				]

				+ SHorizontalBox::Slot()
				.Padding(4.f, 3.f, 0.f, 0.f)
				[
					SNew(SBox)
					.VAlign(VAlign_Center)
					[
						SNew(STextBlock)
						.Font(FAppStyle::GetFontStyle(TEXT("PropertyWindow.NormalFont")))
						.Text(LOCTEXT("ShowCueStackLabel", "Show Cue Stack"))
						.ToolTipText(LOCTEXT("ShowCueStackTooltip", "Shows the full cue stack menu"))
					]
				]
			];

		return CueStackToolbarWidget;
	}

	void SDMXControlConsoleCompactEditorView::PopulateToolbar(UToolMenu* InMenu)
	{
		UDMXControlConsoleCompactEditorMenuContext* ContextObject = InMenu ? InMenu->FindContext<UDMXControlConsoleCompactEditorMenuContext>() : nullptr;
		TSharedPtr<SDMXControlConsoleCompactEditorView> CompactEditorView = ContextObject && ContextObject->WeakCompactEditorView.IsValid() ? ContextObject->WeakCompactEditorView.Pin() : nullptr;
		if (!CompactEditorView.IsValid())
		{
			return;
		}

		// Asset section
		{
			FToolMenuSection& AssetSection = InMenu->AddSection("Asset");

			const FToolMenuEntry SaveEntry = FToolMenuEntry::InitToolBarButton(
				"Save",
				FUIAction(FExecuteAction::CreateSP(CompactEditorView.Get(), &SDMXControlConsoleCompactEditorView::OnSaveClicked)),
				FText::GetEmpty(),
				LOCTEXT("SaveTooltip", "Saves the control console"),
				FSlateIcon(FAppStyle::GetAppStyleSetName(), "AssetEditor.SaveAsset")
			);
			AssetSection.AddEntry(SaveEntry);

			const FToolMenuEntry FindInContentBrowserEntry = FToolMenuEntry::InitToolBarButton(
				"FindInContentBrowser",
				FUIAction(FExecuteAction::CreateSP(CompactEditorView.Get(), &SDMXControlConsoleCompactEditorView::OnFindInContentBrowserClicked)),
				FText::GetEmpty(),
				LOCTEXT("FindInContentBrowserTooltip", "Finds this asset in the content browser"),
				FSlateIcon(FAppStyle::Get().GetStyleSetName(), "SystemWideCommands.FindInContentBrowser")
			);
			AssetSection.AddEntry(FindInContentBrowserEntry);
		}

		// Play Section
		{
			if (UDMXControlConsoleEditorPlayMenuModel* ContextualPlayMenuModel = CompactEditorView->PlayMenuModel)
			{
				ContextualPlayMenuModel->CreatePlayMenu(*InMenu);
			}
		}

		// 'Show Full Editor' section
		{
			FToolMenuSection& ShowFullEditorSection = InMenu->AddSection("ShowFullEditor");

			const TSharedRef<SWidget> ShowFullEditorButton = SNew(SButton)
				.OnClicked(CompactEditorView.Get(), &SDMXControlConsoleCompactEditorView::OnShowFullEditorButtonClicked)
				[
					SNew(SBorder)
					.VAlign(VAlign_Center)
					.BorderImage(FAppStyle::GetBrush("NoBorder"))
					[
						SNew(STextBlock)
						.Font(FAppStyle::GetFontStyle(TEXT("PropertyWindow.NormalFont")))
						.Text(LOCTEXT("ShowFullWindowLabel", "Show Full Editor"))
						.ToolTipText(LOCTEXT("ShowFullWindowTooltip", "Shows the full control console editor"))
					]
				];

			const FToolMenuEntry ShowFullEditorEntry = FToolMenuEntry::InitWidget(
				NAME_None,
				ShowFullEditorButton,
				FText::GetEmpty()
			);

			ShowFullEditorSection.AddEntry(ShowFullEditorEntry);
		}
	}

	void SDMXControlConsoleCompactEditorView::OnSaveClicked()
	{
		if (ControlConsole)
		{
			const TArray<UPackage*> PackagesToSave({ ControlConsole->GetOutermost() });

			constexpr bool bCheckDirtyOnAssetSave = false;
			constexpr bool bPromptToSave = false;
			FEditorFileUtils::PromptForCheckoutAndSave(PackagesToSave, bCheckDirtyOnAssetSave, bPromptToSave);
		}
	}

	void SDMXControlConsoleCompactEditorView::OnFindInContentBrowserClicked()
	{
		if (ControlConsole)
		{
			const TArray<FAssetData> AssetsToFind({ FAssetData(ControlConsole) });
			GEditor->SyncBrowserToObjects(AssetsToFind);
		}
	}

	FReply SDMXControlConsoleCompactEditorView::OnShowFullEditorButtonClicked()
	{
		const FDMXControlConsoleEditorModule& ControlConsoleEditorModule = FModuleManager::GetModuleChecked<FDMXControlConsoleEditorModule>(TEXT("DMXControlConsoleEditor"));
		const TSharedPtr<SDockTab> CompactEditorTab = ControlConsoleEditorModule.GetCompactEditorTab();
		const bool bFloatingWindow = CompactEditorTab.IsValid() && CompactEditorTab->GetParentWindow().IsValid();
		if (bFloatingWindow)
		{
			// Close the compact editor tab if it is not docked
			CompactEditorTab->RequestCloseTab();
		}

		UDMXControlConsoleCompactEditorModel* CompactEditorModel = GetMutableDefault<UDMXControlConsoleCompactEditorModel>();
		CompactEditorModel->RestoreFullEditor();

		return FReply::Handled();
	}

	FText SDMXControlConsoleCompactEditorView::GetAssetNameText() const
	{
		if (ControlConsole && ControlConsole->GetPackage())
		{
			FString AssetName = ControlConsole->GetName();
			if (ControlConsole->GetPackage() && ControlConsole->GetPackage()->IsDirty())
			{
				AssetName += TEXT("*");
			}
			return FText::FromString(AssetName);
		}

		return FText::GetEmpty();
	}

	EVisibility SDMXControlConsoleCompactEditorView::GetCueStackViewVisibility() const
	{
		const bool bIsVisible = CueStackCheckBox.IsValid() && CueStackCheckBox->IsChecked();
		return bIsVisible ? EVisibility::Visible : EVisibility::Collapsed;
	}
}

#undef LOCTEXT_NAMESPACE
