// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MuT/NodeRangePrivate.h"
#include "MuT/NodeRangeFromScalar.h"
#include "MuT/NodeRange.h"
#include "MuT/NodeScalar.h"


namespace mu
{


    class NodeRangeFromScalar::Private : public NodeRange::Private
	{
	public:

		static FNodeType s_type;

        NodeScalarPtr m_pSize;
        FString m_name;
	};


}
