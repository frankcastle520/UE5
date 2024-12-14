// Copyright Epic Games, Inc. All Rights Reserved.

#include "Effector/Extensions/CEEffectorForceExtension.h"

#include "CEClonerEffectorShared.h"
#include "Effector/CEEffectorComponent.h"

void UCEEffectorForceExtension::SetForcesEnabled(bool bInForcesEnabled)
{
	if (bForcesEnabled == bInForcesEnabled)
	{
		return;
	}

	bForcesEnabled = bInForcesEnabled;
	UpdateExtensionParameters(/** UpdateCloners */ true);
}

void UCEEffectorForceExtension::SetAttractionForceStrength(float InForceStrength)
{
	if (FMath::IsNearlyEqual(AttractionForceStrength, InForceStrength))
	{
		return;
	}

	AttractionForceStrength = InForceStrength;
	UpdateExtensionParameters();
}

void UCEEffectorForceExtension::SetAttractionForceFalloff(float InForceFalloff)
{
	if (FMath::IsNearlyEqual(AttractionForceFalloff, InForceFalloff))
	{
		return;
	}

	AttractionForceFalloff = InForceFalloff;
	UpdateExtensionParameters();
}

void UCEEffectorForceExtension::SetGravityForceEnabled(bool bInForceEnabled)
{
	if (bGravityForceEnabled == bInForceEnabled)
	{
		return;
	}

	bGravityForceEnabled = bInForceEnabled;
	UpdateExtensionParameters(/** UpdateCloners */ true);
}

void UCEEffectorForceExtension::SetGravityForceAcceleration(const FVector& InAcceleration)
{
	if (GravityForceAcceleration.Equals(InAcceleration))
	{
		return;
	}

	GravityForceAcceleration = InAcceleration;
	UpdateExtensionParameters();
}

void UCEEffectorForceExtension::SetDragForceEnabled(bool bInEnabled)
{
	if (bDragForceEnabled == bInEnabled)
	{
		return;
	}

	bDragForceEnabled = bInEnabled;
	UpdateExtensionParameters(/** UpdateCloners */ true);
}

void UCEEffectorForceExtension::SetDragForceLinear(float InStrength)
{
	if (FMath::IsNearlyEqual(DragForceLinear, InStrength))
	{
		return;
	}

	DragForceLinear = InStrength;
	UpdateExtensionParameters();
}

void UCEEffectorForceExtension::SetDragForceRotational(float InStrength)
{
	if (FMath::IsNearlyEqual(DragForceRotational, InStrength))
	{
		return;
	}

	DragForceRotational = InStrength;
	UpdateExtensionParameters();
}

void UCEEffectorForceExtension::SetVectorNoiseForceEnabled(bool bInEnabled)
{
	if (bVectorNoiseForceEnabled == bInEnabled)
	{
		return;
	}

	bVectorNoiseForceEnabled = bInEnabled;
	UpdateExtensionParameters(/** UpdateCloners */ true);
}

void UCEEffectorForceExtension::SetVectorNoiseForceAmount(float InAmount)
{
	if (FMath::IsNearlyEqual(VectorNoiseForceAmount, InAmount))
	{
		return;
	}

	VectorNoiseForceAmount = InAmount;
	UpdateExtensionParameters();
}

void UCEEffectorForceExtension::SetOrientationForceEnabled(bool bInForceEnabled)
{
	if (bOrientationForceEnabled == bInForceEnabled)
	{
		return;
	}

	bOrientationForceEnabled = bInForceEnabled;
	UpdateExtensionParameters(/** UpdateCloners */ true);
}

void UCEEffectorForceExtension::SetOrientationForceRate(float InForceOrientationRate)
{
	if (FMath::IsNearlyEqual(OrientationForceRate, InForceOrientationRate))
	{
		return;
	}

	OrientationForceRate = InForceOrientationRate;
	UpdateExtensionParameters();
}

void UCEEffectorForceExtension::SetOrientationForceMin(const FVector& InForceOrientationMin)
{
	if (OrientationForceMin.Equals(InForceOrientationMin))
	{
		return;
	}

	OrientationForceMin = InForceOrientationMin;
	UpdateExtensionParameters();
}

void UCEEffectorForceExtension::SetOrientationForceMax(const FVector& InForceOrientationMax)
{
	if (OrientationForceMax.Equals(InForceOrientationMax))
	{
		return;
	}

	OrientationForceMax = InForceOrientationMax;
	UpdateExtensionParameters();
}

void UCEEffectorForceExtension::SetVortexForceEnabled(bool bInForceEnabled)
{
	if (bVortexForceEnabled == bInForceEnabled)
	{
		return;
	}

	bVortexForceEnabled = bInForceEnabled;
	UpdateExtensionParameters(/** UpdateCloners */ true);
}

