// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

namespace UE::ConcertInsightsClient
{
	class SEditTraceDestinationWidget : public SCompoundWidget
	{
	public:

		SLATE_BEGIN_ARGS(SEditTraceDestinationWidget){}
		SLATE_END_ARGS()

		void Construct(const FArguments& InArgs);
	};
}


