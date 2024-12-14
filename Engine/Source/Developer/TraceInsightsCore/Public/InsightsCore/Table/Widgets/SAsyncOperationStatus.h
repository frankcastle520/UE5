// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

namespace UE::Insights
{

class IAsyncOperationStatusProvider;

// A widget representing the status message used in table / tree views for background operations.
class TRACEINSIGHTSCORE_API SAsyncOperationStatus : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SAsyncOperationStatus) {}

	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, TSharedRef<IAsyncOperationStatusProvider> InStatusProvider);

private:
	EVisibility GetContentVisibility() const;

	FSlateColor GetBackgroundColorAndOpacity() const;
	FSlateColor GetTextColorAndOpacity() const;
	float ComputeOpacity() const;

	FText GetText() const;
	FText GetAnimatedText() const;
	FText GetTooltipText() const;

private:
	TWeakPtr<IAsyncOperationStatusProvider> StatusProvider;
};

} // namespace UE::Insights
