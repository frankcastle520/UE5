// Copyright Epic Games, Inc. All Rights Reserved.

#include "SkeletalMeshOperations.h"

#include "BoneWeights.h"
#include "MeshDescriptionAdapter.h"
#include "SkeletalMeshAttributes.h"
#include "Spatial/MeshAABBTree3.h"


DEFINE_LOG_CATEGORY(LogSkeletalMeshOperations);

#define LOCTEXT_NAMESPACE "SkeletalMeshOperations"

namespace UE::Private
{
	template<typename T>
	struct FCreateAndCopyAttributeValues
	{
		FCreateAndCopyAttributeValues(
			const FMeshDescription& InSourceMesh,
			FMeshDescription& InTargetMesh,
			TArray<FName>& InTargetCustomAttributeNames,
			int32 InTargetVertexIndexOffset)
			: SourceMesh(InSourceMesh)
			, TargetMesh(InTargetMesh)
			, TargetCustomAttributeNames(InTargetCustomAttributeNames)
			, TargetVertexIndexOffset(InTargetVertexIndexOffset)
			{}

		void operator()(const FName InAttributeName, TVertexAttributesConstRef<T> InSrcAttribute)
		{
			// Ignore attributes with reserved names.
			if (FSkeletalMeshAttributes::IsReservedAttributeName(InAttributeName))
			{
				return;
			}
			TAttributesSet<FVertexID>& VertexAttributes = TargetMesh.VertexAttributes();
			const bool bAppend = TargetCustomAttributeNames.Contains(InAttributeName);
			if (!bAppend)
			{
				VertexAttributes.RegisterAttribute<T>(InAttributeName, InSrcAttribute.GetNumChannels(), InSrcAttribute.GetDefaultValue(), InSrcAttribute.GetFlags());
				TargetCustomAttributeNames.Add(InAttributeName);
			}
			//Copy the data
			TVertexAttributesRef<T> TargetVertexAttributes = VertexAttributes.GetAttributesRef<T>(InAttributeName);
			for (const FVertexID SourceVertexID : SourceMesh.Vertices().GetElementIDs())
			{
				const FVertexID TargetVertexID = FVertexID(TargetVertexIndexOffset + SourceVertexID.GetValue());
				TargetVertexAttributes.Set(TargetVertexID, InSrcAttribute.Get(SourceVertexID));
			}
		}

		// Unhandled sub-types.
		void operator()(const FName, TVertexAttributesConstRef<TArrayAttribute<T>>) { }
		void operator()(const FName, TVertexAttributesConstRef<TArrayView<T>>) { }

	private:
		const FMeshDescription& SourceMesh;
		FMeshDescription& TargetMesh;
		TArray<FName>& TargetCustomAttributeNames;
		int32 TargetVertexIndexOffset = 0;
	};

} // ns UE::Private

