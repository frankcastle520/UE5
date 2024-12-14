// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Styling/SlateStyle.h"

class FSubmitToolStyle : public FSlateStyleSet
{
public:
	virtual const FName& GetStyleSetName() const override;

	static const FSubmitToolStyle& Get();
	static void Shutdown();

	~FSubmitToolStyle();

private:
	FSubmitToolStyle();

	static FName StyleName;
	static TUniquePtr<FSubmitToolStyle> Inst;
};
