// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MuT/NodeScalarPrivate.h"

#include "MuT/NodeScalarConstant.h"
#include "MuT/NodeImage.h"

namespace mu
{

	class NodeScalarConstant::Private : public NodeScalar::Private
	{
	public:

		static FNodeType s_type;

		float m_value;
	};


}
