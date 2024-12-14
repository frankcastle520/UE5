// Copyright Epic Games, Inc. All Rights Reserved.
#include "SInterchangePipelineConfigurationDialog.h"

#include "InterchangeEditorPipelineDetails.h"

#include "DetailsViewArgs.h"
#include "Dialog/SCustomDialog.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Framework/Commands/UICommandList.h"
#include "Framework/Views/TableViewMetadata.h"
#include "GameFramework/Actor.h"
#include "IDetailsView.h"
#include "IDocumentation.h"
#include "InterchangeCardsPipeline.h"
#include "InterchangeEditorPipelineStyle.h"
#include "InterchangeManager.h"
#include "InterchangePipelineConfigurationBase.h"
#include "InterchangeProjectSettings.h"
#include "InterchangeTranslatorBase.h"
#include "Interfaces/IMainFrameModule.h"
#include "Layout/Visibility.h"
#include "Misc/App.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/ConfigContext.h"
#include "Misc/ScopedSlowTask.h"
#include "Nodes/InterchangeBaseNodeContainer.h"
#include "Nodes/InterchangeSourceNode.h"
#include "PropertyEditorModule.h"
#include "SInterchangeGraphInspectorWindow.h"
#include "SPrimaryButton.h"
#include "Styling/SlateIconFinder.h"
#include "Styling/StyleColors.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Input/STextComboBox.h"
#include "Widgets/Layout/SUniformGridPanel.h"
#include "Widgets/Input/SEditableTextBox.h"

#define LOCTEXT_NAMESPACE "InterchangePipelineConfiguration"

static bool GInterchangeDefaultShowEssentialsView = false;
static FAutoConsoleVariableRef CCvarInterchangeDefaultShowEssentialsView(
	TEXT("Interchange.FeatureFlags.Import.DefaultShowEssentialsView"),
	GInterchangeDefaultShowEssentialsView,
	TEXT("Whether the import dialog starts by default in essential pipeline properties layout."),
	ECVF_Default);

static bool GInterchangeDefaultShowSettings = false;
static FAutoConsoleVariableRef CCvarInterchangeDefaultShowSettings(
	TEXT("Interchange.FeatureFlags.Import.DefaultShowSettingsView"),
	GInterchangeDefaultShowSettings,
	TEXT("Whether the import dialog shows the settings by default. Settings mode is always shown if GInterchangeDefaultHideCardsView is true."),
	ECVF_Default);

static bool GInterchangeDefaultHideCardsView = true;
static FAutoConsoleVariableRef CCvarInterchangeDefaultHideCardsView(
	TEXT("Interchange.FeatureFlags.Import.DefaultHideCardsView"),
	GInterchangeDefaultHideCardsView,
	TEXT("Whether the import dialog should hide the basic cards view."),
	ECVF_Default);

static bool GInterchangeShowConflictWarningsOnCardsView = true;
static FAutoConsoleVariableRef CCvarInterchangeShowConflictWarningOnCardsView(
	TEXT("Interchange.FeatureFlags.Import.ShowConflictWarningsOnCardsView"),
	GInterchangeShowConflictWarningsOnCardsView,
	TEXT("Whether the import conflict warnings will be shown on cards view."),
	ECVF_Default);

constexpr double SplitterSize = 12.0;
constexpr double AdvancedUIRatio = 1.2;

const FName ReimportStackName = TEXT("ReimportPipeline");

void SInterchangePipelineItem::Construct(
	const FArguments& InArgs,
	const TSharedRef<STableViewBase>& OwnerTable,
	TSharedPtr<FInterchangePipelineItemType> InPipelineElement)
{
	LLM_SCOPE_BYNAME(TEXT("Interchange"));

	PipelineElement = InPipelineElement;
	TObjectPtr<UInterchangePipelineBase> PipelineElementPtr = PipelineElement->Pipeline;
	check(PipelineElementPtr.Get());
	FText PipelineName = LOCTEXT("InvalidPipelineName", "Invalid Pipeline");
	if (PipelineElementPtr.Get())
	{
		FString PipelineNameString = PipelineElement->DisplayName;
		if (!PipelineElement->bShowEssentials)
		{
			PipelineNameString += FString::Printf(TEXT(" (%s)"), *PipelineElementPtr->GetClass()->GetName());
		}
		PipelineName = FText::FromString(PipelineNameString);
	}
	
	static const FSlateBrush* ConflictBrush = FAppStyle::GetBrush("Icons.Error");
	const FText ConflictsComboBoxTooltip = LOCTEXT("ConflictsComboBoxTooltip", "If there is some conflict, simply select one to see more details.");
	const FText Conflict_IconTooltip = FText::Format(LOCTEXT("Conflict_IconTooltip", "There are {0} conflicts. See Conflicts section below for details."), PipelineElement->ConflictInfos.Num());

	STableRow<TSharedPtr<FInterchangePipelineItemType>>::Construct(
		STableRow<TSharedPtr<FInterchangePipelineItemType>>::FArguments()
		.Content()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(0.0f, 2.0f, 6.0f, 2.0f)
			[
				SNew(SImage)
				.Image(this, &SInterchangePipelineItem::GetImageItemIcon)
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(0.0f, 2.0f, 6.0f, 2.0f)
			[
				SNew(SImage)
				.ToolTipText(Conflict_IconTooltip)
				.Image(ConflictBrush)
				.Visibility_Lambda([this]()->EVisibility
					{
						return PipelineElement->ConflictInfos.Num() > 0 ? EVisibility::Visible : EVisibility::Collapsed;
					})
				.ColorAndOpacity(this, &SInterchangePipelineItem::GetTextColor)
			]
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.Padding(3.0f, 0.0f)
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(PipelineName)
				.ColorAndOpacity(this, &SInterchangePipelineItem::GetTextColor)
			]
		], OwnerTable);
}

const FSlateBrush* SInterchangePipelineItem::GetImageItemIcon() const
{
	const FSlateBrush* TypeIcon = nullptr;
	FName IconName = "PipelineConfigurationIcon.Pipeline";
	const FSlateIcon SlateIcon = FSlateIconFinder::FindIcon(IconName);
	TypeIcon = SlateIcon.GetOptionalIcon();
	if (!TypeIcon)
	{
		TypeIcon = FSlateIconFinder::FindIconBrushForClass(AActor::StaticClass());
	}
	return TypeIcon;
}

FSlateColor SInterchangePipelineItem::GetTextColor() const
{
	if (PipelineElement->ConflictInfos.Num() > 0)
	{
		return FStyleColors::Warning;
	}
	return FSlateColor::UseForeground();
}

/************************************************************************/
/* SInterchangePipelineConfigurationDialog Implementation                      */
/************************************************************************/

namespace UE::Private
{
	bool ContainStack(const TArray<FInterchangeStackInfo>& PipelineStacks, const FName StackName)
	{
		return PipelineStacks.FindByPredicate([StackName](const FInterchangeStackInfo& StackInfo)
			{
				return (StackInfo.StackName == StackName);
			}) != nullptr;
	}
}

SInterchangePipelineConfigurationDialog::SInterchangePipelineConfigurationDialog()
{
	PipelineConfigurationDetailsView = nullptr;
	OwnerWindow = nullptr;
}

SInterchangePipelineConfigurationDialog::~SInterchangePipelineConfigurationDialog()
{
	if (TSharedPtr<SWindow> OwnerWindowPinned = OwnerWindow.Pin())
	{
		OwnerWindowPinned->GetOnWindowClosedEvent().RemoveAll(this);
	}

	if (PreviewNodeContainer)
	{
		PreviewNodeContainer->ClearFlags(RF_Standalone | RF_Public);
		PreviewNodeContainer->ClearInternalFlags(EInternalObjectFlags::Async);
		PreviewNodeContainer = nullptr;
	}
}

// Pipelines are renamed with the reimport prefix to avoid conflicts with the duplicates of the original pipelines that end up in the same package.
 // As this is the name displayed in the Dialog, conflicts won't matter.
FString SInterchangePipelineConfigurationDialog::GetPipelineDisplayName(const UInterchangePipelineBase* Pipeline)
{

	FString PipelineDisplayName = Pipeline->ScriptedGetPipelineDisplayName();
	if(PipelineDisplayName.IsEmpty())
	{
		PipelineDisplayName = Pipeline->GetName();
	}

	return PipelineDisplayName;
}

