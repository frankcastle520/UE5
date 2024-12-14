// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Sidebar/ISidebarDrawerContent.h"
#include "Templates/SharedPointer.h"

class FAvaSequencer;
class FName;
class FText;
class ISequencer;
class SWidget;

class FAvaSequencerEaseCurveToolSection : public ISidebarDrawerContent
{
public:
	static const FName UniqueId;

	FAvaSequencerEaseCurveToolSection(const TSharedRef<FAvaSequencer>& InAvaSequencer);

	//~ Begin ISidebarDrawerContent
	virtual FName GetUniqueId() const override;
	virtual FName GetSectionId() const override;
	virtual FText GetSectionDisplayText() const override;
	virtual bool ShouldShowSection() const override;
	virtual int32 GetSortOrder() const override;
	virtual TSharedRef<SWidget> CreateContentWidget() override;
	//~ End ISidebarDrawerContent

protected:
	TWeakPtr<FAvaSequencer> AvaSequencerWeak;
};
