// Copyright Epic Games, Inc. All Rights Reserved.

#include "MuR/OpMeshRemove.h"

#include "MuR/MeshPrivate.h"
#include "MuR/MutableTrace.h"
#include "MuR/Platform.h"


namespace mu
{
    void MeshRemoveRecreateSurface(Mesh* Result, const TBitArray<>& UsedVertices, const TBitArray<>& UsedFaces)
    {
        MUTABLE_CPUPROFILER_SCOPE(MeshRemoveRecreateSurface);
		TArray<FSurfaceSubMesh, TInlineAllocator<32>> OrigSubMeshes;
        for (FMeshSurface& ResultSurf : Result->Surfaces)
        {
			OrigSubMeshes = ResultSurf.SubMeshes;
			ResultSurf.SubMeshes.Reset();            

			int32 PrevMeshRangeVertexEnd = 0;
            int32 PrevMeshRangeIndexEnd = 0;
            for (const FSurfaceSubMesh& SubMesh : OrigSubMeshes)
            {
                int32 MeshRangeVertexEnd = PrevMeshRangeVertexEnd; 
                int32 MeshRangeIndexEnd = PrevMeshRangeIndexEnd;
               
                MeshRangeVertexEnd += UsedVertices.CountSetBits(SubMesh.VertexBegin, SubMesh.VertexEnd);
				
                // Only add the mesh if it has remaining vertices. 
                if (MeshRangeVertexEnd > PrevMeshRangeVertexEnd)
                {
					check(SubMesh.IndexBegin % 3 == 0);
					check((SubMesh.IndexEnd - SubMesh.IndexBegin) % 3 == 0);
                    MeshRangeIndexEnd += UsedFaces.CountSetBits(SubMesh.IndexBegin/3, SubMesh.IndexEnd/3)*3;

                    ResultSurf.SubMeshes.Emplace(FSurfaceSubMesh 
                            {
                                PrevMeshRangeVertexEnd, MeshRangeVertexEnd,
                                PrevMeshRangeIndexEnd, MeshRangeIndexEnd,
                                SubMesh.ExternalId 
                            });
                }

                PrevMeshRangeVertexEnd = MeshRangeVertexEnd;
                PrevMeshRangeIndexEnd = MeshRangeIndexEnd;
            }
        }
		
		// Remove Empty surfaces but always keep the first one. 
		// The previous step has eliminated empty submeshes, so it is only needed to check if the surface has 
		// any submesh.
		for (int32 I = Result->Surfaces.Num() - 1; I >= 1; --I)
		{
			if (!Result->Surfaces[I].SubMeshes.Num())
			{
				Result->Surfaces.RemoveAt(I, EAllowShrinking::No);
			}
		}

		check(Result->Surfaces.Num() >= 1);
	
		// Add a defaulted empty submesh if the surface is empty. A surface always needs a submesh 
		// even if empty.
		if (!Result->Surfaces[0].SubMeshes.Num())
		{
			Result->Surfaces[0].SubMeshes.Emplace();
		}
    }

    //---------------------------------------------------------------------------------------------
    struct FIdInterval
    {
        uint64 idStart;
        int32 idPosition;
        int32 size;
    };


    void ExtractVertexIndexIntervals( TArray<FIdInterval>& intervals, const Mesh* pSource )
    {
		MeshVertexIdIteratorConst itVI( pSource );
		FIdInterval current;
		current.idStart = Mesh::InvalidVertexId;
		current.idPosition = 0;
		current.size = 0;
		for ( int32 sv=0; sv<pSource->GetVertexBuffers().GetElementCount(); ++sv )
        {
            uint64 id = itVI.Get();
            ++itVI;

            if (current.idStart== Mesh::InvalidVertexId)
            {
                current.idStart = id;
                current.idPosition = sv;
                current.size = 1;
            }
            else
            {
                if (id==current.idStart+current.size)
                {
                    ++current.size;
                }
                else
                {
                    intervals.Add(current);
                    current.idStart = id;
                    current.idPosition = sv;
                    current.size = 1;
                }
            }
        }

        if (current.idStart!= Mesh::InvalidVertexId)
        {
            intervals.Add(current);
        }
    }