void SInterchangePipelineConfigurationDialog::SetEditPipeline(FInterchangePipelineItemType* PipelineItemToEdit)
{
	TArray<UObject*> ObjectsToEdit;
	ObjectsToEdit.Add(!PipelineItemToEdit ? nullptr : PipelineItemToEdit->Pipeline);

	if (PipelineItemToEdit)
	{
		PipelineItemToEdit->ConflictInfos.Reset();
		if (PipelineItemToEdit->ReimportObject)
		{
			PipelineItemToEdit->ConflictInfos = PipelineItemToEdit->Pipeline->GetConflictInfos(PipelineItemToEdit->ReimportObject, PipelineItemToEdit->Container, PipelineItemToEdit->SourceData);
		}
		FInterchangePipelineBaseDetailsCustomization::SetConflictsInfo(PipelineItemToEdit->ConflictInfos);

		//Acquire ExtraInformation from SourceNode and pass it to FInterchangePipelineBaseDetailsCustomization:
		TMap<FString, FString> ExtraInformation;
		const UInterchangeSourceNode* SourceNode = UInterchangeSourceNode::GetUniqueInstance(PipelineItemToEdit->Container);
		if (SourceNode)
		{
			SourceNode->GetExtraInformation(ExtraInformation);
		}
		FInterchangePipelineBaseDetailsCustomization::SetExtraInformation(ExtraInformation);
	}
	PipelineConfigurationDetailsView->SetObjects(ObjectsToEdit);
}

FReply SInterchangePipelineConfigurationDialog::OnEditTranslatorSettings()
{
	FPropertyEditorModule& PropertyEditorModule = FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor");
	FDetailsViewArgs DetailsViewArgs;
	DetailsViewArgs.bAllowSearch = true;
	DetailsViewArgs.bShowPropertyMatrixButton = false;
	DetailsViewArgs.bShowSectionSelector = false;
	DetailsViewArgs.bAllowMultipleTopLevelObjects = true;
	DetailsViewArgs.NameAreaSettings = FDetailsViewArgs::HideNameArea;
	TSharedRef<IDetailsView> TranslatorSettingsDetailsView = PropertyEditorModule.CreateDetailView(DetailsViewArgs);

	bool bIsSettingsModified = false;
	TranslatorSettingsDetailsView->OnFinishedChangingProperties().AddLambda([&bIsSettingsModified](const FPropertyChangedEvent& PropertyChangedEvent)
		{
			bIsSettingsModified = true;
		});
	TranslatorSettingsDetailsView->SetObject(TranslatorSettings);

	TSharedRef<SCustomDialog> OptionsDialog =
		SNew(SCustomDialog)
		.Title(LOCTEXT("OptionsDialogTitle", "Translator Project Settings Editor"))
		.WindowArguments(SWindow::FArguments()
			.IsTopmostWindow(true)
			.MinHeight(300.0f)
			.MinWidth(500)
			.SizingRule(ESizingRule::UserSized))
		.HAlignContent(HAlign_Fill)
		.VAlignContent(VAlign_Fill)
		.HAlignIcon(HAlign_Left)
		.VAlignIcon(VAlign_Top)
		.UseScrollBox(true)
		.Content()
		[
			SNew(SBorder)
			.Padding(FMargin(10.0f, 3.0f))
			.BorderImage(FAppStyle::GetBrush("ToolPanel.DarkGroupBorder"))
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				.HAlign(HAlign_Fill)
				.VAlign(VAlign_Fill)
				.FillHeight(1.0)
				[
					TranslatorSettingsDetailsView
				]
			]
			
		]
		.Buttons(
		{
			SCustomDialog::FButton(LOCTEXT("DialogButtonOk", "Ok"))
		});
	OptionsDialog->ShowModal();

	if (bIsSettingsModified && Translator.IsValid() && TranslatorSettings)
	{
		if (UClass* TranslatorSettingsClass = TranslatorSettings->GetClass())
		{
			//Save the config locally before the translation.
			TranslatorSettings->SaveSettings();

			//Need to Translate the source data
			FScopedSlowTask Progress(2.f, NSLOCTEXT("SInterchangePipelineConfigurationDialog", "TranslatingSourceFile...", "Translating source file..."));
			Progress.MakeDialog();
			Progress.EnterProgressFrame(1.f);
			//Reset the container
			BaseNodeContainer->Reset();

			Translator->Translate(*BaseNodeContainer.Get());

			//Refresh the dialog
			RefreshStack(false);

			Progress.EnterProgressFrame(1.f);
		}
	}

	return FReply::Handled();
}

