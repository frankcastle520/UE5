// Copyright Epic Games, Inc. All Rights Reserved.

#include "RCSignatureDescriptionColumn.h"
#include "SRCSignatureDescription.h"

#define LOCTEXT_NAMESPACE "RCSignatureDescriptionColumn"

FName FRCSignatureDescriptionColumn::GetColumnId() const
{
	return TEXT("FRCSignatureDescriptionColumn");
}

bool FRCSignatureDescriptionColumn::ShouldShowColumnByDefault() const
{
	return false;
}

SHeaderRow::FColumn::FArguments FRCSignatureDescriptionColumn::ConstructHeaderRowColumn()
{
	return SHeaderRow::Column(GetColumnId())
		.FillWidth(0.5f)
		.DefaultLabel(LOCTEXT("DisplayName", "Description"));
}

TSharedRef<SWidget> FRCSignatureDescriptionColumn::ConstructRowWidget(TSharedPtr<FRCSignatureTreeItemBase> InItem
	, const TSharedRef<SRCSignatureTree>& InList
	, const TSharedRef<SRCSignatureRow>& InRow)
{
	return SNew(SRCSignatureDescription, InItem.ToSharedRef());
}

#undef LOCTEXT_NAMESPACE
