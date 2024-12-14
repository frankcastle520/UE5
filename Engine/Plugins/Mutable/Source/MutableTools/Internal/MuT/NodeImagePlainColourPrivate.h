// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MuT/NodeColour.h"
#include "MuT/NodeImagePrivate.h"
#include "MuT/NodeImagePlainColour.h"


namespace mu
{


	class NodeImagePlainColour::Private : public NodeImage::Private
	{
	public:

		Private()
		{
            m_sizeX = 4;
            m_sizeY = 4;
		}

		static FNodeType s_type;

		Ptr<NodeColour> m_pColour;
		int32 m_sizeX, m_sizeY;
		EImageFormat Format = EImageFormat::IF_RGBA_UBYTE;

	};


}
