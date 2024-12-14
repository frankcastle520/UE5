// Copyright Epic Games, Inc. All Rights Reserved.

#include "DataWrappers/ChaosVDDebugShapeDataWrapper.h"

void FChaosVDDebugDrawShapeBase::SerializeBase_Internal(FArchive& Ar)
{
	Ar << SolverID;
	Ar << Tag;
	Ar << Color;
}

bool FChaosVDDebugDrawBoxDataWrapper::Serialize(FArchive& Ar)
{
	Ar << bHasValidData;

	if (!bHasValidData)
	{
		return !Ar.IsError();
	}

	SerializeBase_Internal(Ar);

	Ar << Box;
	
	return !Ar.IsError();
}

bool FChaosVDDebugDrawSphereDataWrapper::Serialize(FArchive& Ar)
{
	Ar << bHasValidData;

	if (!bHasValidData)
	{
		return !Ar.IsError();
	}

	SerializeBase_Internal(Ar);

	Ar << Origin;
	Ar << Radius;
	
	return !Ar.IsError();
}

bool FChaosVDDebugDrawLineDataWrapper::Serialize(FArchive& Ar)
{
	Ar << bHasValidData;

	if (!bHasValidData)
	{
		return !Ar.IsError();
	}

	SerializeBase_Internal(Ar);

	Ar << StartLocation;
	Ar << EndLocation;
	Ar << bIsArrow;
	
	return !Ar.IsError();
}

bool FChaosVDDebugDrawImplicitObjectDataWrapper::Serialize(FArchive& Ar)
{
	Ar << bHasValidData;

	if (!bHasValidData)
	{
		return !Ar.IsError();
	}

	SerializeBase_Internal(Ar);

	Ar << ImplicitObjectHash;
	Ar << ParentTransform;
	
	return !Ar.IsError();
}
