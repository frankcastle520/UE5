// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MuT/NodeImageInterpolate.h"
#include "MuT/NodeImagePrivate.h"
#include "MuT/NodeScalar.h"

namespace mu
{


	class NodeImageInterpolate::Private : public NodeImage::Private
	{
	public:

		static FNodeType s_type;

		NodeScalarPtr m_pFactor;
		TArray<NodeImagePtr> m_targets;
	};


}
