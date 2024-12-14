// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MuR/Image.h"
#include "MuR/Ptr.h"
#include "MuR/RefCounted.h"
#include "MuT/Node.h"
#include "MuT/NodeImage.h"


namespace mu
{

	/** Node that outputs a constant image.
	* This node also supports "image references".
	*/
	class MUTABLETOOLS_API NodeImageConstant : public NodeImage
	{
	public:

		/** */
		Ptr<ResourceProxy<Image>> Proxy;
		
		/** */
		FSourceDataDescriptor SourceDataDescriptor;

	public:

		//-----------------------------------------------------------------------------------------
		// Node Interface
		//-----------------------------------------------------------------------------------------

		virtual const FNodeType* GetType() const override { return GetStaticType(); }
		static const FNodeType* GetStaticType() { return &StaticType; }

		//-----------------------------------------------------------------------------------------
		// Own Interface
		//-----------------------------------------------------------------------------------------

        //! Set the image to be output by this node
        void SetValue( const Image* );

        //! Set the image proxy that will provide the image for this node when necessary
        void SetValue( Ptr<ResourceProxy<Image>> pImage );

	protected:

		//! Forbidden. Manage with the Ptr<> template.
		~NodeImageConstant() {}

	private:

		static FNodeType StaticType;

	};

}
