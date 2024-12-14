// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Math/MathFwd.h"
#include "GeometryCollection/ManagedArray.h"

class USkeletalMesh;
class FFleshCollection;
class UStaticMesh;

namespace ChaosFlesh 
{
	CHAOSFLESHENGINE_API FString GetMeshId(const USkeletalMesh* SkeletalMesh, const bool bUseImportModel);

	CHAOSFLESHENGINE_API FString GetMeshId(const UStaticMesh* StaticMesh);

	CHAOSFLESHENGINE_API void BoundSurfacePositions(
		const USkeletalMesh* SkeletalMesh,
		const FFleshCollection* FleshCollection,
		const TManagedArray<FVector3f>* RestVertices,
		const TManagedArray<FVector3f>* SimulatedVertices,
		TArray<FVector3f>& Positions);
}
