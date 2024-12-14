// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Widgets/SWindow.h"

class FSequencerTextFilterExpressionContext;
enum class ESequencerTextFilterValueType : uint8;

class SFilterExpressionHelpDialog : public SWindow
{
public:
	static constexpr float MaxDesiredWidth = 460.f;
	static constexpr float MaxDesiredHeight = 560.f;

	SLATE_BEGIN_ARGS(SFilterExpressionHelpDialog)
		: _DialogTitle(NSLOCTEXT("SFilterExpressionHelpDialog", "DialogTitle", "Text Filter Expression Help"))
		, _HeaderText(NSLOCTEXT("SFilterExpressionHelpDialog", "HeaderText", "Text Filter Expressions"))
		, _DocumentationLink(TEXT("https://dev.epicgames.com/documentation/en-us/unreal-engine/advanced-search-syntax-in-unreal-engine"))
	{}
		SLATE_ARGUMENT(FText, DialogTitle)
		SLATE_ARGUMENT(FText, HeaderText)
		SLATE_ARGUMENT(FString, DocumentationLink)
		SLATE_ARGUMENT(TArray<TSharedRef<FSequencerTextFilterExpressionContext>>, TextFilterExpressionContexts)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

protected:
	static const FSlateColor KeyColor;
	static const FSlateColor ValueColor;

	TSharedRef<SWidget> ConstructDialogHeader();
	TSharedRef<SWidget> ConstructExpressionWidgetList();
	TSharedRef<SWidget> ConstructExpressionWidget(const TSharedPtr<FSequencerTextFilterExpressionContext>& InExpressionContext);
	TSharedRef<SWidget> ConstructKeysWidget(const TSet<FName>& InKeys);
	TSharedRef<SWidget> ConstructValueWidget(const ESequencerTextFilterValueType InValueType);

	void OpenDocumentationLink() const;

	FText HeaderText;
	FString DocumentationLink;

	TArray<TSharedRef<FSequencerTextFilterExpressionContext>> TextFilterExpressionContexts;
};
