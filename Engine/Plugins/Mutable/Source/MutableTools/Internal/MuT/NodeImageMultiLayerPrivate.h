// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MuT/NodeImagePrivate.h"
#include "MuT/NodeImageMultiLayer.h"
#include "MuT/NodeColour.h"


namespace mu
{

    class NodeImageMultiLayer::Private : public NodeImage::Private
	{
	public:

		static FNodeType s_type;

		NodeImagePtr m_pBase;
		NodeImagePtr m_pMask;
        NodeImagePtr m_pBlended;
        NodeRangePtr m_pRange;
		EBlendType m_type;

	};

}
