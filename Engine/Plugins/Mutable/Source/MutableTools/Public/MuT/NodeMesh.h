// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "HAL/Platform.h"
#include "MuR/Ptr.h"
#include "MuR/RefCounted.h"
#include "MuT/Node.h"
#include "Containers/Array.h"
#include "Containers/Map.h"

namespace mu
{

	// Forward definitions
	class Mesh;
	class NodeMesh;
	typedef Ptr<NodeMesh> NodeMeshPtr;
	typedef Ptr<const NodeMesh> NodeMeshPtrConst;


    /** Base class of any node that outputs a mesh. */
	class MUTABLETOOLS_API NodeMesh : public Node
	{
	public:

		// Node Interface
        const FNodeType* GetType() const override;
		static const FNodeType* GetStaticType();

		// Interface pattern
		class Private;

	protected:

		// Forbidden. Manage with the Ptr<> template.
		inline ~NodeMesh() {}
	};


	/** Helper structs for mesh utility methods below. */
	struct FTriangleInfo
	{
		/** Vertex indices in the original mesh. */
		uint32 Indices[3];

		/** Vertex indices in the collapsed vertex list of the mesh. */
		uint32 CollapsedIndices[3];

		/** Optional data with layout block indices. */
		uint16 BlockIndices[3];

		/** Optional data with a flag indicating the UVs have changed during layout for this trioangle. */
		bool bUVsFixed = false;
	};


	/** Fill an array with the indices of all triangles belonging to the same UV island as InFirstTriangle. 
	*/
	extern void GetUVIsland(TArray<FTriangleInfo>& InTriangles,
		const uint32 InFirstTriangle,
		TArray<uint32>& OutTriangleIndices,
		const TArray<FVector2f>& InUVs,
		const TMultiMap<int32, uint32>& InVertexToTriangleMap);


	/** Create a map from vertices into vertices, collapsing vertices that have the same position.
	*/
	extern void MeshCreateCollapsedVertexMap(const mu::Mesh* Mesh, TArray<int32>& CollapsedVertices);

}

