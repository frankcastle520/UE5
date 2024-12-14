// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Styling/SlateStyleRegistry.h"
#include "Styling/SlateStyle.h"

namespace WorldBrowser
{
	class FWorldBrowserStyle final : public FSlateStyleSet
	{
	public:

		FWorldBrowserStyle();
		virtual ~FWorldBrowserStyle() { FSlateStyleRegistry::UnRegisterSlateStyle(*this); }
	
		static FWorldBrowserStyle& Get()
		{
			static FWorldBrowserStyle Inst;
			return Inst;
		}
	};
}

