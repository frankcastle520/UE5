// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Styling/ISlateStyle.h"
#include "Styling/SlateStyle.h"

class FDMTextureSetStyle final : public FSlateStyleSet
{
public:
	static const ISlateStyle& Get();

	FDMTextureSetStyle();

	virtual ~FDMTextureSetStyle() override = default;
};