void UCEEffectorForceExtension::SetVortexForceAmount(float InForceVortexAmount)
{
	if (FMath::IsNearlyEqual(VortexForceAmount, InForceVortexAmount))
	{
		return;
	}

	VortexForceAmount = InForceVortexAmount;
	UpdateExtensionParameters();
}

void UCEEffectorForceExtension::SetVortexForceAxis(const FVector& InForceVortexAxis)
{
	if (VortexForceAxis.Equals(InForceVortexAxis))
	{
		return;
	}

	VortexForceAxis = InForceVortexAxis;
	UpdateExtensionParameters();
}

void UCEEffectorForceExtension::SetCurlNoiseForceEnabled(bool bInForceEnabled)
{
	if (bCurlNoiseForceEnabled == bInForceEnabled)
	{
		return;
	}

	bCurlNoiseForceEnabled = bInForceEnabled;
	UpdateExtensionParameters(/** UpdateCloners */ true);
}

void UCEEffectorForceExtension::SetCurlNoiseForceStrength(float InForceCurlNoiseStrength)
{
	if (FMath::IsNearlyEqual(CurlNoiseForceStrength, InForceCurlNoiseStrength))
	{
		return;
	}

	CurlNoiseForceStrength = InForceCurlNoiseStrength;
	UpdateExtensionParameters();
}

void UCEEffectorForceExtension::SetCurlNoiseForceFrequency(float InForceCurlNoiseFrequency)
{
	if (FMath::IsNearlyEqual(CurlNoiseForceFrequency, InForceCurlNoiseFrequency))
	{
		return;
	}

	CurlNoiseForceFrequency = InForceCurlNoiseFrequency;
	UpdateExtensionParameters();
}

void UCEEffectorForceExtension::SetAttractionForceEnabled(bool bInForceEnabled)
{
	if (bAttractionForceEnabled == bInForceEnabled)
	{
		return;
	}

	bAttractionForceEnabled = bInForceEnabled;
	UpdateExtensionParameters(/** UpdateCloners */ true);
}

void UCEEffectorForceExtension::OnExtensionParametersChanged(UCEEffectorComponent* InComponent)
{
	Super::OnExtensionParametersChanged(InComponent);

	FCEClonerEffectorChannelData& ChannelData = InComponent->GetChannelData();

	if (bForcesEnabled && bOrientationForceEnabled)
	{
		ChannelData.OrientationForceRate = OrientationForceRate;
		ChannelData.OrientationForceMin = OrientationForceMin;
		ChannelData.OrientationForceMax = OrientationForceMax;
	}
	else
	{
		ChannelData.OrientationForceRate = 0.f;
		ChannelData.OrientationForceMin = FVector::ZeroVector;
		ChannelData.OrientationForceMax = FVector::ZeroVector;
	}

	if (bForcesEnabled && bVortexForceEnabled)
	{
		ChannelData.VortexForceAmount = VortexForceAmount;
		ChannelData.VortexForceAxis = VortexForceAxis;
	}
	else
	{
		ChannelData.VortexForceAmount = 0.f;
		ChannelData.VortexForceAxis = FVector::ZeroVector;
	}

	if (bForcesEnabled && bCurlNoiseForceEnabled)
	{
		ChannelData.CurlNoiseForceStrength = CurlNoiseForceStrength;
		ChannelData.CurlNoiseForceFrequency = CurlNoiseForceFrequency;
	}
	else
	{
		ChannelData.CurlNoiseForceStrength = 0.f;
		ChannelData.CurlNoiseForceFrequency = 0.f;
	}

	if (bForcesEnabled && bAttractionForceEnabled)
	{
		ChannelData.AttractionForceStrength = AttractionForceStrength;
		ChannelData.AttractionForceFalloff = AttractionForceFalloff;
	}
	else
	{
		ChannelData.AttractionForceStrength = 0.f;
		ChannelData.AttractionForceFalloff = 0.f;
	}

	if (bForcesEnabled && bGravityForceEnabled)
	{
		ChannelData.GravityForceAcceleration = GravityForceAcceleration;
	}
	else
	{
		ChannelData.GravityForceAcceleration = FVector::ZeroVector;
	}

	if (bForcesEnabled && bDragForceEnabled)
	{
		ChannelData.DragForceLinear = DragForceLinear;
		ChannelData.DragForceRotational = DragForceRotational;
	}
	else
	{
		ChannelData.DragForceLinear = 0.f;
		ChannelData.DragForceRotational = 0.f;
	}

	if (bForcesEnabled && bVectorNoiseForceEnabled)
	{
		ChannelData.VectorNoiseForceAmount = VectorNoiseForceAmount;
	}
	else
	{
		ChannelData.VectorNoiseForceAmount = 0.f;
	}
}