TSharedRef<SBox> SInterchangePipelineConfigurationDialog::SpawnPipelineConfiguration()
{
	const ISlateStyle* InterchangeEditorPipelineStyle = FSlateStyleRegistry::FindSlateStyle("InterchangeEditorPipelineStyle");

	const FSlateBrush* ImportSourceBorderBrush = nullptr;
	const FButtonStyle* ResetSelectPipelineButtonStyle = nullptr;
	if (InterchangeEditorPipelineStyle)
	{
		ImportSourceBorderBrush = InterchangeEditorPipelineStyle->GetBrush("ImportSource.Dropdown.Border");
		ResetSelectPipelineButtonStyle = &InterchangeEditorPipelineStyle->GetWidgetStyle<FButtonStyle>("ButtonStyle.ResetSelectedPipeline");
	}

	AvailableStacks.Reset();
	TSharedPtr<FString> SelectedStack;
	if (bReimport)
	{
		CurrentStackName = ReimportStackName;
	}
	else
	{
		CurrentStackName = FInterchangeProjectSettingsUtils::GetDefaultPipelineStackName(bSceneImport, *SourceData);
	}

	//In case we do not have a valid stack name use the first stack
	FName FirstStackName = PipelineStacks.Num() > 0 ? PipelineStacks[0].StackName : CurrentStackName;
	if (!UE::Private::ContainStack(PipelineStacks, CurrentStackName))
	{
		CurrentStackName = FirstStackName;
	}

	for(FInterchangeStackInfo& Stack : PipelineStacks)
	{
		TSharedPtr<FString> StackNamePtr = MakeShared<FString>(Stack.StackName.ToString());
		if (CurrentStackName == Stack.StackName)
		{
			for (const TObjectPtr<UInterchangePipelineBase>& DefaultPipeline : Stack.Pipelines)
			{
				check(DefaultPipeline);
				if (UInterchangePipelineBase* GeneratedPipeline = UE::Interchange::GeneratePipelineInstance(DefaultPipeline))
				{
					GeneratedPipeline->TransferAdjustSettings(DefaultPipeline);
					if (GeneratedPipeline->IsFromReimportOrOverride())
					{
						//We save the pipeline settings to allow Reset to Default to work
						GeneratedPipeline->SaveSettings(Stack.StackName);
					}
					else
					{
						constexpr bool bResetPreDialogTrue = true;
						//Load the settings for this pipeline
						GeneratedPipeline->LoadSettings(Stack.StackName, bResetPreDialogTrue);
						GeneratedPipeline->PreDialogCleanup(Stack.StackName);
					}
					GeneratedPipeline->SetShowEssentialsMode(bShowEssentials);
					if (bFilterOptions && BaseNodeContainer.IsValid())
					{
						GeneratedPipeline->FilterPropertiesFromTranslatedData(BaseNodeContainer.Get());
					}
					PipelineListViewItems.Add(MakeShareable(new FInterchangePipelineItemType{ GetPipelineDisplayName(DefaultPipeline), GeneratedPipeline, ReimportObject.Get(), BaseNodeContainer.Get(), SourceData.Get(), bShowEssentials }));
				}
			}
			SelectedStack = StackNamePtr;
		}
		AvailableStacks.Add(StackNamePtr);
	}

	FText PipelineListTooltip = LOCTEXT("PipelineListTooltip", "Select a pipeline you want to edit properties for. The pipeline properties will be recorded and changes will be available in subsequent use of that pipeline");
	PipelinesListView = SNew(SPipelineListViewType)
		.SelectionMode(ESelectionMode::Single)
		.ListItemsSource(&PipelineListViewItems)
		.OnGenerateRow(this, &SInterchangePipelineConfigurationDialog::MakePipelineListRowWidget)
		.OnSelectionChanged(this, &SInterchangePipelineConfigurationDialog::OnPipelineSelectionChanged)
		.ToolTipText(PipelineListTooltip);

	TSharedPtr<STextComboBox> TextComboBoxPtr;
	//Only use a combo box if there is more then one stack
	if (AvailableStacks.Num() > 0)
	{
		FText StackComboBoxTooltip = LOCTEXT("StackComboBoxTooltip", "Selected pipeline stack preset will be used for the current import. See the Interchange project settings to modify the pipeline stacks preset list.");
		TextComboBoxPtr = SNew(STextComboBox)
			.OptionsSource(&AvailableStacks)
			.OnSelectionChanged(this, &SInterchangePipelineConfigurationDialog::OnStackSelectionChanged)
			.ContentPadding(FMargin(0.0f, 2.0f))
			.ToolTipText(StackComboBoxTooltip);
		if (SelectedStack.IsValid())
		{
			TextComboBoxPtr->SetSelectedItem(SelectedStack);
		}
	}

	FText CurrentStackText = LOCTEXT("CurrentStackText", "Stacks Preset");

	TSharedPtr<SWidget> StackTextComboBox;
	if (!TextComboBoxPtr.IsValid())
	{
		CurrentStackText = LOCTEXT("CurrentStackTextNoComboBox", "There is no pipeline stack preset available");
		StackTextComboBox = SNew(SBox)
		[
			SNew(STextBlock)
			.Text(CurrentStackText)
		];
	}
	else
	{
		StackTextComboBox = SNew(SBox)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.VAlign(VAlign_Center)
			.Padding(0.0f, 0.0f, 8.0f, 0.0f)
			.AutoWidth()
			[
				SNew(SBox)
				[
					SNew(STextBlock)
					.Text(CurrentStackText)
				]
			]
			+ SHorizontalBox::Slot()
			.VAlign(VAlign_Center)
			.AutoWidth()
			[
				TextComboBoxPtr.ToSharedRef()
			]
		];
	}

	TSharedPtr<SWidget> StackAndGroupWidget;

	//Groups
	FText GroupUsedText = LOCTEXT("GroupUsedText", "Group Used:");

	FInterchangeGroup::EUsedGroupStatus UsedGroupStatus;
	const FInterchangeGroup& UsedInterchangeGroup = FInterchangeProjectSettingsUtils::GetUsedGroup(UsedGroupStatus);

	switch (UsedGroupStatus)
	{
		case FInterchangeGroup::NotSet:
			StackAndGroupWidget = SNew(SBox)
				[
					SNew(SHorizontalBox)
						+ SHorizontalBox::Slot()
						.VAlign(VAlign_Center)
						.AutoWidth()
						[
							StackTextComboBox.ToSharedRef()
						]
				];
			break;
		case FInterchangeGroup::SetAndValid:
			{
				FText GroupComboBoxTooltip = LOCTEXT("GroupComboBoxTooltip", "Group usage can be set in Editor Preferences > Interchange > Groups.");

				StackAndGroupWidget = SNew(SBox)
					[
						SNew(SHorizontalBox)
							+ SHorizontalBox::Slot()
							.VAlign(VAlign_Center)
							.AutoWidth()
							[
								StackTextComboBox.ToSharedRef()
							]
							+ SHorizontalBox::Slot()
							.VAlign(VAlign_Center)
							.AutoWidth()
							[
								SNew(SBox)
									[
										SNew(SHorizontalBox)
											+ SHorizontalBox::Slot()
											.VAlign(VAlign_Center)
											.Padding(16.0f, 0.0f, 4.0f, 0.0f)
											.AutoWidth()
											[
												SNew(SBox)
													[
														SNew(STextBlock)
															.Text(GroupUsedText)
													]
											]
											+ SHorizontalBox::Slot()
											.VAlign(VAlign_Center)
											.AutoWidth()
											[
												SNew(SEditableTextBox)
													.Text(FText::FromName(UsedInterchangeGroup.DisplayName))
													.IsEnabled(false)
													.ToolTipText(GroupComboBoxTooltip)
											]
									]
							]
					];
			}
			break;
		case FInterchangeGroup::SetAndInvalid:
			{
				//invalid Group usage:
				FText InvalidGroupText = LOCTEXT("InvalidGroupText", "Invalid Group setup for usage!");
				FText InvalidGroupTooltip = LOCTEXT("InvalidGroupTooltip", "Please review Group usage in Editor Preferences > Interchange > Groups.");

				StackAndGroupWidget = SNew(SBox)
					[
						SNew(SHorizontalBox)
							+ SHorizontalBox::Slot()
							.VAlign(VAlign_Center)
							.AutoWidth()
							[
								StackTextComboBox.ToSharedRef()
							]
							+ SHorizontalBox::Slot()
							.VAlign(VAlign_Center)
							.AutoWidth()
							[
								SNew(SHorizontalBox)
									+ SHorizontalBox::Slot()
									.VAlign(VAlign_Center)
									.Padding(16.0f, 0.0f, 0.0f, 0.0f)
									.AutoWidth()
									[
										SNew(SBox)
											[
												SNew(STextBlock)
													.Text(InvalidGroupText)
													.ToolTipText(InvalidGroupTooltip)
											]
									]
							]
					];
			}
			break;
		default:
			break;
	}

	TSharedPtr<SBox> InspectorBox;
	TSharedRef<SBox> PipelineConfigurationPanelBox = SNew(SBox)
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.Padding(0.0f, 8.0f)
			.AutoHeight()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				[
					StackAndGroupWidget.ToSharedRef()
				]
				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				[
					SNullWidget::NullWidget
				]
				+SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(4.0f, 0.0f)
				[
					SNew(SBorder)
					.BorderImage(ImportSourceBorderBrush)
					.Padding(4.0f, 0.0f)
					.ToolTipText(LOCTEXT("SInterchangePipelineConfigurationDialog_ShowEssentialsOptions_tooltip", "Display only essentials pipeline properties."))
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						.Padding(4.f, 0.f)
						[
							SNew(STextBlock)
							.Text(LOCTEXT("SInterchangePipelineConfigurationDialog_ShowEssentialsOptions", "Essentials"))
						]
						+ SHorizontalBox::Slot()
						.Padding(4.f, 0.f)
						[
							SNew(SCheckBox)
							.IsChecked(this, &SInterchangePipelineConfigurationDialog::IsShowEssentialsEnabled)
							.OnCheckStateChanged(this, &SInterchangePipelineConfigurationDialog::OnShowEssentialsChanged)
						]
					]
				]
				+SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(4.0f, 0.0f)
				[
					SNew(SBorder)
					.BorderImage(ImportSourceBorderBrush)
					.Padding(4.0f, 0.0f)
					.ToolTipText(LOCTEXT("SInterchangePipelineConfigurationDialog_FilterPipelineOptions_tooltip", "Filter the pipeline options using the source content data."))
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						.Padding(4.f, 0.f)
						[
							SNew(STextBlock)
							.Text(LOCTEXT("SInterchangePipelineConfigurationDialog_FilterPipelineOptions", "Filter on Contents"))
						]
						+ SHorizontalBox::Slot()
						.Padding(4.f, 0.f)
						[
							SNew(SCheckBox)
							.IsChecked(this, &SInterchangePipelineConfigurationDialog::IsFilteringOptions)
							.OnCheckStateChanged(this, &SInterchangePipelineConfigurationDialog::OnFilterOptionsChanged)
						]
					]
				]
				+SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(4.0f, 0.0f, 0.0f, 0.0f)
				[
					SNew(SButton)
					.ButtonStyle(ResetSelectPipelineButtonStyle)
					.IsEnabled_Lambda([this]()
						{
							return PipelinesListView->GetNumItemsSelected() == 1;
						})
					.Text(LOCTEXT("SInterchangePipelineConfigurationDialog_ResetToPipelineAsset", "Reset Selected Pipeline"))
					.ToolTipText_Lambda([bReimportClosure = bReimport]()
						{
							if (bReimportClosure)
							{
								return LOCTEXT("SInterchangePipelineConfigurationDialog_ResetToPipelineAsset_TooltipReimport", "Reset the selected pipeline to the values used the last time this asset was imported.");
							}
							else
							{
								return LOCTEXT("SInterchangePipelineConfigurationDialog_ResetToPipelineAsset_Tooltip", "Reset the properties of the selected pipeline.");
							}
						})
					.OnClicked(this, &SInterchangePipelineConfigurationDialog::OnResetToDefault)
				]
			]
			+ SVerticalBox::Slot()
			.Padding(0.0f, 8.0f)
			.AutoHeight()
			[
				SNew(SBox)
				.MinDesiredHeight(50)
				.MaxDesiredHeight(140)
				[
					PipelinesListView.ToSharedRef()
				]
			]
		]
		+ SVerticalBox::Slot()
		.FillHeight(1.0f)
		[
			SAssignNew(InspectorBox, SBox)
		]
	];

	FPropertyEditorModule& PropertyEditorModule = FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor");
	FDetailsViewArgs DetailsViewArgs;
	DetailsViewArgs.bAllowSearch = true;
	DetailsViewArgs.bShowPropertyMatrixButton = false;
	DetailsViewArgs.bShowSectionSelector = true;
	DetailsViewArgs.bAllowMultipleTopLevelObjects = true;
	DetailsViewArgs.NameAreaSettings = FDetailsViewArgs::HideNameArea;
	PipelineConfigurationDetailsView = PropertyEditorModule.CreateDetailView(DetailsViewArgs);
	InspectorBox->SetContent(PipelineConfigurationDetailsView->AsShared());
	SetEditPipeline(nullptr);
	PipelineConfigurationDetailsView->GetIsPropertyVisibleDelegate().BindLambda([this](const FPropertyAndParent& PropertyAndParent)
		{
			return IsPropertyVisible(PropertyAndParent);
		});
	PipelineConfigurationDetailsView->OnFinishedChangingProperties().AddLambda([this](const FPropertyChangedEvent& PropertyChangedEvent)
	{
		if (CurrentSelectedPipeline && CurrentSelectedPipeline->IsPropertyChangeNeedRefresh(PropertyChangedEvent))
		{
			//Refresh the pipeline
			constexpr bool bStackSelectionChange = false;
			RefreshStack(bStackSelectionChange);
		}
	});
	return PipelineConfigurationPanelBox;
}

