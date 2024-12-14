// Copyright Epic Games, Inc. All Rights Reserved.

#include "SubmitToolWidget.h"

#include "ConfirmDialogWidget.h"
#include "ISettingsModule.h"
#include "OutputLogModule.h"
#include "OutputLogSettings.h"
#include "OutputLogCreationParams.h"

#include "View/SubmitToolStyle.h"
#include "View/SubmitToolCommandHandler.h"
#include "View/SubmitToolMenu.h"
#include "View/Widgets/SIntegrationWidget.h"

#include "Widgets/Layout/SExpandableArea.h"
#include "Widgets/Layout/SUniformGridPanel.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "Widgets/Views/SListView.h"
#include "Widgets/Input/SHyperlink.h"
#include "Widgets/Input/SEditableText.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Images/SThrobber.h"
#include "Widgets/Docking/SDockTab.h"

#include "Models/ModelInterface.h"
#include "Models/SubmitToolUserPrefs.h"
#include "TagSectionWidget.h"
#include "ValidatorsWidget.h"

#include "Version/AppVersion.h"

#include "Modules/ModuleManager.h"
#include "Framework/Docking/TabManager.h"
#include "Framework/Application/SlateApplication.h"

#include "CommandLine/CmdLineParameters.h"
#include "SubmitToolUtils.h"
#include "Configuration/Configuration.h"

#define LOCTEXT_NAMESPACE "SubmitToolWidget"

void SubmitToolWidget::OnCLDescriptionUpdated()
{
	DescriptionBox->Refresh();
}

void SubmitToolWidget::HandleApplicationActivationStateChanged(bool bActive)
{
	if(ModelInterface == nullptr)
	{
		return;
	}
	if(ModelInterface->IsP4OperationRunning() || ModelInterface->IsBlockingOperationRunning())
	{
		return;
	}
	
	if(bActive)
	{
		ModelInterface->CheckForFileEdits();
		ModelInterface->UpdateCLFromP4Async();
	}
	else
	{
		ModelInterface->SendDescriptionToP4();
	}
}