    int64 FindPositionInIntervals( const TArray<FIdInterval>& intervals, int64 id )
    {
        for( const FIdInterval& interval: intervals )
        {
            int64 deltaId = id - interval.idStart;
            if (deltaId>=0 && deltaId<interval.size)
            {
                return interval.idPosition+deltaId;
            }
        }
        return -1;
    }

    // TODO: Remove once the new implementation is proven to be faster for all cases and does not present 
    // any problem. For now keep it as a reference.
	//void MeshRemoveVerticesWithMap( Mesh* Result, const TBitArray<>& RemovedVertices)
	//{
    //    uint32 RemovedVertexCount = RemovedVertices.Num();

	//	int32 FirstFreeVertex = 0;
	//	int32 RemovedIndices = 0;

    //    // Rebuild index buffers

    //    // Map from source vertex index, to new vertex index for used vertices.
    //    // These are indices as in the index buffer, not the absolute vertex index as in the
    //    // vertexbuffer MBS_VERTEXINDEX buffers.
	//	TArray<int32> UsedVertexMap;
    //    TBitArray<> UsedFaces;
	//	UsedVertexMap.Init(-1, Result->GetVertexCount());
    //    UsedFaces.Init(false, Result->GetIndexCount() / 3);  
    //    {
    //        if ( Result->GetIndexBuffers().GetElementSize(0)==4 )
    //        {
    //            MeshBufferIteratorConst<MBF_UINT32,uint32,1> itSource( Result->GetIndexBuffers(), MBS_VERTEXINDEX );
    //            MeshBufferIterator<MBF_UINT32,uint32,1> itDest( Result->GetIndexBuffers(), MBS_VERTEXINDEX );

    //            int32 IndexCount = Result->GetIndexCount();
	//			check(IndexCount%3==0);
    //            for ( int32 f=0; f<IndexCount/3; ++f )
    //            {
    //                uint32 sourceIndices[3];
    //                sourceIndices[0] = (*itSource)[0];
    //                sourceIndices[1] = (*itSource)[1];
    //                sourceIndices[2] = (*itSource)[2];

	//				check(sourceIndices[0] < RemovedVertexCount);
	//				check(sourceIndices[1] < RemovedVertexCount);
	//				check(sourceIndices[2] < RemovedVertexCount);

	//				int32 RemoveCount =  (RemovedVertices[ sourceIndices[0] ]
    //                        + RemovedVertices[ sourceIndices[1] ]
    //                        + RemovedVertices[ sourceIndices[2] ]
    //                        );
	//				bool bFaceRemoved = RemoveCount > 0;
    //                if (!bFaceRemoved)
    //                {
    //                    UsedFaces[f] = true;

    //                    for (int32 i=0;i<3;++i)
    //                    {
    //                        uint32 sourceIndex = sourceIndices[i];

    //                        if (UsedVertexMap[ sourceIndex ] < 0 )
    //                        {
	//							UsedVertexMap[ sourceIndex ] = FirstFreeVertex;
    //                            FirstFreeVertex++;
    //                        }

    //                        uint32 destIndex = UsedVertexMap[ sourceIndex ];
    //                        *(uint32*)itDest.ptr() = destIndex;

    //                        itDest++;
    //                    }
    //                }

    //                itSource+=3;
    //            }

    //            RemovedIndices = itSource - itDest;
    //        }

    //        else if ( Result->GetIndexBuffers().GetElementSize(0)==2 )
    //        {
    //            MeshBufferIteratorConst<MBF_UINT16,uint16,1> itSource( Result->GetIndexBuffers(), MBS_VERTEXINDEX );
    //            MeshBufferIterator<MBF_UINT16,uint16,1> itDest( Result->GetIndexBuffers(), MBS_VERTEXINDEX );

