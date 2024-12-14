// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "FrameRange.h"
#include "PropertyCustomizationHelpers.h"

class CAPTUREDATAEDITOR_API FFrameRangeArrayBuilder : public FDetailArrayBuilder
{
public:

	DECLARE_DELEGATE_RetVal(FFrameNumber, FOnGetCurrentFrame);

	FFrameRangeArrayBuilder(TSharedRef<IPropertyHandle> InBaseProperty, TArray<FFrameRange>& InOutFrameRange, FOnGetCurrentFrame* InOnGetCurrentFrameDelegate = nullptr);

	virtual void GenerateChildContent(IDetailChildrenBuilder& InOutChildrenBuilder) override;

private:

	TArray<FFrameRange>& FrameRange;
	FOnGetCurrentFrame* OnGetCurrentFrameDelegate = nullptr;
};
