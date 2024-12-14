// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	GeometryCollection.cpp: FGeometryCollection methods.
=============================================================================*/

#include "GeometryCollection/Facades/CollectionVertexBoneWeightsFacade.h"

#include "CoreMinimal.h"
#include "GeometryCollection/Facades/CollectionKinematicBindingFacade.h"
#include "GeometryCollection/GeometryCollection.h"

DEFINE_LOG_CATEGORY_STATIC(LogDataflowVertexBoneWeightsFacade, Log, All);

namespace GeometryCollection::Facades
{

	// Attributes
	const FName FVertexBoneWeightsFacade::BoneWeightAttributeName = "BoneWeights";
	const FName FVertexBoneWeightsFacade::BoneIndexAttributeName = "BoneWeightsIndex";
	const FName FVertexBoneWeightsFacade::KinematicAttributeName = "Kinematic";
	
	FVertexBoneWeightsFacade::FVertexBoneWeightsFacade(FManagedArrayCollection& InCollection)
		: ConstCollection(InCollection)
		, Collection(&InCollection)
		, BoneIndexAttribute(InCollection, BoneIndexAttributeName, FGeometryCollection::VerticesGroup, FTransformCollection::TransformGroup)
		, BoneWeightAttribute(InCollection, BoneWeightAttributeName, FGeometryCollection::VerticesGroup, FTransformCollection::TransformGroup)
		, KinematicAttribute(InCollection, KinematicAttributeName, FGeometryCollection::VerticesGroup)
		, ParentAttribute(InCollection, FTransformCollection::ParentAttribute, FTransformCollection::TransformGroup)
		, VerticesAttribute(InCollection, "Vertex", FGeometryCollection::VerticesGroup)
	{
		DefineSchema();
	}

	FVertexBoneWeightsFacade::FVertexBoneWeightsFacade(const FManagedArrayCollection& InCollection)
		: ConstCollection(InCollection)
		, Collection(nullptr)
		, BoneIndexAttribute(InCollection, BoneIndexAttributeName, FGeometryCollection::VerticesGroup, FTransformCollection::TransformGroup)
		, BoneWeightAttribute(InCollection, BoneWeightAttributeName, FGeometryCollection::VerticesGroup, FTransformCollection::TransformGroup)
		, KinematicAttribute(InCollection, KinematicAttributeName, FGeometryCollection::VerticesGroup)
		, ParentAttribute(InCollection, FTransformCollection::ParentAttribute, FTransformCollection::TransformGroup)
		, VerticesAttribute(InCollection, "Vertex", FGeometryCollection::VerticesGroup)
	{
	}


	//
	//  Initialization
	//

	void FVertexBoneWeightsFacade::DefineSchema()
	{
		check(!IsConst());
		BoneIndexAttribute.Add();
		BoneWeightAttribute.Add();
		KinematicAttribute.AddAndFill(false);
		ParentAttribute.Add();
		if (!BoneIndexAttribute.IsValid())
		{
			UE_LOG(LogDataflowVertexBoneWeightsFacade, Warning, TEXT("FVertexBoneWeightsFacade failed to initialize because '%s' attribute from '%s' group expected type TArray<int32>."), *FVertexBoneWeightsFacade::BoneIndexAttributeName.ToString(), *FGeometryCollection::VerticesGroup.ToString());
		}
		if (!BoneWeightAttribute.IsValid())
		{
			UE_LOG(LogDataflowVertexBoneWeightsFacade, Warning, TEXT("FVertexBoneWeightsFacade failed to initialize because '%s' attribute from '%s' group expected type TArray<float>."), *FVertexBoneWeightsFacade::BoneWeightAttributeName.ToString(), *FGeometryCollection::VerticesGroup.ToString());
		}
		if (!KinematicAttribute.IsValid())
		{
			UE_LOG(LogDataflowVertexBoneWeightsFacade, Warning, TEXT("FVertexBoneWeightsFacade failed to initialize because '%s' attribute from '%s' group expected type <bool>."), *FVertexBoneWeightsFacade::KinematicAttributeName.ToString(), *FGeometryCollection::VerticesGroup.ToString());
		}
		if (!ParentAttribute.IsValid())
		{
			UE_LOG(LogDataflowVertexBoneWeightsFacade, Warning, TEXT("FVertexBoneWeightsFacade failed to initialize because '%s' attribute from '%s' group expected type <int32>."), *FTransformCollection::ParentAttribute.ToString(), *FGeometryCollection::TransformGroup.ToString());
		}
	}

	bool FVertexBoneWeightsFacade::IsValid() const
	{
		return BoneIndexAttribute.IsValid() && BoneWeightAttribute.IsValid() && ParentAttribute.IsValid() && VerticesAttribute.IsValid();
	}


	//
	//  Add Weights from a bone to a vertex 
	//
	void FVertexBoneWeightsFacade::AddBoneWeight(int32 VertexIndex, int32 BoneIndex, float BoneWeight)
	{
		TManagedArray< TArray<int32> >& IndicesArray = BoneIndexAttribute.Modify();
		TManagedArray< TArray<float> >& WeightsArray = BoneWeightAttribute.Modify();
		const TManagedArray<FVector3f>& Vertices = VerticesAttribute.Modify();
		if (0 <= VertexIndex && VertexIndex < Vertices.Num())
		{
			if (0 <= BoneIndex && BoneIndex < ParentAttribute.Num())
			{
				IndicesArray[VertexIndex].Add(BoneIndex);
				WeightsArray[VertexIndex].Add(BoneWeight);
			}
		}
	}

