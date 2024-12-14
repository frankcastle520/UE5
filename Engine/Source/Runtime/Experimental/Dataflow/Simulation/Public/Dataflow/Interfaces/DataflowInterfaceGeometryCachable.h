// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UObject/ObjectMacros.h"
#include "UObject/Interface.h"
#include "Misc/Optional.h"
#include "Containers/ArrayView.h"
#include "Math/MathFwd.h"
#include "DataflowInterfaceGeometryCachable.generated.h"

class USkeletalMesh;
class USkinnedAsset;

UINTERFACE(meta = (CannotImplementInterfaceInBlueprint), MinimalAPI)
class UDataflowGeometryCachable : public UInterface
{
	GENERATED_BODY()
};

/** An asset that can produces a geometry cache */
class IDataflowGeometryCachable
{
	GENERATED_BODY()

public:
	/** Computes skeletal mesh positions for geometry cache */
	virtual TArray<FVector3f> GetGeometryCachePositions(const USkeletalMesh* SkeletalMesh) const = 0;

	/** Get mapping from final mesh vertex index to raw import vertex index */
	virtual TOptional<TArray<int32>> GetMeshImportVertexMap(const USkinnedAsset& SkinnedMeshAsset) const = 0;
};
