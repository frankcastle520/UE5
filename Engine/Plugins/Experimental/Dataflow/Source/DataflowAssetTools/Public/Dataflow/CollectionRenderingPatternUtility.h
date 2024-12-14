// Copyright Epic Games, Inc. All Rights Reserved. 
#pragma once

#include "CoreMinimal.h"
#include "Dataflow/DataflowObjectInterface.h"


namespace UE::Geometry { class FDynamicMesh3; }
namespace GeometryCollection::Facades { class FRenderingFacade; }
class UDataflow;
class UObject;

namespace UE::Dataflow
{
	namespace Conversion
	{
		// Convert a rendering facade to a dynamic mesh. if InMeshIndex==INDEX_NONE then all Geometry is converted. 
		void DATAFLOWASSETTOOLS_API RenderingFacadeToDynamicMesh(const GeometryCollection::Facades::FRenderingFacade& Facade, int32 InMeshIndex, UE::Geometry::FDynamicMesh3& DynamicMesh);

		// Convert a dynamic mesh to a rendering facade
		void DATAFLOWASSETTOOLS_API DynamicMeshToRenderingFacade(const UE::Geometry::FDynamicMesh3& DynamicMesh, GeometryCollection::Facades::FRenderingFacade& Facade);
	}

}	// namespace UE::Dataflow