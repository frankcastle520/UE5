// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Templates/SharedPointer.h"
#include "Types/SlateEnums.h"
#include "Input/Reply.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SCompoundWidget.h"

class SWidget;

class SEditorPerformanceReportDialog : public SCompoundWidget
{
	SLATE_BEGIN_ARGS(SEditorPerformanceReportDialog) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

private:
	
	EActiveTimerReturnType UpdateGridPanels(double InCurrentTime, float InDeltaTime);
	
	TSharedRef<SWidget> GetKPIGridPanel();
	TSharedRef<SWidget> GetSettingsGridPanel();
	TSharedRef<SWidget> GetHintGridPanel();

	SVerticalBox::FSlot* SettingsGridSlot = nullptr;
	SVerticalBox::FSlot* KPIGridSlot = nullptr;
	SVerticalBox::FSlot* HintGridSlot = nullptr;
	uint32 CurrentHintIndex=0;

	const TArray<FName> NotifcationOptions = { FName("Notify"), FName("Ignore") };
	const TArray<FName> WarningFilterOptions = { FName("Show All"), FName("Warnings Only") };
};

