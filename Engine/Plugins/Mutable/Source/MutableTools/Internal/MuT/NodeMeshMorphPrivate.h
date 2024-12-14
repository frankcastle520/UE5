// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MuT/NodeMeshMorph.h"
#include "MuT/NodeScalar.h"
#include "MuT/NodeMeshPrivate.h"
#include "MuR/Skeleton.h"

namespace mu
{


	class NodeMeshMorph::Private : public NodeMesh::Private
	{
	public:

		static FNodeType s_type;

		Ptr<NodeScalar> Factor;
		Ptr<NodeMesh> Base;
		Ptr<NodeMesh> Morph;
		
		bool bReshapeSkeleton = false;
		bool bReshapePhysicsVolumes = false;
		
		TArray<FBoneName> BonesToDeform;
		TArray<FBoneName> PhysicsToDeform;
	};


}