//Add specific skeletal mesh descriptions implementation here
void FSkeletalMeshOperations::AppendSkinWeight(const FMeshDescription& SourceMesh, FMeshDescription& TargetMesh, FSkeletalMeshAppendSettings& AppendSettings)
{
	TRACE_CPUPROFILER_EVENT_SCOPE_STR("FSkeletalMeshOperations::AppendSkinWeight");
	FSkeletalMeshConstAttributes SourceSkeletalMeshAttributes(SourceMesh);
	
	FSkeletalMeshAttributes TargetSkeletalMeshAttributes(TargetMesh);
	constexpr bool bKeepExistingAttribute = true;
	TargetSkeletalMeshAttributes.Register(bKeepExistingAttribute);
	
	FSkinWeightsVertexAttributesConstRef SourceVertexSkinWeights = SourceSkeletalMeshAttributes.GetVertexSkinWeights();
	FSkinWeightsVertexAttributesRef TargetVertexSkinWeights = TargetSkeletalMeshAttributes.GetVertexSkinWeights();

	TargetMesh.SuspendVertexIndexing();
	
	//Append Custom VertexAttribute
	if(AppendSettings.bAppendVertexAttributes)
	{
		TArray<FName> TargetCustomAttributeNames;
		TargetMesh.VertexAttributes().GetAttributeNames(TargetCustomAttributeNames);
		int32 TargetVertexIndexOffset = FMath::Max(TargetMesh.Vertices().Num() - SourceMesh.Vertices().Num(), 0);

		SourceMesh.VertexAttributes().ForEachByType<float>(UE::Private::FCreateAndCopyAttributeValues<float>(SourceMesh, TargetMesh, TargetCustomAttributeNames, AppendSettings.SourceVertexIDOffset));
		SourceMesh.VertexAttributes().ForEachByType<FVector2f>(UE::Private::FCreateAndCopyAttributeValues<FVector2f>(SourceMesh, TargetMesh, TargetCustomAttributeNames, AppendSettings.SourceVertexIDOffset));
		SourceMesh.VertexAttributes().ForEachByType<FVector3f>(UE::Private::FCreateAndCopyAttributeValues<FVector3f>(SourceMesh, TargetMesh, TargetCustomAttributeNames, AppendSettings.SourceVertexIDOffset));
		SourceMesh.VertexAttributes().ForEachByType<FVector4f>(UE::Private::FCreateAndCopyAttributeValues<FVector4f>(SourceMesh, TargetMesh, TargetCustomAttributeNames, AppendSettings.SourceVertexIDOffset));
	}

	for (const FVertexID SourceVertexID : SourceMesh.Vertices().GetElementIDs())
	{
		const FVertexID TargetVertexID = FVertexID(AppendSettings.SourceVertexIDOffset + SourceVertexID.GetValue());
		FVertexBoneWeightsConst SourceBoneWeights = SourceVertexSkinWeights.Get(SourceVertexID);
		TArray<UE::AnimationCore::FBoneWeight> TargetBoneWeights;
		const int32 InfluenceCount = SourceBoneWeights.Num();
		for (int32 InfluenceIndex = 0; InfluenceIndex < InfluenceCount; ++InfluenceIndex)
		{
			const FBoneIndexType SourceBoneIndex = SourceBoneWeights[InfluenceIndex].GetBoneIndex();
			if(AppendSettings.SourceRemapBoneIndex.IsValidIndex(SourceBoneIndex))
			{
				UE::AnimationCore::FBoneWeight& TargetBoneWeight = TargetBoneWeights.AddDefaulted_GetRef();
				TargetBoneWeight.SetBoneIndex(AppendSettings.SourceRemapBoneIndex[SourceBoneIndex]);
				TargetBoneWeight.SetRawWeight(SourceBoneWeights[InfluenceIndex].GetRawWeight());
			}
		}
		TargetVertexSkinWeights.Set(TargetVertexID, TargetBoneWeights);
	}

	TargetMesh.ResumeVertexIndexing();
}


