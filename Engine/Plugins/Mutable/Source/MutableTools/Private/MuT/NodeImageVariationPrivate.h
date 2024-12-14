// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MuT/NodePrivate.h"
#include "MuT/NodeImageVariation.h"

namespace mu
{

    //---------------------------------------------------------------------------------------------
    //!
    //---------------------------------------------------------------------------------------------
    class NodeImageVariation::Private : public Node::Private
    {
    public:
        Private() {}

        static FNodeType s_type;

        NodeImagePtr m_defaultImage;

        struct FVariation
        {
            NodeImagePtr m_image;
			FString m_tag;
        };

		TArray<FVariation> m_variations;
    };

} // namespace mu