void SubmitToolWidget::Construct(const FArguments& InArgs)
{
	ParentTab = InArgs._ParentTab.Get();
	ModelInterface = InArgs._ModelInterface;

	IntegrationWidget = SNew(SIntegrationWidget)
						.ModelInterface(ModelInterface)
						.MainWindow(InArgs._ParentWindow);

	FSlateApplication::Get().OnApplicationActivationStateChanged().AddRaw(this, &SubmitToolWidget::HandleApplicationActivationStateChanged);
	OnValidatorFinishedHandle = ModelInterface->AddSingleValidatorFinishedCallback(FOnSingleTaskFinished::FDelegate::CreateRaw(this, &SubmitToolWidget::OnSingleValidatorFinished));
	OnValidationUpdateHandle = ModelInterface->AddValidationUpdatedCallback(FOnTaskFinished::FDelegate::CreateRaw(this, &SubmitToolWidget::OnValidationUpdated));
	OnCLDescriptionUpdatedHandle = ModelInterface->GetCLDescriptionUpdatedDelegate().Add(FOnCLDescriptionUpdated::FDelegate::CreateRaw(this, &SubmitToolWidget::OnCLDescriptionUpdated));

	TSharedPtr<SVerticalBox> Contents;
	ChildSlot
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
			[
				SAssignNew(Contents, SVerticalBox)
			]
		];

	/**** Tags ****/
	Contents->AddSlot()
		.AutoHeight()
		[
			SNew(STagSectionWidget)
			.ParentWindow(InArgs._ParentWindow)
			.ModelInterface(InArgs._ModelInterface)
		];

	TSharedPtr<SSplitter> Splitter;

	Contents->AddSlot()
		.FillHeight(1.f)
		[
			SAssignNew(Splitter, SSplitter)
			.Orientation(EOrientation::Orient_Vertical)
		];


	TSharedPtr<SHorizontalBox> BottomLine = 
		SNew(SHorizontalBox)
		+SHorizontalBox::Slot()
		.HAlign(HAlign_Left)
		[
			SNew(SEditableText)
			.IsReadOnly(true)
			.Text(FText::FromString(FAppVersion::GetVersion()))
		];

	for(size_t i = 0; i < ModelInterface->GetParameters().GeneralParameters.HelpLinks.Num(); ++i)
	{
		const FDocumentationLink& DocLink = ModelInterface->GetParameters().GeneralParameters.HelpLinks[i];
		SHorizontalBox::FScopedWidgetSlotArguments Slot = BottomLine->AddSlot();
		Slot.AttachWidget(
				SNew(SHyperlink)
					.Style(FSubmitToolStyle::Get(), TEXT("NavigationHyperlink"))
					.Text(FText::FromString(DocLink.Text))
					.ToolTipText(FText::FromString(DocLink.Tooltip))
					.OnNavigate_Lambda([&DocLink]() { FPlatformProcess::LaunchURL(*DocLink.Link, nullptr, nullptr); })
			);

		if(i != ModelInterface->GetParameters().GeneralParameters.HelpLinks.Num() - 1)
		{
			Slot.HAlign(HAlign_Center);
		}
		else
		{
			Slot.HAlign(HAlign_Right);
		}
	}

	/**** Version + feedback ****/
	Contents->AddSlot()
		.AutoHeight()
		[
			BottomLine.ToSharedRef()
		];

	/**** Description + Buttons ****/
	DescriptionBox = SNew(SMultiLineEditableTextBox)
		.Text_Lambda([&ModelInterface = ModelInterface]() { return FText::FromString(ModelInterface->GetCLDescription()); })
		.OnTextChanged_Lambda([&ModelInterface = ModelInterface](const FText& newText) { ModelInterface->SetCLDescription(newText); })
		.OnTextCommitted_Lambda([this](const FText& Text, ETextCommit::Type CommitType) { if(CommitType != ETextCommit::OnEnter) { ModelInterface->ValidateCLDescription(); }})
		.AutoWrapText(true)
		.IsReadOnly(false)
		.IsEnabled_Static(&FModelInterface::GetInputEnabled);

	FString PerforceClientName;
	FCmdLineParameters::Get().GetValue(FSubmitToolCmdLine::P4Client, PerforceClientName);

	P4SectionSlot = Splitter->AddSlot()
		.Resizable(true)
		.MinSize(150)
		[
			SNew(SBox)			
			.WidthOverride(520)
			[
				SNew(SVerticalBox)
				+SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(SBorder)
					.BorderBackgroundColor(FAppStyle::GetColor("ValidatorStateFail"))
					.Visibility_Lambda([this]() {
					if (FDateTime::Now().GetHour() < ModelInterface->GetParameters().GeneralParameters.EarlySubmitHour24 || FDateTime::Now().GetHour() >= ModelInterface->GetParameters().GeneralParameters.LateSubmitHour24)
					{
						return EVisibility::All;
					}
					else
					{
						return EVisibility::Collapsed;
					}
						})
					[
						SNew(STextBlock)
							.Justification(ETextJustify::Center)
							.TextStyle(FAppStyle::Get(), "BoldTextNormalSize")
							.Text_Lambda([]() { return FText::FromString(FString::Printf(TEXT("**** It's %s local time, be mindful of submitting late and going away, please remain alert and available for a reasonable period of time (2-3 hours) in case your changes cause issues ****"), *FDateTime::Now().ToFormattedString(TEXT("%H:%M")))); })
					]
				]
				+SVerticalBox::Slot()
				.AutoHeight()
				.Padding(FMargin(5))
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.HAlign(HAlign_Left)
					.AutoWidth()
					[
						SNew(STextBlock)
						.Font(FAppStyle::Get().GetFontStyle("SmallFontBold"))
						.Text(NSLOCTEXT("SourceControl.SubmitPanel", "ChangeListDesc", "Changelist Description"))
					]
					+ SHorizontalBox::Slot()
					.FillWidth(1.f)
					[
						SNew(SSpacer)
					]

					// STREAM 
					+ SHorizontalBox::Slot()
					.HAlign(HAlign_Right)
					.AutoWidth()
					[
						SNew(STextBlock)
						.Font(FAppStyle::Get().GetFontStyle("SmallFontBold"))
						.Text(NSLOCTEXT("SourceControl.SubmitPanel", "Stream", "Stream"))
					]
					+ SHorizontalBox::Slot()
					.HAlign(HAlign_Right)
					.Padding(5, 0, 0, 0)
					.AutoWidth()
					[
						SNew(STextBlock)
						.Font(FAppStyle::Get().GetFontStyle("SmallFont"))
						.Text_Lambda([this]() { return FText::FromString(ModelInterface->GetCurrentStream()); })
					]

					// WORKSPACE
					+ SHorizontalBox::Slot()
					.HAlign(HAlign_Right)
					.Padding(15, 0, 0, 0)
					.AutoWidth()
					[
						SNew(STextBlock)
						.Font(FAppStyle::Get().GetFontStyle("SmallFontBold"))
						.Text(NSLOCTEXT("SourceControl.SubmitPanel", "Workspace", "Workspace"))
					]
					+ SHorizontalBox::Slot()
					.HAlign(HAlign_Right)
					.Padding(5, 0, 0, 0)
					.AutoWidth()
					[
						SNew(STextBlock)
						.Font(FAppStyle::Get().GetFontStyle("SmallFont"))
						.Text(FText::FromString(PerforceClientName))
					]
				]
				+SVerticalBox::Slot()
				.FillHeight(.2f)
				.Padding(FMargin(5, 0, 5, 5))
				[
					DescriptionBox.ToSharedRef()
				] 
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(FMargin(5, 0, 5, 5))
				[
					BuildButtonRow()
				]
				
			]
		].GetSlot();
	
	P4SectionSlot->SetSizeValue(FSubmitToolUserPrefs::Get()->P4SectionSize);

	TSharedPtr<SWidget> LogSection = BuildOutputLogWidget();
	TSharedPtr<SWidget> FilesInCLSection = BuildFilesInCLWidget();

	// Files & Validators
	ValidatorSectionSlot = Splitter->AddSlot()		
		.MinSize(45)
		.SizeRule(SSplitter::ESizeRule::FractionOfParent)
		.Resizable(true)
		[
			SNew(SBox)
			.Padding(0.f,4.f)
			[
				SNew(SScrollBox)
				.Orientation(EOrientation::Orient_Vertical)
				+SScrollBox::Slot()
				.Padding(FMargin(0.f, 2.f))
				.AutoSize()
				[
					FilesInCLSection.ToSharedRef()
				]
				+SScrollBox::Slot()
				.Padding(FMargin(0.f, 2.f))
				.AutoSize()
				[
					SNew(SValidatorsWidget)
					.OnViewLog_Lambda([this](TSharedPtr<const FValidatorBase> validator) { LogTabManager->DrawAttention(ValidatorLogTab.ToSharedRef()); })
					.ModelInterface(ModelInterface)
				]
			]
		].GetSlot();

	ValidatorSectionSlot->SetSizeValue(FSubmitToolUserPrefs::Get()->ValidatorSectionSize);

	// Log
	LogSectionSlot = Splitter->AddSlot()
		.MinSize(200)
		.SizeRule(SSplitter::ESizeRule::FractionOfParent)
		.Resizable(true)
		[
			SNew(SVerticalBox)
			+SVerticalBox::Slot()
			.Padding(FMargin(0.f, 4.f))
			.FillHeight(1.f)
			[
				LogSection.ToSharedRef()
			]
		].GetSlot();

	LogSectionSlot->SetSizeValue(FSubmitToolUserPrefs::Get()->LogSectionSize);

}

