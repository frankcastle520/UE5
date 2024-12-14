// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AudioMaterialEnvelopeSettings.h"
#include "AudioMaterialSlate/AudioMaterialSlateTypes.h"
#include "Components/Widget.h"
#include "Delegates/Delegate.h"
#include "AudioMaterialEnvelope.generated.h"

class SAudioMaterialEnvelope;

/**
 * A simple widget that shows a envelope curve Depending on given AudioMaterialEnvelopeSetings
 * Rendered by using material instead of texture.
 *
 * * No Children
 */
UCLASS()
class AUDIOWIDGETS_API UAudioMaterialEnvelope : public UWidget
{
	GENERATED_BODY()

public:

	UAudioMaterialEnvelope();

	/** The Envelope's style */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Style", meta = (DisplayName = "Style", ShowOnlyInnerProperties))
	FAudioMaterialEnvelopeStyle WidgetStyle;

	/**Envelope settings*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Appearance")
	FAudioMaterialEnvelopeSettings EnvelopeSettings;

public:

#if WITH_EDITOR
	virtual const FText GetPaletteCategory() override;
#endif

	// UWidget
	virtual void SynchronizeProperties() override;
	// End of UWidget

	// UVisual
	virtual void ReleaseSlateResources(bool bReleaseChildren) override;
	// End of UVisual

protected:

	// UWidget
	virtual TSharedRef<SWidget> RebuildWidget() override;
	// End of UWidget

private:

	/** Native Slate Widget */
	TSharedPtr<SAudioMaterialEnvelope> EnvelopeCurve;

};
