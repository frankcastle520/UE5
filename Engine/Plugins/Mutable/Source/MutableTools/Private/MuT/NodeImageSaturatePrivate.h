// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MuT/NodeImagePrivate.h"
#include "MuT/NodeImageSaturate.h"
#include "MuT/NodeScalar.h"

namespace mu
{

	class NodeImageSaturate::Private : public NodeImage::Private
	{
	public:

		static FNodeType s_type;

		NodeImagePtr m_pSource;
		NodeScalarPtr m_pFactor;
	};


}
