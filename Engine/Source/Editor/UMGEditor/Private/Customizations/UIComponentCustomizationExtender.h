// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Customizations/IBlueprintWidgetCustomizationExtender.h"
#include "Blueprint/UserWidget.h"
#include "UObject/WeakObjectPtr.h"

class SWidget;

class FUIComponentCustomizationExtender : public IBlueprintWidgetCustomizationExtender
{
public:
	static TSharedPtr<FUIComponentCustomizationExtender> MakeInstance();
	virtual void CustomizeDetails(IDetailLayoutBuilder& InDetailLayout, const TArrayView<UWidget*> InWidgets, const TSharedRef<FWidgetBlueprintEditor>& InWidgetBlueprintEditor);

private:
	/** The selected widget in the details panel. */
	TWeakObjectPtr<class UWidget> Widget;
	TWeakPtr<FWidgetBlueprintEditor> WidgetBlueprintEditor;

};
