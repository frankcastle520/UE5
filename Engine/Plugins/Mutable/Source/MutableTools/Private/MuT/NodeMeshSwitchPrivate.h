// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MuT/NodeMeshSwitch.h"
#include "MuT/NodeScalar.h"
#include "MuT/NodeMeshPrivate.h"


namespace mu
{


	class NodeMeshSwitch::Private : public NodeMesh::Private
	{
	public:

		static FNodeType s_type;

		NodeScalarPtr m_pParameter;
		TArray<NodeMeshPtr> m_options;

	};


}
