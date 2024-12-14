// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MuT/NodePrivate.h"
#include "MuT/NodeScalarVariation.h"

namespace mu
{

    //---------------------------------------------------------------------------------------------
    //!
    //---------------------------------------------------------------------------------------------
    class NodeScalarVariation::Private : public Node::Private
    {
    public:

        static FNodeType s_type;

        NodeScalarPtr m_defaultScalar;

        struct FVariation
        {
            NodeScalarPtr m_scalar;
            FString m_tag;
        };

        TArray<FVariation> m_variations;

    };

} // namespace mu
