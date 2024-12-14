// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AudioMaterialSlate/AudioMaterialSlateTypes.h"
#include "Components/Widget.h"
#include "Delegates/Delegate.h"
#include "AudioMaterialButton.generated.h"

class SAudioMaterialButton;
struct FAudioMaterialButtonStyle;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnButtonPressedChangedEvent, bool, bIsPressed);


/**
 * A simple widget that shows a button
 * Button is rendered by using material instead of texture.
 *
 * * No Children
 */
UCLASS()
class AUDIOWIDGETS_API UAudioMaterialButton : public UWidget
{
	GENERATED_BODY()

public:

	UAudioMaterialButton();

	/** The button's style */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Style", meta = (DisplayName = "Style", ShowOnlyInnerProperties))
	FAudioMaterialButtonStyle WidgetStyle;

public:
#if WITH_EDITOR
	virtual const FText GetPaletteCategory() override;
#endif // WITH_EDITOR


	// UWidget
	virtual void SynchronizeProperties() override;
	// End of UWidget

	// UVisual
	virtual void ReleaseSlateResources(bool bReleaseChildren) override;
	// End of UVisual

		/** Gets the current value of the slider.*/
	UFUNCTION(BlueprintPure, Category = "Audio Widgets| Audio Material Button")
	bool GetIsPressed() const;

	/** Sets the current value of the slider. InValue is Clamped between 0.f - 1.f */
	UFUNCTION(BlueprintCallable, Category = "Audio Widgets| Audio Material Button")
	void SetIsPressed(bool InPressed);

public:

	/** Called when the value is changed by button. */
	UPROPERTY(BlueprintAssignable, Category = "Widget Event")
	FOnButtonPressedChangedEvent OnButtonPressedChangedEvent;

protected:

	// UWidget
	virtual TSharedRef<SWidget> RebuildWidget() override;
	// End of UWidget

	void HandleOnPressedValueChanged(bool InPressedSate);

private:

	/**Default Value of the button*/
	UPROPERTY(EditAnywhere, BlueprintSetter = SetIsPressed, BlueprintGetter = GetIsPressed, Category = "Appearance")
	bool bIsPressed = false;

private:

	/** Native Slate Widget */
	TSharedPtr<SAudioMaterialButton> Button;

};
