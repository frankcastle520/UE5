// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MuT/NodeScalarPrivate.h"

#include "MuT/NodeScalarEnumParameter.h"
#include "MuT/NodeImage.h"
#include "MuR/ParametersPrivate.h"


namespace mu
{


	class NodeScalarEnumParameter::Private : public NodeScalar::Private
	{
	public:

		static FNodeType s_type;

		FString m_name;
		FString m_uid;
		int32 m_defaultValue = 0;

		struct FOption
		{
			FString name;
			float value;
		};

		TArray<FOption> m_options;

		TArray<Ptr<NodeRange>> m_ranges;
	};

}

