// Copyright Epic Games, Inc. All Rights Reserved.


#include "MuT/NodeImageLuminance.h"

#include "Misc/AssertionMacros.h"
#include "MuT/NodeImageLuminancePrivate.h"
#include "MuT/NodePrivate.h"


namespace mu
{

	//---------------------------------------------------------------------------------------------
	// Static initialisation
	//---------------------------------------------------------------------------------------------
	FNodeType NodeImageLuminance::Private::s_type = FNodeType(Node::EType::ImageLuminance, NodeImage::GetStaticType() );


	//---------------------------------------------------------------------------------------------
	//!
	//---------------------------------------------------------------------------------------------

	MUTABLE_IMPLEMENT_NODE( NodeImageLuminance )


	//---------------------------------------------------------------------------------------------
	// Own Interface
	//---------------------------------------------------------------------------------------------
	NodeImagePtr NodeImageLuminance::GetSource() const
	{
		return m_pD->m_pSource.get();
	}


	//---------------------------------------------------------------------------------------------
	void NodeImageLuminance::SetSource( NodeImagePtr pNode )
	{
		m_pD->m_pSource = pNode;
	}



}