SubmitToolWidget::~SubmitToolWidget()
{
	if(ModelInterface != nullptr)
	{
		FSubmitToolUserPrefs* UserPrefs = FSubmitToolUserPrefs::Get();

		UserPrefs->P4SectionSize = P4SectionSlot->GetSizeValue();
		UserPrefs->ValidatorSectionSize = ValidatorSectionSlot->GetSizeValue();
		UserPrefs->LogSectionSize = LogSectionSlot->GetSizeValue();

		ModelInterface->RemoveValidationFinishedCallback(OnValidatorFinishedHandle);
		ModelInterface->RemoveValidationUpdatedCallback(OnValidationUpdateHandle);
		ModelInterface->GetCLDescriptionUpdatedDelegate().Remove(OnCLDescriptionUpdatedHandle);
		ModelInterface = nullptr;
	}
}

TSharedRef<SHorizontalBox> SubmitToolWidget::BuildButtonRow()
{

	ValidateBtn = SNew(SButton)
		.ToolTipText_Raw(this, &SubmitToolWidget::GetValidateButtonTooltip)
		.IsEnabled_Lambda([&ModelInterface = ModelInterface] { return (FModelInterface::GetInputEnabled() && !ModelInterface->GetFilesInCL().IsEmpty()) || ModelInterface->IsP4OperationRunning(); })
		.ButtonStyle(FAppStyle::Get(), "PrimaryButton")
		.OnClicked(this, &SubmitToolWidget::ValidateClicked)
		[
			SNew(STextBlock)
			.MinDesiredWidth(130)
			.Justification(ETextJustify::Center)
			.Text_Raw(this, &SubmitToolWidget::GetValidateButtonText)
		];

	return SNew(SHorizontalBox)
#if PLATFORM_WINDOWS
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		[
			SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "Button")
				.OnClicked(this, &SubmitToolWidget::CopyAllLogsClicked)
				.IsEnabled_Lambda([this]() { return !ModelInterface->IsValidationRunning(); })
				[
					SNew(STextBlock)
						.MinDesiredWidth(130)
						.Justification(ETextJustify::Center)
						.Text(FText::FromString("Copy All Logs"))
				]
		]
