// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MuT/NodeImagePrivate.h"
#include "MuT/NodeImageLayerPrivate.h"
#include "MuT/NodeImageLayerColour.h"
#include "MuT/NodeColour.h"
#include "MuR/Image.h"


namespace mu
{

	class NodeImageLayerColour::Private : public NodeImage::Private
	{
	public:

		static FNodeType s_type;

		NodeImagePtr m_pBase;
		NodeImagePtr m_pMask;
		NodeColourPtr m_pColour;
		EBlendType m_type;
	};

}
