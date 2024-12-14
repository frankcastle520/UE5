// Copyright Epic Games, Inc. All Rights Reserved.

#include "Cluster.h"
#include "GraphPartitioner.h"
#include "Rasterizer.h"

namespace Nanite
{

template< bool bHasTangents, bool bHasColors >
void CorrectAttributes( float* Attributes )
{
	float* AttributesPtr = Attributes;

	FVector3f& Normal = *reinterpret_cast< FVector3f* >( AttributesPtr );
	Normal.Normalize();
	AttributesPtr += 3;

	if( bHasTangents )
	{
		FVector3f& TangentX = *reinterpret_cast< FVector3f* >( AttributesPtr );
		AttributesPtr += 3;
	
		TangentX -= ( TangentX | Normal ) * Normal;
		TangentX.Normalize();

		float& TangentYSign = *AttributesPtr++;
		TangentYSign = TangentYSign < 0.0f ? -1.0f : 1.0f;
	}

	if( bHasColors )
	{
		FLinearColor& Color = *reinterpret_cast< FLinearColor* >( AttributesPtr );
		AttributesPtr += 3;

		Color = Color.GetClamped();
	}
}

typedef void (CorrectAttributesFunction)( float* Attributes );

static CorrectAttributesFunction* CorrectAttributesFunctions[ 2 ][ 2 ] =	// [ bHasTangents ][ bHasColors ]
{
	{	CorrectAttributes<false, false>,	CorrectAttributes<false, true>	},
	{	CorrectAttributes<true, false>,		CorrectAttributes<true, true>	}
};

FCluster::FCluster(
	const FConstMeshBuildVertexView& InVerts,
	const TConstArrayView< const uint32 >& InIndexes,
	const TConstArrayView< const int32 >& InMaterialIndexes,
	FBuilderSettings& InSettings,
	uint32 TriBegin, uint32 TriEnd,
	const TConstArrayView< const uint32 >& TriIndexes,
	const TConstArrayView< const uint32 >& SortedTo,
	const FAdjacency& Adjacency )
	: Settings( InSettings )
{
	GUID = (uint64(TriBegin) << 32) | TriEnd;
	
	NumTris = TriEnd - TriBegin;
	//ensure(NumTriangles <= FCluster::ClusterSize);

	Verts.Reserve( NumTris * GetVertSize() );
	Indexes.Reserve( 3 * NumTris );
	MaterialIndexes.Reserve( NumTris );
	ExternalEdges.Reserve( 3 * NumTris );
	NumExternalEdges = 0;

	check(InMaterialIndexes.Num() * 3 == InIndexes.Num());

	TMap< uint32, uint32 > OldToNewIndex;
	OldToNewIndex.Reserve( NumTris );

	for( uint32 i = TriBegin; i < TriEnd; i++ )
	{
		uint32 TriIndex = TriIndexes[i];

		for( uint32 k = 0; k < 3; k++ )
		{
			uint32 OldIndex = InIndexes[ TriIndex * 3 + k ];
			uint32* NewIndexPtr = OldToNewIndex.Find( OldIndex );
			uint32 NewIndex = NewIndexPtr ? *NewIndexPtr : ~0u;

			if( NewIndex == ~0u )
			{
				Verts.AddUninitialized( GetVertSize() );
				NewIndex = NumVerts++;
				OldToNewIndex.Add( OldIndex, NewIndex );

				GetPosition( NewIndex ) = InVerts.Position[OldIndex];
				GetNormal( NewIndex ) = InVerts.TangentZ[OldIndex];

				if( Settings.bHasTangents )
				{
					const float TangentYSign = ((InVerts.TangentZ[OldIndex] ^ InVerts.TangentX[OldIndex]) | InVerts.TangentY[OldIndex]);
					GetTangentX( NewIndex ) = InVerts.TangentX[OldIndex];
					GetTangentYSign( NewIndex ) = TangentYSign < 0.0f ? -1.0f : 1.0f;
				}
	
				if( Settings.bHasColors )
				{
					GetColor( NewIndex ) = InVerts.Color[OldIndex].ReinterpretAsLinear();
				}

				if( Settings.NumTexCoords > 0 )
				{
					FVector2f* UVs = GetUVs( NewIndex );
					for( uint32 UVIndex = 0; UVIndex < Settings.NumTexCoords; UVIndex++ )
					{
						UVs[UVIndex] = InVerts.UVs[UVIndex][OldIndex];
					}
				}

				if (Settings.NumBoneInfluences > 0)
				{
					FVector2f* BoneInfluences = GetBoneInfluences(NewIndex);
					for (uint32 Influence = 0; Influence < Settings.NumBoneInfluences; Influence++)
					{
						BoneInfluences[Influence].X = InVerts.BoneIndices[Influence][OldIndex];
						BoneInfluences[Influence].Y = InVerts.BoneWeights[Influence][OldIndex];
					}
				}

				float* Attributes = GetAttributes( NewIndex );

				// Make sure this vertex is valid from the start
				CorrectAttributesFunctions[ Settings.bHasTangents ][ Settings.bHasColors ]( Attributes );
			}

			Indexes.Add( NewIndex );

			int32 EdgeIndex = TriIndex * 3 + k;
			int32 AdjCount = 0;
			
			Adjacency.ForAll( EdgeIndex,
				[ &AdjCount, TriBegin, TriEnd, &SortedTo ]( int32 EdgeIndex, int32 AdjIndex )
				{
					uint32 AdjTri = SortedTo[ AdjIndex / 3 ];
					if( AdjTri < TriBegin || AdjTri >= TriEnd )
						AdjCount++;
				} );

			ExternalEdges.Add( (int8)AdjCount );
			NumExternalEdges += AdjCount != 0 ? 1 : 0;
		}

		MaterialIndexes.Add( InMaterialIndexes[ TriIndex ] );
	}

	SanitizeVertexData();

	for( uint32 VertexIndex = 0; VertexIndex < NumVerts; VertexIndex++ )
	{
		float* Attributes = GetAttributes( VertexIndex );

		// Make sure this vertex is valid from the start
		CorrectAttributesFunctions[ Settings.bHasTangents ][ Settings.bHasColors ]( Attributes );
	}

	Bound();
}

// Split
FCluster::FCluster(
	FCluster& SrcCluster,
	uint32 TriBegin, uint32 TriEnd,
	const TConstArrayView< const uint32 >& TriIndexes,
	const TConstArrayView< const uint32 >& SortedTo,
	const FAdjacency& Adjacency )
	: Settings( SrcCluster.Settings )
	, MipLevel( SrcCluster.MipLevel )
{
	const uint32 VertSize = GetVertSize();

	GUID = Murmur64( { SrcCluster.GUID, (uint64)TriBegin, (uint64)TriEnd } );

	NumTris = TriEnd - TriBegin;
	
	Verts.Reserve( NumTris * GetVertSize() );
	Indexes.Reserve( 3 * NumTris );
	MaterialIndexes.Reserve( NumTris );
	ExternalEdges.Reserve( 3 * NumTris );
	NumExternalEdges = 0;

	TMap< uint32, uint32 > OldToNewIndex;
	OldToNewIndex.Reserve( NumTris );

	for( uint32 i = TriBegin; i < TriEnd; i++ )
	{
		uint32 TriIndex = TriIndexes[i];

		for( uint32 k = 0; k < 3; k++ )
		{
			uint32 OldIndex = SrcCluster.Indexes[ TriIndex * 3 + k ];
			uint32* NewIndexPtr = OldToNewIndex.Find( OldIndex );
			uint32 NewIndex = NewIndexPtr ? *NewIndexPtr : ~0u;

			if( NewIndex == ~0u )
			{
				Verts.AddUninitialized( VertSize );
				NewIndex = NumVerts++;
				OldToNewIndex.Add( OldIndex, NewIndex );

				FMemory::Memcpy( &GetPosition( NewIndex ), &SrcCluster.GetPosition( OldIndex ), VertSize * sizeof( float ) );
			}

			Indexes.Add( NewIndex );

			int32 EdgeIndex = TriIndex * 3 + k;
			int32 AdjCount = SrcCluster.ExternalEdges[ EdgeIndex ];
			
			Adjacency.ForAll( EdgeIndex,
				[ &AdjCount, TriBegin, TriEnd, &SortedTo ]( int32 EdgeIndex, int32 AdjIndex )
				{
					uint32 AdjTri = SortedTo[ AdjIndex / 3 ];
					if( AdjTri < TriBegin || AdjTri >= TriEnd )
						AdjCount++;
				} );

			ExternalEdges.Add( (int8)AdjCount );
			NumExternalEdges += AdjCount != 0 ? 1 : 0;
		}

		MaterialIndexes.Add( SrcCluster.MaterialIndexes[ TriIndex ] );
	}

	Bound();
}

// Merge
FCluster::FCluster( TArrayView< const FCluster* > Children )
	: Settings( Children[0]->Settings )
{
	const uint32 VertSize = GetVertSize();
	const uint32 NumTrisGuess = ClusterSize * Children.Num();

	Verts.Reserve( NumTrisGuess * VertSize );
	Indexes.Reserve( 3 * NumTrisGuess );
	MaterialIndexes.Reserve( NumTrisGuess );
	ExternalEdges.Reserve( 3 * NumTrisGuess );

	FHashTable VertHashTable( 1 << FMath::FloorLog2( NumTrisGuess ), NumTrisGuess );

	for( const FCluster* Child : Children )
	{
		NumTris			+= Child->NumTris;
		Bounds			+= Child->Bounds;
		SurfaceArea		+= Child->SurfaceArea;

		// Can jump multiple levels but guarantee it steps at least 1.
		MipLevel	= FMath::Max( MipLevel,		Child->MipLevel + 1 );
		LODError	= FMath::Max( LODError,		Child->LODError );
		EdgeLength	= FMath::Max( EdgeLength,	Child->EdgeLength );

		for( int32 i = 0; i < Child->Indexes.Num(); i++ )
		{
			uint32 NewIndex = AddVert( &Child->Verts[ Child->Indexes[i] * VertSize ], VertHashTable );

			Indexes.Add( NewIndex );
		}

		ExternalEdges.Append( Child->ExternalEdges );
		MaterialIndexes.Append( Child->MaterialIndexes );

		GUID = Murmur64( { GUID, Child->GUID } );
	}

	FAdjacency Adjacency = BuildAdjacency();

	int32 ChildIndex = 0;
	int32 MinIndex = 0;
	int32 MaxIndex = Children[0]->ExternalEdges.Num();

	for( int32 EdgeIndex = 0; EdgeIndex < ExternalEdges.Num(); EdgeIndex++ )
	{
		if( EdgeIndex >= MaxIndex )
		{
			ChildIndex++;
			MinIndex = MaxIndex;
			MaxIndex += Children[ ChildIndex ]->ExternalEdges.Num();
		}

		int32 AdjCount = ExternalEdges[ EdgeIndex ];

		Adjacency.ForAll( EdgeIndex,
			[ &AdjCount, MinIndex, MaxIndex ]( int32 EdgeIndex, int32 AdjIndex )
			{
				if( AdjIndex < MinIndex || AdjIndex >= MaxIndex )
					AdjCount--;
			} );

		// This seems like a sloppy workaround for a bug elsewhere but it is possible an interior edge is moved during simplification to
		// match another cluster and it isn't reflected in this count. Sounds unlikely but any hole closing could do this.
		// The only way to catch it would be to rebuild full adjacency after every pass which isn't practical.
		AdjCount = FMath::Max( AdjCount, 0 );

		ExternalEdges[ EdgeIndex ] = (int8)AdjCount;
		NumExternalEdges += AdjCount != 0 ? 1 : 0;
	}

	ensure( NumTris == Indexes.Num() / 3 );
}

float FCluster::Simplify( uint32 TargetNumTris, float TargetError, uint32 LimitNumTris )
{
	if( ( TargetNumTris >= NumTris && TargetError == 0.0f ) || LimitNumTris >= NumTris )
	{
		return 0.0f;
	}

	float UVArea[ MAX_STATIC_TEXCOORDS ] = { 0.0f };
	if( Settings.NumTexCoords > 0 )
	{
		for( uint32 TriIndex = 0; TriIndex < NumTris; TriIndex++ )
		{
			uint32 Index0 = Indexes[ TriIndex * 3 + 0 ];
			uint32 Index1 = Indexes[ TriIndex * 3 + 1 ];
			uint32 Index2 = Indexes[ TriIndex * 3 + 2 ];

			FVector2f* UV0 = GetUVs( Index0 );
			FVector2f* UV1 = GetUVs( Index1 );
			FVector2f* UV2 = GetUVs( Index2 );

			for( uint32 UVIndex = 0; UVIndex < Settings.NumTexCoords; UVIndex++ )
			{
				FVector2f EdgeUV1 = UV1[ UVIndex ] - UV0[ UVIndex ];
				FVector2f EdgeUV2 = UV2[ UVIndex ] - UV0[ UVIndex ];
				float SignedArea = 0.5f * ( EdgeUV1 ^ EdgeUV2 );
				UVArea[ UVIndex ] += FMath::Abs( SignedArea );

				// Force an attribute discontinuity for UV mirroring edges.
				// Quadric could account for this but requires much larger UV weights which raises error on meshes which have no visible issues otherwise.
				MaterialIndexes[ TriIndex ] |= ( SignedArea >= 0.0f ? 1 : 0 ) << ( UVIndex + 24 );
			}
		}
	}

	float TriangleSize = FMath::Sqrt( SurfaceArea / (float)NumTris );
	
	FFloat32 CurrentSize( FMath::Max( TriangleSize, THRESH_POINTS_ARE_SAME ) );
	FFloat32 DesiredSize( 0.25f );
	FFloat32 FloatScale( 1.0f );

	// Lossless scaling by only changing the float exponent.
	int32 Exponent = FMath::Clamp( (int)DesiredSize.Components.Exponent - (int)CurrentSize.Components.Exponent, -126, 127 );
	FloatScale.Components.Exponent = Exponent + 127;	//ExpBias
	// Scale ~= DesiredSize / CurrentSize
	float PositionScale = FloatScale.FloatValue;

	for( uint32 i = 0; i < NumVerts; i++ )
	{
		GetPosition(i) *= PositionScale;
	}
	TargetError *= PositionScale;

	uint32 NumAttributes = GetVertSize() - 3;
	float* AttributeWeights = (float*)FMemory_Alloca( NumAttributes * sizeof( float ) );
	float* WeightsPtr = AttributeWeights;

	// Normal
	*WeightsPtr++ = 1.0f;
	*WeightsPtr++ = 1.0f;
	*WeightsPtr++ = 1.0f;

	if( Settings.bHasTangents )
	{
		// Tangent X
		*WeightsPtr++ = 0.0625f;
		*WeightsPtr++ = 0.0625f;
		*WeightsPtr++ = 0.0625f;

		// Tangent Y Sign
		*WeightsPtr++ = 0.5f;
	}

	if( Settings.bHasColors )
	{
		*WeightsPtr++ = 0.0625f;
		*WeightsPtr++ = 0.0625f;
		*WeightsPtr++ = 0.0625f;
		*WeightsPtr++ = 0.0625f;
	}

	// Normalize UVWeights
	for( uint32 UVIndex = 0; UVIndex < Settings.NumTexCoords; UVIndex++ )
	{
		float UVWeight = 0.0f;
		if( Settings.bLerpUVs )
		{
			float TriangleUVSize = FMath::Sqrt( UVArea[UVIndex] / (float)NumTris );
			TriangleUVSize = FMath::Max( TriangleUVSize, THRESH_UVS_ARE_SAME );
			UVWeight =  1.0f / ( 128.0f * TriangleUVSize );
		}
		*WeightsPtr++ = UVWeight;
		*WeightsPtr++ = UVWeight;
	}

	for (uint32 Influence = 0; Influence < Settings.NumBoneInfluences; Influence++)
	{
		// Set all bone index/weight values to 0.0 so that the closest
		// original vertex to the new position will copy its data wholesale.
		// Similar to the !bLerpUV path, but always used for skinning data.
		float InfluenceWeight = 0.0f;

		*WeightsPtr++ = InfluenceWeight; // Bone index
		*WeightsPtr++ = InfluenceWeight; // Bone weight
	}

	check( ( WeightsPtr - AttributeWeights ) == NumAttributes );

	FMeshSimplifier Simplifier( Verts.GetData(), NumVerts, Indexes.GetData(), Indexes.Num(), MaterialIndexes.GetData(), NumAttributes );

	TMap< TTuple< FVector3f, FVector3f >, int8 > LockedEdges;

	for( int32 EdgeIndex = 0; EdgeIndex < ExternalEdges.Num(); EdgeIndex++ )
	{
		if( ExternalEdges[ EdgeIndex ] )
		{
			uint32 VertIndex0 = Indexes[ EdgeIndex ];
			uint32 VertIndex1 = Indexes[ Cycle3( EdgeIndex ) ];
	
			const FVector3f& Position0 = GetPosition( VertIndex0 );
			const FVector3f& Position1 = GetPosition( VertIndex1 );

			Simplifier.LockPosition( Position0 );
			Simplifier.LockPosition( Position1 );

			LockedEdges.Add( MakeTuple( Position0, Position1 ), ExternalEdges[ EdgeIndex ] );
		}
	}

	Simplifier.SetAttributeWeights( AttributeWeights );
	Simplifier.SetCorrectAttributes( CorrectAttributesFunctions[ Settings.bHasTangents ][ Settings.bHasColors ] );
	Simplifier.SetEdgeWeight( 2.0f );
	Simplifier.SetMaxEdgeLengthFactor( Settings.MaxEdgeLengthFactor );

	float MaxErrorSqr = Simplifier.Simplify(
		NumVerts, TargetNumTris, FMath::Square( TargetError ),
		0, LimitNumTris, MAX_flt );

	check( Simplifier.GetRemainingNumVerts() > 0 );
	check( Simplifier.GetRemainingNumTris() > 0 );

	if( Settings.bPreserveArea )
		Simplifier.PreserveSurfaceArea();

	Simplifier.Compact();
	
	Verts.SetNum( Simplifier.GetRemainingNumVerts() * GetVertSize() );
	Indexes.SetNum( Simplifier.GetRemainingNumTris() * 3 );
	MaterialIndexes.SetNum( Simplifier.GetRemainingNumTris() );
	ExternalEdges.Init( 0, Simplifier.GetRemainingNumTris() * 3 );

	NumVerts = Simplifier.GetRemainingNumVerts();
	NumTris = Simplifier.GetRemainingNumTris();

	NumExternalEdges = 0;
	for( int32 EdgeIndex = 0; EdgeIndex < ExternalEdges.Num(); EdgeIndex++ )
	{
		auto Edge = MakeTuple(
			GetPosition( Indexes[ EdgeIndex ] ),
			GetPosition( Indexes[ Cycle3( EdgeIndex ) ] )
		);
		int8* AdjCount = LockedEdges.Find( Edge );
		if( AdjCount )
		{
			ExternalEdges[ EdgeIndex ] = *AdjCount;
			NumExternalEdges++;
		}
	}

	float InvScale = 1.0f / PositionScale;
	for( uint32 i = 0; i < NumVerts; i++ )
	{
		GetPosition(i) *= InvScale;
		Bounds += GetPosition(i);
	}

	for( uint32 TriIndex = 0; TriIndex < NumTris; TriIndex++ )
	{
		// Remove UV mirroring bits
		MaterialIndexes[ TriIndex ] &= 0xffffff;
	}

	return FMath::Sqrt( MaxErrorSqr ) * InvScale;
}

void FCluster::Split( FGraphPartitioner& Partitioner, const FAdjacency& Adjacency ) const
{
	FDisjointSet DisjointSet( NumTris );
	for( int32 EdgeIndex = 0; EdgeIndex < Indexes.Num(); EdgeIndex++ )
	{
		Adjacency.ForAll( EdgeIndex,
			[ &DisjointSet ]( int32 EdgeIndex0, int32 EdgeIndex1 )
			{
				if( EdgeIndex0 > EdgeIndex1 )
					DisjointSet.UnionSequential( EdgeIndex0 / 3, EdgeIndex1 / 3 );
			} );
	}

	auto GetCenter = [ this ]( uint32 TriIndex )
	{
		FVector3f Center;
		Center  = GetPosition( Indexes[ TriIndex * 3 + 0 ] );
		Center += GetPosition( Indexes[ TriIndex * 3 + 1 ] );
		Center += GetPosition( Indexes[ TriIndex * 3 + 2 ] );
		return Center * (1.0f / 3.0f);
	};

	Partitioner.BuildLocalityLinks( DisjointSet, Bounds, MaterialIndexes, GetCenter );

	auto* RESTRICT Graph = Partitioner.NewGraph( NumTris * 3 );

	for( uint32 i = 0; i < NumTris; i++ )
	{
		Graph->AdjacencyOffset[i] = Graph->Adjacency.Num();

		uint32 TriIndex = Partitioner.Indexes[i];

		// Add shared edges
		for( int k = 0; k < 3; k++ )
		{
			Adjacency.ForAll( 3 * TriIndex + k,
				[ &Partitioner, Graph ]( int32 EdgeIndex, int32 AdjIndex )
				{
					Partitioner.AddAdjacency( Graph, AdjIndex / 3, 4 * 65 );
				} );
		}

		Partitioner.AddLocalityLinks( Graph, TriIndex, 1 );
	}
	Graph->AdjacencyOffset[ NumTris ] = Graph->Adjacency.Num();

	Partitioner.PartitionStrict( Graph, false );
}

FAdjacency FCluster::BuildAdjacency() const
{
	FAdjacency Adjacency( Indexes.Num() );
	FEdgeHash EdgeHash( Indexes.Num() );

	for( int32 EdgeIndex = 0; EdgeIndex < Indexes.Num(); EdgeIndex++ )
	{
		Adjacency.Direct[ EdgeIndex ] = -1;

		EdgeHash.ForAllMatching( EdgeIndex, true,
			[ this ]( int32 CornerIndex )
			{
				return GetPosition( Indexes[ CornerIndex ] );
			},
			[&]( int32 EdgeIndex, int32 OtherEdgeIndex )
			{
				Adjacency.Link( EdgeIndex, OtherEdgeIndex );
			} );
	}

	return Adjacency;
}

uint32 FCluster::AddVert( const float* Vert, FHashTable& HashTable )
{
	const uint32 VertSize = GetVertSize();
	const FVector3f& Position = *reinterpret_cast< const FVector3f* >( Vert );

	uint32 Hash = HashPosition( Position );
	uint32 NewIndex;
	for( NewIndex = HashTable.First( Hash ); HashTable.IsValid( NewIndex ); NewIndex = HashTable.Next( NewIndex ) )
	{
		uint32 i;
		for( i = 0; i < VertSize; i++ )
		{
			if( Vert[i] != Verts[ NewIndex * VertSize + i ] )
				break;
		}
		if( i == VertSize )
			break;
	}
	if( !HashTable.IsValid( NewIndex ) )
	{
		Verts.AddUninitialized( VertSize );
		NewIndex = NumVerts++;
		HashTable.Add( Hash, NewIndex );

		FMemory::Memcpy( &GetPosition( NewIndex ), Vert, GetVertSize() * sizeof( float ) );
	}

	return NewIndex;
}

void FCluster::Bound()
{
	Bounds = FBounds3f();
	SurfaceArea = 0.0f;
	
	TArray< FVector3f, TInlineAllocator<128> > Positions;
	Positions.SetNum( NumVerts, EAllowShrinking::No );

	for( uint32 i = 0; i < NumVerts; i++ )
	{
		Positions[i] = GetPosition(i);
		Bounds += Positions[i];
	}
	SphereBounds = FSphere3f( Positions.GetData(), Positions.Num() );
	LODBounds = SphereBounds;
	
	float MaxEdgeLength2 = 0.0f;
	for( int i = 0; i < Indexes.Num(); i += 3 )
	{
		FVector3f v[3];
		v[0] = GetPosition( Indexes[ i + 0 ] );
		v[1] = GetPosition( Indexes[ i + 1 ] );
		v[2] = GetPosition( Indexes[ i + 2 ] );

		FVector3f Edge01 = v[1] - v[0];
		FVector3f Edge12 = v[2] - v[1];
		FVector3f Edge20 = v[0] - v[2];

		MaxEdgeLength2 = FMath::Max( MaxEdgeLength2, Edge01.SizeSquared() );
		MaxEdgeLength2 = FMath::Max( MaxEdgeLength2, Edge12.SizeSquared() );
		MaxEdgeLength2 = FMath::Max( MaxEdgeLength2, Edge20.SizeSquared() );

		float TriArea = 0.5f * ( Edge01 ^ Edge20 ).Size();
		SurfaceArea += TriArea;
	}
	EdgeLength = FMath::Sqrt( MaxEdgeLength2 );
}

void FCluster::Voxelize( float VoxelSize )
{
	const uint32 VertSize = GetVertSize();

	TArray< float > NewVerts;
	TArray< int32 > NewMaterialIndexes;

	TMap< FIntVector3, uint32 > Voxels;

	if( NumTris )
	{
		const float Scale = 1.0f / VoxelSize;
		FVector3f Bias(
			-FMath::Floor( Scale * Bounds.Min.X ),
			-FMath::Floor( Scale * Bounds.Min.Y ),
			-FMath::Floor( Scale * Bounds.Min.Z ) );

		for( uint32 TriIndex = 0; TriIndex < NumTris; TriIndex++ )
		{
			FVector3f Triangle[3];
			for( int k = 0; k < 3; k++ )
				Triangle[k] = GetPosition( Indexes[ TriIndex * 3 + k ] ) * Scale + Bias;
	
			float* Attributes0 = GetAttributes( Indexes[ TriIndex * 3 + 0 ] );
			float* Attributes1 = GetAttributes( Indexes[ TriIndex * 3 + 1 ] );
			float* Attributes2 = GetAttributes( Indexes[ TriIndex * 3 + 2 ] );

			VoxelizeTri( Triangle, FIntVector3( MIN_int32 ), FIntVector3( MAX_int32 ),
				[&]( int32 x, int32 y, int32 z, const FVector3f& Barycentrics )
				{
					FIntVector3 Voxel(x,y,z);

					uint32& NewIndex = Voxels.FindOrAdd( Voxel, ~0u );
					if( NewIndex == ~0u )
					{
						NewIndex = Voxels.Num() - 1;
	
						NewVerts.AddUninitialized( VertSize );
						NewMaterialIndexes.Add( MaterialIndexes[ TriIndex ] );
	
						FVector3f& NewPosition = *reinterpret_cast< FVector3f* >( &NewVerts[ NewIndex * VertSize ] );
						NewPosition = ( FVector3f( Voxel ) - Bias ) * VoxelSize;
			
						float* NewAttributes = &NewVerts[ NewIndex * VertSize + 3 ];
						uint32 AttrSize = VertSize - 3;
						for( uint32 i = 0; i < AttrSize; i++ )
						{
							NewAttributes[i] =
								Attributes0[i] * Barycentrics[0] +
								Attributes1[i] * Barycentrics[1] +
								Attributes2[i] * Barycentrics[2];
						}
						CorrectAttributesFunctions[Settings.bHasTangents][Settings.bHasColors](NewAttributes);
					}
				} );
		}

		Indexes.Empty();
		ExternalEdges.Empty();
		NumExternalEdges = 0;

		NumVerts = Voxels.Num();
		NumTris = 0;
	}
	else
	{
		for( uint32 VertIndex = 0; VertIndex < NumVerts; VertIndex++ )
		{
			FVector3f Position = GetPosition( VertIndex );

			FIntVector3 Voxel;
			Voxel.X = FMath::FloorToInt32( Position.X / VoxelSize );
			Voxel.Y = FMath::FloorToInt32( Position.Y / VoxelSize );
			Voxel.Z = FMath::FloorToInt32( Position.Z / VoxelSize );
	
			uint32& NewIndex = Voxels.FindOrAdd( Voxel, ~0u );
			if( NewIndex == ~0u )
			{
				NewIndex = Voxels.Num() - 1;
	
				NewVerts.AddUninitialized( VertSize );
				NewMaterialIndexes.Add( MaterialIndexes[ VertIndex ] );
	
				FVector3f& NewPosition = *reinterpret_cast< FVector3f* >( &NewVerts[ NewIndex * VertSize ] );
				NewPosition = FVector3f( Voxel ) * VoxelSize;
	
				float* NewAttributes = &NewVerts[ NewIndex * VertSize + 3 ];
				uint32 AttrSize = VertSize - 3;
				FMemory::Memcpy( NewAttributes, GetAttributes( VertIndex ), AttrSize * sizeof( float ) );
			}
		}

		NumVerts = Voxels.Num();
		NumTris = 0;
	}

	Swap( Verts,			NewVerts );
	Swap( MaterialIndexes,	NewMaterialIndexes );

	check( MaterialIndexes.Num() > 0 );
}

void FCluster::BuildMaterialRanges()
{
	check( MaterialRanges.Num() == 0 );
	check( NumTris * 3 == Indexes.Num() );

	TArray< int32, TInlineAllocator<128> > MaterialElements;
	TArray< int32, TInlineAllocator<64> > MaterialCounts;

	MaterialElements.AddUninitialized( MaterialIndexes.Num() );
	MaterialCounts.AddZeroed( NANITE_MAX_CLUSTER_MATERIALS );

	// Tally up number per material index
	for( int32 i = 0; i < MaterialIndexes.Num(); i++ )
	{
		MaterialElements[i] = i;
		MaterialCounts[ MaterialIndexes[i] ]++;
	}

	// Sort by range count descending, and material index ascending.
	// This groups the material ranges from largest to smallest, which is
	// more efficient for evaluating the sequences on the GPU, and also makes
	// the minus one encoding work (the first range must have more than 1 tri).
	MaterialElements.Sort(
		[&]( int32 A, int32 B )
		{
			int32 IndexA = MaterialIndexes[A];
			int32 IndexB = MaterialIndexes[B];
			int32 CountA = MaterialCounts[ IndexA ];
			int32 CountB = MaterialCounts[ IndexB ];

			if( CountA != CountB )
				return CountA > CountB;

			return IndexA < IndexB;
		} );

	FMaterialRange CurrentRange;
	CurrentRange.RangeStart = 0;
	CurrentRange.RangeLength = 0;
	CurrentRange.MaterialIndex = MaterialElements.Num() > 0 ? MaterialIndexes[ MaterialElements[0] ] : 0;

	for( int32 i = 0; i < MaterialElements.Num(); i++ )
	{
		int32 MaterialIndex = MaterialIndexes[ MaterialElements[i] ];

		// Material changed, so add current range and reset
		if (CurrentRange.RangeLength > 0 && MaterialIndex != CurrentRange.MaterialIndex)
		{
			MaterialRanges.Add(CurrentRange);

			CurrentRange.RangeStart = i;
			CurrentRange.RangeLength = 1;
			CurrentRange.MaterialIndex = MaterialIndex;
		}
		else
		{
			++CurrentRange.RangeLength;
		}
	}

	// Add last triangle to range
	if (CurrentRange.RangeLength > 0)
	{
		MaterialRanges.Add(CurrentRange);
	}

	TArray< uint32 >	NewIndexes;
	TArray< int32 >		NewMaterialIndexes;
	
	NewIndexes.AddUninitialized( Indexes.Num() );
	NewMaterialIndexes.AddUninitialized( MaterialIndexes.Num() );
	
	for( uint32 NewIndex = 0; NewIndex < NumTris; NewIndex++ )
	{
		uint32 OldIndex = MaterialElements[ NewIndex ];
		NewIndexes[ NewIndex * 3 + 0 ] = Indexes[ OldIndex * 3 + 0 ];
		NewIndexes[ NewIndex * 3 + 1 ] = Indexes[ OldIndex * 3 + 1 ];
		NewIndexes[ NewIndex * 3 + 2 ] = Indexes[ OldIndex * 3 + 2 ];
		NewMaterialIndexes[ NewIndex ] = MaterialIndexes[ OldIndex ];
	}
	Swap( Indexes,			NewIndexes );
	Swap( MaterialIndexes,	NewMaterialIndexes );
}

static void SanitizeFloat( float& X, float MinValue, float MaxValue, float DefaultValue )
{
	if( X >= MinValue && X <= MaxValue )
		;
	else if( X < MinValue )
		X = MinValue;
	else if( X > MaxValue )
		X = MaxValue;
	else
		X = DefaultValue;
}

static void SanitizeVector( FVector3f& V, float MaxValue, FVector3f DefaultValue )
{
	if ( !(	V.X >= -MaxValue && V.X <= MaxValue &&
			V.Y >= -MaxValue && V.Y <= MaxValue &&
			V.Z >= -MaxValue && V.Z <= MaxValue ) )	// Don't flip condition. This is intentionally written like this to be NaN-safe.
	{
		V = DefaultValue;
	}
}

void FCluster::SanitizeVertexData()
{
	const float FltThreshold = NANITE_MAX_COORDINATE_VALUE;

	for( uint32 VertexIndex = 0; VertexIndex < NumVerts; VertexIndex++ )
	{
		FVector3f& Position = GetPosition( VertexIndex );
		SanitizeFloat( Position.X, -FltThreshold, FltThreshold, 0.0f );
		SanitizeFloat( Position.Y, -FltThreshold, FltThreshold, 0.0f );
		SanitizeFloat( Position.Z, -FltThreshold, FltThreshold, 0.0f );

		FVector3f& Normal = GetNormal( VertexIndex );
		SanitizeVector( Normal, FltThreshold, FVector3f::UpVector );

		if( Settings.bHasTangents )
		{
			FVector3f& TangentX = GetTangentX( VertexIndex );
			SanitizeVector( TangentX, FltThreshold, FVector3f::ForwardVector );

			float& TangentYSign = GetTangentYSign( VertexIndex );
			TangentYSign = TangentYSign < 0.0f ? -1.0f : 1.0f;
		}

		if( Settings.bHasColors )
		{
			FLinearColor& Color = GetColor( VertexIndex );
			SanitizeFloat( Color.R, 0.0f, 1.0f, 1.0f );
			SanitizeFloat( Color.G, 0.0f, 1.0f, 1.0f );
			SanitizeFloat( Color.B, 0.0f, 1.0f, 1.0f );
			SanitizeFloat( Color.A, 0.0f, 1.0f, 1.0f );
		}

		if( Settings.NumTexCoords > 0 )
		{
			FVector2f* UVs = GetUVs( VertexIndex );
			for( uint32 UVIndex = 0; UVIndex < Settings.NumTexCoords; UVIndex++ )
			{
				SanitizeFloat( UVs[ UVIndex ].X, -FltThreshold, FltThreshold, 0.0f );
				SanitizeFloat( UVs[ UVIndex ].Y, -FltThreshold, FltThreshold, 0.0f );
			}
		}

		if (Settings.NumBoneInfluences > 0)
		{
			FVector2f* BoneInfluences = GetBoneInfluences(VertexIndex);
			for (uint32 Influence = 0; Influence < Settings.NumBoneInfluences; Influence++)
			{
				SanitizeFloat(BoneInfluences[Influence].X, -FltThreshold, FltThreshold, 0.0f);
				SanitizeFloat(BoneInfluences[Influence].Y, -FltThreshold, FltThreshold, 0.0f);
			}
		}
	}
}

FArchive& operator<<(FArchive& Ar, FMaterialRange& Range)
{
	Ar << Range.RangeStart;
	Ar << Range.RangeLength;
	Ar << Range.MaterialIndex;
	Ar << Range.BatchTriCounts;
	return Ar;
}

FArchive& operator<<(FArchive& Ar, FStripDesc& Desc)
{
	for (uint32 i = 0; i < 4; i++)
	{
		for (uint32 j = 0; j < 3; j++)
		{
			Ar << Desc.Bitmasks[i][j];
		}
	}
	Ar << Desc.NumPrevRefVerticesBeforeDwords;
	Ar << Desc.NumPrevNewVerticesBeforeDwords;
	return Ar;
}
} // namespace Nanite