#endif
		+SHorizontalBox::Slot()
		.FillWidth(1.f)
		[
			SNew(SSpacer)
		]
		+ SHorizontalBox::Slot()
		.FillWidth(1.f)
		.HAlign(HAlign_Center)
		.MaxWidth(700)
		.Padding(8, 0)
		.AutoWidth()
		[
			SNew(SThrobber)
			.NumPieces(8)
			.Visibility_Lambda([this]() { return ModelInterface->IsP4OperationRunning() || ModelInterface->IsBlockingOperationRunning() ? EVisibility::Visible : EVisibility::Collapsed; })
		]
		+SHorizontalBox::Slot()
		.FillWidth(1.f)
		.HAlign(HAlign_Right)
		.MaxWidth(700)
		.Padding(4, 0)
		.AutoWidth()
		[
			SNew(SHorizontalBox)
			+SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(SCheckBox)
				.IsChecked_Lambda([this]() { return ModelInterface->bSubmitOnSuccessfulValidation ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
				.OnCheckStateChanged_Lambda([this](ECheckBoxState InNewState) { ModelInterface->bSubmitOnSuccessfulValidation = !ModelInterface->bSubmitOnSuccessfulValidation; })
			]
			+SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "InvisibleButton")
				.IsFocusable(false)	
				.OnClicked_Lambda([this]() { ModelInterface->bSubmitOnSuccessfulValidation = !ModelInterface->bSubmitOnSuccessfulValidation; return FReply::Handled(); })
				[
					SNew(STextBlock)
					.Justification(ETextJustify::Left)
					.Font(FAppStyle::Get().GetFontStyle("SmallFont"))
					.MinDesiredWidth(60)
					.Text(FText::FromString(TEXT("Submit On Successful Validation")))
				]
			]
		]
		+SHorizontalBox::Slot()
		.FillWidth(1.f)
		.HAlign(HAlign_Right)
		.MaxWidth(700)
		.AutoWidth()
		[
			SNew(SHorizontalBox)
			+SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(SCheckBox)
				.IsChecked_Lambda([this]() { return FSubmitToolUserPrefs::Get()->bCloseOnSubmit ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
				.OnCheckStateChanged_Lambda([this](ECheckBoxState InNewState) { FSubmitToolUserPrefs::Get()->bCloseOnSubmit = !FSubmitToolUserPrefs::Get()->bCloseOnSubmit; })
			]
			+SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "InvisibleButton")
				.IsFocusable(false)	
				.OnClicked_Lambda([this]() { FSubmitToolUserPrefs::Get()->bCloseOnSubmit = !FSubmitToolUserPrefs::Get()->bCloseOnSubmit; return FReply::Handled(); })
				[
					SNew(STextBlock)
					.Justification(ETextJustify::Left)
					.Font(FAppStyle::Get().GetFontStyle("SmallFont"))
					.MinDesiredWidth(60)
					.Text(FText::FromString(TEXT("Close on Submit Success")))
				]
			]
		]
		+SHorizontalBox::Slot()
		.FillWidth(1.f)
		.HAlign(HAlign_Right)
		.MaxWidth(700)
		.AutoWidth()
		[
			SNew(SButton)
			.Text(FText::FromString(TEXT("Open Integration Window")))
			.Visibility_Lambda([this] { return ModelInterface->IsIntegrationRequired() && ModelInterface->bIsUserInAllowlist ? EVisibility::Visible : EVisibility::Collapsed; })
			.OnClicked_Lambda([this] { IntegrationWidget->Open(); return FReply::Handled(); })
			.IsEnabled_Lambda([&ModelInterface = ModelInterface]
				{
					if (ModelInterface->IsPreflightRequestInProgress())
					{
						return false;
					}

					return ModelInterface->IsCLValid();
				})
			[
				SNew(STextBlock)
				.MinDesiredWidth(130)
					.Justification(ETextJustify::Center)
					.Text(FText::FromString(TEXT("Open Integration Window")))
			]
		]
		+ SHorizontalBox::Slot()
			.FillWidth(1.f)
			.HAlign(HAlign_Right)
			.MaxWidth(700)
			.AutoWidth()
			[
				SNew(SUniformGridPanel)
				.SlotPadding(FAppStyle::GetMargin("StandardDialog.SlotPadding"))
				.MinDesiredSlotWidth(FAppStyle::GetFloat("StandardDialog.MinDesiredSlotWidth"))
				+ SUniformGridPanel::Slot(0, 0)
				[
					SNew(SButton)
					.IsEnabled_Lambda([&ModelInterface = ModelInterface] 
					{
						if (ModelInterface->IsPreflightRequestInProgress())
						{
							return false;
						}

						if (ModelInterface->GetState() == ESubmitToolAppState::Finished)
						{
							return true;
						}
					
						if (ModelInterface->GetState() == ESubmitToolAppState::WaitingUserInput || (ModelInterface->IsIntegrationRequired() && ModelInterface->bIsUserInAllowlist))
						{
							return ModelInterface->IsCLValid();
						}

						if(ModelInterface->IsIntegrationRequired())
						{
							return ModelInterface->IsCLValid() || (ModelInterface->GetFilesInCL().IsEmpty() && ModelInterface->HasShelvedFiles() && ModelInterface->HasSubmitToolTag());
						}

						return false;
					})
					.OnClicked(this, &SubmitToolWidget::SubmitClicked)
					.ButtonStyle(FAppStyle::Get(), "PrimaryButton")
					[
						SNew(STextBlock)
						.MinDesiredWidth(130)
						.Justification(ETextJustify::Center)
						.Text_Raw(this, &SubmitToolWidget::GetMainButtonText)
					]
				]
			+SUniformGridPanel::Slot(1, 0)
			[
				ValidateBtn.ToSharedRef()
			]
		];
}

