// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MuR/MeshPrivate.h"
#include "MuR/Platform.h"

#include "MuR/OpMeshRemove.h"

namespace mu
{
    inline void MeshExtractFromVertices( const Mesh* Source,
                                         Mesh* Result,
                                         const TArray<int32>& OldToNew,
                                         const TArray<int32>& NewToOld )
    {
        int32 ResultVertices = NewToOld.Num();

        // Assemble the new vertex buffer
        Result->GetVertexBuffers().SetElementCount(ResultVertices);
        for (int32 BufferIndex = 0; BufferIndex < Result->GetVertexBuffers().GetBufferCount(); ++BufferIndex)
        {
            const uint8* pSourceData = Source->GetVertexBuffers().GetBufferData(BufferIndex);
            uint8* pDest = Result->GetVertexBuffers().GetBufferData(BufferIndex);
            int32 Size = Result->GetVertexBuffers().GetElementSize(BufferIndex);

            for (int32 NewIndex = 0; NewIndex < ResultVertices; ++NewIndex)
            {
                int32 OldIndex = NewToOld[NewIndex];
                FMemory::Memcpy(pDest, pSourceData + Size*OldIndex, Size);

                pDest += Size;
            }
        }

		// If the vertices are explicit or relative, the above operation will already handle them correctly
		// Otherwise, create a relative vertex ID buffer if necessary
		Result->MeshIDPrefix = Source->MeshIDPrefix;
		if (Source->AreVertexIdsImplicit() 
			&& 
			// If we extract everything, we can keep the ids implicit.
			ResultVertices!=Source->GetVertexCount())
		{
			// Add a new buffer
			int32 NewBuffer = Result->VertexBuffers.GetBufferCount();
			Result->VertexBuffers.SetBufferCount(NewBuffer + 1);
			EMeshBufferSemantic Semantic = MBS_VERTEXINDEX;
			int32 SemanticIndex = 0;
			EMeshBufferFormat Format = MBF_UINT32;
			int32 Components = 1;
			int32 Offset = 0;
			Result->VertexBuffers.SetBuffer(NewBuffer, sizeof(uint32), 1, &Semantic, &SemanticIndex, &Format, &Components, &Offset);
			uint32* pIdData = reinterpret_cast<uint32*>(Result->VertexBuffers.GetBufferData(NewBuffer));

			for (int32 NewIndex = 0; NewIndex < ResultVertices; ++NewIndex)
			{
				uint32 OldIndex = NewToOld[NewIndex];
				(*pIdData++) = OldIndex;
			}
		}

        // Assemble the new index buffers
		TBitArray<> UsedSourceFaces;

		UsedSourceFaces.SetNum(Source->GetFaceCount(), false);
        UntypedMeshBufferIteratorConst itIndex(Source->GetIndexBuffers(), MBS_VERTEXINDEX);
        UntypedMeshBufferIterator itResultIndex(Result->GetIndexBuffers(), MBS_VERTEXINDEX);
        
        int32 IndexCount = 0;
        if (itIndex.GetFormat() == MBF_UINT32)
        {
            const uint32* pIndices = reinterpret_cast<const uint32*>(itIndex.ptr());
            uint32* pDestIndices = reinterpret_cast<uint32*>(itResultIndex.ptr());
            for (int32 i = 0; i < Source->GetIndexCount()/3; ++i)
            {
                if (OldToNew[pIndices[i*3 + 0]] >= 0 && OldToNew[pIndices[i*3 + 1]] >= 0 && OldToNew[pIndices[i*3 + 2]] >= 0)
                {
                    UsedSourceFaces[i] = true;

                    // Clamp in case triangles go across blocks
                    pDestIndices[IndexCount++] = FMath::Max(0, OldToNew[pIndices[i*3 + 0]]);
                    pDestIndices[IndexCount++] = FMath::Max(0, OldToNew[pIndices[i*3 + 1]]);
                    pDestIndices[IndexCount++] = FMath::Max(0, OldToNew[pIndices[i*3 + 2]]);
                }
            }
        }
        else if (itIndex.GetFormat() == MBF_UINT16)
        {
            const uint16* pIndices = reinterpret_cast<const uint16*>(itIndex.ptr());
            uint16* pDestIndices = reinterpret_cast<uint16*>(itResultIndex.ptr());
            for (int32 i = 0; i < Source->GetIndexCount()/3; ++i)
            {
                if (OldToNew[pIndices[i*3 + 0]] >= 0 && OldToNew[pIndices[i*3 + 1]] >= 0 && OldToNew[pIndices[i*3 + 2]] >= 0)
                {
                    UsedSourceFaces[i] = true;

                    // Clamp in case triangles go across blocks
                    pDestIndices[IndexCount++] = (uint16)FMath::Max(0, OldToNew[pIndices[i*3 + 0]]);
                    pDestIndices[IndexCount++] = (uint16)FMath::Max(0, OldToNew[pIndices[i*3 + 1]]);
                    pDestIndices[IndexCount++] = (uint16)FMath::Max(0, OldToNew[pIndices[i*3 + 2]]);
                }
            }
        }
        else
        {
            check(false);
        }
     
        Result->GetIndexBuffers().SetElementCount(IndexCount);

        const int32 NumOriginalVerts = OldToNew.Num();

        TBitArray<> UsedVertices;
        UsedVertices.SetNum(NumOriginalVerts, false);

        for (int32 I = 0; I < NumOriginalVerts; ++I)
        {
            UsedVertices[I] = OldToNew[I] >= 0;
        }

        MeshRemoveRecreateSurface(Result, UsedVertices, UsedSourceFaces);
    }


