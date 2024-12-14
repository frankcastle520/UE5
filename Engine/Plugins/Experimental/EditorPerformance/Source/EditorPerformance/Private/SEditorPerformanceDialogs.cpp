// Copyright Epic Games, Inc. All Rights Reserved.

#include "SEditorPerformanceDialogs.h"
#include "Algo/Sort.h"
#include "Framework/Application/SlateApplication.h"
#include "Internationalization/FastDecimalFormat.h"
#include "Math/BasicMathExpressionEvaluator.h"
#include "Math/UnitConversion.h"
#include "Misc/ExpressionParser.h"
#include "Styling/StyleColors.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SToolTip.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SGridPanel.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SHyperlink.h"
#include "EditorPerformanceModule.h"
#include "Modules/ModuleManager.h"
#include "ISettingsModule.h"
#include "Editor/EditorPerformanceSettings.h"

#define LOCTEXT_NAMESPACE "EditorPerformance"

void SEditorPerformanceReportDialog::Construct(const FArguments& InArgs)
{
	const float RowMargin = 0.0f;
	const float TitleMargin = 10.0f;
	const float ColumnMargin = 10.0f;
	const FMargin DefaultMarginFirstColumn(ColumnMargin, RowMargin);
	const FSlateColor TitleColor = FStyleColors::AccentWhite;
	const FSlateFontInfo TitleFont = FCoreStyle::GetDefaultFontStyle("Bold", 10);

	FEditorPerformanceModule& EditorPerfModule = FModuleManager::LoadModuleChecked<FEditorPerformanceModule>("EditorPerformance");

	this->ChildSlot
	[
		SNew(SScrollBox)
		.Orientation(Orient_Vertical)
		+ SScrollBox::Slot()
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.Padding(0, 20, 0, 0)
			.AutoHeight()
			[
				SNew(SHorizontalBox)
				+SHorizontalBox::Slot()
				.FillWidth(1.0f)
				[
					SNew(STextBlock)
					.Margin(TitleMargin)
					.ColorAndOpacity(TitleColor)
					.Font(TitleFont)
					.Justification(ETextJustify::Left)
					.Text_Lambda([this,&EditorPerfModule]
						{ 
							return FText::FromString(*FString::Printf(TEXT("Profile : %s"), *EditorPerfModule.GetKPIProfileName())); 
						}
					)
				]
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0, 5, 0, 0)
			.Expose(KPIGridSlot)
			[
				GetKPIGridPanel()
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0, 5, 0, 0)
			.Expose(HintGridSlot)
			[
				GetHintGridPanel()
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0, 5, 0, 0)
			.Expose(SettingsGridSlot)
			[
				GetSettingsGridPanel()
			]
		]
	];

	RegisterActiveTimer(5.0f, FWidgetActiveTimerDelegate::CreateSP(this, &SEditorPerformanceReportDialog::UpdateGridPanels));
}

EActiveTimerReturnType SEditorPerformanceReportDialog::UpdateGridPanels(double InCurrentTime, float InDeltaTime)
{
	(*SettingsGridSlot)
	[
		GetSettingsGridPanel()
	];

	(*KPIGridSlot)
	[
		GetKPIGridPanel()
	];

	(*HintGridSlot)
	[
		GetHintGridPanel()
	];

	SlatePrepass(GetPrepassLayoutScaleMultiplier());

	return EActiveTimerReturnType::Continue;
}

