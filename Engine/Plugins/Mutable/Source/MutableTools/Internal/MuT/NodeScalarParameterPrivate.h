// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MuT/NodeScalarPrivate.h"

#include "MuT/NodeScalarParameter.h"
#include "MuT/NodeImage.h"
#include "MuT/NodeRange.h"
#include "MuR/ParametersPrivate.h"


namespace mu
{


	class NodeScalarParameter::Private : public NodeScalar::Private
	{
	public:

		static FNodeType s_type;

		float m_defaultValue = 0.0f;
		FString m_name;
		FString m_uid;

		TArray<Ptr<NodeRange>> m_ranges;
	};

}