	/** Extract all vertices that have a layout block from the passed list on the given channel. */
    inline void MeshExtractLayoutBlock(Mesh* Result, const Mesh* Source,
                                           uint32 LayoutIndex,
                                           uint16 BlockCount,
                                           const uint64* BlockIds, bool& bOutSuccess)
	{
		check(Source);
		bOutSuccess = true;

		// TODO: Optimise
		Result->CopyFrom(*Source);

		UntypedMeshBufferIteratorConst itBlocks(Source->GetVertexBuffers(), MBS_LAYOUTBLOCK, LayoutIndex);

        if (itBlocks.GetFormat() != MBF_NONE)
        {
            int32 ResultVertices = 0;
			TArray<int32> OldToNew;
			OldToNew.Init(-1, Source->GetVertexCount());
			TArray<int32> NewToOld;
            NewToOld.Reserve(Source->GetVertexCount());

            if (itBlocks.GetFormat() == MBF_UINT16)
            {
                const uint16* pBlocks = reinterpret_cast<const uint16*>(itBlocks.ptr());
                for ( int32 i=0; i<Source->GetVertexCount(); ++i )
                {
                    uint64 VertexBlockRelative = pBlocks[i];
					uint64 VertexBlockId = (uint64(Source->MeshIDPrefix) << 32) | VertexBlockRelative;

                    bool bFound = false;
                    for (int32 j = 0; j < BlockCount; ++j)
                    {
                        if (VertexBlockId == BlockIds[j])
                        {
                            bFound = true;
                            break;
                        }
                    }

                    if (bFound)
                    {
                        OldToNew[i] = ResultVertices++;
                        NewToOld.Add(i);
                    }
                }
            }
            else if (itBlocks.GetFormat() == MBF_UINT64)
			{
				const uint64* pBlocks = reinterpret_cast<const uint64*>(itBlocks.ptr());
				for (int32 i = 0; i < Source->GetVertexCount(); ++i)
				{
					uint64 VertexBlockId = pBlocks[i];

					bool bFound = false;
					for (int32 j = 0; j < BlockCount; ++j)
					{
						if (VertexBlockId == BlockIds[j])
						{
							bFound = true;
							break;
						}
					}

					if (bFound)
					{
						OldToNew[i] = ResultVertices++;
						NewToOld.Add(i);
					}
				}
			}
			else
            {
                check( false );
            }

            MeshExtractFromVertices(Source, Result, OldToNew, NewToOld);
        }
	}

	/** Extract all vertices that have a valid layout block on the given channel. */
	inline void MeshExtractLayoutBlock(Mesh* Result, const Mesh* Source, uint32 LayoutIndex, bool& bOutSuccess)
	{
		check(Source);
		bOutSuccess = true;

		// TODO: Optimise
		Result->CopyFrom(*Source);

		UntypedMeshBufferIteratorConst itBlocks(Source->GetVertexBuffers(), MBS_LAYOUTBLOCK, LayoutIndex);

		if (itBlocks.GetFormat() != MBF_NONE)
		{
			int32 ResultVertices = 0;
			TArray<int32> OldToNew;
			OldToNew.Init(-1, Source->GetVertexCount());
			TArray<int32> NewToOld;
			NewToOld.Reserve(Source->GetVertexCount());

			if (itBlocks.GetFormat() == MBF_UINT16)
			{
				const uint16* pBlocks = reinterpret_cast<const uint16*>(itBlocks.ptr());
				for (int32 i = 0; i < Source->GetVertexCount(); ++i)
				{
					uint16 VertexBlockRelative = pBlocks[i];

					bool bFound = VertexBlockRelative != std::numeric_limits<uint16>::max();
					if (bFound)
					{
						OldToNew[i] = ResultVertices++;
						NewToOld.Add(i);
					}
				}
			}
			else if (itBlocks.GetFormat() == MBF_UINT64)
			{
				const uint64* pBlocks = reinterpret_cast<const uint64*>(itBlocks.ptr());
				for (int32 i = 0; i < Source->GetVertexCount(); ++i)
				{
					uint64 VertexBlockId = pBlocks[i];

					bool bFound = VertexBlockId != std::numeric_limits<uint64>::max();
					if (bFound)
					{
						OldToNew[i] = ResultVertices++;
						NewToOld.Add(i);
					}
				}
			}
			else
			{
				check(false);
			}

			MeshExtractFromVertices(Source, Result, OldToNew, NewToOld);
		}
	}

}
