// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UI/Signature/IRCSignatureColumn.h"

class FRCSignatureDescriptionColumn : public IRCSignatureColumn
{
	//~ Begin IRCSignatureColumn
	virtual FName GetColumnId() const override;
	virtual bool ShouldShowColumnByDefault() const override;
	virtual SHeaderRow::FColumn::FArguments ConstructHeaderRowColumn() override;
	virtual TSharedRef<SWidget> ConstructRowWidget(TSharedPtr<FRCSignatureTreeItemBase> InItem, const TSharedRef<SRCSignatureTree>& InList, const TSharedRef<SRCSignatureRow>& InRow) override;
	//~ End IRCSignatureColumn
};
