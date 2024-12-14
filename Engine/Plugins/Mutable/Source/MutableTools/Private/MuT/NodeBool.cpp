// Copyright Epic Games, Inc. All Rights Reserved.


#include "MuT/NodeBool.h"

#include "Misc/AssertionMacros.h"


namespace mu
{

	static FNodeType s_nodeBoolType = FNodeType(Node::EType::Bool, Node::GetStaticType() );


	//---------------------------------------------------------------------------------------------
	const FNodeType* NodeBool::GetType() const
	{
		return GetStaticType();
	}


	//---------------------------------------------------------------------------------------------
	const FNodeType* NodeBool::GetStaticType()
	{
		return &s_nodeBoolType;
	}


}


