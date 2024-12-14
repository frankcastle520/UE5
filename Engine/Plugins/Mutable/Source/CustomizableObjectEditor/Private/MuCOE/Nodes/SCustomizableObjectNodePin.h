// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "SGraphPin.h"

class SWidget;
class UEdGraphPin;
struct FSlateBrush;


class SCustomizableObjectNodePin : public SGraphPin
{
public:

	SLATE_BEGIN_ARGS(SCustomizableObjectNodePin) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, UEdGraphPin* InGraphPinObj);
	
	// SGraphPin interface
protected:
	virtual const FSlateBrush* GetPinIcon() const override;

private:
	const FSlateBrush* PassThroughImageConnected = nullptr;
	const FSlateBrush* PassThroughImageDisconnected = nullptr;
};