void UCEEffectorForceExtension::OnForceOptionsChanged()
{
	UpdateExtensionParameters(/** UpdateCloners */ true);
}

#if WITH_EDITOR
const TCEPropertyChangeDispatcher<UCEEffectorForceExtension> UCEEffectorForceExtension::PropertyChangeDispatcher =
{
	/** Force */
	{ GET_MEMBER_NAME_CHECKED(UCEEffectorForceExtension, bForcesEnabled), &UCEEffectorForceExtension::OnForceOptionsChanged },
	{ GET_MEMBER_NAME_CHECKED(UCEEffectorForceExtension, bOrientationForceEnabled), &UCEEffectorForceExtension::OnForceOptionsChanged },
	{ GET_MEMBER_NAME_CHECKED(UCEEffectorForceExtension, OrientationForceRate), &UCEEffectorForceExtension::OnForceOptionsChanged },
	{ GET_MEMBER_NAME_CHECKED(UCEEffectorForceExtension, OrientationForceMin), &UCEEffectorForceExtension::OnForceOptionsChanged },
	{ GET_MEMBER_NAME_CHECKED(UCEEffectorForceExtension, OrientationForceMax), &UCEEffectorForceExtension::OnForceOptionsChanged },
	{ GET_MEMBER_NAME_CHECKED(UCEEffectorForceExtension, bVortexForceEnabled), &UCEEffectorForceExtension::OnForceOptionsChanged },
	{ GET_MEMBER_NAME_CHECKED(UCEEffectorForceExtension, VortexForceAmount), &UCEEffectorForceExtension::OnForceOptionsChanged },
	{ GET_MEMBER_NAME_CHECKED(UCEEffectorForceExtension, VortexForceAxis), &UCEEffectorForceExtension::OnForceOptionsChanged },
	{ GET_MEMBER_NAME_CHECKED(UCEEffectorForceExtension, bCurlNoiseForceEnabled), &UCEEffectorForceExtension::OnForceOptionsChanged },
	{ GET_MEMBER_NAME_CHECKED(UCEEffectorForceExtension, CurlNoiseForceStrength), &UCEEffectorForceExtension::OnForceOptionsChanged },
	{ GET_MEMBER_NAME_CHECKED(UCEEffectorForceExtension, CurlNoiseForceFrequency), &UCEEffectorForceExtension::OnForceOptionsChanged },
	{ GET_MEMBER_NAME_CHECKED(UCEEffectorForceExtension, bAttractionForceEnabled), &UCEEffectorForceExtension::OnForceOptionsChanged },
	{ GET_MEMBER_NAME_CHECKED(UCEEffectorForceExtension, AttractionForceStrength), &UCEEffectorForceExtension::OnForceOptionsChanged },
	{ GET_MEMBER_NAME_CHECKED(UCEEffectorForceExtension, AttractionForceFalloff), &UCEEffectorForceExtension::OnForceOptionsChanged },
	{ GET_MEMBER_NAME_CHECKED(UCEEffectorForceExtension, bGravityForceEnabled), &UCEEffectorForceExtension::OnForceOptionsChanged },
	{ GET_MEMBER_NAME_CHECKED(UCEEffectorForceExtension, GravityForceAcceleration), &UCEEffectorForceExtension::OnForceOptionsChanged },
	{ GET_MEMBER_NAME_CHECKED(UCEEffectorForceExtension, bDragForceEnabled), &UCEEffectorForceExtension::OnForceOptionsChanged },
	{ GET_MEMBER_NAME_CHECKED(UCEEffectorForceExtension, DragForceLinear), &UCEEffectorForceExtension::OnForceOptionsChanged },
	{ GET_MEMBER_NAME_CHECKED(UCEEffectorForceExtension, DragForceRotational), &UCEEffectorForceExtension::OnForceOptionsChanged },
	{ GET_MEMBER_NAME_CHECKED(UCEEffectorForceExtension, bVectorNoiseForceEnabled), &UCEEffectorForceExtension::OnForceOptionsChanged },
	{ GET_MEMBER_NAME_CHECKED(UCEEffectorForceExtension, VectorNoiseForceAmount), &UCEEffectorForceExtension::OnForceOptionsChanged },
};

void UCEEffectorForceExtension::PostEditChangeProperty(FPropertyChangedEvent& InPropertyChangedEvent)
{
	Super::PostEditChangeProperty(InPropertyChangedEvent);

	PropertyChangeDispatcher.OnPropertyChanged(this, InPropertyChangedEvent);
}
#endif
