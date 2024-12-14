// Copyright Epic Games, Inc. All Rights Reserved.


#include "SimModule/VehicleBlackboard.h"


void FVehicleBlackboard::Invalidate(FName ObjName)
{
	ObjectsByName.Remove(ObjName);
}

void FVehicleBlackboard::Invalidate(EInvalidationReason Reason)
{
	switch (Reason)
	{
		default:
		case EInvalidationReason::FullReset:
			ObjectsByName.Empty();
			break;

		// TODO: Support other reasons
	}
}

