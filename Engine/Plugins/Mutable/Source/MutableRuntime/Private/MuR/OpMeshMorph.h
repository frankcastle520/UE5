// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MuR/MeshPrivate.h"
#include "MuR/ConvertData.h"
#include "MuR/MutableTrace.h"
#include "MuR/Platform.h"
#include "MuR/SparseIndexMap.h"

namespace mu
{
	/**
	 * Optimized linear factor version for morphing 2 targets
	 */
    inline void MeshMorph2(Mesh* BaseMesh, const Mesh* MinMesh, const Mesh* MaxMesh, const float Factor)
    {
        MUTABLE_CPUPROFILER_SCOPE(MeshMorph2);

		if (!BaseMesh)
		{
			return;
		}

		const auto MakeIndexMap = [](
			MeshVertexIdIteratorConst BaseIdIter, int32 BaseNum,
			MeshVertexIdIteratorConst MorphIdIter, int32 MorphNum)
		-> SparseIndexMapSet
		{	
			TArray<SparseIndexMapSet::FRangeDesc> RangeDescs;

			// Detect all ranges and their limits
			{
				for (int32 Index = 0; Index < BaseNum; ++Index, ++BaseIdIter)
				{
					const uint64 BaseId = BaseIdIter.Get();

					uint32 Prefix = BaseId >> 32;
					uint32 Id = BaseId & 0xffffffff;
					bool bFound = false;
					for (SparseIndexMapSet::FRangeDesc& Range : RangeDescs)
					{
						if (Range.Prefix == Prefix)
						{
							Range.MinIndex = FMath::Min(Id, Range.MinIndex);
							Range.MaxIndex = FMath::Max(Id, Range.MaxIndex);
							bFound = true;
							break;
						}
					}

					if (!bFound)
					{
						RangeDescs.Add({Prefix, Id, Id});
					}
				}
			}

			SparseIndexMapSet IndexMap(RangeDescs);
           
            for (int32 Index = 0; Index < MorphNum; ++Index, ++MorphIdIter)
            {
                const uint64 MorphId = MorphIdIter.Get();
                
                IndexMap.Insert(MorphId, Index);
            }

			return IndexMap;
		};

        const auto ApplyNormalMorph = [](
			MeshVertexIdIteratorConst BaseIdIter, const TStaticArray<UntypedMeshBufferIterator, 3>& BaseTangentFrameIters, const int32 BaseNum,
			MeshVertexIdIteratorConst MorphIdIter, UntypedMeshBufferIteratorConst& MorphNormalIter, const int32 MorphNum,
				const SparseIndexMapSet& IndexMap, const float Factor)
        -> void
        {
			const UntypedMeshBufferIterator& BaseNormalIter = BaseTangentFrameIters[2];
			const UntypedMeshBufferIterator& BaseTangentIter = BaseTangentFrameIters[1];
			const UntypedMeshBufferIterator& BaseBiNormalIter = BaseTangentFrameIters[0];

            const EMeshBufferFormat NormalFormat = BaseNormalIter.GetFormat();
            const int32 NormalComps = BaseNormalIter.GetComponents();

            const EMeshBufferFormat TangentFormat = BaseTangentIter.GetFormat();
            const int32 TangentComps = BaseTangentIter.GetComponents();

            const EMeshBufferFormat BiNormalFormat = BaseBiNormalIter.GetFormat();
            const int32 BiNormalComps = BaseBiNormalIter.GetComponents();

			// When normal is packed, binormal channel is not expected. It is not a big deal if it's there but we would be doing extra unused work in that case. 
			ensureAlways(!(NormalFormat == MBF_PACKEDDIR8_W_TANGENTSIGN || NormalFormat == MBF_PACKEDDIRS8_W_TANGENTSIGN) || !BaseBiNormalIter.ptr());

            for (int32 VertexIndex = 0; VertexIndex < BaseNum; ++VertexIndex)
            {
                const uint64 BaseId = (BaseIdIter + VertexIndex).Get();
                const int32 MorphIndex = static_cast<int32>(IndexMap.Find(BaseId));

                if (MorphIndex == SparseIndexMap::NotFoundValue)
                {
                    continue;
                }

                // Find consecutive run.
				MeshVertexIdIteratorConst RunBaseIter = BaseIdIter + VertexIndex;
				MeshVertexIdIteratorConst RunMorphIter = MorphIdIter + MorphIndex;

                int32 RunSize = 0;
                for ( ; VertexIndex + RunSize < BaseNum && MorphIndex + RunSize < MorphNum && RunBaseIter.Get() == RunMorphIter.Get();
                        ++RunSize, ++RunBaseIter, ++RunMorphIter);

				for (int32 RunIndex = 0; RunIndex < RunSize; ++RunIndex)
				{
					UntypedMeshBufferIterator NormalIter = BaseNormalIter + (VertexIndex + RunIndex);

					const FVector3f BaseNormal = NormalIter.GetAsVec3f();
					const FVector3f MorphNormal = (MorphNormalIter + (MorphIndex + RunIndex)).GetAsVec3f();
 
					const FVector3f Normal = (BaseNormal + MorphNormal*Factor).GetSafeNormal();
				
					// Leave the tangent basis sign untouched for packed normals formats.
					for (int32 C = 0; C < NormalComps && C < 3; ++C)
					{
						ConvertData(C, NormalIter.ptr(), NormalFormat, &Normal, MBF_FLOAT32);
					}

					// Tangent
					if (BaseTangentIter.ptr())
					{
						UntypedMeshBufferIterator TangentIter = BaseTangentIter + (VertexIndex + RunIndex);

						const FVector3f BaseTangent = TangentIter.GetAsVec3f();

						// Orthogonalize Tangent based on new Normal. This assumes Normal and BaseTangent are normalized and different.
						const FVector3f Tangent = (BaseTangent - FVector3f::DotProduct(Normal, BaseTangent) * Normal).GetSafeNormal();

						for (int32 C = 0; C < TangentComps && C < 3; ++C)
						{
							ConvertData(C, TangentIter.ptr(), TangentFormat, &Tangent, MBF_FLOAT32);
						}

						// BiNormal
						if (BaseBiNormalIter.ptr())
						{
							UntypedMeshBufferIterator BiNormalIter = BaseBiNormalIter + (VertexIndex + RunIndex);

							const FVector3f& N = BaseNormal;
							const FVector3f& T = BaseTangent;
							const FVector3f  B = BiNormalIter.GetAsVec3f();

							const float BaseTangentBasisDeterminant =  
									B.X*T.Y*N.Z + B.Z*T.X*N.Y + B.Y*T.Z*N.Y -
									B.Z*T.Y*N.X - B.Y*T.X*N.Z - B.X*T.Z*N.Y;

							const float BaseTangentBasisDeterminantSign = BaseTangentBasisDeterminant >= 0 ? 1.0f : -1.0f;

							const FVector3f BiNormal = FVector3f::CrossProduct(Tangent, Normal) * BaseTangentBasisDeterminantSign;

							for (int32 C = 0; C < BiNormalComps && C < 3; ++C)
							{
								ConvertData(C, BiNormalIter.ptr(), BiNormalFormat, &BiNormal, MBF_FLOAT32);
							}
						}
					}
				}

				VertexIndex += FMath::Max(RunSize - 1, 0);
            }
        };

        const auto ApplyGenericMorph = []( 
			MeshVertexIdIteratorConst BaseIdIter, const TArray<UntypedMeshBufferIterator>& BaseChannelsIters, const int32 BaseNum,
			MeshVertexIdIteratorConst MorphIdIter, const TArray<UntypedMeshBufferIteratorConst>& MorphChannelsIters, const int32 MorphNum,
				const SparseIndexMapSet& IndexMap, const float Factor)
        -> void
        {
            for (int32 VertexIndex = 0; VertexIndex < BaseNum; ++VertexIndex)
            {
                const uint64 BaseId = (BaseIdIter + VertexIndex).Get();
                const uint32 MorphIndex = IndexMap.Find(BaseId);

                if (MorphIndex == SparseIndexMap::NotFoundValue)
                {
                    continue;
                }

                // Find consecutive run.
				MeshVertexIdIteratorConst RunBaseIter = BaseIdIter + VertexIndex;
				MeshVertexIdIteratorConst RunMorphIter = MorphIdIter + MorphIndex;

                int32 RunSize = 0;
                for ( ; VertexIndex + RunSize < BaseNum && int32(MorphIndex) + RunSize < MorphNum && RunBaseIter.Get() == RunMorphIter.Get();
                        ++RunSize, ++RunBaseIter, ++RunMorphIter);

                const int32 ChannelNum = MorphChannelsIters.Num();
                for (int32 ChannelIndex = 0; ChannelIndex < ChannelNum; ++ChannelIndex)
                {
                	if (!(BaseChannelsIters[ChannelIndex].ptr() && MorphChannelsIters[ChannelIndex].ptr()))
                	{
                		continue;
                	}
                
                    UntypedMeshBufferIterator ChannelBaseIter = BaseChannelsIters[ChannelIndex] + VertexIndex;
					UntypedMeshBufferIteratorConst ChannelMorphIter = MorphChannelsIters[ChannelIndex] + MorphIndex;
                   
                    const EMeshBufferFormat DestChannelFormat = BaseChannelsIters[ChannelIndex].GetFormat();
                    const int32 DestChannelComps = BaseChannelsIters[ChannelIndex].GetComponents();

                    // Apply Morph to range found above.
                    for (int32 R = 0; R < RunSize; ++R, ++ChannelBaseIter, ++ChannelMorphIter)
                    {
                        const FVector4f Value = ChannelBaseIter.GetAsVec4f() + ChannelMorphIter.GetAsVec4f()*Factor;

                        // TODO: Optimize this for the specific components.
                        // Max 4 components
                        for (int32 Comp = 0; Comp < DestChannelComps && Comp < 4; ++Comp)
                        {
                            ConvertData(Comp, ChannelBaseIter.ptr(), DestChannelFormat, &Value, MBF_FLOAT32);
                        }
                    }
                }

				VertexIndex += FMath::Max(RunSize - 1, 0);
            }
        };


		// Number of vertices to modify
		const int32 MinNum = MinMesh ? MinMesh->GetVertexBuffers().GetElementCount() : 0;
		const int32 MaxNum = MaxMesh ? MaxMesh->GetVertexBuffers().GetElementCount() : 0;
		const int32 BaseNum = BaseMesh ? BaseMesh->GetVertexBuffers().GetElementCount() : 0;
		const Mesh* RefTarget = MinNum > 0 ? MinMesh : MaxMesh;

		if (BaseNum == 0 || (MinNum + MaxNum) == 0)
		{
			return;
		}

		if (RefTarget)
		{
			constexpr int32 MorphBufferDataChannel = 0;
			const int32 ChannelsNum = RefTarget->GetVertexBuffers().GetBufferChannelCount(MorphBufferDataChannel);

			TArray<UntypedMeshBufferIterator> BaseChannelsIters;
			BaseChannelsIters.SetNum(ChannelsNum);
			TArray<UntypedMeshBufferIteratorConst> MinChannelsIters;
			MinChannelsIters.SetNum(ChannelsNum);
			TArray<UntypedMeshBufferIteratorConst> MaxChannelsIters;
			MaxChannelsIters.SetNum(ChannelsNum);

			// {BiNormal, Tangent, Normal}
			TStaticArray<UntypedMeshBufferIterator, 3> BaseTangentFrameChannelsIters;
			UntypedMeshBufferIteratorConst MinNormalChannelIter;
			UntypedMeshBufferIteratorConst MaxNormalChannelIter;

			const bool bBaseHasNormals = UntypedMeshBufferIteratorConst(BaseMesh->GetVertexBuffers(), MBS_NORMAL, 0).ptr() != nullptr;
			for (int32 ChannelIndex = 0; ChannelIndex < ChannelsNum; ++ChannelIndex)
			{
				const FMeshBufferSet& MBSPriv = RefTarget->GetVertexBuffers();
				const FMeshBufferChannel& Channel = MBSPriv.Buffers[MorphBufferDataChannel].Channels[ChannelIndex];
				EMeshBufferSemantic Sem = Channel.Semantic;
				int32 SemIndex = Channel.SemanticIndex;
			
				if (Sem == MBS_NORMAL && bBaseHasNormals)
				{
					BaseTangentFrameChannelsIters[2] = UntypedMeshBufferIterator(BaseMesh->GetVertexBuffers(), Sem, SemIndex);
					if (MinNum > 0)
					{
						MinNormalChannelIter = UntypedMeshBufferIteratorConst(MinMesh->GetVertexBuffers(), Sem, SemIndex);
					}

					if (MaxNum > 0)
					{
						MaxNormalChannelIter = UntypedMeshBufferIteratorConst(MaxMesh->GetVertexBuffers(), Sem, SemIndex);
					}
				}
				else if (Sem == MBS_TANGENT && bBaseHasNormals)
				{
					BaseTangentFrameChannelsIters[1] = UntypedMeshBufferIterator(BaseMesh->GetVertexBuffers(), Sem, SemIndex);
				}
				else if (Sem == MBS_BINORMAL && bBaseHasNormals)
				{
					BaseTangentFrameChannelsIters[0] = UntypedMeshBufferIterator(BaseMesh->GetVertexBuffers(), Sem, SemIndex);
				}
				else
				{
					BaseChannelsIters[ChannelIndex] = UntypedMeshBufferIterator(BaseMesh->GetVertexBuffers(), Sem, SemIndex);
					if (MinNum > 0)
					{
						MinChannelsIters[ChannelIndex] = UntypedMeshBufferIteratorConst(MinMesh->GetVertexBuffers(), Sem, SemIndex);
					}

					if (MaxNum > 0)
					{
						MaxChannelsIters[ChannelIndex] = UntypedMeshBufferIteratorConst(MaxMesh->GetVertexBuffers(), Sem, SemIndex);
					}
				}
			}
			
			MeshVertexIdIteratorConst BaseIdIter(BaseMesh);

			if (MinNum > 0)
			{
				MeshVertexIdIteratorConst MinIdIter(MinMesh);
				SparseIndexMapSet IndexMap = MakeIndexMap(BaseIdIter, BaseNum, MinIdIter, MinNum);

				ApplyGenericMorph(BaseIdIter, BaseChannelsIters, BaseNum, MinIdIter, MinChannelsIters, MinNum, IndexMap, 1.0f - Factor);
				if (MinNormalChannelIter.ptr())
				{
					ApplyNormalMorph(BaseIdIter, BaseTangentFrameChannelsIters, BaseNum, MinIdIter, MinNormalChannelIter, MinNum, IndexMap, 1.0f - Factor);
				}
			}

			if (MaxNum > 0)
			{
				MeshVertexIdIteratorConst MaxIdIter(MaxMesh);
				SparseIndexMapSet IndexMap = MakeIndexMap(BaseIdIter, BaseNum, MaxIdIter, MaxNum);

				ApplyGenericMorph(BaseIdIter, BaseChannelsIters, BaseNum, MaxIdIter, MaxChannelsIters, MaxNum, IndexMap, Factor);
				
				if (MaxNormalChannelIter.ptr())
				{
					ApplyNormalMorph(BaseIdIter, BaseTangentFrameChannelsIters, BaseNum, MaxIdIter, MaxNormalChannelIter, MaxNum, IndexMap, Factor);
				}
			}
		}
    }

	//TODO Optimized linear factor version
	inline void MeshMorph(Mesh* BaseMesh, const Mesh* MorphMesh, float Factor)
	{
		MeshMorph2(BaseMesh, nullptr, MorphMesh, Factor);
	}

    //TODO Optimized Factor-less version
	inline void MeshMorph(Mesh* BaseMesh, const Mesh* MorphMesh)
    {
        // Trust the compiler to remove the factor
		MeshMorph(BaseMesh, MorphMesh, 1.0f);
    }
}