bool FSkeletalMeshOperations::CopySkinWeightAttributeFromMesh(
	const FMeshDescription& InSourceMesh,
	FMeshDescription& InTargetMesh,
	const FName InSourceProfile,
	const FName InTargetProfile,
	const TMap<int32, int32>* SourceBoneIndexToTargetBoneIndexMap
	)
{
	// This is effectively a slower and dumber version of FTransferBoneWeights.
	using namespace UE::AnimationCore;
	using namespace UE::Geometry;

	FSkeletalMeshConstAttributes SourceAttributes(InSourceMesh);
	FSkeletalMeshAttributes TargetAttributes(InTargetMesh);
	
	FSkinWeightsVertexAttributesConstRef SourceWeights = SourceAttributes.GetVertexSkinWeights(InSourceProfile);
	FSkinWeightsVertexAttributesRef TargetWeights = TargetAttributes.GetVertexSkinWeights(InTargetProfile);
	TVertexAttributesConstRef<FVector3f> TargetPositions = TargetAttributes.GetVertexPositions();

	if (!SourceWeights.IsValid() || !TargetWeights.IsValid())
	{
		return false;
	}
	
	FMeshDescriptionTriangleMeshAdapter MeshAdapter(&InSourceMesh);
	TMeshAABBTree3<FMeshDescriptionTriangleMeshAdapter> BVH(&MeshAdapter);

	auto RemapBoneWeights = [SourceBoneIndexToTargetBoneIndexMap](const FVertexBoneWeightsConst& InWeights) -> FBoneWeights
	{
		TArray<FBoneWeight, TInlineAllocator<MaxInlineBoneWeightCount>> Weights;

		if (SourceBoneIndexToTargetBoneIndexMap)
		{
			for (FBoneWeight OriginalWeight: InWeights)
			{
				if (const int32* BoneIndexPtr = SourceBoneIndexToTargetBoneIndexMap->Find(OriginalWeight.GetBoneIndex()))
				{
					FBoneWeight NewWeight(static_cast<FBoneIndexType>(*BoneIndexPtr), OriginalWeight.GetRawWeight());
					Weights.Add(NewWeight);
				}
			}

			if (Weights.IsEmpty())
			{
				const FBoneWeight RootBoneWeight(0, 1.0f);
				Weights.Add(RootBoneWeight);
			}
		}
		else
		{
			for (FBoneWeight Weight: InWeights)
			{
				Weights.Add(Weight);
			}
		}
		return FBoneWeights::Create(Weights);
	};
	
	auto InterpolateWeights = [&MeshAdapter, &SourceWeights, &RemapBoneWeights](int32 InTriangleIndex, const FVector3d& InTargetPoint) -> FBoneWeights
	{
		const FDistPoint3Triangle3d Query = TMeshQueries<FMeshDescriptionTriangleMeshAdapter>::TriangleDistance(MeshAdapter, InTriangleIndex, InTargetPoint);

		const FIndex3i TriangleVertexes = MeshAdapter.GetTriangle(InTriangleIndex);
		const FVector3f BaryCoords(VectorUtil::BarycentricCoords(Query.ClosestTrianglePoint, MeshAdapter.GetVertex(TriangleVertexes.A), MeshAdapter.GetVertex(TriangleVertexes.B), MeshAdapter.GetVertex(TriangleVertexes.C)));
		const FBoneWeights WeightsA = RemapBoneWeights(SourceWeights.Get(TriangleVertexes.A));
		const FBoneWeights WeightsB = RemapBoneWeights(SourceWeights.Get(TriangleVertexes.B));
		const FBoneWeights WeightsC = RemapBoneWeights(SourceWeights.Get(TriangleVertexes.C));

		FBoneWeights BoneWeights = FBoneWeights::Blend(WeightsA, WeightsB, WeightsC, BaryCoords.X, BaryCoords.Y, BaryCoords.Z);
		
		// Blending can leave us with zero weights. Let's strip them out here.
		BoneWeights.Renormalize();
		return BoneWeights;
	};

	TArray<FBoneWeights> TargetBoneWeights;
	TargetBoneWeights.SetNum(InTargetMesh.Vertices().GetArraySize());

	ParallelFor(InTargetMesh.Vertices().GetArraySize(), [&BVH, &InTargetMesh, &TargetPositions, &TargetBoneWeights, &InterpolateWeights](int32 InVertexIndex)
	{
		const FVertexID VertexID(InVertexIndex);
		if (!InTargetMesh.Vertices().IsValid(VertexID))
		{
			return;
		}

		const FVector3d TargetPoint(TargetPositions.Get(VertexID));

		const IMeshSpatial::FQueryOptions Options;
		double NearestDistanceSquared;
		const int32 NearestTriangleIndex = BVH.FindNearestTriangle(TargetPoint, NearestDistanceSquared, Options);

		if (!ensure(NearestTriangleIndex != IndexConstants::InvalidID))
		{
			return;
		}

		TargetBoneWeights[InVertexIndex] = InterpolateWeights(NearestTriangleIndex, TargetPoint);
	});

	// Transfer the computed bone weights to the target mesh.
	for (FVertexID TargetVertexID: InTargetMesh.Vertices().GetElementIDs())
	{
		FBoneWeights& BoneWeights = TargetBoneWeights[TargetVertexID];
		if (BoneWeights.Num() == 0)
		{
			// Bind to root so that we have something.
			BoneWeights.SetBoneWeight(FBoneIndexType{0}, 1.0);
		}

		TargetWeights.Set(TargetVertexID, BoneWeights);
	}

	return true;
}

