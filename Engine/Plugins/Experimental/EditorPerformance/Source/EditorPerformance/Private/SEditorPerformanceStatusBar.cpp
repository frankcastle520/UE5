// Copyright Epic Games, Inc. All Rights Reserved.

#include "SEditorPerformanceStatusBar.h"

#include "Async/Future.h"
#include "Containers/Array.h"
#include "Delegates/Delegate.h"
#include "EditorPerformanceModule.h"
#include "Framework/Commands/InputChord.h"
#include "Framework/Commands/UIAction.h"
#include "Framework/Commands/UICommandInfo.h"
#include "Framework/Commands/UICommandList.h"
#include "Framework/MultiBox/MultiBoxDefs.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Framework/SlateDelegates.h"
#include "HAL/PlatformCrt.h"
#include "ISettingsModule.h"
#include "Internationalization/Internationalization.h"
#include "Layout/Children.h"
#include "Layout/Margin.h"
#include "Math/Color.h"
#include "Math/UnitConversion.h"
#include "Math/UnrealMathSSE.h"
#include "Misc/Attribute.h"
#include "Misc/CoreMisc.h"
#include "Modules/ModuleManager.h"
#include "SlotBase.h"
#include "Styling/AppStyle.h"
#include "Styling/ISlateStyle.h"
#include "Styling/SlateColor.h"
#include "Styling/SlateTypes.h"
#include "Textures/SlateIcon.h"
#include "ToolMenu.h"
#include "ToolMenuContext.h"
#include "ToolMenuSection.h"
#include "ToolMenus.h"
#include "Types/WidgetActiveTimerDelegate.h"
#include "UObject/NameTypes.h"
#include "UObject/UnrealNames.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"
#include "Settings/EditorProjectSettings.h"
#include "Editor/EditorPerformanceSettings.h"

class SWidget;
struct FSlateBrush;

#define LOCTEXT_NAMESPACE "EditorPerformance"


TSharedRef<FUICommandList> FEditorPerformanceStatusBarMenuCommands::ActionList(new FUICommandList());

FEditorPerformanceStatusBarMenuCommands::FEditorPerformanceStatusBarMenuCommands()
	: TCommands<FEditorPerformanceStatusBarMenuCommands>
	(
		"EditorPerformanceSettings",
		LOCTEXT("Editor Performance", "Editor Performance"),
		"LevelEditor",
		FAppStyle::GetAppStyleSetName()
		)
{}

void FEditorPerformanceStatusBarMenuCommands::RegisterCommands()
{
	UI_COMMAND(ChangeSettings, "Change Performance Settings", "Opens the Editor Performance Settings.", EUserInterfaceActionType::Button, FInputChord());
	UI_COMMAND(ViewPerformanceReport, "View Performance Report", "Opens the Editor Performance Report panel.", EUserInterfaceActionType::Button, FInputChord());

	ActionList->MapAction(
		ChangeSettings,
		FExecuteAction::CreateStatic(&FEditorPerformanceStatusBarMenuCommands::ChangeSettings_Clicked)
	);

	ActionList->MapAction(
		ViewPerformanceReport,
		FExecuteAction::CreateStatic(&FEditorPerformanceStatusBarMenuCommands::ViewPerformanceReport_Clicked)
	);
} 

void FEditorPerformanceStatusBarMenuCommands::ChangeSettings_Clicked()
{
	FModuleManager::LoadModuleChecked<ISettingsModule>("Settings").ShowViewer("Editor", "General", "EditorPerformanceSettings");
}

void FEditorPerformanceStatusBarMenuCommands::ViewPerformanceReport_Clicked()
{
	FModuleManager::LoadModuleChecked<FEditorPerformanceModule>("EditorPerformance").ShowPerformanceReportTab();
}

