// Copyright Epic Games, Inc. All Rights Reserved.

#include "NiagaraSystemEmitterState.h"
#include "NiagaraCustomVersion.h"

#if WITH_EDITORONLY_DATA
void FNiagaraEmitterStateData::PostSerialize(const FArchive& Ar)
{
	if (Ar.IsLoading() && Ar.CustomVer(FNiagaraCustomVersion::GUID) < FNiagaraCustomVersion::EmitterStateAddLoopDelayEnabled)
	{
		const FNiagaraStatelessRangeFloat LoopDelayRange = LoopDelay.CalculateRange();
		if ( LoopDelayRange.ParameterOffset != INDEX_NONE || !FMath::IsNearlyZero(LoopDelayRange.Min) || !FMath::IsNearlyZero(LoopDelayRange.Max) )
		{
			bLoopDelayEnabled = true;
		}
	}
}
#endif