bool FSkeletalMeshOperations::RemapBoneIndicesOnSkinWeightAttribute(FMeshDescription& InMesh, TConstArrayView<int32> InBoneIndexMapping)
{
	using namespace UE::AnimationCore;
	
	FSkeletalMeshAttributes MeshAttributes(InMesh);

	// Don't renormalize, since we are not changing the weights or order.
	FBoneWeightsSettings Settings;
	Settings.SetNormalizeType(EBoneWeightNormalizeType::None);
	
	TArray<FBoneWeight> NewBoneWeights;
	for (const FName AttributeName: MeshAttributes.GetSkinWeightProfileNames())
	{
		FSkinWeightsVertexAttributesRef SkinWeights(MeshAttributes.GetVertexSkinWeights(AttributeName));

		for (FVertexID VertexID: InMesh.Vertices().GetElementIDs())
		{
			FVertexBoneWeights OldBoneWeights = SkinWeights.Get(VertexID);
			NewBoneWeights.Reset(OldBoneWeights.Num());

			for (FBoneWeight BoneWeight: OldBoneWeights)
			{
				if (!ensure(InBoneIndexMapping.IsValidIndex(BoneWeight.GetBoneIndex())))
				{
					return false;
				}
				
				BoneWeight.SetBoneIndex(InBoneIndexMapping[BoneWeight.GetBoneIndex()]);
				NewBoneWeights.Add(BoneWeight);
			}

			SkinWeights.Set(VertexID, FBoneWeights::Create(NewBoneWeights, Settings));
		}
	}
	return true;
}