TSharedRef<SWidget> SEditorPerformanceReportDialog::GetSettingsGridPanel()
{
	TSharedRef<SGridPanel> Panel =
		SNew(SGridPanel);

	const float RowMargin = 0.0f;
	const float ColumnMargin = 10.0f;
	const FSlateColor TitleColor = FStyleColors::AccentWhite;
	const FSlateFontInfo TitleFont = FCoreStyle::GetDefaultFontStyle("Bold", 10);

	const FMargin TitleMargin(0.0f, 10.0f, ColumnMargin, 10.0f);
	const FMargin TitleMarginFirstColumn(ColumnMargin, 10.0f);
	const FMargin DefaultMargin(0.0f, RowMargin, ColumnMargin, RowMargin);
	const FMargin DefaultMarginFirstColumn(ColumnMargin, RowMargin);

	int32 Row = 0;

	Panel->AddSlot(0, Row)
		.HAlign(HAlign_Left)
		[
			SNew(STextBlock)
			.Margin(TitleMarginFirstColumn)
			.ColorAndOpacity(TitleColor)
			.Font(TitleFont)
			.Text(LOCTEXT("SettingsText", "Settings"))
		];

	Row++;

	Panel->AddSlot(0, Row)
		.HAlign(HAlign_Left)
		[
			SNew(SCheckBox)
			.IsChecked_Lambda([]
				{
					const UEditorPerformanceSettings* EditorPerformanceSettings = GetDefault<UEditorPerformanceSettings>();
					return EditorPerformanceSettings && EditorPerformanceSettings->bEnableNotifications ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
				})
			.OnCheckStateChanged_Lambda([this](ECheckBoxState NewState)
			{
				UEditorPerformanceSettings* EditorPerformanceSettings = GetMutableDefault<UEditorPerformanceSettings>();

				if (EditorPerformanceSettings)
				{
					EditorPerformanceSettings->bEnableNotifications = NewState == ECheckBoxState::Checked;
					EditorPerformanceSettings->PostEditChange();
					EditorPerformanceSettings->SaveConfig();
				}

				UpdateGridPanels(0.0f, 0.0f);
			})
			.Padding(FMargin(4.0f, 0.0f))
			[
				SNew(STextBlock)
				.Text(LOCTEXT("EnableNotificationsText", "Notifications"))
				.ToolTipText(LOCTEXT("EnableNotificationsToolTip", "Enable All Notifications"))
				.ColorAndOpacity(EStyleColor::Foreground)
			]
		];

	Panel->AddSlot(1, Row)
		.HAlign(HAlign_Left)
		[
			SNew(SCheckBox)
			.IsChecked_Lambda([]
				{
					const UEditorPerformanceSettings* EditorPerformanceSettings = GetDefault<UEditorPerformanceSettings>();
					return EditorPerformanceSettings && EditorPerformanceSettings->bEnableSnapshots ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
				})
			.OnCheckStateChanged_Lambda([this](ECheckBoxState NewState)
				{
					UEditorPerformanceSettings* EditorPerformanceSettings = GetMutableDefault<UEditorPerformanceSettings>();

					if (EditorPerformanceSettings)
					{
						EditorPerformanceSettings->bEnableSnapshots = NewState == ECheckBoxState::Checked;
						EditorPerformanceSettings->PostEditChange();
						EditorPerformanceSettings->SaveConfig();
					}

					UpdateGridPanels(0.0f, 0.0f);
				})
				.Padding(FMargin(4.0f, 0.0f))
				[
					SNew(STextBlock)
					.Text(LOCTEXT("EnableSnapshotsText", "Snapshots"))
					.ToolTipText(LOCTEXT("EnableSnapshotsToolTip", "Enable Automatic Capture of Unreal Insights Snaphsots"))
					.ColorAndOpacity(EStyleColor::Foreground)
				]
			];

	Panel->AddSlot(2, Row)
		.HAlign(HAlign_Left)
		[
			SNew(SCheckBox)
			.IsChecked_Lambda([]
				{
					const UEditorPerformanceSettings* EditorPerformanceSettings = GetDefault<UEditorPerformanceSettings>();
					return EditorPerformanceSettings && EditorPerformanceSettings->bEnableTelemetry ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
				})
			.OnCheckStateChanged_Lambda([this](ECheckBoxState NewState)
			{
				UEditorPerformanceSettings* EditorPerformanceSettings = GetMutableDefault<UEditorPerformanceSettings>();

				if (EditorPerformanceSettings)
				{
					EditorPerformanceSettings->bEnableTelemetry = NewState == ECheckBoxState::Checked;
					EditorPerformanceSettings->PostEditChange();
					EditorPerformanceSettings->SaveConfig();
				}

				UpdateGridPanels(0.0f, 0.0f);
			})
			.Padding(FMargin(4.0f, 0.0f))
			[
				SNew(STextBlock)
				.Text(LOCTEXT("EnableTelemetryText", "Telemetry"))
				.ToolTipText(LOCTEXT("EnableTelemetryToolTip", "Record Warning Telemetry Events To Analytics System"))
				.ColorAndOpacity(EStyleColor::Foreground)
			]
		];

	Panel->AddSlot(3, Row)
		.HAlign(HAlign_Left)
		[
			SNew(SCheckBox)
			.IsChecked_Lambda([]
				{
					const UEditorPerformanceSettings* EditorPerformanceSettings = GetDefault<UEditorPerformanceSettings>();
					return EditorPerformanceSettings && EditorPerformanceSettings->bThrottleCPUWhenNotForeground ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
				})
			.OnCheckStateChanged_Lambda([this](ECheckBoxState NewState)
			{
				UEditorPerformanceSettings* EditorPerformanceSettings = GetMutableDefault<UEditorPerformanceSettings>();

				if (EditorPerformanceSettings)
				{
					EditorPerformanceSettings->bThrottleCPUWhenNotForeground = NewState == ECheckBoxState::Checked;
					EditorPerformanceSettings->PostEditChange();
					EditorPerformanceSettings->SaveConfig();
				}

				UpdateGridPanels(0.0f, 0.0f);
			})
			.Padding(FMargin(4.0f, 0.0f))
			[
				SNew(STextBlock)
				.Text(LOCTEXT("EnableBackgroundThrottlingText", "Throttling"))
				.ToolTipText(LOCTEXT("EnableBackgroundThrottlingToolTip", "Enable CPU throttling when the Editor is in the background."))
				.ColorAndOpacity(EStyleColor::Foreground)
			]
		];

	Panel->AddSlot(4, Row)
		.HAlign(HAlign_Left)
		[
			SNew(SCheckBox)
				.IsChecked_Lambda([]
					{
						const UEditorPerformanceSettings* EditorPerformanceSettings = GetDefault<UEditorPerformanceSettings>();
						return EditorPerformanceSettings && EditorPerformanceSettings->bShowFrameRateAndMemory ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
					})
				.OnCheckStateChanged_Lambda([this](ECheckBoxState NewState)
					{
						UEditorPerformanceSettings* EditorPerformanceSettings = GetMutableDefault<UEditorPerformanceSettings>();

						if (EditorPerformanceSettings)
						{
							EditorPerformanceSettings->bShowFrameRateAndMemory = NewState == ECheckBoxState::Checked;
							EditorPerformanceSettings->PostEditChange();
							EditorPerformanceSettings->SaveConfig();
						}

						UpdateGridPanels(0.0f, 0.0f);
					})
						.Padding(FMargin(4.0f, 0.0f))
						[
							SNew(STextBlock)
								.Text(LOCTEXT("EnableShowFrameRateAndMemoryText", "Diagnostics"))
								.ToolTipText(LOCTEXT("EnableShowFrameRateAndMemoryToolTip", "Show the Frame Rate, Memory and Stalls."))
								.ColorAndOpacity(EStyleColor::Foreground)
						]
		];

	Panel->AddSlot(5, Row)
		.HAlign(HAlign_Left)
		.Padding(FMargin(10.0f, 10.0f))
		[
			SNew(SButton)
			.Text(LOCTEXT("OpenSettingsText", "All Settings"))
			.ToolTipText(LOCTEXT("OpenSettingsToolTip", "Open the Editor Performance Settings Tab."))
			.OnClicked_Lambda([this]()
			{
				FModuleManager::LoadModuleChecked<ISettingsModule>("Settings").ShowViewer("Editor", "General", "EditorPerformanceSettings");
				return FReply::Handled();
			})
		];

	return Panel;
}