    //            int32 indexCount = Result->GetIndexCount();
    //            for ( int32 f=0; f<indexCount/3; ++f )
    //            {
    //                uint16 sourceIndices[3];
    //                sourceIndices[0] = (*itSource)[0];
    //                sourceIndices[1] = (*itSource)[1];
    //                sourceIndices[2] = (*itSource)[2];

	//				check(sourceIndices[0] < RemovedVertexCount);
	//				check(sourceIndices[1] < RemovedVertexCount);
	//				check(sourceIndices[2] < RemovedVertexCount);

	//				int32 RemoveCount =  (RemovedVertices[ sourceIndices[0] ]
    //                        + RemovedVertices[ sourceIndices[1] ]
    //                        + RemovedVertices[ sourceIndices[2] ]
    //                        );
	//				bool bFaceRemoved = RemoveCount > 0;

    //                if (!bFaceRemoved)
    //                {
    //                    UsedFaces[f] = true;
    //                    for (int32 i = 0; i < 3; ++i)
    //                    {
    //                        uint16 sourceIndex = sourceIndices[i];

    //                        if (UsedVertexMap[ sourceIndex ] < 0 )
    //                        {
	//							UsedVertexMap[ sourceIndex ] = FirstFreeVertex;
    //                            FirstFreeVertex++;
    //                        }

    //                        uint16 destIndex = (uint16)UsedVertexMap[ sourceIndex ];
    //                        *(uint16*)itDest.ptr() = destIndex;

    //                        itDest++;
    //                    }
    //                }

    //                itSource+=3;
    //            }

    //            RemovedIndices = itSource - itDest;
    //        }

    //        else
    //        {
    //            // Index buffer format case not implemented
    //            check( false );
    //        }

    //        check( RemovedIndices%3==0 );

    //        int32 FaceCount = Result->GetFaceCount();
    //        Result->GetIndexBuffers().SetElementCount( FaceCount*3-RemovedIndices );
    //    }


    //    // Rebuild the vertex buffers

	//	// If we had implicit indices, make them explicit or relative to keep them valid
	//	if (RemovedIndices && Result->AreVertexIdsImplicit())
	//	{
	//		Result->MakeVertexIdsRelative();
	//	}

	//	// The temp array is necessary because if the vertex buffer is not sorted according to the index buffer we cannot do it in-place
	//	// This happens with some mesh import options.
	//	TArray<uint8> Temp;
	//	for ( int32 b=0; b<Result->GetVertexBuffers().GetBufferCount(); ++b )
    //    {
    //        int32 elemSize = Result->GetVertexBuffers().GetElementSize( b );
    //        const uint8* SourceData = Result->GetVertexBuffers().GetBufferData( b );

	//		Temp.SetNumUninitialized(FirstFreeVertex*elemSize,EAllowShrinking::No);
    //        uint8* DestData = Temp.GetData();

    //        for ( int32 v=0; v<Result->GetVertexCount(); ++v )
    //        {
    //            int32 span = 0;
    //            for ( int32 s=0; v+s<Result->GetVertexCount(); ++s )
    //            {
    //                if (UsedVertexMap[v+s]>=0 )
    //                {
    //                    if (span==0)
    //                    {
    //                        ++span;
    //                    }
    //                    else
    //                    {
    //                        if (UsedVertexMap[v+s] == UsedVertexMap[v+s-1]+1 )
    //                        {
    //                            ++span;
    //                        }
    //                        else
    //                        {
    //                            break;
    //                        }
    //                    }
    //                }
    //                else
    //                {
    //                    break;
    //                }
    //            }


    //            if (span>0)
    //            {
    //                FMemory::Memcpy( DestData+elemSize*UsedVertexMap[v], SourceData+elemSize*v, elemSize*span );

    //                v += span-1;
    //            }
    //        }

