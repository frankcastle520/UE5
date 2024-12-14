// Copyright Epic Games, Inc. All Rights Reserved.


#pragma once

#include "MuT/NodeImageConditional.h"
#include "MuT/NodeImagePrivate.h"
#include "MuT/NodeBool.h"

namespace mu
{


    class NodeImageConditional::Private : public NodeImage::Private
	{
	public:

		static FNodeType s_type;

        Ptr<NodeBool> m_parameter;
        NodeImagePtr m_true;
        NodeImagePtr m_false;

	};


}

