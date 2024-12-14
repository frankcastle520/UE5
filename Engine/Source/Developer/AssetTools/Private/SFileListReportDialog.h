// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Input/Reply.h"
#include "Widgets/SCompoundWidget.h"

class ITableRow;
class STableViewBase;

class SFileListReportDialog : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SFileListReportDialog) {}

		SLATE_ARGUMENT(FText, Header)
		SLATE_ARGUMENT(TArray<FText>, Files)

	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	static void OpenDialog(const FText& InTitle, const FText& InHeader, const TArray<FText>& InFiles, bool bOpenAsModal = false);

private:
	TSharedRef<ITableRow> MakeListViewWidget(TSharedRef<FText> Item, const TSharedRef<STableViewBase>& OwnerTable);

	FReply CloseClicked();

private:
	FText Header;
	TArray< TSharedRef<FText> > Files;
};
