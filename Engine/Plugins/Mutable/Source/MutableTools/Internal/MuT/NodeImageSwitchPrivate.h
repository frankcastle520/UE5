// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MuT/NodeImageSwitch.h"
#include "MuT/NodeImagePrivate.h"
#include "MuT/NodeScalar.h"


namespace mu
{


	class NodeImageSwitch::Private : public NodeImage::Private
	{
	public:

		static FNodeType s_type;

		NodeScalarPtr m_pParameter;
		TArray<NodeImagePtr> m_options;
	};


}