TSharedRef<SWidget> SubmitToolWidget::BuildOutputLogWidget()
{
	/*** Output Log Widget ***/
	FOutputLogModule& OutputLogModule = FModuleManager::Get().LoadModuleChecked<FOutputLogModule>("OutputLog");

	// hide the debug console
	IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(TEXT("OutputLogModule.HideConsole"));
	if(ensure(CVar))
	{
		CVar->Set(true);
	}

	// setup OutputLog settings
	UOutputLogSettings* Settings = GetMutableDefault<UOutputLogSettings>();
	if(Settings)
	{
		Settings->CategoryColorizationMode = ELogCategoryColorizationMode::ColorizeWholeLine;
	}

	ISettingsModule* SettingsModule = FModuleManager::GetModulePtr<ISettingsModule>("Settings");
	if(SettingsModule)
	{
		SettingsModule->RegisterSettings("Editor", "General", "Output Log",
			NSLOCTEXT("OutputLog", "OutputLogSettingsName", "Output Log"),
			NSLOCTEXT("OutputLog", "OutputLogSettingsDescription", "Set up preferences for the Output Log appearance and workflow."),
			Settings
		);
	}

	LogTabManager = FGlobalTabmanager::Get()->NewTabManager(ParentTab.Pin().ToSharedRef());
	LogTabManager->SetCanDoDragOperation(false);

	// Menu
	// TODO BC: this is a poor solution to have the menu maintained between the two tabs please revisit
	TSharedRef<FUICommandList> CommandList = MakeShared<FUICommandList>();
	FSubmitToolCommandHandler CommandHandler;
	CommandHandler.AddToCommandList(ModelInterface, CommandList);

	FMenuBarBuilder MenuBarBuilder = FMenuBarBuilder(CommandList);
	MenuBarBuilder.AddPullDownMenu(LOCTEXT("MainMenu", "Main Menu"), LOCTEXT("OpensMainMenu", "Opens Main Menu"), FNewMenuDelegate::CreateStatic(&FSubmitToolMenu::FillMainMenuEntries));
#if !UE_BUILD_SHIPPING
	if(!FPaths::IsStaged())
	{
		MenuBarBuilder.AddPullDownMenu(LOCTEXT("Debug Tools", "Debug"), LOCTEXT("OpensDebugMenu", "Opens Debug Menu"), FNewMenuDelegate::CreateStatic(&FSubmitToolMenu::FillDebugMenuEntries));
	}
#endif

	const TSharedRef<SWidget> MenuWidget = MenuBarBuilder.MakeWidget();
	LogTabManager->SetAllowWindowMenuBar(true);
	LogTabManager->SetMenuMultiBox(MenuBarBuilder.GetMultiBox(), MenuWidget);

	const FName SummaryTabId = "SummaryLogTab";
	const FName ValidatorLogTabId = "ValidatorLogTab";
	const FName PresubmitLogTabId = "PresubmitLogTab";

	// ----------------------------------------------------------------------
	// Summary Log Tab
	// ----------------------------------------------------------------------
	FOutputLogCreationParams SummaryLogCreationParams;

	// Initially only allow Submit Tool Log as the log filter
	SummaryLogCreationParams.AllowAsInitialLogCategory = FAllowLogCategoryCallback::CreateLambda([](const FName LogCategory) {
		return LogCategory == LogValidators.GetCategoryName();
		});

	SummaryLogCreationParams.DefaultCategorySelection.Emplace(LogSubmitTool.GetCategoryName(), true);
	SummaryLogCreationParams.DefaultCategorySelection.Emplace(LogValidatorsResult.GetCategoryName(), true);
	SummaryLogCreationParams.DefaultCategorySelection.Emplace(LogPresubmitResult.GetCategoryName(), true);
	SummaryLogCreationParams.DefaultCategorySelection.Emplace(LogSubmitToolP4.GetCategoryName(), true);

	SummaryLogDockTab = SNew(SDockTab)
		.CanEverClose(false)
		.TabRole(ETabRole::PanelTab)
		.Label(FText::FromString(TEXT("Summary")))
		.OnCanCloseTab(SDockTab::FCanCloseTab::CreateLambda([] { return false; }))
		[
			OutputLogModule.MakeOutputLogWidget(SummaryLogCreationParams)
		];
	
	LogTabManager->RegisterTabSpawner(SummaryTabId, FOnSpawnTab::CreateLambda([this, &OutputLogModule, &SummaryLogCreationParams](const FSpawnTabArgs& SpawnArgs) {	return SummaryLogDockTab.ToSharedRef(); }));

	// ----------------------------------------------------------------------
	// Validators Log Tab
	// ----------------------------------------------------------------------
	FOutputLogCreationParams ValidatorLogCreationParams;
	ValidatorLogCreationParams.AllowAsInitialLogCategory = FAllowLogCategoryCallback::CreateLambda([](const FName LogCategory) {
		return LogCategory == LogValidators.GetCategoryName();
		});

	ValidatorLogCreationParams.DefaultCategorySelection.Emplace(LogValidators.GetCategoryName(), true);

	ValidatorLogTab = SNew(SDockTab)
		.CanEverClose(false)
		.Label(FText::FromString(TEXT("Validators Log")))
		.TabRole(ETabRole::PanelTab)
		.OnCanCloseTab(SDockTab::FCanCloseTab::CreateLambda([] { return false; }))
		[
			OutputLogModule.MakeOutputLogWidget(ValidatorLogCreationParams)
		];

	LogTabManager->RegisterTabSpawner(ValidatorLogTabId, FOnSpawnTab::CreateLambda([&OutputLogModule, this](const FSpawnTabArgs& SpawnArgs) { return ValidatorLogTab.ToSharedRef(); }));

	// ----------------------------------------------------------------------
	// Pre Submit Log Tab
	// ----------------------------------------------------------------------
	FOutputLogCreationParams PresubmitLogCreationParams;
	PresubmitLogCreationParams.AllowAsInitialLogCategory = FAllowLogCategoryCallback::CreateLambda([](const FName LogCategory) {
		return LogCategory == LogPresubmit.GetCategoryName();
		});

	PresubmitLogCreationParams.DefaultCategorySelection.Emplace(LogPresubmit.GetCategoryName(), true);

	PresubmitLogTab = SNew(SDockTab)
		.CanEverClose(false)
		.Label(FText::FromString(TEXT("Presubmit Log")))
		.TabRole(ETabRole::PanelTab)
		.OnCanCloseTab(SDockTab::FCanCloseTab::CreateLambda([] { return false; }))
		[
			OutputLogModule.MakeOutputLogWidget(PresubmitLogCreationParams)
		];

	LogTabManager->RegisterTabSpawner(PresubmitLogTabId, FOnSpawnTab::CreateLambda([&OutputLogModule, this](const FSpawnTabArgs& SpawnArgs) { return PresubmitLogTab.ToSharedRef(); }));


	// ----------------------------------------------------------------------
	// Logs Layout
	// ----------------------------------------------------------------------
	TSharedRef<FTabManager::FLayout> Layout = FTabManager::NewLayout("SubmitToolLogLayout")
		->AddArea
		(
			FTabManager::NewPrimaryArea()
			->SetOrientation(Orient_Horizontal)
			->Split
			(
				FTabManager::NewStack()
				->SetSizeCoefficient(1.0f)
				->SetHideTabWell(true)
				->AddTab(SummaryTabId, ETabState::OpenedTab)
				->AddTab(ValidatorLogTabId, ETabState::OpenedTab)
				->AddTab(PresubmitLogTabId, ETabState::OpenedTab)
				->SetForegroundTab(SummaryTabId)
			)
		);

	return LogTabManager->RestoreFrom(Layout, ParentTab.Pin()->GetParentWindow()).ToSharedRef();
}