TSharedRef<SBox> SInterchangePipelineConfigurationDialog::SpawnCardsConfiguration()
{
	const FSlateBrush* AdvanceSettingsIcon = FSlateIconFinder::FindIcon("PipelineConfigurationIcon.SidePanelRight").GetOptionalIcon();

	CreateCardsViewList();

	TSharedRef<SWidget> BodyWidget = CardViewList.IsValid() ? CardViewList.ToSharedRef() : SNullWidget::NullWidget;
	TSharedRef<SBox> CardsConfigurationPanelBox = SNew(SBox)
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.HAlign(HAlign_Fill)
		.Padding(0.0f, 8.0f)
		.AutoHeight()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.VAlign(VAlign_Center)
			.HAlign(HAlign_Left)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("SInterchangePipelineConfigurationDialog_AssetFoundText", "Assets Found In Source:"))
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(SCheckBox)
				.Padding(FMargin(0.f, 4.f))
				.Style(&FAppStyle::Get().GetWidgetStyle<FCheckBoxStyle>("ToggleButtonCheckBox"))
				.Type(ESlateCheckBoxType::ToggleButton)
				.IsChecked_Lambda([this]() { return bShowSettings ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
				.OnCheckStateChanged_Lambda([this](ECheckBoxState CheckState)
					{
						bShowSettings = CheckState == ECheckBoxState::Checked;
						TSharedPtr<SWindow> DialogWindow = OwnerWindow.Pin();
							
						//Resize the window
						FVector2D ClientSize = DialogWindow->GetClientSizeInScreen();
						double MaxResize = 1500.0;
						FWindowSizeLimits SizeLimits = DialogWindow->GetSizeLimits();
						double MinimumWidth = OriginalMinWindowSize;
						double NewSize = 0.0;
						if (bShowSettings)
						{
							MinimumWidth = OriginalMinWindowSize + (OriginalMinWindowSize * AdvancedUIRatio) + SplitterSize;
							//Increase the window size in X
							double SizeRatio = (SplitAdvancedRatio / (1.0f - SplitAdvancedRatio));
							//Add the splitter to the max size
							NewSize = ClientSize.X + FMath::Min(MaxResize, (ClientSize.X * SizeRatio)) + SplitterSize;
							ClientSize.X = FMath::CeilToDouble(NewSize);
						}
						else
						{
							MinimumWidth = OriginalMinWindowSize;
							//Half the window size in X
							double SizeRatio = (1.0f - SplitAdvancedRatio);
							NewSize = (ClientSize.X - SplitterSize) * SizeRatio;
							ClientSize.X = FMath::Floor(NewSize);
						}

						SizeLimits.SetMinWidth(static_cast<float>(MinimumWidth));
						DialogWindow->SetSizeLimits(SizeLimits);
						DialogWindow->Resize(ClientSize);

						constexpr bool bStackSelectionChange = false;
						RefreshStack(bStackSelectionChange);
						GConfig->SetBool(TEXT("InterchangeImportDialogOptions"), TEXT("ShowSettings"), bShowSettings, GEditorPerProjectIni);
					})
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(0.0f, 0.0f, 8.0f, 0.0f)
					[
						SNew(SImage)
						.Image(AdvanceSettingsIcon)
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(0.0f, 0.0f, 8.0f, 0.0f)
					[
						SNew(STextBlock)
						.Text(LOCTEXT("SInterchangePipelineConfigurationDialog_AdvanceSettingsButtonText", "Advanced Settings"))
					]
				]
			]
		]
		+ SVerticalBox::Slot()
		.Padding(0.0f, 8.0f)
		.FillHeight(1.0f)
		[
			SNew(SScrollBox)
			+ SScrollBox::Slot()
			[
				BodyWidget
			]
		]
	];
	return CardsConfigurationPanelBox;
}

