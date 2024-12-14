// Copyright Epic Games, Inc. All Rights Reserved.


#include "MuT/NodeImageConstant.h"

#include "Misc/AssertionMacros.h"
#include "MuR/Image.h"
#include "MuR/Serialisation.h"
#include "MuT/NodePrivate.h"


namespace mu
{

	//---------------------------------------------------------------------------------------------
	// Static initialisation
	//---------------------------------------------------------------------------------------------
	FNodeType NodeImageConstant::StaticType = FNodeType(Node::EType::ImageConstant, NodeImage::GetStaticType());


	//---------------------------------------------------------------------------------------------
	void NodeImageConstant::SetValue(const Image* Value)
	{
		Proxy = new ResourceProxyMemory<Image>(Value);
	}


	//---------------------------------------------------------------------------------------------
	void NodeImageConstant::SetValue(Ptr<ResourceProxy<Image>> InProxy)
	{
		Proxy = InProxy;
	}

}
