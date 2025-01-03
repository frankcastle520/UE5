// Copyright Epic Games, Inc. All Rights Reserved.

#include "Physics/PhysicsInterfaceUtils.h"
#include "CollisionQueryParams.h"
#include "Physics/PhysicsFiltering.h"
#include "PhysicsInterfaceTypesCore.h"

FCollisionFilterData CreateObjectQueryFilterData(const bool bTraceComplex, const int32 MultiTrace/*=1 if multi. 0 otherwise*/, const struct FCollisionObjectQueryParams & ObjectParam)
{
	/**
	* Format for QueryData :
	*		word0 (meta data - ECollisionQuery. Extendable)
	*
	*		For object queries
	*
	*		word1 (object type queries)
	*		word2 (unused)
	*		word3 (Multi (1) or single (0) (top 8) + Flags (lower 24))
	*/

	FCollisionFilterData NewData;

	NewData.Word0 = (uint32)ECollisionQuery::ObjectQuery;

	if (bTraceComplex)
	{
		NewData.Word3 |= EPDF_ComplexCollision;
	}
	else
	{
		NewData.Word3 |= EPDF_SimpleCollision;
	}

	// get object param bits
	NewData.Word1 = ObjectParam.GetQueryBitfield();

	// if 'nothing', then set no bits
	NewData.Word3 |= CreateChannelAndFilter((ECollisionChannel)MultiTrace, ObjectParam.IgnoreMask);

	return NewData;
}

FCollisionFilterData CreateTraceQueryFilterData(const uint8 MyChannel, const bool bTraceComplex, const FCollisionResponseContainer& InCollisionResponseContainer, const FCollisionQueryParams& Params)
{
	/**
	* Format for QueryData :
	*		word0 (meta data - ECollisionQuery. Extendable)
	*
	*		For trace queries
	*
	*		word1 (blocking channels)
	*		word2 (touching channels)
	*		word3 (MyChannel (top 8) as ECollisionChannel + Flags (lower 24))
	*/

	FCollisionFilterData NewData;

	NewData.Word0 = (uint32)ECollisionQuery::TraceQuery;

	if (bTraceComplex)
	{
		NewData.Word3 |= EPDF_ComplexCollision;
	}
	else
	{
		NewData.Word3 |= EPDF_SimpleCollision;
	}

	// word1 encodes 'what i block', word2 encodes 'what i touch'
	for (int32 i = 0; i<UE_ARRAY_COUNT(InCollisionResponseContainer.EnumArray); i++)
	{
		if (InCollisionResponseContainer.EnumArray[i] == ECR_Block)
		{
			// if i block, set that in word1
			NewData.Word1 |= CRC_TO_BITFIELD(i);
		}
		else if (InCollisionResponseContainer.EnumArray[i] == ECR_Overlap)
		{
			// if i touch, set that in word2
			NewData.Word2 |= CRC_TO_BITFIELD(i);
		}
	}

	// if 'nothing', then set no bits
	NewData.Word3 |= CreateChannelAndFilter((ECollisionChannel)MyChannel, Params.IgnoreMask);

	return NewData;
}

#define TRACE_MULTI		1
#define TRACE_SINGLE	0

/** Utility for creating a PhysX PxFilterData for performing a query (trace) against the scene */
FCollisionFilterData CreateQueryFilterData(const uint8 MyChannel, const bool bTraceComplex, const FCollisionResponseContainer& InCollisionResponseContainer, const struct FCollisionQueryParams& QueryParam, const struct FCollisionObjectQueryParams & ObjectParam, const bool bMultitrace)
{
	//#TODO implement chaos
	if(ObjectParam.IsValid())
	{
		return CreateObjectQueryFilterData(bTraceComplex, (bMultitrace ? TRACE_MULTI : TRACE_SINGLE), ObjectParam);
	}
	else
	{
		return CreateTraceQueryFilterData(MyChannel, bTraceComplex, InCollisionResponseContainer, QueryParam);
	}
}

//
// NOTE: Once the old Create/Destroy methods are deprecated, remove the default implementations
//

#include "PhysicsReplication.h" // Remove when the deprecated Create() function is removed
TUniquePtr<IPhysicsReplication> IPhysicsReplicationFactory::CreatePhysicsReplication(FPhysScene* OwningPhysScene)
{
PRAGMA_DISABLE_DEPRECATION_WARNINGS
	UE_LOG(LogPhysics, Warning, TEXT("\nIPhysicsReplicationFactory::Create has been deprecated in favor of IPhysicsReplicationFactory::CreatePhysicsReplication. Use the new CreatePhysicsReplication method instead moving forward, as the Create function will be removed in future versions. Please update your code to the new API before upgrading to the next release, otherwise your project will no longer compile."));
	return TUniquePtr<IPhysicsReplication>(Create(OwningPhysScene));
PRAGMA_ENABLE_DEPRECATION_WARNINGS
}