	//		// Copy from temp buffer to final vertex buffer
	//		FMemory::Memcpy( Result->GetVertexBuffers().GetBufferData(b), DestData, FirstFreeVertex*elemSize);
    //    }
    //    
    //    Result->GetVertexBuffers().SetElementCount(FirstFreeVertex);

    //    //Temp fix, transform the vertex map to a bitset.
    //    TBitArray<> UsedVertices;
    //    UsedVertices.SetNum(RemovedVertexCount, false);

    //    for (uint32 I = 0; I < RemovedVertexCount; ++I)
    //    {
    //        UsedVertices[I] = RemovedVertices[I] == 0;
    //    }

    //    MeshRemoveRecreateSurface(Result, UsedVertices, UsedFaces);
    //}

    void MeshRemoveVerticesWithCullSet(Mesh* Result, const TBitArray<>& VerticesToCull, bool bRemoveIfAllVerticesCulled)
    { 
        MUTABLE_CPUPROFILER_SCOPE(MeshRemoveVerticesWithCullSet);
		
        const UntypedMeshBufferIterator IndicesBegin(Result->GetIndexBuffers(), MBS_VERTEXINDEX);
		
        const int32 NumFaces = Result->GetFaceCount();
        const int32 NumVertices = Result->GetVertexCount();

		TBitArray<> UsedVertices;
		UsedVertices.SetNum(NumVertices, false);
	
		TBitArray<> UsedFaces;
		UsedFaces.SetNum(NumFaces, false);

        int32 NumUsedFaces = 0;
		const uint32 IndexTypeSize = IndicesBegin.GetElementSize();

        if (IndexTypeSize == 4)
        {
            for (int32 FaceIndex = 0; FaceIndex < NumFaces; ++FaceIndex)
            {
                const uint32* FaceIndicesData = reinterpret_cast<uint32*>((IndicesBegin + FaceIndex*3).ptr());

				bool bRemoved = false;

				if (bRemoveIfAllVerticesCulled)
				{
					const bool bAllVertsRemoved =
						VerticesToCull[FaceIndicesData[0]] &
						VerticesToCull[FaceIndicesData[1]] &
						VerticesToCull[FaceIndicesData[2]];
					bRemoved = bAllVertsRemoved;
				}
				else
				{
					const bool bOneVertRemoved =
						VerticesToCull[FaceIndicesData[0]] |
						VerticesToCull[FaceIndicesData[1]] |
						VerticesToCull[FaceIndicesData[2]];
					bRemoved = bOneVertRemoved;
				}

                if (!bRemoved)
                {
                    ++NumUsedFaces;
                    UsedFaces[FaceIndex] = true;

                    UsedVertices[FaceIndicesData[0]] = true;
                    UsedVertices[FaceIndicesData[1]] = true;
                    UsedVertices[FaceIndicesData[2]] = true;
                }
            }
        }
        else if (IndexTypeSize == 2)
        {
            for (int32 FaceIndex = 0; FaceIndex < NumFaces; ++FaceIndex)
            {
                const uint16* FaceIndicesData = reinterpret_cast<uint16*>((IndicesBegin + FaceIndex*3).ptr());

                const bool bAllVertsRemoved = 
                        VerticesToCull[FaceIndicesData[0]] & 
                        VerticesToCull[FaceIndicesData[1]] & 
                        VerticesToCull[FaceIndicesData[2]];

                if (!bAllVertsRemoved)
                {
                    ++NumUsedFaces;
                    UsedFaces[FaceIndex] = true;

                    UsedVertices[FaceIndicesData[0]] = true;
                    UsedVertices[FaceIndicesData[1]] = true;
                    UsedVertices[FaceIndicesData[2]] = true;
                }
            }
        }
        else
        {
            check(false);
        }
 
		if (NumUsedFaces < NumFaces && Result->AreVertexIdsImplicit())
		{
			Result->MakeVertexIdsRelative();
		}

		TArray<int32> UsedVerticesMap;
#if DO_CHECK
        UsedVerticesMap.Init(-1, NumVertices);
#else
        // This data will only be accessed by indices that have been mapped, No need to initialize. 
		UsedVerticesMap.SetNumUninitialized(NumVertices);
#endif
		FMeshBufferSet& VertexBufferSet = Result->GetVertexBuffers();

		const int32 NumBuffers = VertexBufferSet.GetBufferCount();

		// Compute vertices indices remap
        int32 NumVerticesRemaining = 0;
		if (NumUsedFaces > 0) 
        {
		    int32 LastFreeVertexIndex = 0;
            for (int32 VertexIndex = UsedVertices.FindFrom(true, 0); VertexIndex >= 0;)
			{
                const int32 UsedSpanBegin = VertexIndex;
                VertexIndex = UsedVertices.FindFrom(false, VertexIndex);
				
				// At the end of the buffer we may not find a false element, in that case
				// FindForm returns INDEX_NONE, set the vertex at the range end. 
                VertexIndex = VertexIndex >= 0 ? VertexIndex : NumVertices;
                const int32 UsedSpanEnd = VertexIndex;

				// VertexIndex may be one past the end of the array, VertexIndex will become INDEX_NONE
				// and the loop will finish.
                VertexIndex = UsedVertices.FindFrom(true, VertexIndex);
				
                for (int32 I = UsedSpanBegin; I < UsedSpanEnd; ++I)
                {
                    UsedVerticesMap[I] = LastFreeVertexIndex + I - UsedSpanBegin;
                }

				LastFreeVertexIndex += UsedSpanEnd - UsedSpanBegin;
			}

            NumVerticesRemaining = LastFreeVertexIndex;
		}

		// Copy move buffers. We are recomputing the spans for each buffer, should be ok as
        // finding the span is fast compared to the data move.
        if (NumVerticesRemaining > 0)
        {
            for (int32 BufferIndex = 0; BufferIndex < NumBuffers; ++BufferIndex)
            {
                uint8* BufferData = VertexBufferSet.GetBufferData(BufferIndex); 
                const uint32 ElemSize = VertexBufferSet.GetElementSize(BufferIndex);

                int32 LastFreeVertexIndex = 0;

                for (int32 VertexIndex = UsedVertices.FindFrom(true, 0); VertexIndex >= 0;)
                {
                    const int32 UsedSpanBegin = VertexIndex;
                    VertexIndex = UsedVertices.FindFrom(false, VertexIndex);
                    VertexIndex = VertexIndex >= 0 ? VertexIndex : NumVertices;
                    const int32 UsedSpanEnd = VertexIndex;

                    VertexIndex = UsedVertices.FindFrom(true, VertexIndex);

                    // Copy vertex buffer span.	
                    const int32 UsedSpanSize = UsedSpanEnd - UsedSpanBegin;
                    
                    if (LastFreeVertexIndex != UsedSpanBegin)
                    {
                        FMemory::Memmove(
                                BufferData + LastFreeVertexIndex*ElemSize,
                                BufferData + UsedSpanBegin*ElemSize,
                                UsedSpanSize*ElemSize);
                    }

                    LastFreeVertexIndex += UsedSpanSize;
                }

                check(LastFreeVertexIndex == NumVerticesRemaining);
            }
        }
		Result->GetVertexBuffers().SetElementCount(NumVerticesRemaining);

		int32 LastFreeFaceIndex = 0;
        if (NumUsedFaces > 0)
        {
            for (int32 FaceIndex = UsedFaces.FindFrom(true, 0); FaceIndex >= 0;)
            {		
                const int32 UsedSpanStart = FaceIndex;
                FaceIndex = UsedFaces.FindFrom(false, FaceIndex);
                FaceIndex = FaceIndex >= 0 ? FaceIndex : NumFaces;
                const int32 UsedSpanEnd = FaceIndex;

                FaceIndex = UsedFaces.FindFrom(true, FaceIndex);

                const int32 UsedSpanSize = UsedSpanEnd - UsedSpanStart;
        
                if (LastFreeFaceIndex != UsedSpanStart)
                {
                    FMemory::Memmove(
                            (IndicesBegin + LastFreeFaceIndex*3).ptr(), 
                            (IndicesBegin + UsedSpanStart*3).ptr(), 
                            UsedSpanSize*IndexTypeSize*3);
                }

                // Remap vertices
                if (IndexTypeSize == 4)
                {
                    for (int32 I = LastFreeFaceIndex; I < LastFreeFaceIndex + UsedSpanSize; ++I)
                    {
                        uint32* FaceIndicesData = reinterpret_cast<uint32*>((IndicesBegin + I*3).ptr());

                        check(UsedVerticesMap[FaceIndicesData[0]] >= 0);
                        check(UsedVerticesMap[FaceIndicesData[1]] >= 0);
                        check(UsedVerticesMap[FaceIndicesData[2]] >= 0);

                        FaceIndicesData[0] = UsedVerticesMap[FaceIndicesData[0]];
                        FaceIndicesData[1] = UsedVerticesMap[FaceIndicesData[1]];
                        FaceIndicesData[2] = UsedVerticesMap[FaceIndicesData[2]];
                    }
                }
                else if (IndexTypeSize == 2)
                {	
                    for (int32 I = LastFreeFaceIndex; I < LastFreeFaceIndex + UsedSpanSize; ++I)
                    {
                        uint16* FaceIndicesData = reinterpret_cast<uint16*>((IndicesBegin + I*3).ptr());

                        check(UsedVerticesMap[FaceIndicesData[0]] >= 0);
                        check(UsedVerticesMap[FaceIndicesData[1]] >= 0);
                        check(UsedVerticesMap[FaceIndicesData[2]] >= 0);
                        
                        FaceIndicesData[0] = static_cast<uint16>(UsedVerticesMap[FaceIndicesData[0]]);
                        FaceIndicesData[1] = static_cast<uint16>(UsedVerticesMap[FaceIndicesData[1]]);
                        FaceIndicesData[2] = static_cast<uint16>(UsedVerticesMap[FaceIndicesData[2]]);
                    }
                }
                else
                {
                    check(false);
                }

                LastFreeFaceIndex += UsedSpanSize;
            }
        }

		check(LastFreeFaceIndex <= NumFaces);

		Result->GetIndexBuffers().SetElementCount(LastFreeFaceIndex*3);

		MeshRemoveRecreateSurface(Result, UsedVertices, UsedFaces);
    }