	void FVertexBoneWeightsFacade::ModifyBoneWeight(int32 VertexIndex, TArray<int32> VertexBoneIndex, TArray<float> VertexBoneWeight)
	{
		TManagedArray< TArray<int32> >& IndicesArray = BoneIndexAttribute.Modify();
		TManagedArray< TArray<float> >& WeightsArray = BoneWeightAttribute.Modify();
		const TManagedArray<FVector3f>& Vertices = VerticesAttribute.Modify();
		if (VerticesAttribute.IsValidIndex(VertexIndex))
		{
			IndicesArray[VertexIndex].Empty();
			WeightsArray[VertexIndex].Empty();
			float TotalWeight = 0.f;
			for (int32 Idx = 0; Idx < VertexBoneIndex.Num(); ++Idx)
			{
				if (ParentAttribute.IsValidIndex(VertexBoneIndex[Idx]))
				{
					IndicesArray[VertexIndex].Add(VertexBoneIndex[Idx]);
					WeightsArray[VertexIndex].Add(VertexBoneWeight[Idx]);
					TotalWeight += VertexBoneWeight[Idx];
				}
			}
			if (TotalWeight < 1.f - UE_KINDA_SMALL_NUMBER || TotalWeight > 1.f + UE_KINDA_SMALL_NUMBER)
			{
				UE_LOG(LogChaos, Warning, TEXT("FVertexBoneWeightsFacade::ModifyBoneWeight: Bone weight sum %f is not 1 on vertex %d"), TotalWeight, VertexIndex);
			}
		}
	}

	void FVertexBoneWeightsFacade::SetVertexKinematic(int32 VertexIndex)
	{
		if (KinematicAttribute.IsValid() && KinematicAttribute.IsValidIndex(VertexIndex))
		{
			KinematicAttribute.ModifyAt(VertexIndex, true);
		}
	}

	void FVertexBoneWeightsFacade::SetVertexArrayKinematic(const TArray<int32>& VertexIndices)
	{
		if (KinematicAttribute.IsValid())
		{
			TManagedArray<bool>& KinematicArray = KinematicAttribute.Modify();
			for (const int32& VertexIndex : VertexIndices)
			{
				if (KinematicArray.IsValidIndex(VertexIndex))
				{
					KinematicArray[VertexIndex] = true;
				}
			}
		}
	}

	bool FVertexBoneWeightsFacade::IsKinematicVertex(int32 VertexIndex) const
	{
		if (KinematicAttribute.IsValid())
		{
			return KinematicAttribute.IsValidIndex(VertexIndex) && KinematicAttribute.Get()[VertexIndex];
		}
		else //backward compatibility for KinematicAttribute added in 5.5
		{
			return BoneIndexAttribute.IsValid() && BoneIndexAttribute.IsValidIndex(VertexIndex) && BoneIndexAttribute.Get()[VertexIndex].Num()
				&& BoneWeightAttribute.IsValid() && BoneWeightAttribute.IsValidIndex(VertexIndex) && BoneWeightAttribute.Get()[VertexIndex].Num();
		}
	};

	//
	//  Add Weights from Selection 
	//

	void FVertexBoneWeightsFacade::AddBoneWeightsFromKinematicBindings() {
		check(!IsConst());
		DefineSchema();

		if (IsValid())
		{
			TArray<float> Weights;
			TArray<int32> Indices;

			int32 NumBones = ParentAttribute.Num(), NumVertices = BoneIndexAttribute.Num();
			TManagedArray< TArray<int32> >& IndicesArray = BoneIndexAttribute.Modify();
			TManagedArray< TArray<float> >& WeightsArray = BoneWeightAttribute.Modify();

			TArray<float> TotalWeights;
			TotalWeights.Init(0.f, WeightsArray.Num());

			for (int32 Vert = 0; Vert < WeightsArray.Num(); Vert++)
			{
				for (int32 i = 0; i < WeightsArray[Vert].Num(); i++)
				{
					TotalWeights[Vert] += WeightsArray[Vert][i];
				}
			}

			GeometryCollection::Facades::FKinematicBindingFacade BindingFacade(ConstCollection);
			for (int32 Kdx = BindingFacade.NumKinematicBindings() - 1; 0 <= Kdx; Kdx--)
			{
				int32 Bone;
				TArray<int32> OutBoneVerts;
				TArray<float> OutBoneWeights;

				BindingFacade.GetBoneBindings(BindingFacade.GetKinematicBindingKey(Kdx), Bone, OutBoneVerts, OutBoneWeights);

				if (0 <= Bone && Bone < NumBones)
				{
					for (int32 Vdx = 0; Vdx < OutBoneVerts.Num(); Vdx++)
					{
						int32 Vert = OutBoneVerts[Vdx]; float Weight = OutBoneWeights[Vdx];
						if (0 <= Vert && Vert < NumVertices && !IndicesArray[Vert].Contains(Bone))
						{
							SetVertexKinematic(Vert);
							int32 BoneIndex = IndicesArray[Vert].Find(Bone);
							if (TotalWeights[Vert] + Weight <= 1.f + UE_KINDA_SMALL_NUMBER)
							{
								if (BoneIndex == INDEX_NONE)
								{
									IndicesArray[Vert].Add(Bone);
									WeightsArray[Vert].Add(Weight);
									TotalWeights[Vert] += Weight;
								}
								else if (0 <= BoneIndex && BoneIndex < WeightsArray[Vert].Num())
								{
									WeightsArray[Vert][BoneIndex] = Weight;
									TotalWeights[Vert] += Weight;
								}
							}
							else
							{
								UE_LOG(LogChaos, Warning, TEXT("Bone weight sum %f exceeds 1 on vertex %d"), TotalWeights[Vert] + Weight, Vert);
							}
						}
					}
				}
			}

		}
	}

};


