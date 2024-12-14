// Copyright Epic Games, Inc. All Rights Reserved.

#include "Animators/PropertyAnimatorBounce.h"

#include "Properties/Handlers/PropertyAnimatorCoreHandlerBase.h"
#include "Properties/PropertyAnimatorFloatContext.h"
#include "PropertyAnimatorShared.h"

void UPropertyAnimatorBounce::SetInvertEffect(bool bInvert)
{
	if (bInvertEffect == bInvert)
	{
		return;
	}

	bInvertEffect = bInvert;
	OnInvertEffect();
}

void UPropertyAnimatorBounce::OnAnimatorRegistered(FPropertyAnimatorCoreMetadata& InMetadata)
{
	Super::OnAnimatorRegistered(InMetadata);

	InMetadata.Name = TEXT("Bounce");
}

bool UPropertyAnimatorBounce::EvaluateProperty(const FPropertyAnimatorCoreData& InPropertyData, UPropertyAnimatorCoreContext* InContext, FInstancedPropertyBag& InParameters, FInstancedPropertyBag& OutEvaluationResult) const
{
	const float Frequency = InParameters.GetValueDouble(FrequencyParameterName).GetValue();
	const double TimeElapsed = InParameters.GetValueDouble(TimeElapsedParameterName).GetValue();

	double TimeProgress = FMath::Fmod(TimeElapsed, 1.f / Frequency);

	TimeProgress = bInvertEffect ? TimeProgress : 1 - TimeProgress;

	const double EasingValue = UE::PropertyAnimator::Easing::Bounce(TimeProgress, EPropertyAnimatorEasingType::In);

	InParameters.AddProperty(AlphaParameterName, EPropertyBagPropertyType::Float);
	InParameters.SetValueFloat(AlphaParameterName, EasingValue);

	return InContext->EvaluateProperty(InPropertyData, InParameters, OutEvaluationResult);
}
