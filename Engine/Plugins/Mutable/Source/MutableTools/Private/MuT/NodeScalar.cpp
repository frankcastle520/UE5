// Copyright Epic Games, Inc. All Rights Reserved.


#include "MuT/NodeScalar.h"

#include "Misc/AssertionMacros.h"


namespace mu
{

	static FNodeType s_nodeScalarType = FNodeType(Node::EType::Scalar, Node::GetStaticType() );


	//---------------------------------------------------------------------------------------------
	const FNodeType* NodeScalar::GetType() const
	{
		return GetStaticType();
	}


	//---------------------------------------------------------------------------------------------
	const FNodeType* NodeScalar::GetStaticType()
	{
		return &s_nodeScalarType;
	}


}


