// Copyright Epic Games, Inc. All Rights Reserved.

#include "MuT/NodeImage.h"

#include "Misc/AssertionMacros.h"

namespace mu
{

	//---------------------------------------------------------------------------------------------
	// Static initialisation
	//---------------------------------------------------------------------------------------------
	static FNodeType s_nodeImageType = 	FNodeType( Node::EType::Image, Node::GetStaticType() );


	//---------------------------------------------------------------------------------------------
	const FNodeType* NodeImage::GetType() const
	{
		return GetStaticType();
	}


	//---------------------------------------------------------------------------------------------
	const FNodeType* NodeImage::GetStaticType()
	{
		return &s_nodeImageType;
	}


}


