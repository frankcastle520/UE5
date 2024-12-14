// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MuT/NodeScalarPrivate.h"
#include "MuT/NodeScalarArithmeticOperation.h"

namespace mu
{
    MUTABLE_DEFINE_ENUM_SERIALISABLE(NodeScalarArithmeticOperation::OPERATION)

    class NodeScalarArithmeticOperation::Private : public NodeScalar::Private
	{
	public:

		static FNodeType s_type;

        NodeScalarArithmeticOperation::OPERATION m_operation;
        NodeScalarPtr m_pA;
        NodeScalarPtr m_pB;

	};


}
