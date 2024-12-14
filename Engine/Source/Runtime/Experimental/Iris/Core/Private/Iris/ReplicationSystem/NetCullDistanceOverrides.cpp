// Copyright Epic Games, Inc. All Rights Reserved.

#include "Iris/ReplicationSystem/NetCullDistanceOverrides.h"
#include "Iris/Core/IrisMemoryTracker.h"

namespace UE::Net
{

void FNetCullDistanceOverrides::Init(const FNetCullDistanceOverridesInitParams& InitParams)
{
	ValidCullDistanceSqr.Init(InitParams.MaxInternalNetRefIndex);
}

void FNetCullDistanceOverrides::OnMaxInternalNetRefIndexIncreased(UE::Net::Private::FInternalNetRefIndex NewMaxInternalIndex)
{
	ValidCullDistanceSqr.SetNumBits(NewMaxInternalIndex);
}

bool FNetCullDistanceOverrides::ClearCullDistanceSqr(uint32 ObjectIndex)
{
	const bool bWasBitSet = ValidCullDistanceSqr.IsBitSet(ObjectIndex);
	ValidCullDistanceSqr.ClearBit(ObjectIndex);
	return bWasBitSet;
}

void FNetCullDistanceOverrides::SetCullDistanceSqr(uint32 ObjectIndex, float CullDistSqr)
{
	ValidCullDistanceSqr.SetBit(ObjectIndex);
	if (ObjectIndex >= uint32(CullDistanceSqr.Num()))
	{
		LLM_SCOPE_BYTAG(Iris);
		CullDistanceSqr.Add(ObjectIndex + 1U - CullDistanceSqr.Num());
	}
	CullDistanceSqr[ObjectIndex] = CullDistSqr;
}

}
