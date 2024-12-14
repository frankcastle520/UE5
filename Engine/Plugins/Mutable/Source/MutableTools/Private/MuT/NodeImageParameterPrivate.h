// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MuT/NodeImageParameter.h"
#include "MuT/NodeImagePrivate.h"
#include "MuT/NodeRange.h"


namespace mu
{

    class NodeImageParameter::Private : public NodeImage::Private
	{
	public:

		static FNodeType s_type;

    	FName m_defaultValue;
		FString m_name;
		FString m_uid;

		TArray<Ptr<NodeRange>> m_ranges;
	};

}
