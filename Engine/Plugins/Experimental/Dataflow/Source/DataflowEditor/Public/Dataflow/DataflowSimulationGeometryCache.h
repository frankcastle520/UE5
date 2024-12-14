// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "Containers/ContainersFwd.h"
#include "GeometryCollection/ManagedArray.h"

class USkinnedAsset;
class UGeometryCache;

DECLARE_LOG_CATEGORY_EXTERN(LogDataflowSimulationGeometryCache, Log, All);

namespace UE::DataflowSimulationGeometryCache
{
	/** Saves positions into the geometry cache */
	DATAFLOWEDITOR_API void SaveGeometryCache(UGeometryCache& GeometryCache, const USkinnedAsset& Asset, TConstArrayView<uint32> ImportedVertexNumbers, TArrayView<TArray<FVector3f>> PositionsToMoveFrom);

	/** Saves the geometry cache */
	DATAFLOWEDITOR_API void SavePackage(UObject& Object);
};