// Copyright Epic Games, Inc. All Rights Reserved.


#include "MuT/NodeString.h"

#include "Misc/AssertionMacros.h"


namespace mu
{

	//---------------------------------------------------------------------------------------------
	// Static initialisation
	//---------------------------------------------------------------------------------------------
	static FNodeType s_nodeStringType = FNodeType(Node::EType::String, Node::GetStaticType() );


	//---------------------------------------------------------------------------------------------
	const FNodeType* NodeString::GetType() const
	{
		return GetStaticType();
	}


	//---------------------------------------------------------------------------------------------
	const FNodeType* NodeString::GetStaticType()
	{
		return &s_nodeStringType;
	}


}