TSharedRef<SExpandableArea> SubmitToolWidget::BuildFilesInCLWidget()
{
	TSharedPtr<SListView<FSourceControlStateRef>> FileList = SNew(SListView<FSourceControlStateRef>)
		.SelectionMode(ESelectionMode::Single)
		.ListItemsSource(&ModelInterface->GetFilesInCL())
		.OnGenerateRow_Lambda([this](FSourceControlStateRef InItem, const TSharedRef<STableViewBase>& OwnerTable)
			{
				return SNew(STableRow<FSourceControlStateRef>, OwnerTable)
					.Padding(2.0f)
					[
						SNew(SHorizontalBox)
				
						+ SHorizontalBox::Slot()
						.MaxWidth(24)
						.HAlign(HAlign_Left)
						[
							SNew(SImage)
							.Image(InItem->GetIcon().GetIcon())
						]

						+ SHorizontalBox::Slot()
						.AutoWidth()
						.HAlign(HAlign_Left)
						[
							SNew(STextBlock)
							.Text(FText::FromString(InItem->GetFilename()))
						]
					];

			});

	ModelInterface->FileRefreshedCallback.AddLambda([FileList]() {FileList->RequestListRefresh(); });

	return SNew(SExpandableArea)
		.InitiallyCollapsed(!FSubmitToolUserPrefs::Get()->bExpandFilesInCL)
		.OnAreaExpansionChanged_Lambda([](bool bExpanded) { FSubmitToolUserPrefs::Get()->bExpandFilesInCL = bExpanded; })
		.BorderImage(FAppStyle::Get().GetBrush("Brushes.Header"))
		.BodyBorderImage(FAppStyle::Get().GetBrush("Brushes.Recessed"))
		.HeaderPadding(FMargin(4.0f, 2.0f))
		.Padding(1.0f)
		.MaxHeight(200.0f)
		.AllowAnimatedTransition(true)
		.HeaderContent()
		[
			SNew(STextBlock)
			.Text(NSLOCTEXT("SourceControl.SubmitPanel", "ChangeListFiles", "Files in Changelist"))
		]
		.BodyContent()
		[
			SNew(SBox)
			.Padding(2.5)
			[
				FileList.ToSharedRef()
			]
		];
}

