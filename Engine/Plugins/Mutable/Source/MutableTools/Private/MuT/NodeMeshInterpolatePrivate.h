// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MuT/NodeMeshInterpolate.h"
#include "MuT/NodeScalar.h"
#include "MuT/NodeMeshPrivate.h"


namespace mu
{


	class NodeMeshInterpolate::Private : public NodeMesh::Private
	{
	public:

		static FNodeType s_type;

		NodeScalarPtr m_pFactor;
		TArray<NodeMeshPtr> m_targets;

		//!
		struct CHANNEL
		{
			CHANNEL()
			{
				semantic = MBS_NONE;
				semanticIndex = 0;
			}

			CHANNEL( EMeshBufferSemantic asemantic,
                     int32_t asemanticIndex )
			{
				semantic = asemantic;
				semanticIndex = asemanticIndex;
			}

			EMeshBufferSemantic semantic;
            int32_t semanticIndex;
		};

		TArray<CHANNEL> m_channels;

	};


}
