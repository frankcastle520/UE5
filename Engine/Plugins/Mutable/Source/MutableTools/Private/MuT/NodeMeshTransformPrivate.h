// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MuT/NodeMeshPrivate.h"
#include "MuT/NodeMeshTransform.h"
#include "MuR/MutableMath.h"


namespace mu
{


    class NodeMeshTransform::Private : public NodeMesh::Private
	{
	public:

		static FNodeType s_type;

		NodeMeshPtr Source;
		FMatrix44f Transform;

	};


}
