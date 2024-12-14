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
	class NodeImageFormat;
	typedef Ptr<NodeImageFormat> NodeImageFormatPtr;
	typedef Ptr<const NodeImageFormat> NodeImageFormatPtrConst;


	//! Node that composes a new image by gathering pixel data from channels in other images.
	//! \ingroup model
	class MUTABLETOOLS_API NodeImageFormat : public NodeImage
	{
	public:

		NodeImageFormat();


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
        void SetFormat(EImageFormat format, EImageFormat formatIfAlpha = EImageFormat::IF_NONE );

		//!
		NodeImagePtr GetSource() const;

		void SetSource( NodeImagePtr );


		//-----------------------------------------------------------------------------------------
		// Interface pattern
		//-----------------------------------------------------------------------------------------
		class Private;
		Private* GetPrivate() const;

	protected:

		//! Forbidden. Manage with the Ptr<> template.
		~NodeImageFormat();

	private:

		Private* m_pD;

	};


}

