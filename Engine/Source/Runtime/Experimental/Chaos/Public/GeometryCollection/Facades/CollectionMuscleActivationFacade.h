// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GeometryCollection/GeometryCollection.h"
#include "GeometryCollection/ManagedArrayAccessor.h"
#include "GeometryCollection/ManagedArrayCollection.h"
#include "GeometryCollection/Facades/CollectionSelectionFacade.h"

namespace GeometryCollection::Facades
{
	//Activation data for each muscle
	struct FMuscleActivationData
	{
		int32 GeometryGroupIndex; //Geometry group index of the muscle
		TArray<int32> MuscleActivationElement; //Contractible tetrahedra
		FIntVector2 OriginInsertionPair; //Muscle origin point and insertion point (to determine muscle length)
		float OriginInsertionRestLength; //Muscle origin-insertion rest length
		TArray<Chaos::PMatrix33d> FiberDirectionMatrix; //Per-element fiber direction orthogonal matrix: [v, w1, w2], v is the fiber direction
		TArray<float> ContractionVolumeScale; // Per-element volume scale for muscle contraction
		TArray<TArray<FVector3f>> FiberStreamline; //Fiber streamline(s) for inverse dynamics
		TArray<float> FiberStreamlineRestLength; //Fiber streamline rest length(s)
	};

	/** Muscle Activation Facade */
	class FMuscleActivationFacade
	{
	public:

		static CHAOS_API const FName GroupName;
		static CHAOS_API const FName GeometryGroupIndex;
		static CHAOS_API const FName MuscleActivationElement;
		static CHAOS_API const FName OriginInsertionPair;
		static CHAOS_API const FName OriginInsertionRestLength;
		static CHAOS_API const FName FiberDirectionMatrix;
		static CHAOS_API const FName ContractionVolumeScale;
		static CHAOS_API const FName FiberStreamline;
		static CHAOS_API const FName FiberStreamlineRestLength;

		CHAOS_API FMuscleActivationFacade(FManagedArrayCollection& InCollection);
		CHAOS_API FMuscleActivationFacade(const FManagedArrayCollection& InCollection);

		/** Create the facade attributes. */
		CHAOS_API void DefineSchema();

		/** Is the facade defined constant. */
		bool IsConst() const { return Collection == nullptr; }

		/** Is the Facade defined on the collection? */
		CHAOS_API bool IsValid() const;

		//
		//  Skeletal Mesh Bone Bindings
		//
		CHAOS_API int32 AddMuscleActivationData(const FMuscleActivationData& InputData);
		CHAOS_API bool UpdateMuscleActivationData(const int32 DataIndex, FMuscleActivationData& InputData);
		CHAOS_API FMuscleActivationData GetMuscleActivationData(const int32 DataIndex) const;
		CHAOS_API bool IsValidGeometryIndex(const int32 Index) const { return 0 <= Index && Index < ConstCollection.NumElements(FGeometryCollection::GeometryGroup); };
		CHAOS_API bool IsValidElementIndex(const int32 Index) const { return 0 <= Index && Index < ConstCollection.NumElements("Tetrahedral"); };
		CHAOS_API int32 NumMuscles() const { return MuscleActivationElementAttribute.Num(); }
		CHAOS_API bool SetUpMuscleActivation(const TArray<int32>& Origin, const TArray<int32>& Insertion, float ContractionVolumeScale);
		CHAOS_API TArray<TArray<TArray<FVector3f>>> BuildStreamlines(const TArray<int32>& Origin, const TArray<int32>& Insertion,
			 int32 NumLinesMultiplier, int32 MaxStreamlineIterations, int32 MaxPointsPerLine);
	private:
		const FManagedArrayCollection& ConstCollection;
		FManagedArrayCollection* Collection = nullptr;
		TManagedArrayAccessor<int32> GeometryGroupIndexAttribute;
		TManagedArrayAccessor<TArray<int32>> MuscleActivationElementAttribute;
		TManagedArrayAccessor<FIntVector2> OriginInsertionPairAttribute;
		TManagedArrayAccessor<float> OriginInsertionRestLengthAttribute;
		TManagedArrayAccessor<TArray<Chaos::PMatrix33d>> FiberDirectionMatrixAttribute;
		TManagedArrayAccessor<TArray<float>> ContractionVolumeScaleAttribute;
		TManagedArrayAccessor<TArray<TArray<FVector3f>>> FiberStreamlineAttribute;
		TManagedArrayAccessor<TArray<float>> FiberStreamlineRestLengthAttribute;
	};
}
