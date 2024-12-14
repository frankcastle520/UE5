// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Math/Bounds.h"

class FBVHCluster
{
public:
			FBVHCluster( uint32 InNumElements, uint32 InMinPartitionSize, uint32 InMaxPartitionSize );

			template< typename FGetBounds >
	void	Build( const FGetBounds& GetBounds );

	// Inclusive
	struct FRange
	{
		uint32	Begin;
		uint32	End;

		bool operator<( const FRange& Other) const { return Begin < Other.Begin; }
	};
	TArray< FRange >	Ranges;
	TArray< uint32 >	Indexes;
	TArray< uint32 >	SortedTo;

private:
			template< typename FGetBounds >
	void	Build( uint32 Offset, uint32 Num, const FGetBounds& GetBounds );
			template< typename FGetBounds >
	uint32	Split( uint32 Offset, uint32 Num, const FGetBounds& GetBounds );
			template< typename FGetBounds >
	void	Sort( uint32* RESTRICT Dst, uint32* RESTRICT Src, uint32 Num, int32 Dim, const FGetBounds& GetBounds );

	uint32		NumElements;
	uint32		MinPartitionSize;
	uint32		MaxPartitionSize;

	TArray< float >	CostLeft;
	TArray< float >	CostRight;
};

template< typename FGetBounds >
void FBVHCluster::Build( const FGetBounds& GetBounds )
{
	Build( 0, NumElements, GetBounds );

	for( uint32 i = 0; i < NumElements; i++ )
		SortedTo[ Indexes[i] ] = i;
}

template< typename FGetBounds >
void FBVHCluster::Build( uint32 Offset, uint32 Num, const FGetBounds& GetBounds )
{
	if( Num <= MaxPartitionSize )
	{
		Ranges.Add( { Offset, Offset + Num } );
		return;
	}

	uint32 SplitIndex = Split( Offset, Num, GetBounds );

	uint32 Num0 = SplitIndex + 1;
	uint32 Num1 = Num - Num0;
	check( Num1 > 0 );

	Build( Offset, Num0, GetBounds );
	Build( Offset + Num0, Num1, GetBounds );
}

template< typename FGetBounds >
uint32 FBVHCluster::Split( uint32 Offset, uint32 Num, const FGetBounds& GetBounds )
{
	FVector3f	LeastCost( MAX_flt );
	FIntVector	LeastCostSplit( -1 );

	uint32* Unsorted = &Indexes[ Offset ];
	uint32* Sorted = &SortedTo[ Offset ];

	for( int32 Dim = 0; Dim < 3; Dim++ )
	{
		Sort( Sorted, Unsorted, Num, Dim, GetBounds );

		FBounds3f Bounds;

		// SAH sweep forward
		for( uint32 i = 0; i < Num; i++ )
		{
			Bounds += GetBounds( Sorted[i] );

			FVector3f Size = Bounds.Max - Bounds.Min;
			float HalfSurfaceArea = Size.X * Size.Y + Size.X * Size.Z + Size.Y * Size.Z;

			int32 Count = i + 1;
			CostLeft[ Offset + i ] = HalfSurfaceArea * (float)Count;
			//CostLeft[ Offset + i ] = Count * Size.SizeSquared();
		}

		Bounds = FBounds3f();

		// SAH sweep back
		for( int32 i = Num - 1; i >= 0; i-- )
		{
			Bounds += GetBounds( Sorted[i] );

			FVector3f Size = Bounds.Max - Bounds.Min;
			float HalfSurfaceArea = Size.X * Size.Y + Size.X * Size.Z + Size.Y * Size.Z;

			int32 Count = Num - i - 1;
			CostRight[ Offset + i ] = HalfSurfaceArea * (float)Count;
			//CostRight[ Offset + i ] = Count * Size.SizeSquared();
		}

		uint32 NumPartitions = FMath::DivideAndRoundUp( Num, MaxPartitionSize );

		// Find least SAH cost
		for( uint32 i = 0; i < Num - 1; i++ )
		{
			uint32 Num0 = i + 1;
			uint32 Num1 = Num - Num0;

			uint32 NumPartitions0 = FMath::DivideAndRoundUp( Num0, MaxPartitionSize );
			uint32 NumPartitions1 = FMath::DivideAndRoundUp( Num1, MaxPartitionSize );

			if( NumPartitions0 + NumPartitions1 == NumPartitions )
			{
				float Cost = CostLeft[ Offset + i ] + CostRight[ Offset + i + 1 ];
				if( Cost < LeastCost[ Dim ] )
				{
					LeastCost[ Dim ] = Cost;
					LeastCostSplit[ Dim ] = i;
				}
			}
		}

		Swap( Sorted, Unsorted );
	}

	uint32 BestDim = FMath::Min3Index( LeastCost[0], LeastCost[1], LeastCost[2] );
	check( LeastCost[ BestDim ] > 0.0f );

	Sort( Sorted, Unsorted, Num, BestDim, GetBounds );

	// Even number of sorts means final sorted values are in the original array.

	return LeastCostSplit[ BestDim ];
}

template< typename FGetBounds >
void FBVHCluster::Sort( uint32* RESTRICT Dst, uint32* RESTRICT Src, uint32 Num, int32 Dim, const FGetBounds& GetBounds )
{
	RadixSort32( Dst, Src, Num,
		[ this, &GetBounds, Dim ]( uint32 Index )
		{
			FBounds3f Bounds = GetBounds( Index );

			union { float f; uint32 i; } Center;
			Center.f = 0.5f * ( Bounds.Min[ Dim ] + Bounds.Max[ Dim ] );

			uint32 Mask = -int32( Center.i >> 31 ) | 0x80000000;
			return Center.i ^ Mask;
		} );
}