void SInterchangePipelineConfigurationDialog::Construct(const FArguments& InArgs)
{
	LLM_SCOPE_BYNAME(TEXT("Interchange"));
	//Make sure there is a valid default value

	OwnerWindow = InArgs._OwnerWindow;
	SourceData = InArgs._SourceData;
	bSceneImport = InArgs._bSceneImport;
	bReimport = InArgs._bReimport;
	PipelineStacks = InArgs._PipelineStacks;
	OutPipelines = InArgs._OutPipelines;
	BaseNodeContainer = InArgs._BaseNodeContainer;
	ReimportObject = InArgs._ReimportObject;
	SourceData = InArgs._SourceData;
	Translator = InArgs._Translator;
	if (Translator.IsValid())
	{
		TranslatorSettings = Translator->GetSettings();
	}

	if (ReimportObject.IsValid())
	{
		ensure(bReimport);
	}
	
	FText ReuseSettingsTooltipText = LOCTEXT("InspectorGraphWindow_ReuseSettingsTooltipText", "When importing multiple files this checkbox allow users to use the same settings for source of the same extension.");
	const bool bTranslatorThreadSafe = Translator.IsValid() ? Translator->IsThreadSafe() : false;
	if (!bTranslatorThreadSafe)
	{
		FString Extension = SourceData.IsValid() ? FPaths::GetExtension(SourceData->GetFilename()) : TEXT("N/A");
		ReuseSettingsTooltipText = FText::Format(LOCTEXT("InspectorGraphWindow_ReuseSettingsNotThreadSafeTooltipText", "{0} translator is not thread safe and must use the same settings for subsequent files"), FText::FromString(Extension));
	}

	check(OutPipelines);

	check(OwnerWindow.IsValid());
	TSharedPtr<SWindow> OwnerWindowPinned = OwnerWindow.Pin();
	if (OwnerWindowPinned.IsValid())
	{
		OwnerWindowPinned->GetOnWindowClosedEvent().AddRaw(this, &SInterchangePipelineConfigurationDialog::OnWindowClosed);
		OriginalMinWindowSize = OwnerWindowPinned->GetSizeLimits().GetMinWidth().Get(0.0);
		if (OriginalMinWindowSize < 1.0)
		{
			OriginalMinWindowSize = 350.0;
		}
		DeltaClientWindowSize = (OwnerWindowPinned->GetSizeInScreen() - OwnerWindowPinned->GetClientSizeInScreen()).X;
	}

	//Get the default layout when the user open the import dialog for the first time.
	bShowEssentials = GInterchangeDefaultShowEssentialsView;
	bShowCards = !GInterchangeDefaultHideCardsView;

	bShowSettings = GInterchangeDefaultShowSettings || !bShowCards;

	if (bReimport)
	{
		bFilterOptions = false;
	}
	
	if(GConfig->DoesSectionExist(TEXT("InterchangeImportDialogOptions"), GEditorPerProjectIni))
	{
		if (!bReimport)
		{
			GConfig->GetBool(TEXT("InterchangeImportDialogOptions"), TEXT("FilterOptions"), bFilterOptions, GEditorPerProjectIni);
		}
		GConfig->GetBool(TEXT("InterchangeImportDialogOptions"), TEXT("ShowEssentials"), bShowEssentials, GEditorPerProjectIni);
		GConfig->GetBool(TEXT("InterchangeImportDialogOptions"), TEXT("ShowSettings"), bShowSettings, GEditorPerProjectIni);
		//Make sure settings are shown if we hide cards
		if(!bShowCards)
		{
			bShowSettings = true;
		}
		GConfig->GetDouble(TEXT("InterchangeImportDialogOptions"), TEXT("SplitAdvancedRatio"), SplitAdvancedRatio, GEditorPerProjectIni);
	}

	//Make sure the windows is width enough to show all the ui part (cards and settings)
	if (OwnerWindowPinned.IsValid())
	{
		FVector2D WidowsClientSize = OwnerWindowPinned->GetClientSizeInScreen();
		FWindowSizeLimits SizeLimits = OwnerWindowPinned->GetSizeLimits();
		double MinimumSizeX = 0.0;
		if (bShowCards)
		{
			//Add show cards width
			MinimumSizeX += OriginalMinWindowSize;
		}
		if (bShowSettings)
		{
			//Add settings width
			MinimumSizeX += (OriginalMinWindowSize * AdvancedUIRatio);
			
			//If we have both we have to add the splitter width
			if (bShowCards)
			{
				MinimumSizeX += SplitterSize;
			}
		}
		SizeLimits.SetMinWidth(static_cast<float>(MinimumSizeX));
		OwnerWindowPinned->SetSizeLimits(SizeLimits);
		//Resize the window to respect the limits
		OwnerWindowPinned->Resize(WidowsClientSize);
	}

	//SpawnPipelineConfiguration must always be call because it create the pipeline list from the project settings
	TSharedRef<SBox> MainBodyAdvanced = SpawnPipelineConfiguration();
	UpdatePipelineSupportedAssetClasses();
	TSharedRef<SBox> MainBodyCardsConfiguration = SpawnCardsConfiguration();

	const FSlateBrush* TranslatorSettingsIcon = FSlateIconFinder::FindIcon("PipelineConfigurationIcon.TranslatorSettings").GetOptionalIcon();

	const ISlateStyle* InterchangeEditorPipelineStyle = FSlateStyleRegistry::FindSlateStyle("InterchangeEditorPipelineStyle");

	const FSlateBrush* ImportSourceBorderBrush = nullptr;
	if (InterchangeEditorPipelineStyle)
	{
		ImportSourceBorderBrush = InterchangeEditorPipelineStyle->GetBrush("ImportSource.Dropdown.Border");
	}

	this->ChildSlot
	[
		SNew(SBorder)
		.Padding(FMargin(16.0f, 16.0f))
		.BorderImage(FAppStyle::GetBrush("ToolPanel.DarkGroupBorder"))
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.AutoHeight()
			.HAlign(HAlign_Fill)
			.Padding(0.0f, 0.0f, 0.0f, 8.0f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(8.0f, 0.0f, 8.0f, 0.0f)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("SInterchangePipelineConfigurationDialog_SourceLabel", "Import Source"))
				]
				+SHorizontalBox::Slot()
				.FillWidth(1.0f)
				.VAlign(VAlign_Center)
				[
					SNew(SBorder)
					.BorderImage(ImportSourceBorderBrush)
					.Padding(0.0f, 4.0f)
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot()
						.FillWidth(1.0f)
						.VAlign(VAlign_Center)
						.Padding(8.0f, 0.0f, 0.0f, 0.0f)
						[
							SNew(STextBlock)
							.Text(this, &SInterchangePipelineConfigurationDialog::GetSourceDescription)
						]
						+ SHorizontalBox::Slot()
						.AutoWidth()
						[
							SNew(SSeparator)
							.Orientation(EOrientation::Orient_Vertical)
							.Thickness(1.0f)
						]
						+SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						[
							SNew(SButton)
							.ButtonStyle(&FAppStyle::Get().GetWidgetStyle<FButtonStyle>("NoBorder"))
							.ContentPadding(FMargin(0.0f))
							.Visibility_Lambda([this]()
								{
									return !TranslatorSettings ? EVisibility::Collapsed : EVisibility::Visible;
								})
							.ToolTipText(LOCTEXT("SInterchangePipelineConfigurationDialog_TranslatorSettings_Tooltip", "Edit translator project settings."))
							.OnClicked(this, &SInterchangePipelineConfigurationDialog::OnEditTranslatorSettings)
							[
								SNew(SImage)
								.Image(TranslatorSettingsIcon)
							]
						]
					]
				]
			]
			+ SVerticalBox::Slot()
			.FillHeight(1.0f)
			[
				SAssignNew(CardsAndAdvancedSplitter, SSplitter)
				.OnSplitterFinishedResizing_Lambda([this]()
					{
						SplitAdvancedRatio = (1.0 - static_cast<double>(CardsAndAdvancedSplitter->SlotAt(0).GetSizeValue()));
						GConfig->SetDouble(TEXT("InterchangeImportDialogOptions"), TEXT("SplitAdvancedRatio"), SplitAdvancedRatio, GEditorPerProjectIni);
					})
				+ SSplitter::Slot()
				.MinSize(static_cast<float>(OriginalMinWindowSize - DeltaClientWindowSize))
				.Value(1.0f - static_cast<float>(SplitAdvancedRatio))
				[
					SNew(SBox)
					.Visibility_Lambda([this]()
						{
							return bShowCards ? EVisibility::Visible : EVisibility::Collapsed;
						})
					.Padding_Lambda([this]()
						{
							return (bShowSettings && bShowCards) ? FMargin(0.0f, 0.0f, 20.0f, 0.0f) : FMargin(0.0f, 0.0f, 0.0f, 0.0f);
						})
					[
						MainBodyCardsConfiguration
					]
				]
				+ SSplitter::Slot()
				.MinSize(static_cast<float>((OriginalMinWindowSize * AdvancedUIRatio) - DeltaClientWindowSize))
				.Value(static_cast<float>(SplitAdvancedRatio))
				[
					SNew(SBox)
					.Visibility_Lambda([this]()
						{
							return bShowSettings ? EVisibility::Visible : EVisibility::Collapsed;
						})
					.Padding_Lambda([this]()
						{
							return bShowCards ? FMargin(8.0f, 0.0f, 0.0f, 0.0f) : FMargin(0.0f, 0.0f, 0.0f, 0.0f);
						})
					.Padding(8.0f, 0.0f, 0.0f, 0.0f)
					.Clipping(EWidgetClipping::ClipToBounds)
					[
						MainBodyAdvanced
					]
				]
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 8.0f, 0.0f, 0.0f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.Padding(4.f, 0.f)
				.AutoWidth()
				[
					IDocumentation::Get()->CreateAnchor(FString("interchange-framework-in-unreal-engine"))
				]
				+ SHorizontalBox::Slot()
				.Padding(4.f, 0.f)
				.AutoWidth()
				[
					SNew(SHorizontalBox)
					.ToolTipText(LOCTEXT("InspectorGraphWindow_ReuseSettingsToolTip", "When importing multiple files, keep the same import settings for every file or open the settings dialog for each file."))
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.HAlign(HAlign_Right)
					.VAlign(VAlign_Center)
					.Padding(4.f, 0.f)
					[
						SNew(STextBlock)
						.IsEnabled(bTranslatorThreadSafe)
						.Text(LOCTEXT("InspectorGraphWindow_ReuseSettings", "Use the same settings for subsequent files"))
						.ToolTipText(ReuseSettingsTooltipText)
					]
					+ SHorizontalBox::Slot()
					.Padding(4.f, 0.f)
					[
						SAssignNew(UseSameSettingsForAllCheckBox, SCheckBox)
						.IsChecked(true)
						.IsEnabled_Lambda([this, bTranslatorThreadSafe]()
							{
								if (!bTranslatorThreadSafe)
								{
									return false;
								}
								return IsImportButtonEnabled();
							})
					]
				]
				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				[
					SNullWidget::NullWidget
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(4.f, 0.f)
				[
					SNew(SButton)
					.Visibility_Lambda([this]()
						{
							return bShowSettings ? EVisibility::Visible : EVisibility::Collapsed;
						})
					.HAlign(HAlign_Center)
					.Text(LOCTEXT("InspectorGraphWindow_Preview", "Preview..."))
					.OnClicked(this, &SInterchangePipelineConfigurationDialog::OnPreviewImport)
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(4.f, 0.f) 
				[
					SNew(SPrimaryButton)
					.Icon(this, &SInterchangePipelineConfigurationDialog::GetImportButtonIcon)
					.Text(LOCTEXT("InspectorGraphWindow_Import", "Import"))
					.ToolTipText(this, &SInterchangePipelineConfigurationDialog::GetImportButtonTooltip)
					.IsEnabled(this, &SInterchangePipelineConfigurationDialog::IsImportButtonEnabled)
					.OnClicked(this, &SInterchangePipelineConfigurationDialog::OnCloseDialog, ECloseEventType::Import)
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(4.f, 0.f) 
				[
					SNew(SButton)
					.HAlign(HAlign_Center)
					.Text(LOCTEXT("InspectorGraphWindow_Cancel", "Cancel"))
					.OnClicked(this, &SInterchangePipelineConfigurationDialog::OnCloseDialog, ECloseEventType::Cancel)
				]
			]
		]
	];

	//Select the first pipeline
	if (PipelineListViewItems.Num() > 0)
	{
		bool bSelectFirst = true;
		FString LastPipelineName;
		FString KeyName = CurrentStackName.ToString() + TEXT("_LastSelectedPipeline");
		if (GConfig->GetString(TEXT("InterchangeSelectPipeline"), *KeyName, LastPipelineName, GEditorPerProjectIni))
		{
			for (TSharedPtr<FInterchangePipelineItemType> PipelineItem: PipelineListViewItems)
			{
				FString PipelineItemName = PipelineItem->Pipeline->GetClass()->GetName();
				if (PipelineItemName.Equals(LastPipelineName))
				{
					PipelinesListView->SetSelection(PipelineItem, ESelectInfo::Direct);
					bSelectFirst = false;
					break;
				}
			}
		}
		if (bSelectFirst)
		{
			PipelinesListView->SetSelection(PipelineListViewItems[0], ESelectInfo::Direct);
		}

		if (GInterchangeShowConflictWarningsOnCardsView)
		{
			RefreshCardsViewList();
		}
	}
}

