// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MuT/NodeImageFormat.h"
#include "MuT/NodeImagePrivate.h"
#include "MuR/ImagePrivate.h"


namespace mu
{


	class NodeImageFormat::Private : public NodeImage::Private
	{
	public:

		static FNodeType s_type;

		EImageFormat m_format = EImageFormat::IF_NONE;
		EImageFormat m_formatIfAlpha = EImageFormat::IF_NONE;
        NodeImagePtr m_source;

	};


}
