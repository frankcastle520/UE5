// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Presets/PropertyAnimatorCorePropertyPreset.h"
#include "PropertyAnimatorPresetTextLocation.generated.h"

class AActor;
class UPropertyAnimatorCoreBase;

/**
 * Preset for text character position properties (X, Y, Z) on scene component
 */
UCLASS()
class UPropertyAnimatorPresetTextLocation : public UPropertyAnimatorCorePropertyPreset
{
	GENERATED_BODY()

public:
	UPropertyAnimatorPresetTextLocation()
	{
		PresetName = TEXT("TextCharacterLocation");
	}

protected:
	//~ Begin UPropertyAnimatorCorePresetBase
	virtual void GetPresetProperties(const AActor* InActor, const UPropertyAnimatorCoreBase* InAnimator, TSet<FPropertyAnimatorCoreData>& OutProperties) const override;
	virtual void OnPresetApplied(UPropertyAnimatorCoreBase* InAnimator, const TSet<FPropertyAnimatorCoreData>& InProperties) override;
	virtual bool LoadPreset() override { return true; }
	//~ End UPropertyAnimatorCorePresetBase
};