bool SInterchangePipelineConfigurationDialog::IsPropertyVisible(const FPropertyAndParent& PropertyAndParent) const
{
	if (bReimport)
	{
		const FName ReimportRestrictKey(TEXT("ReimportRestrict"));
		return !(PropertyAndParent.Property.GetBoolMetaData(ReimportRestrictKey));
	}
	return true;
}

const FSlateBrush* SInterchangePipelineConfigurationDialog::GetImportButtonIcon() const
{
	const FSlateBrush* TypeIcon = nullptr;
	if (bShowSettings || GInterchangeShowConflictWarningsOnCardsView)
	{
		for (TSharedPtr<FInterchangePipelineItemType> PipelineItem : PipelineListViewItems)
		{
			if (PipelineItem.IsValid() && PipelineItem->Pipeline)
			{
				if (PipelineItem->ConflictInfos.Num() > 0)
				{
					const FSlateIcon SlateIcon = FSlateIconFinder::FindIcon("Icons.Warning");
					return SlateIcon.GetOptionalIcon();
				}
			}
		}
	}
	return TypeIcon;
}


FText SInterchangePipelineConfigurationDialog::GetSourceDescription() const
{
	FText ActionDescription;
	if (SourceData.IsValid())
	{
		ActionDescription = FText::FromString(SourceData->GetFilename());
	}
	return ActionDescription;
}

FReply SInterchangePipelineConfigurationDialog::OnResetToDefault()
{
	FReply Result = FReply::Handled();
	TArray<TWeakObjectPtr<UObject>> SelectedPipelines = PipelineConfigurationDetailsView->GetSelectedObjects();
	if (CurrentStackName == NAME_None)
	{
		return Result;
	}

	FInterchangePipelineItemType* PipelineToEdit = nullptr;

	//Multi selection is not allowed
	for(TWeakObjectPtr<UObject> WeakObject : SelectedPipelines)
	{
		//We test the cast because we can have null or other type selected (i.e. translator settings class default object).
		if (UInterchangePipelineBase* Pipeline = Cast<UInterchangePipelineBase>(WeakObject.Get()))
		{
			const UClass* PipelineClass = Pipeline->GetClass();

			for (FInterchangeStackInfo& Stack : PipelineStacks)
			{
				if (Stack.StackName != CurrentStackName)
				{
					continue;
				}
				for (const TObjectPtr<UInterchangePipelineBase>& DefaultPipeline : Stack.Pipelines)
				{
					//We assume the pipelines inside one stack are all different classes, we use the class to know which default asset we need to duplicate
					if (DefaultPipeline->GetClass() == PipelineClass)
					{
						for(int32 PipelineIndex = 0; PipelineIndex < PipelineListViewItems.Num(); ++PipelineIndex)
						{
							TObjectPtr<UInterchangePipelineBase> PipelineElement = PipelineListViewItems[PipelineIndex]->Pipeline;
							if (PipelineElement.Get() == Pipeline)
							{
								if (UInterchangePipelineBase* GeneratedPipeline = UE::Interchange::GeneratePipelineInstance(DefaultPipeline))
								{
									GeneratedPipeline->TransferAdjustSettings(DefaultPipeline);
									GeneratedPipeline->SetShowEssentialsMode(bShowEssentials);
									if(bFilterOptions && BaseNodeContainer.IsValid())
									{
										GeneratedPipeline->FilterPropertiesFromTranslatedData(BaseNodeContainer.Get());
									}
									//Switch the pipeline the element point on
									PipelineListViewItems[PipelineIndex]->Pipeline = GeneratedPipeline;
									PipelineToEdit = PipelineListViewItems[PipelineIndex].Get();
									PipelinesListView->SetSelection(PipelineListViewItems[PipelineIndex], ESelectInfo::Direct);
									PipelinesListView->RequestListRefresh();
									break;
								}
							}
						}
					}
				}
			}
		}
	}
	SetEditPipeline(PipelineToEdit);

	//Update the cards
	RefreshCardsViewList();

	return Result;
}

bool SInterchangePipelineConfigurationDialog::ValidateAllPipelineSettings(TOptional<FText>& OutInvalidReason) const
{
	for (TSharedPtr<FInterchangePipelineItemType> PipelineElement : PipelineListViewItems)
	{
		check(PipelineElement->Pipeline);
		if (!PipelineElement->Pipeline->IsSettingsAreValid(OutInvalidReason))
		{
			return false;
		}
	}
	return true;
}

bool SInterchangePipelineConfigurationDialog::IsImportButtonEnabled() const
{
	TOptional<FText> InvalidReason;
	return ValidateAllPipelineSettings(InvalidReason);
}

FText SInterchangePipelineConfigurationDialog::GetImportButtonTooltip() const
{
	//Pipeline validation
	TOptional<FText> InvalidReason;
	if (!ValidateAllPipelineSettings(InvalidReason) && InvalidReason.IsSet())
	{
		return InvalidReason.GetValue();
	}

	//Pipeline conflicts
	for (TSharedPtr<FInterchangePipelineItemType> PipelineItem : PipelineListViewItems)
	{
		if (PipelineItem.IsValid() && PipelineItem->Pipeline)
		{
			if (PipelineItem->ConflictInfos.Num() > 0)
			{
				return LOCTEXT("ImportButtonConflictTooltip", "There is one or more pipeline conflicts, look at any conflict in the pipeline list to have more detail.");
			}
		}
	}

	//Default tooltip
	return LOCTEXT("ImportButtonDefaultTooltip", "Selected pipeline stack will be used for the current import");
}

void SInterchangePipelineConfigurationDialog::SaveAllPipelineSettings() const
{
	for (TSharedPtr<FInterchangePipelineItemType> PipelineElement : PipelineListViewItems)
	{
		if (PipelineElement->Pipeline)
		{
			PipelineElement->Pipeline->SaveSettings(CurrentStackName);
		}
	}
}

void SInterchangePipelineConfigurationDialog::ClosePipelineConfiguration(const ECloseEventType CloseEventType)
{
	if (CloseEventType == ECloseEventType::Cancel || CloseEventType == ECloseEventType::WindowClosing)
	{
		bCanceled = true;
		bImportAll = false;
	}
	else //ECloseEventType::Import
	{
		bCanceled = false;
		bImportAll = UseSameSettingsForAllCheckBox->IsChecked();
		
		//Fill the OutPipelines array
		for (TSharedPtr<FInterchangePipelineItemType> PipelineElement : PipelineListViewItems)
		{
			OutPipelines->Add(PipelineElement->Pipeline);
		}
		if (UInterchangeCardsPipeline* InterchangeCardsPipeline = GenerateTransientCardsPipeline())
		{
			//Add the cards pipeline if valid
			OutPipelines->Add(InterchangeCardsPipeline);
		}
	}

	//Save the settings only if its not a re-import
	if (!bReimport)
	{
		SaveAllPipelineSettings();
	}

	PipelineConfigurationDetailsView = nullptr;

	if (CloseEventType != ECloseEventType::WindowClosing)
	{
		if (TSharedPtr<SWindow> OwnerWindowPin = OwnerWindow.Pin())
		{
			OwnerWindowPin->GetOnWindowClosedEvent().RemoveAll(this);
			OwnerWindowPin->RequestDestroyWindow();
		}
	}
	OwnerWindow = nullptr;
}

FReply SInterchangePipelineConfigurationDialog::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	if (InKeyEvent.GetKey() == EKeys::Escape)
	{
		return OnCloseDialog(ECloseEventType::Cancel);
	}
	return FReply::Unhandled();
}

