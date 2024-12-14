// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "HAL/Platform.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

namespace UE::MultiUserClient::Replication
{
	class FMultiViewOptions;

	/** Displays FMultiViewOptions in a combo button. */
	class SMultiViewOptions : public SCompoundWidget
    {
    public:

		SLATE_BEGIN_ARGS(SMultiViewOptions){}
		SLATE_END_ARGS()

		void Construct(const FArguments& InArgs, FMultiViewOptions& InViewOptions UE_LIFETIMEBOUND);

	private:

		/** The view options being displayed and mutated by this widget. */
		FMultiViewOptions* ViewOptions = nullptr;

		/** @return The menu content for the combo button */
		TSharedRef<SWidget> GetViewButtonContent() const;
    };
}

