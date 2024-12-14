// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MuT/NodeMeshApplyPose.h"
#include "MuT/NodeScalar.h"
#include "MuT/NodeMeshPrivate.h"

namespace mu
{


    class NodeMeshApplyPose::Private : public NodeMesh::Private
	{
	public:

		static FNodeType s_type;

        NodeMeshPtr m_pBase;
        NodeMeshPtr m_pPose;

	};


}