void SInterchangePipelineConfigurationDialog::RefreshStack(bool bStackSelectionChange)
{
	LLM_SCOPE_BYNAME(TEXT("Interchange"));

	//Save current stack settings, we want the same settings when we will go back to the same stack
	//When doing a reimport we do not want to save the setting because the context have special default
	//value for some options like: (Import Materials, Import Textures...).
	//So when doing a reimport switching stack is like doing a reset to default on all pipelines
	if (!bReimport || !bStackSelectionChange)
	{
		SaveAllPipelineSettings();
	}

	int32 CurrentPipelineIndex = 0;
	if (!bStackSelectionChange)
	{
		//store the selected pipeline
		for (int32 PipelineIndex = 0; PipelineIndex < PipelineListViewItems.Num(); ++PipelineIndex)
		{
			const TSharedPtr<FInterchangePipelineItemType> PipelineItem = PipelineListViewItems[PipelineIndex];
			if (PipelineItem->Pipeline == CurrentSelectedPipeline)
			{
				CurrentPipelineIndex = PipelineIndex;
				break;
			}
		}
	}

	//Rebuild the Pipeline list item
	PipelineListViewItems.Reset();

	for (FInterchangeStackInfo& Stack : PipelineStacks)
	{
		TSharedPtr<FString> StackNamePtr = MakeShared<FString>(Stack.StackName.ToString());
		if (CurrentStackName != Stack.StackName)
		{
			continue;
		}
		for (const TObjectPtr<UInterchangePipelineBase>& DefaultPipeline : Stack.Pipelines)
		{
			check(DefaultPipeline);
			if (UInterchangePipelineBase* GeneratedPipeline = UE::Interchange::GeneratePipelineInstance(DefaultPipeline))
			{
				GeneratedPipeline->TransferAdjustSettings(DefaultPipeline);
				if (!GeneratedPipeline->IsFromReimportOrOverride() || !bStackSelectionChange)
				{
					//Load the settings for this pipeline
					GeneratedPipeline->LoadSettings(Stack.StackName, bStackSelectionChange);
					if (bStackSelectionChange)
					{
						//Do not reset pipeline value if we are just refreshing the filtering
						GeneratedPipeline->PreDialogCleanup(Stack.StackName);
					}
				}
				GeneratedPipeline->SetShowEssentialsMode(bShowEssentials);
				if (bFilterOptions && BaseNodeContainer.IsValid())
				{
					GeneratedPipeline->FilterPropertiesFromTranslatedData(BaseNodeContainer.Get());
				}
				PipelineListViewItems.Add(MakeShareable(new FInterchangePipelineItemType{ GetPipelineDisplayName(DefaultPipeline), GeneratedPipeline, ReimportObject.Get(), BaseNodeContainer.Get(), SourceData.Get(), bShowEssentials }));
			}
		}
	}
	//Select the first pipeline
	if (PipelineListViewItems.Num() > 0)
	{
		CurrentPipelineIndex = PipelineListViewItems.IsValidIndex(CurrentPipelineIndex) ? CurrentPipelineIndex : 0;
		if (bShowSettings)
		{
			PipelinesListView->SetSelection(PipelineListViewItems[CurrentPipelineIndex], ESelectInfo::Direct);
			PipelinesListView->RequestListRefresh();
		}
		else
		{
			for (TSharedPtr<FInterchangePipelineItemType>& PiplineListViewItem : PipelineListViewItems)
			{
				PiplineListViewItem->ConflictInfos.Reset();
				if (PiplineListViewItem->ReimportObject)
				{
					PiplineListViewItem->ConflictInfos = PiplineListViewItem->Pipeline->GetConflictInfos(PiplineListViewItem->ReimportObject, PiplineListViewItem->Container, PiplineListViewItem->SourceData);
				}
			}
		}
	}

	//Update the cards
	RefreshCardsViewList();
}

void SInterchangePipelineConfigurationDialog::OnStackSelectionChanged(TSharedPtr<FString> String, ESelectInfo::Type)
{
	if (!String.IsValid())
	{
		return;
	}

	FName NewStackName = FName(*String.Get());
	if (!UE::Private::ContainStack(PipelineStacks, NewStackName))
	{
		return;
	}

	//Nothing change the selection is the same
	if (CurrentStackName == NewStackName)
	{
		return;
	}

	if (!bReimport)
	{
		//Use the stack select by interchange manager
		SaveAllPipelineSettings();
	}
	CurrentStackName = NewStackName;

	constexpr bool bStackSelectionChange = true;
	RefreshStack(bStackSelectionChange);
}

TSharedRef<ITableRow> SInterchangePipelineConfigurationDialog::MakePipelineListRowWidget(
	TSharedPtr<FInterchangePipelineItemType> InElement,
	const TSharedRef<STableViewBase>& OwnerTable)
{
	check(InElement->Pipeline);
	return SNew(SInterchangePipelineItem, OwnerTable, InElement);
}

void SInterchangePipelineConfigurationDialog::OnPipelineSelectionChanged(TSharedPtr<FInterchangePipelineItemType> InItem, ESelectInfo::Type SelectInfo)
{
	CurrentSelectedPipeline = nullptr;
	if (InItem)
	{
		CurrentSelectedPipeline = InItem->Pipeline;
	}
	SetEditPipeline(InItem.Get());
	
	if (CurrentSelectedPipeline)
	{
		FString CurrentPipelineName = CurrentSelectedPipeline->GetClass()->GetName();
		FString KeyName = CurrentStackName.ToString() + TEXT("_LastSelectedPipeline");
		GConfig->SetString(TEXT("InterchangeSelectPipeline"), *KeyName, *CurrentPipelineName, GEditorPerProjectIni);
	}
}
void SInterchangePipelineConfigurationDialog::UpdatePipelineSupportedAssetClasses()
{
	PipelineSupportAssetClasses.Reset();
	for (TSharedPtr<FInterchangePipelineItemType> PipelineItem : PipelineListViewItems)
	{
		TArray<UClass*> PipelineSupportedClasses;
		PipelineItem->Pipeline->GetSupportAssetClasses(PipelineSupportedClasses);
		for (UClass* AssetClass : PipelineSupportedClasses)
		{
			if (!PipelineSupportAssetClasses.Contains(AssetClass))
			{
				PipelineSupportAssetClasses.Add(AssetClass);
			}
		}
	}
}

void SInterchangePipelineConfigurationDialog::UpdateEnableDataPerFactoryNodeClass()
{
	if (!ensure(PreviewNodeContainer))
	{
		return;
	}
	TArray<UClass*> ValidCardClasses;
	PreviewNodeContainer->IterateNodesOfType<UInterchangeFactoryBaseNode>([this, &ValidCardClasses](const FString& NodeUid, UInterchangeFactoryBaseNode* FactoryNode)
		{
			UClass* NodeObjectClass = FactoryNode->GetObjectClass();
			// Don't add factory node class twice.
			// Don't add factory node class that don't have a valid object class
			// Don't add factory node class that are not a main asset from pipelines
			if (!ValidCardClasses.Contains(FactoryNode->GetClass())
				&& NodeObjectClass)
			{
				bool bClassIsSupported = false;
				for (UClass* SupportedClass : PipelineSupportAssetClasses)
				{
					if (NodeObjectClass->IsChildOf(SupportedClass))
					{
						bClassIsSupported = true;
						break;
					}
				}
				if (bClassIsSupported)
				{
					ValidCardClasses.Add(FactoryNode->GetClass());
					if (!EnableDataPerFactoryNodeClass.Contains(FactoryNode->GetClass()))
					{
						//Add a new entry that is enabled by default
						EnableDataPerFactoryNodeClass.FindOrAdd(FactoryNode->GetClass()).ObjectClass = FactoryNode->GetObjectClass();
					}
				}
			}
		});

	//Remove class card that do not exist anymore
	TArray<UClass*> CardsClassesToRemove;
	for (TPair<UClass*, FFactoryNodeEnabledData>& FactoryNodeClassAndEnableStatus : EnableDataPerFactoryNodeClass)
	{
		if (!ValidCardClasses.Contains(FactoryNodeClassAndEnableStatus.Key))
		{
			CardsClassesToRemove.Add(FactoryNodeClassAndEnableStatus.Key);
		}
	}
	for (UClass* CardToRemove : CardsClassesToRemove)
	{
		EnableDataPerFactoryNodeClass.Remove(CardToRemove);
	}
}

