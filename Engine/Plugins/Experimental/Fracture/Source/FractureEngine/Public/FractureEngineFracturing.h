// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Math/Box.h"

#include "FractureEngineFracturing.generated.h"

namespace UE { namespace Geometry { class FDynamicMesh3; } }

struct FDataflowTransformSelection;
struct FManagedArrayCollection;

UENUM(BlueprintType)
enum class EFractureBrickBondEnum : uint8
{
	Dataflow_FractureBrickBond_Stretcher UMETA(DisplayName = "Stretcher"),
	Dataflow_FractureBrickBond_Stack UMETA(DisplayName = "Stack"),
	Dataflow_FractureBrickBond_English UMETA(DisplayName = "English"),
	Dataflow_FractureBrickBond_Header UMETA(DisplayName = "Header"),
	Dataflow_FractureBrickBond_Flemish UMETA(DisplayName = "Flemish"),
};

UENUM(BlueprintType)
enum class EMeshCutterCutDistribution : uint8
{
	// Cut only once, at the cutting mesh's current location in the level
	SingleCut UMETA(DisplayName = "Single Cut"),
	// Scatter the cutting mesh in a uniform random distribution around the geometry bounding box
	UniformRandom UMETA(DisplayName = "Uniform Random"),
	// Arrange the cutting mesh in a regular grid pattern
	Grid UMETA(DisplayName = "Grid"),
};

class FRACTUREENGINE_API FFractureEngineFracturing
{
public:

	static void GenerateExplodedViewAttribute(FManagedArrayCollection& InOutCollection, const FVector& InScale, float InUniformScale);

	static int32 VoronoiFracture(FManagedArrayCollection& InOutCollection,
		FDataflowTransformSelection InTransformSelection,
		TArray<FVector> InSites,
		const FTransform& InTransform,
		int32 InRandomSeed,
		float InChanceToFracture,
		bool InSplitIslands,
		float InGrout,
		float InAmplitude,
		float InFrequency,
		float InPersistence,
		float InLacunarity,
		int32 InOctaveNumber,
		float InPointSpacing,
		bool InAddSamplesForCollision,
		float InCollisionSampleSpacing);

	static int32 PlaneCutter(FManagedArrayCollection& InOutCollection,
		FDataflowTransformSelection InTransformSelection,
		const FBox& InBoundingBox,
		const FTransform& InTransform,
		int32 InNumPlanes,
		int32 InRandomSeed,
		float InChanceToFracture,
		bool InSplitIslands,
		float InGrout,
		float InAmplitude,
		float InFrequency,
		float InPersistence,
		float InLacunarity,
		int32 InOctaveNumber,
		float InPointSpacing,
		bool InAddSamplesForCollision,
		float InCollisionSampleSpacing);

	static void GenerateSliceTransforms(TArray<FTransform>& InOutCuttingPlaneTransforms,
		const FBox& InBoundingBox,
		int32 InSlicesX,
		int32 InSlicesY,
		int32 InSlicesZ,
		int32 InRandomSeed,
		float InSliceAngleVariation,
		float InSliceOffsetVariation);

	static int32 SliceCutter(FManagedArrayCollection& InOutCollection,
		FDataflowTransformSelection InTransformSelection,
		const FBox& InBoundingBox,
		int32 InSlicesX,
		int32 InSlicesY,
		int32 InSlicesZ,
		float InSliceAngleVariation,
		float InSliceOffsetVariation,
		int32 InRandomSeed,
		float InChanceToFracture,
		bool InSplitIslands,
		float InGrout,
		float InAmplitude,
		float InFrequency,
		float InPersistence,
		float InLacunarity,
		int32 InOctaveNumber,
		float InPointSpacing,
		bool InAddSamplesForCollision,
		float InCollisionSampleSpacing);

	static void AddBoxEdges(TArray<TTuple<FVector, FVector>>& InOutEdges, 
		const FVector& InMin, 
		const FVector& InMax);

	static void GenerateBrickTransforms(const FBox& InBounds,
		TArray<FTransform>& InOutBrickTransforms,
		const EFractureBrickBondEnum InBond,
		const float InBrickLength,
		const float InBrickHeight,
		const float InBrickDepth,
		TArray<TTuple<FVector, FVector>>& InOutEdges);

	static int32 BrickCutter(FManagedArrayCollection& InOutCollection,
		FDataflowTransformSelection InTransformSelection,
		const FBox& InBoundingBox, 
		const FTransform& InTransform,
		EFractureBrickBondEnum InBond,
		float InBrickLength,
		float InBrickHeight,
		float InBrickDepth,
		int32 InRandomSeed,
		float InChanceToFracture,
		bool InSplitIslands,
		float InGrout,
		float InAmplitude,
		float InFrequency,
		float InPersistence,
		float InLacunarity,
		int32 InOctaveNumber,
		float InPointSpacing,
		bool InAddSamplesForCollision,
		float InCollisionSampleSpacing);

	static void GenerateMeshTransforms(TArray<FTransform>& MeshTransforms,
		const FBox& InBoundingBox,
		const int32 InRandomSeed,
		const EMeshCutterCutDistribution InCutDistribution,
		const int32 InNumberToScatter,
		const int32 InGridX,
		const int32 InGridY,
		const int32 InGridZ,
		const float InVariability,
		const float InMinScaleFactor,
		const float InMaxScaleFactor,
		const bool InRandomOrientation,
		const float InRollRange,
		const float InPitchRange,
		const float InYawRange);

	static int32 MeshCutter(TArray<FTransform>& MeshTransforms,
		FManagedArrayCollection& InOutCollection,
		FDataflowTransformSelection InTransformSelection,
		const UE::Geometry::FDynamicMesh3& InDynCuttingMesh,
		const FTransform& InTransform,
		const int32 InRandomSeed,
		const float InChanceToFracture,
		const bool InSplitIslands,
		const float InCollisionSampleSpacing);
};

#if UE_ENABLE_INCLUDE_ORDER_DEPRECATED_IN_5_2
#include "Algo/Count.h"
#include "CoreMinimal.h"
#include "Dataflow/DataflowSelection.h"
#include "GeometryCollection/GeometryCollection.h"
#include "GeometryCollection/ManagedArrayCollection.h"
#endif
