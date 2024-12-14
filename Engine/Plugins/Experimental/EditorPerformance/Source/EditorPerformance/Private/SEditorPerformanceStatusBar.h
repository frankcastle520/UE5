// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Framework/Commands/Commands.h"
#include "Internationalization/Text.h"
#include "Templates/SharedPointer.h"
#include "Types/SlateEnums.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

class FUICommandInfo;
class FUICommandList;
class SNotificationItem;
class SWidget;
class FKPIValue;
struct FSlateBrush;


enum class EEditorPerformanceState : uint8
{
	Good,
	Warnings,
};

class FEditorPerformanceStatusBarMenuCommands : public TCommands<FEditorPerformanceStatusBarMenuCommands>
{
public:

	FEditorPerformanceStatusBarMenuCommands();

	virtual void RegisterCommands() override;

private:

	static void ChangeSettings_Clicked();
	static void ViewPerformanceReport_Clicked();
	
public:

	TSharedPtr< FUICommandInfo > ChangeSettings;
	TSharedPtr< FUICommandInfo > ViewPerformanceReport;
	static TSharedRef<FUICommandList> ActionList;
};

class SEditorPerformanceStatusBarWidget : public SCompoundWidget
{
	SLATE_BEGIN_ARGS(SEditorPerformanceStatusBarWidget) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

private:

	FText							GetTitleToolTipText() const;
	FText							GetTitleText() const;
	const FSlateBrush*				GetStatusIcon() const;
	const FSlateColor				GetStatusIconColor() const;
	FText							GetStatusToolTipText() const;
	TSharedRef<SWidget>				CreateStatusBarMenu();
	EActiveTimerReturnType			UpdateState(double InCurrentTime, float InDeltaTime);
	
	TSharedPtr<SNotificationItem>	NotificationItem;
	EEditorPerformanceState			EditorPerformanceState= EEditorPerformanceState::Good;
	FText							EditorPerformanceStateMessage;
	FText							CurrentNotificationMessage;
	FName							CurrentNotificationName;
	TArray<FName>					AcknowledgedNotifications;
	uint32							WarningCount = 0;

};

