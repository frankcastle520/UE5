// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MuT/NodeMeshGeometryOperation.h"
#include "MuT/NodeScalar.h"
#include "MuT/NodeMeshPrivate.h"

namespace mu
{


	class NodeMeshGeometryOperation::Private : public NodeMesh::Private
	{
	public:

		static FNodeType s_type;

		NodeMeshPtr m_pMeshA;
		NodeMeshPtr m_pMeshB;
		NodeScalarPtr m_pScalarA;
		NodeScalarPtr m_pScalarB;

	};

}
