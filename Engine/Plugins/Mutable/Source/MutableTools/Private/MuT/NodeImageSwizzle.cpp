// Copyright Epic Games, Inc. All Rights Reserved.


#include "MuT/NodeImageSwizzle.h"

#include "Misc/AssertionMacros.h"
#include "MuR/ImagePrivate.h"
#include "MuT/NodeImageSwizzlePrivate.h"
#include "MuT/NodePrivate.h"


namespace mu
{

	//---------------------------------------------------------------------------------------------
	// Static initialisation
	//---------------------------------------------------------------------------------------------
	FNodeType NodeImageSwizzle::Private::s_type = FNodeType(Node::EType::ImageSwizzle, NodeImage::GetStaticType() );


	//---------------------------------------------------------------------------------------------
	//!
	//---------------------------------------------------------------------------------------------

	MUTABLE_IMPLEMENT_NODE( NodeImageSwizzle )


	//---------------------------------------------------------------------------------------------
	// Own Interface
	//---------------------------------------------------------------------------------------------
	EImageFormat NodeImageSwizzle::GetFormat() const
	{
		return m_pD->m_format;
	}


	//---------------------------------------------------------------------------------------------
	void NodeImageSwizzle::SetFormat(EImageFormat format )
	{
		m_pD->m_format = format;

		int32 channelCount = GetImageFormatData( format ).Channels;
		m_pD->m_sources.SetNum( channelCount );
		m_pD->m_sourceChannels.SetNum( channelCount );
	}


	//---------------------------------------------------------------------------------------------
	NodeImagePtr NodeImageSwizzle::GetSource( int32 t ) const
	{
		if (m_pD->m_sources.IsValidIndex(t))
		{
			return m_pD->m_sources[t].get();
		}
		ensure(false);
		return nullptr;
	}


	//---------------------------------------------------------------------------------------------
	void NodeImageSwizzle::SetSource( int32 t, NodeImagePtr pNode )
	{
		if (m_pD->m_sources.IsValidIndex(t))
		{
			m_pD->m_sources[t] = pNode;
		}
		else
		{
			ensure(false);
		}
	}


	//---------------------------------------------------------------------------------------------
	int32 NodeImageSwizzle::GetSourceChannel( int32 t ) const
	{
		if (m_pD->m_sourceChannels.IsValidIndex(t))
		{
			return m_pD->m_sourceChannels[t];
		}
		else
		{
			ensure(false);
			return 0;
		}
	}


	//---------------------------------------------------------------------------------------------
	void NodeImageSwizzle::SetSourceChannel( int32 OutputChannel, int32 SourceChannel )
	{
		if (m_pD->m_sourceChannels.IsValidIndex(OutputChannel))
		{
			m_pD->m_sourceChannels[OutputChannel] = SourceChannel;
		}
		else
		{
			ensure(false);
		}
	}



}
