// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MuT/NodeImagePrivate.h"
#include "MuT/NodeImageResize.h"


namespace mu
{


	class NodeImageResize::Private : public NodeImage::Private
	{
	public:

		static FNodeType s_type;

		NodeImagePtr m_pBase;
		bool m_relative = true;
		float m_sizeX = 0.5f, m_sizeY = 0.5f;

	};


}