namespace UE::Impl
{
static void PoseMesh(
	FMeshDescription& InOutTargetMesh,
	TConstArrayView<FMatrix44f> InRefToUserTransforms, 
	const FName InSkinWeightProfile, 
	const TMap<FName, float>& InMorphTargetWeights
	)
{
	struct FMorphInfo
	{
		TVertexAttributesRef<FVector3f> PositionDelta;
		TVertexInstanceAttributesRef<FVector3f> NormalDelta;
		float Weight = 0.0f;
	};
	
	FSkeletalMeshAttributes Attributes(InOutTargetMesh);

	// We need the mesh to be compact for the parallel for to work.
	FElementIDRemappings Remappings;
	InOutTargetMesh.Compact(Remappings);

	TVertexAttributesRef<FVector3f> PositionAttribute = Attributes.GetVertexPositions();
	TVertexInstanceAttributesRef<FVector3f> NormalAttribute = Attributes.GetVertexInstanceNormals();
	TVertexInstanceAttributesRef<FVector3f> TangentAttribute = Attributes.GetVertexInstanceTangents();
	TVertexInstanceAttributesRef<float> BinormalSignsAttribute = Attributes.GetVertexInstanceBinormalSigns();

	// See which morph target attributes we can peel out. If the normal attributes are not all valid, then
	// we have to automatically compute the normal from the positions. Otherwise, we only use the normal deltas. 
	TArray<FMorphInfo> MorphInfos;
	bool bAllMorphNormalsValid = true;
	for (const TPair<FName, float>& Item: InMorphTargetWeights)
	{
		const FName MorphName{Item.Key};
		const float MorphWeight{Item.Value};
		TVertexAttributesRef<FVector3f> PositionDelta = Attributes.GetVertexMorphPositionDelta(MorphName);
		// Q: Should we use the value of `r.MorphTarget.WeightThreshold` instead? The following condition is
		// identical to the default setting of that value.
		if (PositionDelta.IsValid() && !FMath::IsNearlyZero(MorphWeight))
		{
			TVertexInstanceAttributesRef<FVector3f> NormalDelta = Attributes.GetVertexInstanceMorphNormalDelta(MorphName);
			if (!NormalDelta.IsValid())
			{
				bAllMorphNormalsValid = false;
			}
			FMorphInfo MorphInfo;
			MorphInfo.PositionDelta = PositionDelta;
			MorphInfo.NormalDelta = NormalDelta;
			MorphInfo.Weight = MorphWeight;
			MorphInfos.Add(MorphInfo);
		}
	}

	// First we apply the morph info on the positions and normals.
	if (!MorphInfos.IsEmpty())
	{
		struct FMorphProcessContext
		{
			TSet<FVertexInstanceID> DirtyVertexInstances;
			TArray<FVertexID> Neighbors;
		};
		TArray<FMorphProcessContext> Contexts;
		ParallelForWithTaskContext(Contexts, InOutTargetMesh.Vertices().Num(),
			[&Mesh=InOutTargetMesh, &PositionAttribute, &MorphInfos, bAllMorphNormalsValid](FMorphProcessContext& Context, int32 Index)
			{
				const FVertexID VertexID{Index};

				FVector3f Position = PositionAttribute.Get(VertexID);
				bool bMoved = false;
				for (const FMorphInfo& MorphInfo: MorphInfos)
				{
					const FVector3f PositionDelta = MorphInfo.PositionDelta.Get(VertexID) * MorphInfo.Weight;
					if (!PositionDelta.IsNearlyZero())
					{
						Position += PositionDelta;
						bMoved = true;
					}
				}

				// If we need to re-generate the normals, store which vertices got moved _and_ their neighbors, since the whole
				// triangle moved, which affects neighboring vertices of the moved vertex.
				if (bMoved)
				{
					PositionAttribute.Set(VertexID, Position);
					
					if (!bAllMorphNormalsValid)
					{
						Context.DirtyVertexInstances.Append(Mesh.GetVertexVertexInstanceIDs(VertexID));

						Mesh.GetVertexAdjacentVertices(VertexID, Context.Neighbors);
						for (const FVertexID NeighborVertexID: Context.Neighbors)
						{
							Context.DirtyVertexInstances.Append(Mesh.GetVertexVertexInstanceIDs(NeighborVertexID));
						}
					}
				}
			});

		if (bAllMorphNormalsValid)
		{
			ParallelForWithTaskContext(Contexts, InOutTargetMesh.VertexInstances().Num(),
				[&MorphInfos, &NormalAttribute, &TangentAttribute, &BinormalSignsAttribute](FMorphProcessContext& Context, int32 Index)
				{
					FVertexInstanceID VertexInstanceID{Index};

					FVector3f Normal = NormalAttribute.Get(VertexInstanceID);
					FVector3f Tangent = TangentAttribute.Get(VertexInstanceID);
					FVector3f Binormal = FVector3f::CrossProduct(Normal, Tangent) * BinormalSignsAttribute.Get(VertexInstanceID);
					
					bool bMoved = false;
					for (const FMorphInfo& MorphInfo: MorphInfos)
					{
						const FVector3f NormalDelta = MorphInfo.NormalDelta.Get(VertexInstanceID) * MorphInfo.Weight;
						if (!NormalDelta.IsNearlyZero())
						{
							Normal += NormalDelta;
							bMoved = true;
						}
					}

					if (bMoved)
					{
						if (Normal.Normalize())
						{
							// Badly named function. This orthonormalizes X & Y using Z as the control. 
							FVector3f::CreateOrthonormalBasis(Tangent, Binormal, Normal);
							
							NormalAttribute.Set(VertexInstanceID, Normal);
							TangentAttribute.Set(VertexInstanceID, Tangent);
							const float BinormalSign = FMatrix44f(Tangent, Binormal, Normal, FVector3f::ZeroVector).Determinant() < 0 ? -1.0f : +1.0f;
							BinormalSignsAttribute.Set(VertexInstanceID, BinormalSign);
						}
						else
						{
							// Something went wrong. Reconstruct the normal from the tangent and binormal.
							Context.DirtyVertexInstances.Add(VertexInstanceID);
						}
					}
				});
		}
		
		// Clear out any normals that were affected by the point move, or ended up being degenerate during normal offsetting.
		TSet<FVertexInstanceID> DirtyVertexInstances;
		for (const FMorphProcessContext& ProcessContext: Contexts)
		{
			DirtyVertexInstances.Append(ProcessContext.DirtyVertexInstances);
		}
		
		if (!DirtyVertexInstances.IsEmpty())
		{
			// Mark any vector as zero that we want to regenerate from triangle + neighbors + tangents.
			for (const FVertexInstanceID VertexInstanceID: DirtyVertexInstances)
			{
				NormalAttribute.Set(VertexInstanceID, FVector3f::ZeroVector);
			}

			FSkeletalMeshOperations::ComputeTriangleTangentsAndNormals(InOutTargetMesh, UE_SMALL_NUMBER, nullptr);

			// Compute the normals on the dirty vertices, and adjust the tangents to match.
			FSkeletalMeshOperations::ComputeTangentsAndNormals(InOutTargetMesh, EComputeNTBsFlags::WeightedNTBs);
			
			// We don't need the triangle tangents and normals anymore.
			InOutTargetMesh.TriangleAttributes().UnregisterAttribute(MeshAttribute::Triangle::Normal);
			InOutTargetMesh.TriangleAttributes().UnregisterAttribute(MeshAttribute::Triangle::Tangent);
			InOutTargetMesh.TriangleAttributes().UnregisterAttribute(MeshAttribute::Triangle::Binormal);
		}
	}

	using namespace UE::AnimationCore;

	// The normal needs to be transformed using the inverse transpose of the transform matrices to ensure that
	// scaling works correctly. 
	TArray<FMatrix44f> RefToUserTransformsNormal;
	RefToUserTransformsNormal.Reserve(InRefToUserTransforms.Num());
	for (const FMatrix44f& Mat: InRefToUserTransforms)
	{
		RefToUserTransformsNormal.Add(Mat.Inverse().GetTransposed());
	}
	
	FSkinWeightsVertexAttributesRef SkinWeightAttribute = Attributes.GetVertexSkinWeights(InSkinWeightProfile);
	ParallelFor(InOutTargetMesh.Vertices().Num(),
		[&Mesh=InOutTargetMesh, &PositionAttribute, &NormalAttribute, &TangentAttribute, &SkinWeightAttribute, &RefToUserTransforms=InRefToUserTransforms, &RefToUserTransformsNormal](int32 Index)
		{
			const FVertexID VertexID(Index);
			const FVertexBoneWeights BoneWeights = SkinWeightAttribute.Get(VertexID);
			const FVector3f Position = PositionAttribute.Get(VertexID);
			FVector3f SkinnedPosition = FVector3f::ZeroVector;

			for (const FBoneWeight BW: BoneWeights)
			{
				SkinnedPosition += RefToUserTransforms[BW.GetBoneIndex()].TransformPosition(Position) * BW.GetWeight();
			}
			PositionAttribute.Set(VertexID, SkinnedPosition);
			
			for (const FVertexInstanceID VertexInstanceID: Mesh.GetVertexVertexInstanceIDs(VertexID))
			{
				const FVector3f Normal = NormalAttribute.Get(VertexInstanceID);
				const FVector3f Tangent = TangentAttribute.Get(VertexInstanceID);
				FVector3f SkinnedNormal = FVector3f::ZeroVector;
				FVector3f SkinnedTangent = FVector3f::ZeroVector;

				for (const FBoneWeight BW: BoneWeights)
				{
					SkinnedNormal += RefToUserTransformsNormal[BW.GetBoneIndex()].TransformVector(Normal) * BW.GetWeight();
					SkinnedTangent += RefToUserTransforms[BW.GetBoneIndex()].TransformVector(Tangent) * BW.GetWeight();
				}
				
				SkinnedNormal.Normalize();
				SkinnedTangent.Normalize();

				NormalAttribute.Set(VertexInstanceID, SkinnedNormal);
				TangentAttribute.Set(VertexInstanceID, SkinnedTangent);
			}
		});
}

} // namespace Impl

