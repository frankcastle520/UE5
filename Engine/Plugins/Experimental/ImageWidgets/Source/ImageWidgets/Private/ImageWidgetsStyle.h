// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Styling/SlateStyle.h"

namespace UE::ImageWidgets
{
	/**
	 * Style declarations for the image widgets.
	 */
	class FImageWidgetsStyle : public FSlateStyleSet
	{
	public:
		static FName StyleName;

		static FImageWidgetsStyle& Get();

	private:
		FImageWidgetsStyle();
		virtual ~FImageWidgetsStyle() override;
	};
}
