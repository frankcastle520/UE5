// Copyright Epic Games, Inc. All Rights Reserved.

#include "Effector/Modes/CEEffectorNoiseMode.h"

#include "CEClonerEffectorShared.h"
#include "Effector/CEEffectorComponent.h"

void UCEEffectorNoiseMode::SetLocationStrength(const FVector& InStrength)
{
	if (LocationStrength.Equals(InStrength))
	{
		return;
	}

	LocationStrength = InStrength;
	UpdateExtensionParameters();
}

void UCEEffectorNoiseMode::SetRotationStrength(const FRotator& InStrength)
{
	if (RotationStrength.Equals(InStrength))
	{
		return;
	}

	RotationStrength = InStrength;
	UpdateExtensionParameters();
}

void UCEEffectorNoiseMode::SetScaleStrength(const FVector& InStrength)
{
	if (ScaleStrength.Equals(InStrength))
	{
		return;
	}

	ScaleStrength = InStrength;
	UpdateExtensionParameters();
}

void UCEEffectorNoiseMode::SetPan(const FVector& InPan)
{
	if (Pan.Equals(InPan))
	{
		return;
	}

	Pan = InPan;
	UpdateExtensionParameters();
}

void UCEEffectorNoiseMode::SetFrequency(float InFrequency)
{
	InFrequency = FMath::Max(InFrequency, 0);

	if (FMath::IsNearlyEqual(Frequency, InFrequency))
	{
		return;
	}

	Frequency = InFrequency;
	UpdateExtensionParameters();
}

void UCEEffectorNoiseMode::OnExtensionParametersChanged(UCEEffectorComponent* InComponent)
{
	Super::OnExtensionParametersChanged(InComponent);

	FCEClonerEffectorChannelData& ChannelData = InComponent->GetChannelData();
	ChannelData.LocationDelta = LocationStrength;
	ChannelData.RotationDelta = FVector(RotationStrength.Yaw, RotationStrength.Pitch, RotationStrength.Roll);
	ChannelData.ScaleDelta = ScaleStrength;
	ChannelData.Frequency = Frequency;
	ChannelData.Pan = Pan;
}

#if WITH_EDITOR
const TCEPropertyChangeDispatcher<UCEEffectorNoiseMode> UCEEffectorNoiseMode::PropertyChangeDispatcher =
{
	{ GET_MEMBER_NAME_CHECKED(UCEEffectorNoiseMode, LocationStrength), &UCEEffectorNoiseMode::OnExtensionPropertyChanged },
	{ GET_MEMBER_NAME_CHECKED(UCEEffectorNoiseMode, RotationStrength), &UCEEffectorNoiseMode::OnExtensionPropertyChanged },
	{ GET_MEMBER_NAME_CHECKED(UCEEffectorNoiseMode, ScaleStrength), &UCEEffectorNoiseMode::OnExtensionPropertyChanged },
	{ GET_MEMBER_NAME_CHECKED(UCEEffectorNoiseMode, Pan), &UCEEffectorNoiseMode::OnExtensionPropertyChanged },
	{ GET_MEMBER_NAME_CHECKED(UCEEffectorNoiseMode, Frequency), &UCEEffectorNoiseMode::OnExtensionPropertyChanged },
};

void UCEEffectorNoiseMode::PostEditChangeProperty(FPropertyChangedEvent& InPropertyChangedEvent)
{
	Super::PostEditChangeProperty(InPropertyChangedEvent);

	PropertyChangeDispatcher.OnPropertyChanged(this, InPropertyChangedEvent);
}
#endif