bool FSkeletalMeshOperations::GetPosedMesh(
	const FMeshDescription& InSourceMesh, 
	FMeshDescription& OutTargetMesh,
	TConstArrayView<FTransform> InComponentSpaceTransforms, 
	const FName InSkinWeightProfile, 
	const TMap<FName, float>& InMorphTargetWeights
	)
{
	// Verify that the mesh is valid.
	FSkeletalMeshConstAttributes Attributes(InSourceMesh);
	if (!Attributes.HasBonePoseAttribute() || !Attributes.HasBoneParentIndexAttribute())
	{
		return false;
	}

	if (!Attributes.GetVertexSkinWeights(InSkinWeightProfile).IsValid())
	{
		return false;
	}

	if (Attributes.GetNumBones() != InComponentSpaceTransforms.Num())
	{
		return false;
	}

	// Convert the component-space transforms into a set of matrices that transform from the reference pose to
	// the user pose. These are then used to nudge the vertices from the reference pose to the wanted
	// user pose by weighing the influence of each bone on a given vertex. If the user pose and the reference pose
	// are identical, these are all identity matrices.
	TArray<FMatrix44f> RefToUserTransforms;
	TArray<FMatrix44f> RefPoseTransforms;
	
	FSkeletalMeshAttributes::FBonePoseAttributesConstRef BonePoseAttribute = Attributes.GetBonePoses(); 
	FSkeletalMeshAttributes::FBoneParentIndexAttributesConstRef ParentBoneIndexAttribute = Attributes.GetBoneParentIndices(); 
	RefToUserTransforms.SetNumUninitialized(Attributes.GetNumBones());
	RefPoseTransforms.SetNumUninitialized(Attributes.GetNumBones());
	
	for (int32 BoneIndex = 0; BoneIndex < Attributes.GetNumBones(); BoneIndex++)
	{
		const int32 ParentBoneIndex = ParentBoneIndexAttribute.Get(BoneIndex);
		RefPoseTransforms[BoneIndex] = FMatrix44f{BonePoseAttribute.Get(BoneIndex).ToMatrixWithScale()};

		if (ParentBoneIndex != INDEX_NONE)
		{
			RefPoseTransforms[BoneIndex] = RefPoseTransforms[BoneIndex] * RefPoseTransforms[ParentBoneIndex];
		}

		RefToUserTransforms[BoneIndex] = RefPoseTransforms[BoneIndex].Inverse() * FMatrix44f{InComponentSpaceTransforms[BoneIndex].ToMatrixWithScale()};
	}
	
	// Start with a fresh duplicate and then pose the target mesh in-place.
	OutTargetMesh = InSourceMesh;
	UE::Impl::PoseMesh(OutTargetMesh, RefToUserTransforms, InSkinWeightProfile, InMorphTargetWeights);

	// Write out the current ref pose (in bone-space) to the mesh. 
	FSkeletalMeshAttributes WriteAttributes(OutTargetMesh);
	FSkeletalMeshAttributes::FBonePoseAttributesRef WriteBonePoseAttribute = WriteAttributes.GetBonePoses(); 
	for (int32 BoneIndex = 0; BoneIndex < Attributes.GetNumBones(); BoneIndex++)
	{
		const int32 ParentBoneIndex = ParentBoneIndexAttribute.Get(BoneIndex);
		FTransform RefPoseTransform = InComponentSpaceTransforms[BoneIndex];

		if (ParentBoneIndex != INDEX_NONE)
		{
			RefPoseTransform = RefPoseTransform.GetRelativeTransform(InComponentSpaceTransforms[ParentBoneIndex]);
		}
		WriteBonePoseAttribute.Set(BoneIndex, RefPoseTransform);
	}
	
	return true;
}


