// Copyright Epic Games, Inc. All Rights Reserved.


#include "MuT/NodeRange.h"

#include "Misc/AssertionMacros.h"

namespace mu
{

	//---------------------------------------------------------------------------------------------
	// Static initialisation
	//---------------------------------------------------------------------------------------------
    static FNodeType s_nodeRangeType = 	FNodeType(Node::EType::Range, Node::GetStaticType() );


	//---------------------------------------------------------------------------------------------
    const FNodeType* NodeRange::GetType() const
	{
		return GetStaticType();
	}


	//---------------------------------------------------------------------------------------------
    const FNodeType* NodeRange::GetStaticType()
	{
        return &s_nodeRangeType;
	}


}


