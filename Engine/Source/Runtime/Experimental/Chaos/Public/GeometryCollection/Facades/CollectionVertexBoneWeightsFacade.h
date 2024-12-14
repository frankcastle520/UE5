// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "Containers/UnrealString.h"
#include "Containers/Array.h"
#include "GeometryCollection/Facades/CollectionSelectionFacade.h"
#include "GeometryCollection/ManagedArrayCollection.h"
#include "GeometryCollection/ManagedArrayAccessor.h"

namespace GeometryCollection::Facades
{

	/**
	* FVertexBoneWeightsFacade
	* 
	* Defines common API for storing a vertex weights bound to a bone. This mapping is from the 
	* the vertex to the bone index. Kinematic array specifies whether vertices are considered kinematic. 
	* Non-kinematic vertices can also have associated bone indices and weights.
	* 
	* Then arrays can be accessed later by:
	*	const TManagedArray< TArray<int32> >* BoneIndices = FVertexBoneWeightsFacade::GetBoneIndices(this);
	*	const TManagedArray< TArray<float> >* BoneWeights = FVertexBoneWeightsFacade::GetBoneWeights(this);
	* 
	* The following attributes are created on the collection:
	* 
	*	- FindAttribute<TArray<int32>>(FVertexSetInterface::IndexAttribute, FGeometryCollection::VerticesGroup);
	*	- FindAttribute<TArray<float>>(FVertexSetInterface::WeightAttribute, FGeometryCollection::VerticesGroup);
	* 
	*/
	class FVertexBoneWeightsFacade
	{
	public:

		// Attributes
		static CHAOS_API const FName BoneIndexAttributeName;
		static CHAOS_API const FName BoneWeightAttributeName;
		static CHAOS_API const FName KinematicAttributeName;
		/**
		* FVertexBoneWeightsFacade Constuctor
		*/
		CHAOS_API FVertexBoneWeightsFacade(FManagedArrayCollection& InSelf);
		CHAOS_API FVertexBoneWeightsFacade(const FManagedArrayCollection& InSelf);

		/** Define the facade */
		CHAOS_API void DefineSchema();

		/** Is the Facade const */
		bool IsConst() const { return Collection == nullptr; }

		/** Is the Facade defined on the collection? */
		CHAOS_API bool IsValid() const;

		/** Add bone weight based on the kinematic bindings. */
		CHAOS_API void AddBoneWeightsFromKinematicBindings();

		/** Add single bone/weight to vertex */
		CHAOS_API void AddBoneWeight(int32 VertexIndex, int32 BoneIndex, float BoneWeight);

		/** Modify bone weight based on the kinematic bindings. */
		CHAOS_API void ModifyBoneWeight(int32 VertexIndex, TArray<int32> VertexBoneIndex, TArray<float> VertexBoneWeight);

		/** Set vertex to be kinematic */
		CHAOS_API void SetVertexKinematic(int32 VertexIndex);
		CHAOS_API void SetVertexArrayKinematic(const TArray<int32>& VertexIndices);

		/** Return the vertex bone indices from the collection. Null if not initialized.  */
		const TManagedArray< TArray<int32> >* FindBoneIndices()  const { return BoneIndexAttribute.Find(); }
		const TManagedArray< TArray<int32> >& GetBoneIndices() const { return BoneIndexAttribute.Get(); }

		/** Return if the vertex is kinematic
		Pre 5.5 we did not have per-vertex kinematic attribute. 
		This supports defining kinematics without per-vertex kinematic attribute. */
		CHAOS_API bool IsKinematicVertex(int32 VertexIndex) const;

		/** Return number of vertices */
		CHAOS_API int32 NumVertices() const { return VerticesAttribute.Num(); };

		/** Return the vertex bone weights from the collection. Null if not initialized. */
		const TManagedArray< TArray<float> >* FindBoneWeights()  const { return BoneWeightAttribute.Find(); }
		const TManagedArray< TArray<float> >& GetBoneWeights() const { return BoneWeightAttribute.Get(); }

	private:
		const FManagedArrayCollection& ConstCollection;
		FManagedArrayCollection* Collection = nullptr;

		TManagedArrayAccessor<TArray<int32>> BoneIndexAttribute;
		TManagedArrayAccessor<TArray<float>> BoneWeightAttribute;
		TManagedArrayAccessor<bool> KinematicAttribute;
		TManagedArrayAccessor<int32> ParentAttribute;
		TManagedArrayAccessor<FVector3f> VerticesAttribute;

	};

}