	void MeshRemoveMask(Mesh* Result, const Mesh* Source, const Mesh* Mask, bool bRemoveIfAllVerticesCulled, bool& bOutSuccess)
    {
        MUTABLE_CPUPROFILER_SCOPE(MeshRemoveMask);
		bOutSuccess = true;

        if (!Mask->GetVertexCount() || !Source->GetVertexCount() || !Source->GetIndexCount())
        {
			bOutSuccess = false;
            return;
        }

		Result->CopyFrom(*Source);

		int32 MaskElementCount = Mask->GetVertexBuffers().GetElementCount();

        // For each source vertex, true if it is removed.
        int32 ResultVertexCount = Result->GetVertexCount();
		TBitArray<> RemovedVertices;
		RemovedVertices.SetNum(ResultVertexCount, false);
        {
			TArray<FIdInterval> Intervals;
            ExtractVertexIndexIntervals(Intervals, Result);

			MeshVertexIdIteratorConst itMaskVI(Mask);
			for ( int32 mv=0; mv<MaskElementCount; ++mv )
            {
                uint64 MaskVertexId = itMaskVI.Get();
                ++itMaskVI;

                int32 IndexInSource = FindPositionInIntervals(Intervals, MaskVertexId);
                if (IndexInSource >= 0)
                {
					RemovedVertices[IndexInSource] = true;
                }
            }
        }
		
        MeshRemoveVerticesWithCullSet(Result, RemovedVertices, bRemoveIfAllVerticesCulled);
	}
}
