// Copyright Epic Games, Inc. All Rights Reserved.

#include "BaseBehaviors/DoubleClickBehavior.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(DoubleClickBehavior)


UDoubleClickInputBehavior::UDoubleClickInputBehavior()
{
	HitTestOnRelease = true;
}


FInputCaptureRequest UDoubleClickInputBehavior::WantsCapture(const FInputDeviceState& Input)
{
	if (IsDoubleClicked(Input) && (ModifierCheckFunc == nullptr || ModifierCheckFunc(Input)) )
	{
		FInputRayHit HitResult = Target->IsHitByClick(GetDeviceRay(Input));
		if (HitResult.bHit)
		{
			return FInputCaptureRequest::Begin(this, EInputCaptureSide::Any, HitResult.HitDepth);
		}
	}
	return FInputCaptureRequest::Ignore();
}