FReply SubmitToolWidget::SubmitClicked()
{
	if(ModelInterface->GetState() == ESubmitToolAppState::Finished)
	{
		ParentTab.Pin()->RequestCloseTab();
	}
	else
	{
		if (ModelInterface->IsIntegrationRequired() && !ModelInterface->bIsUserInAllowlist)
		{
			IntegrationWidget->Open();
			return FReply::Handled();
		}

		if(ModelInterface->IsCLValid())
		{
			if(ModelInterface->HasShelvedFiles())
			{
				EDialogFactoryResult DeleteShelveResult = ShowDeleteShelveDialog();
				if(DeleteShelveResult == EDialogFactoryResult::Confirm)
				{
					ModelInterface->StartSubmitProcess();
				}
			}
			else
			{
				ModelInterface->StartSubmitProcess();
			}
		}
	}

	return FReply::Handled();
}

FReply SubmitToolWidget::ValidateClicked()
{
	if(ModelInterface->IsValidationRunning())
	{
		ModelInterface->CancelValidations();
	}
	else
	{
		if(ModelInterface->IsP4OperationRunning())
		{
			ModelInterface->CancelP4Operations();
		}
		else
		{
			ModelInterface->ValidateChangelist();
		}
	}

	return FReply::Handled();
}

EDialogFactoryResult SubmitToolWidget::ShowDeleteShelveDialog() const
{
	const FText TextTitle = NSLOCTEXT("SourceControl.SubmitWindow", "DeleteShelvedFilesDialogTitle", "Delete shelved files?");
	const FText TextDescription = NSLOCTEXT("SourceControl.SubmitWindow", "DeleteShelvedFilesDialogDescription", "There are shelved files in this changelist. Do you want to delete your shelf?\nIf you do not, the submit will be cancelled.");
	const FText TextCancel = NSLOCTEXT("SourceControl.SubmitWindow", "DeleteShelvedFilesDialogCancel", "Cancel submit");
	const FText TextConfirm = NSLOCTEXT("SourceControl.SubmitWindow", "DeleteShelvedFilesDialogConfirm", "Delete shelve");

	return FDialogFactory::ShowConfirmDialog(TextTitle, TextDescription);
}

