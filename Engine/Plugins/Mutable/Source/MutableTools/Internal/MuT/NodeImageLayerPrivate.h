// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MuT/NodeImagePrivate.h"
#include "MuT/NodeImageLayer.h"
#include "MuT/NodeColour.h"


namespace mu
{

	class NodeImageLayer::Private : public NodeImage::Private
	{
	public:

		static FNodeType s_type;

		NodeImagePtr m_pBase;
		NodeImagePtr m_pMask;
		NodeImagePtr m_pBlended;
		EBlendType m_type;
	};

}
