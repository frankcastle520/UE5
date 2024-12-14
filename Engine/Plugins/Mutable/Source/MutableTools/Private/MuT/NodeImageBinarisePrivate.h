// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MuT/NodeImagePrivate.h"
#include "MuT/NodeImageBinarise.h"
#include "MuT/NodeScalar.h"


namespace mu
{


	class NodeImageBinarise::Private : public NodeImage::Private
	{
	public:

		static FNodeType s_type;

		NodeImagePtr m_pBase;
		NodeScalarPtr m_pThreshold;
	};


}
