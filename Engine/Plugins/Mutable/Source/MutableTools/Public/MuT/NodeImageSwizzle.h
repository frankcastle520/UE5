// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MuR/Image.h"
#include "MuR/Ptr.h"
#include "MuR/RefCounted.h"
#include "MuT/Node.h"
#include "MuT/NodeImage.h"


namespace mu
{

	// Forward definitions
	class NodeImageSwizzle;
	typedef Ptr<NodeImageSwizzle> NodeImageSwizzlePtr;
	typedef Ptr<const NodeImageSwizzle> NodeImageSwizzlePtrConst;


	//! Node that composes a new image by gathering pixel data from channels in other images.
	//! \ingroup model
	class MUTABLETOOLS_API NodeImageSwizzle : public NodeImage
	{
	public:

		NodeImageSwizzle();

		//-----------------------------------------------------------------------------------------
		// Node Interface
		//-----------------------------------------------------------------------------------------

        const FNodeType* GetType() const override;
		static const FNodeType* GetStaticType();

		//-----------------------------------------------------------------------------------------
		// Own Interface
		//-----------------------------------------------------------------------------------------

		//!
		EImageFormat GetFormat() const;
		void SetFormat(EImageFormat);

		//!
		NodeImagePtr GetSource( int32 ) const;
		int GetSourceChannel( int32 ) const;

		void SetSource( int32, NodeImagePtr );
		void SetSourceChannel( int32 OutputChannel, int32 SourceChannel );

		//-----------------------------------------------------------------------------------------
		// Interface pattern
		//-----------------------------------------------------------------------------------------
		class Private;
		Private* GetPrivate() const;

	protected:

		//! Forbidden. Manage with the Ptr<> template.
		~NodeImageSwizzle();

	private:

		Private* m_pD;

	};


}
