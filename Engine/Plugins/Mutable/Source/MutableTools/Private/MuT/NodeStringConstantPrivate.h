// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MuT/NodeStringPrivate.h"

#include "MuT/NodeStringConstant.h"
#include "MuT/NodeImage.h"

namespace mu
{


	class NodeStringConstant::Private : public NodeString::Private
	{
	public:

		static FNodeType s_type;

		FString m_value;

	};


}
