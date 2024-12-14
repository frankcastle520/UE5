// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AudioMaterialSlate/AudioMaterialSlateTypes.h"
#include "AudioWidgetsStyle.h"
#include "Framework/SlateDelegates.h"
#include "Styling/ISlateStyle.h"
#include "Styling/SlateWidgetStyleAsset.h"
#include "Widgets/SLeafWidget.h"

class UObject;
struct FAudioMaterialButtonStyle;

/**
 * A simple slate that renders button in single material and modifies the material on pressed state change.
 *
 */
class AUDIOWIDGETS_API SAudioMaterialButton : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SAudioMaterialButton)
	: _AudioMaterialButtonStyle(&FAudioWidgetsStyle::Get().GetWidgetStyle<FAudioMaterialButtonStyle>("AudioMaterialButton.Style"))
	{}

	/** The owner object*/
	SLATE_ARGUMENT(TWeakObjectPtr<UObject>, Owner)

	/**State of the button*/
	SLATE_ATTRIBUTE(bool, bIsPressedAttribute)

	/** The style used to draw the button. */
	SLATE_STYLE_ARGUMENT(FAudioMaterialButtonStyle, AudioMaterialButtonStyle)

	/** Called when the button's state changes. */
	SLATE_EVENT(FOnBooleanValueChanged, OnBooleanValueChanged)

	/** Invoked when the mouse is released and a capture ends. */
	SLATE_EVENT(FSimpleDelegate, OnMouseCaptureEnd)

	SLATE_END_ARGS()

	/** Constructs this widget with InArgs */
	void Construct(const FArguments& InArgs);

	/** Press the button */
	void SetPressedState(bool InPressedState);

	/** Apply new material to be used to render the Slate.*/
	UMaterialInstanceDynamic* ApplyNewMaterial();

	/**Set desired size of the Slate*/
	void SetDesiredSizeOverride(const FVector2D InSize);

public:

	FOnBooleanValueChanged OnBooleanValueChanged;

	// Holds a delegate that is executed when the mouse is let up and a capture ends.
	FSimpleDelegate OnMouseCaptureEnd;

protected:

	// SWidget overrides
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;
	virtual FVector2D ComputeDesiredSize(float) const override;
	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)override;

private:

	/**Commits new state*/
	void CommitNewState(bool InPressedState);

private:

	// Holds the owner of the Slate
	TWeakObjectPtr<UObject> Owner;

	// Holds the Modifiable Material that represent the Button
	mutable TWeakObjectPtr<UMaterialInstanceDynamic> DynamicMaterial;

	// Holds the style for the Slate
	const FAudioMaterialButtonStyle* AudioMaterialButtonStyle = nullptr;

	//Current pressed state of this button
	TAttribute<bool> bIsPressedAttribute = false;

	// Holds the optional desired size for the Slate
	TAttribute<TOptional<FVector2D>> DesiredSizeOverride;

};