TSharedRef<SWidget> SEditorPerformanceReportDialog::GetHintGridPanel()
{
	FEditorPerformanceModule& EditorPerfModule = FModuleManager::LoadModuleChecked<FEditorPerformanceModule>("EditorPerformance");

	int32 NumHints = 0;

	TArray<FKPIHint> KPIHints;

	const FKPIValues& KPIValues = EditorPerfModule.GetKPIRegistry().GetKPIValues();

	for (FKPIValues::TConstIterator It(KPIValues); It; ++It)
	{
		const FKPIValue& KPIValue = It->Value;

		FKPIHint KPIHint;

		if (KPIValue.State == FKPIValue::Bad && EditorPerfModule.GetKPIRegistry().GetKPIHint(KPIValue.Id, KPIHint))
		{
			KPIHints.Emplace(KPIHint);
		}
	}

	TSharedRef<SGridPanel> Panel =
		SNew(SGridPanel);

	const float RowMargin = 0.0f;
	const float ColumnMargin = 10.0f;
	const FSlateColor TitleColor = FStyleColors::AccentWhite;
	const FSlateFontInfo TitleFont = FCoreStyle::GetDefaultFontStyle("Bold", 10);

	const FMargin TitleMargin(0.0f, 10.0f, ColumnMargin, 10.0f);
	const FMargin TitleMarginFirstColumn(ColumnMargin, 10.0f);
	const FMargin DefaultMargin(0.0f, RowMargin, ColumnMargin, RowMargin);
	const FMargin DefaultMarginFirstColumn(ColumnMargin, RowMargin);

	int32 Row = 0;

	if (KPIHints.Num()>0)
	{
		CurrentHintIndex = CurrentHintIndex % KPIHints.Num();

		const FKPIHint& KPIHint = KPIHints[CurrentHintIndex];

		FKPIValue KPIValue;

		if (EditorPerfModule.GetKPIRegistry().GetKPIValue(KPIHint.Id, KPIValue) == true)
		{
			Panel->AddSlot(0, Row)
				.HAlign(HAlign_Left)
				[
					SNew(STextBlock)
						.Margin(TitleMarginFirstColumn)
						.ColorAndOpacity(TitleColor)
						.Font(TitleFont)
						.Justification(ETextJustify::Left)
						.Text(LOCTEXT("HintsTitle", "Hints"))
				];

			Row++;

			Panel->AddSlot(0, Row)
				.HAlign(HAlign_Left)
				[
					SNew(STextBlock)
						.Margin(TitleMarginFirstColumn)
						.ColorAndOpacity(EStyleColor::Foreground)
						.Font(TitleFont)
						.Text(FText::FromString(*FString::Printf(TEXT("%s %s"), *KPIValue.Category.ToString(), *KPIValue.Name.ToString())))
				];

			Row++;

			Panel->AddSlot(0, Row)
				.HAlign(HAlign_Left)
				[
					SNew(STextBlock)
						.AutoWrapText(true)
						.Margin(DefaultMarginFirstColumn)
						.ColorAndOpacity(EStyleColor::Foreground)
						.Justification(ETextJustify::Left)
						.Text(KPIHint.Message)
				];

			Row++;

			if (!KPIHint.URL.IsEmpty())
			{
				Panel->AddSlot(0, Row)
					.HAlign(HAlign_Left)
					.Padding(FMargin(10.0f, 10.0f))
					[
						SNew(SHyperlink)
							.Text(LOCTEXT("HintLinkName", "Further Help & Documentation"))
							.ToolTipText_Lambda([=]() { return FText::FromString(*KPIHint.URL.ToString()); })
							.OnNavigate_Lambda([=]() { FPlatformProcess::LaunchURL(*KPIHint.URL.ToString(), nullptr, nullptr); })
					];

				Row++;
			}

			if (KPIHints.Num() > 1)
			{
				Panel->AddSlot(0, Row)
					.HAlign(HAlign_Left)
					.Padding(FMargin(10.0f, 10.0f))
					[
						SNew(SButton)
							.Text(LOCTEXT("NextHintName", "Next Hint"))
							.OnClicked_Lambda([this]()
								{
									CurrentHintIndex++;
									UpdateGridPanels(0.0f, 0.0f);
									return FReply::Handled();
								})
					];

				Row++;
			}
		}
	}

	return Panel;
}