TSharedRef<SWidget> SEditorPerformanceStatusBarWidget::CreateStatusBarMenu()
{
	UToolMenu* Menu = UToolMenus::Get()->RegisterMenu("StatusBar.ToolBar.EditorPerformance", NAME_None, EMultiBoxType::Menu, false);

	{
		FToolMenuSection& Section = Menu->AddSection("PerformanceSettingsSection", LOCTEXT("PerformanceSettingsSection", "Settings"));

		Section.AddMenuEntry(
			FEditorPerformanceStatusBarMenuCommands::Get().ChangeSettings,
			TAttribute<FText>(),
			TAttribute<FText>(),
			FSlateIcon(FAppStyle::GetAppStyleSetName(), "EditorPerformance.Settings")
		);
	}

	{
		FToolMenuSection& Section = Menu->AddSection("PerformanceReportSection", LOCTEXT("PerformanceReportSection", "Panels"));

		Section.AddMenuEntry(
			FEditorPerformanceStatusBarMenuCommands::Get().ViewPerformanceReport,
			TAttribute<FText>(),
			TAttribute<FText>(),
			FSlateIcon(FAppStyle::GetAppStyleSetName(), "EditorPerformance.Report.Panel")
		);
	}

	return UToolMenus::Get()->GenerateWidget("StatusBar.ToolBar.EditorPerformance", FToolMenuContext(FEditorPerformanceStatusBarMenuCommands::ActionList));
}

void SEditorPerformanceStatusBarWidget::Construct(const FArguments& InArgs)
{
	this->ChildSlot
		[
			SNew(SComboButton)
			.ContentPadding(FMargin(6.0f, 0.0f))
			.MenuPlacement(MenuPlacement_AboveAnchor)
			.ComboButtonStyle(&FAppStyle::Get().GetWidgetStyle<FComboButtonStyle>("SimpleComboButton"))
			.ButtonContent()
			[

				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(0, 0, 3, 0)
				[
					SNew(SOverlay)
					+ SOverlay::Slot()
					.HAlign(HAlign_Center)
					.VAlign(VAlign_Top)
				[
					SNew(SImage)
					.ColorAndOpacity_Lambda([this] { return GetStatusIconColor(); })
					.Image_Lambda([this] { return GetStatusIcon(); })
					.ToolTipText_Lambda([this] { return GetStatusToolTipText(); })
				]
			]

			+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(0, 0, 10, 0)
				[
					SNew(STextBlock)
					.Text_Lambda([this] { return GetTitleText(); })
					.ToolTipText_Lambda([this] { return GetStatusToolTipText(); })
				]
	]
	.OnGetMenuContent(FOnGetContent::CreateRaw(this, &SEditorPerformanceStatusBarWidget::CreateStatusBarMenu))
		];

	RegisterActiveTimer(5.0f, FWidgetActiveTimerDelegate::CreateSP(this, &SEditorPerformanceStatusBarWidget::UpdateState));


}

