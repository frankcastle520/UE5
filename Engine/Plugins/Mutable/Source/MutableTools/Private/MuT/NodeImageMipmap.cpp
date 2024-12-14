// Copyright Epic Games, Inc. All Rights Reserved.


#include "MuT/NodeImageMipmap.h"

#include "Misc/AssertionMacros.h"
#include "MuR/ImagePrivate.h"
#include "MuT/NodeImageMipmapPrivate.h"
#include "MuT/NodePrivate.h"


namespace mu
{

	//---------------------------------------------------------------------------------------------
	// Static initialisation
	//---------------------------------------------------------------------------------------------
    FNodeType NodeImageMipmap::Private::s_type = FNodeType(Node::EType::ImageMipmap, NodeImage::GetStaticType() );


	//---------------------------------------------------------------------------------------------
	//!
	//---------------------------------------------------------------------------------------------

    MUTABLE_IMPLEMENT_NODE( NodeImageMipmap )


	//---------------------------------------------------------------------------------------------
	// Own Interface
	//---------------------------------------------------------------------------------------------
    NodeImagePtr NodeImageMipmap::GetSource() const
	{
		return m_pD->m_pSource.get();
	}


	//---------------------------------------------------------------------------------------------
    void NodeImageMipmap::SetSource( NodeImagePtr pNode )
	{
		m_pD->m_pSource = pNode;
	}

	//---------------------------------------------------------------------------------------------
	void NodeImageMipmap::SetMipmapGenerationSettings(EMipmapFilterType FilterType, EAddressMode AddressMode)
	{
		m_pD->m_settings = FMipmapGenerationSettings{ FilterType, AddressMode };
	}

}
