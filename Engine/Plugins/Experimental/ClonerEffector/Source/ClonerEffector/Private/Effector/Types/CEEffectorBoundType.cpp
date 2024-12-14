// Copyright Epic Games, Inc. All Rights Reserved.

#include "Effector/Types/CEEffectorBoundType.h"

#include "Effector/CEEffectorComponent.h"

void UCEEffectorBoundType::SetInvertType(bool bInInvert)
{
	if (bInvertType == bInInvert)
	{
		return;
	}

	bInvertType = bInInvert;
	UpdateExtensionParameters();
}

void UCEEffectorBoundType::SetEasing(ECEClonerEasing InEasing)
{
	if (InEasing == Easing)
	{
		return;
	}

	Easing = InEasing;
	UpdateExtensionParameters();
}

void UCEEffectorBoundType::OnExtensionParametersChanged(UCEEffectorComponent* InComponent)
{
	Super::OnExtensionParametersChanged(InComponent);

	FCEClonerEffectorChannelData& ChannelData = InComponent->GetChannelData();
	ChannelData.Easing = Easing;
	ChannelData.Magnitude = bInvertType ? -InComponent->GetMagnitude() : InComponent->GetMagnitude();
}

#if WITH_EDITOR
const TCEPropertyChangeDispatcher<UCEEffectorBoundType> UCEEffectorBoundType::PropertyChangeDispatcher =
{
	{ GET_MEMBER_NAME_CHECKED(UCEEffectorBoundType, bInvertType), &UCEEffectorBoundType::OnExtensionPropertyChanged },
	{ GET_MEMBER_NAME_CHECKED(UCEEffectorBoundType, Easing), &UCEEffectorBoundType::OnExtensionPropertyChanged }
};

void UCEEffectorBoundType::PostEditChangeProperty(FPropertyChangedEvent& InPropertyChangedEvent)
{
	Super::PostEditChangeProperty(InPropertyChangedEvent);

	PropertyChangeDispatcher.OnPropertyChanged(this, InPropertyChangedEvent);
}
#endif
