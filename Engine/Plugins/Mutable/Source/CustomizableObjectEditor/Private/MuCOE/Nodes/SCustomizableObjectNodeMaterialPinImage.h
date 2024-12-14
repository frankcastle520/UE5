// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "SCustomizableObjectNodePin.h"
#include "SGraphPin.h"

class SWidget;
class UEdGraphPin;
struct FSlateBrush;


/** Implements the "MUTABLE" and "PASSTHROUGH" text next to the pin name. */
class SCustomizableObjectNodeMaterialPinImage : public SCustomizableObjectNodePin
{
public:

	SLATE_BEGIN_ARGS(SCustomizableObjectNodeMaterialPinImage) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, UEdGraphPin* InGraphPinObj);

	TSharedRef<SWidget>	GetDefaultValueWidget() override;

private:
	/** Return pin state text. */
	FText GetDefaultValueText() const;

	/** Return true if the pin state is visible. */
	EVisibility GetDefaultValueVisibility() const;

	/** Return pin tool tip. */
	FText GetPinTooltipText() const;
};
