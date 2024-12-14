// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MuT/NodeMeshPrivate.h"
#include "MuT/NodeMeshMakeMorph.h"


namespace mu
{

    class NodeMeshMakeMorph::Private : public NodeMesh::Private
	{
	public:

		static FNodeType s_type;

		NodeMeshPtr m_pBase;
		NodeMeshPtr m_pTarget;
		
		bool bOnlyPositionAndNormal = false;
	};


}
