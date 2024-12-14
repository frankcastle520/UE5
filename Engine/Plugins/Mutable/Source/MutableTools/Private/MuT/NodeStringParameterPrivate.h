// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MuT/NodeStringPrivate.h"

#include "MuT/NodeStringParameter.h"
#include "MuT/NodeImage.h"
#include "MuT/NodeRange.h"


namespace mu
{

	class NodeStringParameter::Private : public NodeString::Private
	{
	public:

		static FNodeType s_type;

		FString m_defaultValue;
		FString m_name;
		FString m_uid;

        TArray<Ptr<NodeImage>> m_additionalImages;

		TArray<Ptr<NodeRange>> m_ranges;

	};

}
