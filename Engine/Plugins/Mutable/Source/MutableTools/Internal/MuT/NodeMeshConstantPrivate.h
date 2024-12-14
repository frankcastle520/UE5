// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MuT/NodeMeshPrivate.h"

#include "MuT/NodeMeshConstant.h"


namespace mu
{
	class NodeLayout;

	class NodeMeshConstant::Private : public NodeMesh::Private
	{
	public:

		static FNodeType s_type;

		Ptr<Mesh> Value;

		TArray<Ptr<NodeLayout>> Layouts;

		struct FMorph
		{
			FString Name;
			Ptr<Mesh> MorphedMesh;
		};

		TArray<FMorph> Morphs;

	};

}