FText SubmitToolWidget::GetMainButtonText() const
{
	FText FinalText;

	if(ModelInterface->GetState() == ESubmitToolAppState::Finished)
	{
		FinalText = NSLOCTEXT("SourceControl.SubmitPanel", "CloseButton", "Close");
	}
	else
	{
		FinalText = ModelInterface->IsIntegrationRequired() && !ModelInterface->bIsUserInAllowlist ? NSLOCTEXT("SourceControl.SubmitPanel", "IntegrationButton", "Open Integration Window") : NSLOCTEXT("SourceControl.SubmitPanel", "SubmitButton", "Submit");
	}

	return FinalText;
}

FText SubmitToolWidget::GetValidateButtonTooltip() const
{

	FText FinalText;

	if(ModelInterface->IsValidationRunning())
	{
		FinalText = NSLOCTEXT("SourceControl.SubmitPanel", "CancelValidateButtonTooltip", "Stops the currently running validations.");
	}
	else
	{
		if(ModelInterface->IsP4OperationRunning())
		{
			FinalText = NSLOCTEXT("SourceControl.SubmitPanel", "CancelP4OpButtonTooltip", "Cancels the currently running P4 Operations.");
		}
		else
		{
			FinalText = NSLOCTEXT("SourceControl.SubmitPanel", "ValidateButtonTooltip", "Run all the validators for this changelist.");
		}
	}
	return FinalText;
}

FText SubmitToolWidget::GetValidateButtonText() const
{
	FText FinalText;

	if(ModelInterface->IsValidationRunning())
	{
		FinalText = NSLOCTEXT("SourceControl.SubmitPanel", "CancelValidateButton", "Stop Validations");
	}
	else
	{
		if(ModelInterface->IsP4OperationRunning())
		{
			FinalText = NSLOCTEXT("SourceControl.SubmitPanel", "CancelP4OpButtonText", "Cancel P4 Operations");
		}
		else
		{
			FinalText = NSLOCTEXT("SourceControl.SubmitPanel", "ValidateButtonText", "Validate");
		}
	}
	return FinalText;
}

void SubmitToolWidget::OnSingleValidatorFinished(const FValidatorBase& InValidator)
{
	if(!InValidator.GetHasPassed())
	{
		ValidatorLogTab->FlashTab();
	}
}

void SubmitToolWidget::OnValidationUpdated(bool bValid)
{
	if(bValid)
	{
		ValidateBtn->SetButtonStyle(&FAppStyle::Get().GetWidgetStyle<FButtonStyle>("Button"));
	}
	else
	{
		ValidateBtn->SetButtonStyle(&FAppStyle::Get().GetWidgetStyle<FButtonStyle>("PrimaryButton"));
	}
}

#if PLATFORM_WINDOWS
FReply SubmitToolWidget::CopyAllLogsClicked()
{
	TArray<FString> Files;
	for (const FString& Path : ModelInterface->GetParameters().CopyLogParameters.LogsToCollect)
	{
		Files.Emplace(FPaths::ConvertRelativePathToFull(FConfiguration::Substitute(Path)));
	}

	FSubmitToolUtils::CopyDiagnosticFilesToClipboard(Files);
	return FReply::Handled();
}
#endif


#undef LOCTEXT_NAMESPACE
