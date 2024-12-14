// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MuT/NodeImageSwizzle.h"
#include "MuT/NodeImagePrivate.h"
#include "MuR/ImagePrivate.h"


namespace mu
{


	class NodeImageSwizzle::Private : public NodeImage::Private
	{
	public:

		static FNodeType s_type;

		EImageFormat m_format;
		TArray<NodeImagePtr> m_sources;
		TArray<int> m_sourceChannels;
	};


}
