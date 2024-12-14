// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MuR/Ptr.h"
#include "MuR/RefCounted.h"
#include "MuT/Node.h"
#include "MuT/NodeImage.h"


namespace mu
{

	// Forward definitions
	class NodeScalar;
	typedef Ptr<NodeScalar> NodeScalarPtr;
	typedef Ptr<const NodeScalar> NodeScalarPtrConst;

	class NodeImageResize;
	typedef Ptr<NodeImageResize> NodeImageResizePtr;
	typedef Ptr<const NodeImageResize> NodeImageResizePtrConst;


	//! Node that multiplies the colors of an image, channel by channel.
	//! \ingroup model
	class MUTABLETOOLS_API NodeImageResize : public NodeImage
	{
	public:

		NodeImageResize();


		//-----------------------------------------------------------------------------------------
		// Node Interface
		//-----------------------------------------------------------------------------------------

        const FNodeType* GetType() const override;
		static const FNodeType* GetStaticType();

		//-----------------------------------------------------------------------------------------
		// Own Interface
		//-----------------------------------------------------------------------------------------

		//! Base image to resize.
		NodeImagePtr GetBase() const;
		void SetBase( NodeImagePtr );

		//! Is the size a relative factor or an absolute size?
		void SetRelative( bool );

		//! New size or relative factor
		void SetSize( float x, float y );

		//-----------------------------------------------------------------------------------------
		// Interface pattern
		//-----------------------------------------------------------------------------------------
		class Private;
		Private* GetPrivate() const;

	protected:

		//! Forbidden. Manage with the Ptr<> template.
		~NodeImageResize();

	private:

		Private* m_pD;

	};


}
