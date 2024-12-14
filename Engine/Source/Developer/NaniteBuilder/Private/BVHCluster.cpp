// Copyright Epic Games, Inc. All Rights Reserved.

#include "BVHCluster.h"

FBVHCluster::FBVHCluster( uint32 InNumElements, uint32 InMinPartitionSize, uint32 InMaxPartitionSize )
	: NumElements( InNumElements )
	, MinPartitionSize( InMinPartitionSize )
	, MaxPartitionSize( InMaxPartitionSize )
{
	Indexes.AddUninitialized( NumElements );
	for( uint32 i = 0; i < NumElements; i++ )
		Indexes[i] = i;

	SortedTo.AddUninitialized( NumElements );
	CostLeft.AddUninitialized( NumElements );
	CostRight.AddUninitialized( NumElements );
}