EActiveTimerReturnType SEditorPerformanceStatusBarWidget::UpdateState(double InCurrentTime, float InDeltaTime)
{
	EditorPerformanceState = EEditorPerformanceState::Good;
	EditorPerformanceStateMessage = LOCTEXT("EditorPerfMesssageGood", "Good");

	FEditorPerformanceModule& EditorPerfModule = FModuleManager::LoadModuleChecked<FEditorPerformanceModule>("EditorPerformance");

	const UEditorPerformanceSettings* EditorPerformanceSettings = GetDefault<UEditorPerformanceSettings>();

	WarningCount = 0;

	EditorPerfModule.UpdateKPIs(InDeltaTime);

	// Check for KPIs that have exceeded their value
	for (FKPIValues::TConstIterator It(EditorPerfModule.GetKPIRegistry().GetKPIValues()); It; ++It)
	{
		const FKPIValue& KPIValue = It->Value;

		if (KPIValue.GetState() == FKPIValue::Bad)
		{
			// Currently exceeding the threshold	
			EditorPerformanceState = EEditorPerformanceState::Warnings;

			if (EditorPerformanceSettings)
			{
				if (EditorPerformanceSettings->NotificationList.Find(KPIValue.Path) != INDEX_NONE && AcknowledgedNotifications.Find(KPIValue.Path) == INDEX_NONE && CurrentNotificationName.IsNone())
				{
					CurrentNotificationMessage = FText::FromString(*FString::Printf(TEXT("%s - %s was %s but should be %s than %s"),
						*KPIValue.Category.ToString(),
						*KPIValue.Name.ToString(),
						*FKPIValue::GetValueAsString(KPIValue.CurrentValue, KPIValue.DisplayType),
						*FKPIValue::GetComparisonAsPrettyString(KPIValue.Compare),
						*FKPIValue::GetValueAsString(KPIValue.ThresholdValue, KPIValue.DisplayType)));

					CurrentNotificationName = KPIValue.Path;
				}
			}

			WarningCount++;
		}
		else
		{
			// No longer exceeding threshold, so no need to acknowledge the last time it was raised to the user
			// There may be subsequent times that this same KPI is exceeded this session so we may want to alert the user again
			AcknowledgedNotifications.Remove(KPIValue.Path);
		}
	}

	if (WarningCount > 0)
	{
		if (WarningCount == 1)
		{
			EditorPerformanceStateMessage = FText::FromString(*FString::Printf(TEXT("There Is 1 Warning.\n\nView Performance Report For Details.")));
		}
		else
		{
			EditorPerformanceStateMessage = FText::FromString(*FString::Printf(TEXT("There Are %d Warnings.\n\nView Performance Report For Details."), WarningCount));
		}
	}

	if (EditorPerformanceSettings && EditorPerformanceSettings->bEnableNotifications)
	{
		if (CurrentNotificationName.IsNone() == false)
		{	
			if (NotificationItem.IsValid() == false || NotificationItem->GetCompletionState() == SNotificationItem::CS_None)
			{
				FNotificationInfo Info(LOCTEXT("NotificationTitle", "Editor Performance Warning"));

				Info.SubText= CurrentNotificationMessage;
				Info.bUseSuccessFailIcons = true;
				Info.bFireAndForget = false;
				Info.bUseThrobber = true;
				Info.FadeOutDuration = 1.0f;
				Info.ExpireDuration = 0.0f;

				// No existing notification or the existing one has finished
				TPromise<TWeakPtr<SNotificationItem>> AcknowledgeNotificationPromise;

				Info.ButtonDetails.Add(FNotificationButtonInfo(LOCTEXT("AcknowledgeNotificationButton", "Dismiss"), FText(), FSimpleDelegate::CreateLambda([NotificationFuture = AcknowledgeNotificationPromise.GetFuture().Share(),this]()
					{
						// User has acknowledged this warning
						TWeakPtr<SNotificationItem> NotificationPtr = NotificationFuture.Get();
						if (TSharedPtr<SNotificationItem> Notification = NotificationPtr.Pin())
						{
							Notification->SetCompletionState(SNotificationItem::CS_None);
							Notification->ExpireAndFadeout();
						}

						AcknowledgedNotifications.Add(CurrentNotificationName);
						CurrentNotificationName = FName();

					}), SNotificationItem::ECompletionState::CS_Fail));

				// Create the notification item
				NotificationItem = FSlateNotificationManager::Get().AddNotification(Info);

				if (NotificationItem.IsValid())
				{
					AcknowledgeNotificationPromise.SetValue(NotificationItem);
					NotificationItem->SetCompletionState(SNotificationItem::CS_Fail);
				}
			}
		}
	}
	else
	{
		// No longer any warnings so kill any existing notifications
		if (NotificationItem.IsValid())
		{
			NotificationItem->SetCompletionState(SNotificationItem::CS_None);
			NotificationItem->ExpireAndFadeout();
		}
	}

	return EActiveTimerReturnType::Continue;
}

const FSlateBrush* SEditorPerformanceStatusBarWidget::GetStatusIcon() const
{
	switch (EditorPerformanceState)
	{
		default:
		case EEditorPerformanceState::Good:
		{
			return FAppStyle::Get().GetBrush("EditorPerformance.Notification.Good");
			break;
		}

		case EEditorPerformanceState::Warnings:
		{
			return FAppStyle::Get().GetBrush("EditorPerformance.Notification.Warning");
			break;
		}
	}
}

const FSlateColor SEditorPerformanceStatusBarWidget::GetStatusIconColor() const
{
	switch (EditorPerformanceState)
	{
		default:
		case EEditorPerformanceState::Good:
		{
			return FLinearColor::White; 
			break;
		}

		case EEditorPerformanceState::Warnings:
		{
			return FLinearColor::White;
			break;
		}
	}
}

FText SEditorPerformanceStatusBarWidget::GetStatusToolTipText() const
{
	return EditorPerformanceStateMessage;
}

FText SEditorPerformanceStatusBarWidget::GetTitleText() const
{
	return LOCTEXT("EditorPerformanceToolBarName", "Performance");
}

FText SEditorPerformanceStatusBarWidget::GetTitleToolTipText() const
{
	return GetTitleText();
}

#undef LOCTEXT_NAMESPACE
