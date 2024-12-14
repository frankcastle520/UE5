// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MuT/NodeMeshPrivate.h"
#include "MuT/NodeMeshFormat.h"
#include "MuR/Mesh.h"

namespace mu
{


	class NodeMeshFormat::Private : public NodeMesh::Private
	{
	public:

		static FNodeType s_type;

		//! Source mesh to transform
		Ptr<NodeMesh> Source;

		/** New mesh format.The buffers in the sets have no elements, but they define the formats. */
		FMeshBufferSet VertexBuffers;
		FMeshBufferSet IndexBuffers;
		
		/** */
		bool bOptimizeBuffers = false;
	};


}
