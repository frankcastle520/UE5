// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Chaos/ChaosEngineInterface.h"
#include "SimModule/SimulationModuleBase.h"

namespace Chaos
{

/** Suspension world ray/shape trace start and end positions */
struct CHAOSVEHICLESCORE_API FSpringTrace
{
	FVector Start;
	FVector End;

	FVector TraceDir()
	{
		FVector Dir(End - Start);
		return Dir.FVector::GetSafeNormal();
	}

	float Length()
	{
		FVector Dir(End - Start);
		return Dir.Size();
	}
};

/** Suspension target point data. */
struct CHAOSVEHICLESCORE_API FSuspensionTargetPoint
{
	FSuspensionTargetPoint() {}

	FSuspensionTargetPoint(const FVector& InTargetPosition, const FVector& InImpactNormal, const float InHitDistance, const bool bInWheelInContact, const TEnumAsByte<EPhysicalSurface> InSurfaceType)
		: TargetPosition(InTargetPosition)
		, ImpactNormal(InImpactNormal)
		, HitDistance(InHitDistance)
		, bWheelInContact(bInWheelInContact)
		, SurfaceType(InSurfaceType)
	{}

	void Serialize(FArchive& Ar)
	{
		Ar << TargetPosition;
		Ar << ImpactNormal;
		Ar << HitDistance;
		Ar << bWheelInContact;
		Ar << SurfaceType;
	}

	FVector TargetPosition = FVector::ZeroVector;
	FVector ImpactNormal = FVector::ZeroVector;
	float HitDistance = 0.0f;
	bool bWheelInContact = false;
	TEnumAsByte<EPhysicalSurface> SurfaceType = EPhysicalSurface::SurfaceType_Default;
};

class CHAOSVEHICLESCORE_API FSuspensionBaseInterface 
	: public ISimulationModuleBase
	, public TSimulationModuleTypeable<FSuspensionBaseInterface>
{
public:

	DEFINE_CHAOSSIMTYPENAME(FSuspensionBaseInterface);
	FSuspensionBaseInterface() {}

	virtual ~FSuspensionBaseInterface() {}

	virtual bool IsBehaviourType(eSimModuleTypeFlags InType) const override;

	virtual float GetMaxSpringLength() const = 0;
	
	virtual float GetSpringLength() const = 0;
	virtual void SetSpringLength(float InLength, float WheelRadius) = 0;
	
	virtual void GetWorldRaycastLocation(const FTransform& BodyTransform, float WheelRadius, FSpringTrace& OutTrace) = 0;

	void SetTargetPoint(const FSuspensionTargetPoint& InTargetPoint);
	const FSuspensionTargetPoint& GetTargetPoint() const { return TargetPoint; }
	bool IsWheelInContact() const { return TargetPoint.bWheelInContact; }
	
	void SetWheelSimTreeIndex(int WheelTreeIndexIn) { WheelSimTreeIndex = WheelTreeIndexIn; }
	int GetWheelSimTreeIndex() const { return WheelSimTreeIndex; }
	
	TEnumAsByte<EPhysicalSurface> GetSurfaceType() const { return TargetPoint.SurfaceType; }
	
	void SetImpactNormal(const FVector& NewValue) { TargetPoint.ImpactNormal = NewValue; }
	const FVector& GetImpactNormal() const { return TargetPoint.ImpactNormal; }

	void SetHitDistance(const float NewValue) { TargetPoint.HitDistance = NewValue; }
	float GetHitDistance() const { return TargetPoint.HitDistance; }

	void SetTargetPosition(const FVector& NewValue) { TargetPoint.TargetPosition = NewValue; }
	const FVector& GetTargetPosition() const { return TargetPoint.TargetPosition; }

protected:

	int WheelSimTreeIndex = INVALID_IDX;

private:

	FSuspensionTargetPoint TargetPoint;
};


} // namespace Chaos
