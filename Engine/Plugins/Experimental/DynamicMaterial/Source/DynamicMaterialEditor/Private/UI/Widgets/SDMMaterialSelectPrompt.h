// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Widgets/SCompoundWidget.h"

class SDMMaterialSelectPrompt : public SCompoundWidget
{
	SLATE_DECLARE_WIDGET(SDMMaterialSelectPrompt, SCompoundWidget)

	SLATE_BEGIN_ARGS(SDMMaterialSelectPrompt) {}
	SLATE_END_ARGS()

public:
	virtual ~SDMMaterialSelectPrompt() override = default;

	void Construct(const FArguments& InArgs);
};