TSharedRef<SWidget> SEditorPerformanceReportDialog::GetKPIGridPanel()
{
	TSharedRef<SGridPanel> Panel =
		SNew(SGridPanel);

	const float RowMargin = 0.0f;
	const float ColumnMargin = 10.0f;
	const FSlateColor TitleColor = FStyleColors::AccentWhite;
	const FSlateFontInfo TitleFont = FCoreStyle::GetDefaultFontStyle("Bold", 10);

	const FMargin TitleMargin(0.0f, 10.0f, ColumnMargin, 10.0f);
	const FMargin TitleMarginFirstColumn(ColumnMargin, 10.0f);
	const FMargin DefaultMargin(0.0f, RowMargin, ColumnMargin, RowMargin);
	const FMargin DefaultMarginFirstColumn(ColumnMargin, RowMargin);

	const UEditorPerformanceSettings* EditorPerformanceSettings = GetDefault<UEditorPerformanceSettings>();
	FEditorPerformanceModule& EditorPerfModule = FModuleManager::LoadModuleChecked<FEditorPerformanceModule>("EditorPerformance");

	const bool EnableNotifcations = EditorPerformanceSettings && EditorPerformanceSettings->bEnableNotifications;
	const bool ShowWarningsOnly = EditorPerformanceSettings && EditorPerformanceSettings->bShowWarningsOnly;
	
	int32 Row = 0;

	Panel->AddSlot(0, Row)
		.HAlign(HAlign_Left)
		[
			SNew(STextBlock)
			.Margin(TitleMarginFirstColumn)
			.ColorAndOpacity(TitleColor)
			.Font(TitleFont)
			.Text(LOCTEXT("Measurements", "Measurements"))
		];

	Row++;

	Panel->AddSlot(0, Row)
		.HAlign(HAlign_Left)
		[
			SNew(SComboBox<FName>)
			.OptionsSource(&WarningFilterOptions)
			.InitiallySelectedItem( EditorPerformanceSettings && EditorPerformanceSettings->bShowWarningsOnly ? WarningFilterOptions[1] : WarningFilterOptions[0] )
			.OnGenerateWidget_Lambda([](FName Name)
				{	
					return SNew(STextBlock)
						.Text(FText::FromString(*Name.ToString()));
				})
			.OnSelectionChanged_Lambda([this](FName Name, ESelectInfo::Type)
			{
				UEditorPerformanceSettings* EditorPerformanceSettings = GetMutableDefault<UEditorPerformanceSettings>();

				if (EditorPerformanceSettings)
				{
					EditorPerformanceSettings->bShowWarningsOnly = (Name == WarningFilterOptions[1]);
					EditorPerformanceSettings->PostEditChange();
					EditorPerformanceSettings->SaveConfig();
				}

				UpdateGridPanels(0.0f, 0.0f);
			})
			.Content()
			[
				SNew(STextBlock)
				.Text(FText::FromString((EditorPerformanceSettings && EditorPerformanceSettings->bShowWarningsOnly) ? *WarningFilterOptions[1].ToString() : *WarningFilterOptions[0].ToString()) )
			]
		];

	Row++;

	Panel->AddSlot(1, Row)
		.HAlign(HAlign_Left)
		[
			SNew(STextBlock)
			.Margin(DefaultMargin)
			.ColorAndOpacity(EStyleColor::Foreground)
			.Font(TitleFont)
			.Text(LOCTEXT("CurrentValueColumn", "Current"))
		];

	Panel->AddSlot(3, Row)
		.HAlign(HAlign_Left)
		[
			SNew(STextBlock)
			.Margin(DefaultMargin)
			.ColorAndOpacity(EStyleColor::Foreground)
			.Font(TitleFont)
			.Text(LOCTEXT("ExpectedValueColumn", "Expected"))
		];

	Panel->AddSlot(4, Row)
		.HAlign(HAlign_Left)
		[
			SNew(STextBlock)
				.Margin(DefaultMargin)
				.ColorAndOpacity(EStyleColor::Foreground)
				.Font(TitleFont)
				.Text(LOCTEXT("FailedValueColumn", "Failures"))
		];

	if (EnableNotifcations)
	{
		Panel->AddSlot(6, Row)
			.HAlign(HAlign_Center)
			[
				SNew(STextBlock)
				.Margin(DefaultMargin)
				.ColorAndOpacity(EStyleColor::Foreground)
				.Font(TitleFont)
				.Text(LOCTEXT("NotifyColumn", "Notify"))
			];
	}

	Row++;
	
	TMap<FName, TArray<FKPIValue>> SortedKPIValues;

	for (FKPIValues::TConstIterator It(EditorPerfModule.GetKPIRegistry().GetKPIValues()); It; ++It)
	{
		const FKPIValue& KPIValue = It->Value;

		if (ShowWarningsOnly && KPIValue.GetState()!=FKPIValue::Bad )
		{
			continue;
		}

		if (SortedKPIValues.Find(KPIValue.Category)!=nullptr)
		{
			SortedKPIValues[KPIValue.Category].Emplace(KPIValue);
		}
		else
		{
			TArray<FKPIValue> KPIArray;
			KPIArray.Emplace(KPIValue);
			SortedKPIValues.Emplace(KPIValue.Category, KPIArray);
		}
	}

	for (TMap<FName, TArray<FKPIValue>>::TConstIterator It(SortedKPIValues); It; ++It)
	{
		const TArray<FKPIValue>& KPIValues = It->Value;
		const FName& Category = It->Key;

		// Render the category name
		Panel->AddSlot(0, Row)
			.HAlign(HAlign_Left)
			[
				SNew(STextBlock)
				.Margin(TitleMarginFirstColumn)
				.ColorAndOpacity(EStyleColor::Foreground)
				.Font(TitleFont)
				.Text(FText::FromString(*Category.ToString()))
			];
		
		Row++;

		for (const FKPIValue& KPIValue : KPIValues)
		{
			const FKPIValue::EState KPIValueState = KPIValue.GetState();

			const FSlateColor KPIColor = KPIValueState==FKPIValue::Bad ? EStyleColor::Warning : EStyleColor::Foreground;
			const FSlateBrush* KPIWarningIcon = FAppStyle::Get().GetBrush("EditorPerformance.Report.Warning");
			const float KPIIconSize = 8.0f;
			const FName& KPIName = KPIValue.Name;
			const FName& KPIPath = KPIValue.Path;

			Panel->AddSlot(0, Row)
				.HAlign(HAlign_Left)
				[
					SNew(STextBlock)
					.Margin(DefaultMarginFirstColumn)
					.ColorAndOpacity(EStyleColor::Foreground)
					.Text(FText::FromString(*KPIName.ToString()))
				];

			if (KPIValueState != FKPIValue::NotSet)
			{
				Panel->AddSlot(1, Row)
					.HAlign(HAlign_Left)
					[
						SNew(STextBlock)
						.Margin(DefaultMargin)
						.ColorAndOpacity(KPIColor)
						.Text(FText::FromString(*FKPIValue::GetValueAsString(KPIValue.CurrentValue, KPIValue.DisplayType)))
					];

				Panel->AddSlot(2, Row)
					.HAlign(HAlign_Left)
					[
						SNew(STextBlock)
						.Margin(DefaultMargin)
						.ColorAndOpacity(KPIColor)
						.Text(FText::FromString(*FKPIValue::GetComparisonAsString(KPIValue.Compare)))
					];

				Panel->AddSlot(3, Row)
					.HAlign(HAlign_Left)
					[
						SNew(STextBlock)
						.Margin(DefaultMargin)
						.ColorAndOpacity(KPIColor)
						.Text(FText::FromString(*FKPIValue::GetValueAsString(KPIValue.ThresholdValue, KPIValue.DisplayType)))
					];

				Panel->AddSlot(4, Row)
					.HAlign(HAlign_Left)
					[
						SNew(STextBlock)
							.Margin(DefaultMargin)
							.ColorAndOpacity(KPIColor)
							.Text(FText::FromString(*FString::Printf(TEXT("%d"),KPIValue.FailureCount)))
					];

				if (KPIValueState != FKPIValue::Good)
				{
					Panel->AddSlot(5, Row)
						[
							SNew(SImage)
							.Image(KPIWarningIcon)
						];
				}
			}
			else
			{
				Panel->AddSlot(1, Row)
					.HAlign(HAlign_Left)
					[
						SNew(STextBlock)
						.Margin(DefaultMargin)
						.ColorAndOpacity(KPIColor)
						.Text(LOCTEXT("PendingValue", "..."))
					];
			}

			if (EnableNotifcations)
			{
				Panel->AddSlot(6, Row)
					.HAlign(HAlign_Left)
					[
						SNew(SComboBox<FName>)
						.OptionsSource(&NotifcationOptions)
						.InitiallySelectedItem(EditorPerformanceSettings->NotificationList.Find(KPIPath) != INDEX_NONE ? NotifcationOptions[0] : NotifcationOptions[1])
						.OnGenerateWidget_Lambda([](FName Name)
							{
								return SNew(STextBlock)
									.Text(FText::FromString(*Name.ToString()));
							})
						.OnSelectionChanged_Lambda([this, KPIPath, &EditorPerfModule](FName Name, ESelectInfo::Type)
						{
							UEditorPerformanceSettings* EditorPerformanceSettings = GetMutableDefault<UEditorPerformanceSettings>();

							if (EditorPerformanceSettings)
							{
								if (Name == NotifcationOptions[0])
								{
									if (EditorPerformanceSettings->NotificationList.Find(KPIPath) == INDEX_NONE)
									{
										// Add this KPI to the notification list
										EditorPerformanceSettings->NotificationList.Emplace(KPIPath);
									}
								}
								else
								{
									// Remove this KPI to the notification ignore list
									EditorPerformanceSettings->NotificationList.Remove(KPIPath);
								}

								EditorPerformanceSettings->PostEditChange();
								EditorPerformanceSettings->SaveConfig();
							}

							UpdateGridPanels(0.0f, 0.0f);
						})
						.Content()
						[
							SNew(STextBlock)
							.Text(FText::FromString(EditorPerformanceSettings->NotificationList.Find(KPIPath) != INDEX_NONE ? *NotifcationOptions[0].ToString() : *NotifcationOptions[1].ToString() ))
						]
					];
			}

			Row++;
		}
	}

	return Panel;
}

#undef LOCTEXT_NAMESPACE
