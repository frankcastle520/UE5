// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "BoneIndices.h"
#include "Engine/AssetUserData.h"
#include "Animation/AnimCurveTypes.h"

class IDNAReader;
class USkeleton;
class USkeletalMesh;

struct FDNAIndexMapping
{
	template <typename T>
	struct TArrayWrapper
	{
		TArray<T> Values;
	};
	
	struct FMeshPoseBoneControlAttributeMapping
	{
		FMeshPoseBoneIndex MeshPoseBoneIndex;
		int32 DNAJointIndex;
		int32 RotationX;
		int32 RotationY;
		int32 RotationZ;
		int32 RotationW;
	};

	using FCachedIndexedCurve = TBaseBlendedCurve<FDefaultAllocator, UE::Anim::FCurveElementIndexed>; 

	FGuid SkeletonGuid;

	// all the control attributes that we will need to extract, alongside their control index
	FCachedIndexedCurve ControlAttributeCurves;
	FCachedIndexedCurve NeuralNetworkMaskCurves;
	TArray<FMeshPoseBoneControlAttributeMapping> DriverJointsToControlAttributesMap;
	TArray<FMeshPoseBoneIndex> JointsMapDNAIndicesToMeshPoseBoneIndices;
	TArray<FCachedIndexedCurve> MorphTargetCurvesPerLOD;
	TArray<FCachedIndexedCurve> MaskMultiplierCurvesPerLOD;

	void MapControlCurves(const IDNAReader* DNAReader, const USkeleton* Skeleton);
	void MapNeuralNetworkMaskCurves(const IDNAReader* DNAReader, const USkeleton* Skeleton);
	void MapDriverJoints(const IDNAReader* DNAReader, const USkeletalMesh* SkeletalMesh);
	void MapJoints(const IDNAReader* DNAReader, const USkeletalMesh* SkeletalMesh);
	void MapMorphTargets(const IDNAReader* DNAReader, const USkeleton* Skeleton, const USkeletalMesh* SkeletalMesh);
	void MapMaskMultipliers(const IDNAReader* DNAReader, const USkeleton* Skeleton);

};

