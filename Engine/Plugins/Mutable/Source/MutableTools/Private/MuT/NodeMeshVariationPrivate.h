// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MuT/NodePrivate.h"
#include "MuT/NodeMeshVariation.h"

namespace mu
{

    //---------------------------------------------------------------------------------------------
    //!
    //---------------------------------------------------------------------------------------------
    class NodeMeshVariation::Private : public Node::Private
    {
    public:
        Private() {}

        static FNodeType s_type;

        Ptr<NodeMesh> m_defaultMesh;

        struct FVariation
        {
			Ptr<NodeMesh> m_mesh;
            FString m_tag;
        };

        TArray<FVariation> m_variations;
    };

} // namespace mu
