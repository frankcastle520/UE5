// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Styling/SlateStyle.h"

namespace UE::ToolWidgets
{
	class FToolWidgetsStyle final
		: public FSlateStyleSet
	{
	public:
		static FName StyleName;

		static TOOLWIDGETS_API FToolWidgetsStyle& Get();

	private:
		FToolWidgetsStyle();
		virtual ~FToolWidgetsStyle() override;
	};
}
