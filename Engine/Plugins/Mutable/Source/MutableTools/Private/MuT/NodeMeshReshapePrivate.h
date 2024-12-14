// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MuT/NodeMeshReshape.h"
#include "MuT/NodeScalar.h"
#include "MuT/NodeMeshPrivate.h"
#include "MuR/Skeleton.h"

namespace mu
{


	class NodeMeshReshape::Private : public NodeMesh::Private
	{
	public:

		static FNodeType s_type;

		Ptr<NodeMesh> BaseMesh;
		Ptr<NodeMesh> BaseShape;
		Ptr<NodeMesh> TargetShape;
		bool bReshapeVertices = true;
		bool bRecomputeNormals = false;
		bool bApplyLaplacian = false;
		bool bReshapeSkeleton = false;
		bool bReshapePhysicsVolumes = false;

		EVertexColorUsage ColorRChannelUsage = EVertexColorUsage::None;
		EVertexColorUsage ColorGChannelUsage = EVertexColorUsage::None;
		EVertexColorUsage ColorBChannelUsage = EVertexColorUsage::None;
		EVertexColorUsage ColorAChannelUsage = EVertexColorUsage::None;

		TArray<FBoneName> BonesToDeform;
		TArray<FBoneName> PhysicsToDeform;

	};

}
