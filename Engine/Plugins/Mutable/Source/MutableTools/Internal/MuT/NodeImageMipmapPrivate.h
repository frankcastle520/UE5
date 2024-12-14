// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MuT/NodeImagePrivate.h"
#include "MuT/NodeImageMipmap.h"
#include "MuT/NodeScalar.h"

#include "MuR/ImagePrivate.h"


namespace mu
{	
    class NodeImageMipmap::Private : public NodeImage::Private
	{
	public:

		static FNodeType s_type;

		NodeImagePtr m_pSource;
		NodeScalarPtr m_pFactor;

		FMipmapGenerationSettings m_settings;

	};


}