void SInterchangePipelineConfigurationDialog::FillAssetCardsList()
{
	//Update the pipeline supported asset class
	UpdatePipelineSupportedAssetClasses();

	//Update the preview container
	constexpr bool bUpdateCardsTrue = true;
	UpdatePreviewContainer(bUpdateCardsTrue);
	if (ensure(PreviewNodeContainer))
	{
		UpdateEnableDataPerFactoryNodeClass();
		AssetCards.Reset();
		for (TPair<UClass*, FFactoryNodeEnabledData>& FactoryNodeClassAndEnableStatus : EnableDataPerFactoryNodeClass)
		{
			UClass* FactoryNodeClass = FactoryNodeClassAndEnableStatus.Key;
			UClass* AssetClass = FactoryNodeClassAndEnableStatus.Value.ObjectClass;
			if (AssetClass)
			{
				TSharedPtr<SInterchangeAssetCard> AssetCard = SNew(SInterchangeAssetCard)
					.PreviewNodeContainer(PreviewNodeContainer)
					.AssetClass(AssetClass)
					.ShouldImportAssetType_Lambda([this, FactoryNodeClass]()
						{
							return EnableDataPerFactoryNodeClass.FindChecked(FactoryNodeClass).bEnable;
						})
					.OnImportAssetTypeChanged_Lambda([this, FactoryNodeClass](bool bNewEnabledValue)
						{
							EnableDataPerFactoryNodeClass.FindChecked(FactoryNodeClass).bEnable = bNewEnabledValue;
						});

				if (GInterchangeShowConflictWarningsOnCardsView
					&& PipelineListViewItems.Num() > 0)
				{
					for (TSharedPtr<FInterchangePipelineItemType>& PipelineListViewItem : PipelineListViewItems)
					{
						if (AssetCard->RefreshHasConflicts(PipelineListViewItem->ConflictInfos))
						{
							break;
						}
					}
				}
				AssetCards.Add(AssetCard);
			}
		}
	}
}

void SInterchangePipelineConfigurationDialog::CreateCardsViewList()
{
	FillAssetCardsList();
	if (AssetCards.IsEmpty())
	{
		CardViewList = nullptr;
	}
	else
	{
		CardViewList = SNew(SInterchangeAssetCardList).AssetCards(&AssetCards);
	}
}

void SInterchangePipelineConfigurationDialog::RefreshCardsViewList()
{
	FillAssetCardsList();
	if (CardViewList.IsValid())
	{
		CardViewList->RefreshList(PreviewNodeContainer);
	}
}

UInterchangeCardsPipeline* SInterchangePipelineConfigurationDialog::GenerateTransientCardsPipeline() const
{
	UInterchangeCardsPipeline* InterchangeCardsPipeline = nullptr;
	if (!bReimport)
	{
		TArray<UClass*> DisabledNodeClasses;
		for (const TPair<UClass*, FFactoryNodeEnabledData>& FactoryNodeClassAndEnableStatus : EnableDataPerFactoryNodeClass)
		{
			if(!FactoryNodeClassAndEnableStatus.Value.bEnable)
			{
				DisabledNodeClasses.Add(FactoryNodeClassAndEnableStatus.Key);
			}
		}

		if (!DisabledNodeClasses.IsEmpty())
		{
			InterchangeCardsPipeline = NewObject<UInterchangeCardsPipeline>();
			InterchangeCardsPipeline->SetDisabledFactoryNodes(DisabledNodeClasses);
		}
	}
	return InterchangeCardsPipeline;
}

void SInterchangePipelineConfigurationDialog::OnFilterOptionsChanged(ECheckBoxState CheckState)
{
	bool bNewCheckValue = CheckState == ECheckBoxState::Checked ? true : false;
	if (bNewCheckValue == bFilterOptions)
	{
		//Check state did not change
		return;
	}
	bFilterOptions = bNewCheckValue;
	//Refresh the pipeline
	constexpr bool bStackSelectionChange = false;
	RefreshStack(bStackSelectionChange);

	GConfig->SetBool(TEXT("InterchangeImportDialogOptions"), TEXT("FilterOptions"), bFilterOptions, GEditorPerProjectIni);
}

void SInterchangePipelineConfigurationDialog::OnShowEssentialsChanged(ECheckBoxState CheckState)
{
	bool bNewCheckValue = CheckState == ECheckBoxState::Checked ? true : false;
	if (bNewCheckValue == bShowEssentials)
	{
		//Check state did not change
		return;
	}
	bShowEssentials = bNewCheckValue;
	//Refresh the pipeline
	constexpr bool bStackSelectionChange = false;
	RefreshStack(bStackSelectionChange);

	GConfig->SetBool(TEXT("InterchangeImportDialogOptions"), TEXT("ShowEssentials"), bShowEssentials, GEditorPerProjectIni);
}

void SInterchangePipelineConfigurationDialog::UpdatePreviewContainer(bool bUpdateCards) const
{
	auto ClearObjectFlags = [](UObject* Obj)
		{
			Obj->ClearFlags(RF_Standalone | RF_Public);
			Obj->ClearInternalFlags(EInternalObjectFlags::Async);
		};
	if (PreviewNodeContainer)
	{
		ClearObjectFlags(PreviewNodeContainer);
		PreviewNodeContainer = nullptr;
	}
	PreviewNodeContainer = DuplicateObject<UInterchangeBaseNodeContainer>(BaseNodeContainer.Get(), GetTransientPackage());

	TArray<UInterchangeSourceData*> SourceDatas;
	SourceDatas.Add(SourceData.Get());

	//Execute all pipelines on the duplicated container
	UInterchangeResultsContainer* Results = NewObject<UInterchangeResultsContainer>(GetTransientPackage());
	for (int32 PipelineIndex = 0; PipelineIndex < PipelineListViewItems.Num(); ++PipelineIndex)
	{
		const TSharedPtr<FInterchangePipelineItemType> PipelineItem = PipelineListViewItems[PipelineIndex];

		//Duplicate the pipeline because ScriptedExecutePipeline is not const
		if (UInterchangePipelineBase* DuplicatedPipeline = DuplicateObject<UInterchangePipelineBase>(PipelineItem->Pipeline, GetTransientPackage()))
		{
			DuplicatedPipeline->TransferAdjustSettings(PipelineItem->Pipeline);
			DuplicatedPipeline->SetResultsContainer(Results);
			DuplicatedPipeline->ScriptedExecutePipeline(PreviewNodeContainer, SourceDatas, FString());
			ClearObjectFlags(DuplicatedPipeline);
		}
	}

	if (!bUpdateCards)
	{
		//If we do not update cards execute the cards pipeline since its a final preview
		if (UInterchangeCardsPipeline* InterchangeCardsPipeline = GenerateTransientCardsPipeline())
		{
			InterchangeCardsPipeline->ScriptedExecutePipeline(PreviewNodeContainer, SourceDatas, FString());
		}
	}

	PreviewNodeContainer->IterateNodesOfType<UInterchangeFactoryBaseNode>([ClosureReimportObject = ReimportObject](const FString& NodeUid, UInterchangeFactoryBaseNode* Node)
		{

			//Set all node in preview mode so hide the internal data attributes
			Node->UserInterfaceContext = EInterchangeNodeUserInterfaceContext::Preview;

			//If we reimport a specific object we want to disabled all factory nodes that are not supporting the reimport object class
			if (ClosureReimportObject.IsValid())
			{
				Node->SetEnabled(ClosureReimportObject.Get()->IsA(Node->GetObjectClass()));
			}
		});

	//Make sure all temporary object are not flags to persist
	ClearObjectFlags(Results);
}

FReply SInterchangePipelineConfigurationDialog::OnPreviewImport() const
{
	auto ClearObjectFlags = [](UObject* Obj)
		{
			Obj->ClearFlags(RF_Standalone | RF_Public);
			Obj->ClearInternalFlags(EInternalObjectFlags::Async);
		};

	constexpr bool bUpdateCardsFalse = false;
	UpdatePreviewContainer(bUpdateCardsFalse);

	if (!ensure(PreviewNodeContainer))
	{
		return FReply::Handled();
	}

	//Create and show the graph inspector UI dialog
	TSharedRef<SWindow> Window = SNew(SWindow)
		.ClientSize(FVector2D(800.f, 650.f))
		.Title(NSLOCTEXT("SInterchangePipelineConfigurationDialog", "InterchangePreviewTitle", "Interchange Preview"));
	TSharedPtr<SInterchangeGraphInspectorWindow> InterchangeGraphInspectorWindow;

	Window->SetContent
	(
		SAssignNew(InterchangeGraphInspectorWindow, SInterchangeGraphInspectorWindow)
		.InterchangeBaseNodeContainer(PreviewNodeContainer)
		.bPreview(true)
		.OwnerWindow(Window)
	);

	FSlateApplication::Get().AddModalWindow(Window, OwnerWindow.Pin(), false);

	return FReply::Handled();
}

#undef LOCTEXT_NAMESPACE