bool FSkeletalMeshOperations::GetPosedMesh(
	const FMeshDescription& InSourceMesh,
	FMeshDescription& OutTargetMesh,
	const TMap<FName, FTransform>& InBoneSpaceTransforms,
	const FName InSkinWeightProfile, 
	const TMap<FName, float>& InMorphTargetWeights
	)
{
	// Verify that the mesh is valid.
	FSkeletalMeshConstAttributes Attributes(InSourceMesh);
	if (!Attributes.HasBoneNameAttribute() || !Attributes.HasBonePoseAttribute() || !Attributes.HasBoneParentIndexAttribute())
	{
		return false;
	}

	if (!Attributes.GetVertexSkinWeights(InSkinWeightProfile).IsValid())
	{
		return false;
	}

	TArray<FMatrix44f> RefToUserTransforms;
	TArray<FMatrix44f> RefPoseTransforms;
	TArray<FMatrix44f> UserPoseTransforms;
	
	FSkeletalMeshAttributes::FBoneNameAttributesConstRef BoneNameAttribute = Attributes.GetBoneNames(); 
	FSkeletalMeshAttributes::FBonePoseAttributesConstRef BonePoseAttribute = Attributes.GetBonePoses(); 
	FSkeletalMeshAttributes::FBoneParentIndexAttributesConstRef ParentBoneIndexAttribute = Attributes.GetBoneParentIndices();
	
	RefToUserTransforms.SetNumUninitialized(Attributes.GetNumBones());
	RefPoseTransforms.SetNumUninitialized(Attributes.GetNumBones());
	UserPoseTransforms.SetNumUninitialized(Attributes.GetNumBones());

	for (int32 BoneIndex = 0; BoneIndex < Attributes.GetNumBones(); BoneIndex++)
	{
		const FName BoneName = BoneNameAttribute.Get(BoneIndex);
		const int32 ParentBoneIndex = ParentBoneIndexAttribute.Get(BoneIndex);
		RefPoseTransforms[BoneIndex] = FMatrix44f{BonePoseAttribute.Get(BoneIndex).ToMatrixWithScale()};
		if (const FTransform* UserTransform = InBoneSpaceTransforms.Find(BoneName))
		{
			UserPoseTransforms[BoneIndex] = FMatrix44f{UserTransform->ToMatrixWithScale()};

			// Update the pose on the mesh to match the user pose.
		}
		else
		{
			UserPoseTransforms[BoneIndex] = RefPoseTransforms[BoneIndex];
		}
		
		if (ParentBoneIndex != INDEX_NONE)
		{
			RefPoseTransforms[BoneIndex] = RefPoseTransforms[BoneIndex] * RefPoseTransforms[ParentBoneIndex];
			UserPoseTransforms[BoneIndex] = UserPoseTransforms[BoneIndex] * UserPoseTransforms[ParentBoneIndex];
		}

		RefToUserTransforms[BoneIndex] = RefPoseTransforms[BoneIndex].Inverse() * UserPoseTransforms[BoneIndex];
	}

	// Start with a fresh duplicate and then pose the target mesh in-place.
	OutTargetMesh = InSourceMesh;
	UE::Impl::PoseMesh(OutTargetMesh, RefToUserTransforms, InSkinWeightProfile, InMorphTargetWeights);

	FSkeletalMeshAttributes WriteAttributes(OutTargetMesh);
	FSkeletalMeshAttributes::FBonePoseAttributesRef WriteBonePoseAttribute = WriteAttributes.GetBonePoses(); 
	for (int32 BoneIndex = 0; BoneIndex < Attributes.GetNumBones(); BoneIndex++)
	{
		const FName BoneName = BoneNameAttribute.Get(BoneIndex);
		if (const FTransform* UserTransform = InBoneSpaceTransforms.Find(BoneName))
		{
			WriteBonePoseAttribute.Set(BoneIndex, *UserTransform);
		}
	}

	return true;
}


#undef LOCTEXT_NAMESPACE
