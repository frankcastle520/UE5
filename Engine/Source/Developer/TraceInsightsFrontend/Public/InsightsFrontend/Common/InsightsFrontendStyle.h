// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"

#include "Math/Color.h"
#include "Styling/SlateStyle.h"
#include "Styling/SlateTypes.h"
#include "Templates/SharedPointer.h"
#include "Types/SlateEnums.h"
#include "UObject/NameTypes.h"

struct FSlateBrush;

namespace UE::Insights
{

class FInsightsFrontendStyle
{
public:
	static void Initialize();
	static void Shutdown();

	TRACEINSIGHTSFRONTEND_API static FName GetStyleSetName();
	TRACEINSIGHTSFRONTEND_API static const ISlateStyle& Get();

	static const FLinearColor& GetColor(FName PropertyName, const ANSICHAR* Specifier = NULL)
	{
		return StyleInstance->GetColor(PropertyName, Specifier);
	}

	static const FSlateBrush* GetBrush(FName PropertyName, const ANSICHAR* Specifier = NULL)
	{
		return StyleInstance->GetBrush(PropertyName, Specifier);
	}

	static const FSlateBrush* GetOutlineBrush(EHorizontalAlignment HAlign)
	{
		if (HAlign == HAlign_Left)
		{
			return StyleInstance->GetBrush("Border.L");
		}
		else if (HAlign == HAlign_Right)
		{
			return StyleInstance->GetBrush("Border.R");
		}
		else
		{
			return StyleInstance->GetBrush("Border.TB");
		}
	}

	class TRACEINSIGHTSFRONTEND_API FStyle : public FSlateStyleSet
	{
	public:
		FStyle(const FName& InStyleSetName);

		void Initialize();

		void SyncParentStyles();

	public:
		// Styles inherited from the parent style
		FTextBlockStyle NormalText;
		FButtonStyle Button;
		FSlateColor SelectorColor;
		FSlateColor SelectionColor;
		FSlateColor SelectionColor_Inactive;
		FSlateColor SelectionColor_Pressed;
	};

private:
	static TSharedRef<FInsightsFrontendStyle::FStyle> Create();

	TRACEINSIGHTSFRONTEND_API static TSharedPtr<FInsightsFrontendStyle::FStyle> StyleInstance;
};

} // namespace UE::Insights
