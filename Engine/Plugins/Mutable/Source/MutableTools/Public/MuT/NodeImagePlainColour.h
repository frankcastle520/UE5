// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MuR/Ptr.h"
#include "MuR/RefCounted.h"
#include "MuT/Node.h"
#include "MuT/NodeImage.h"


namespace mu
{

	// Forward definitions
	class NodeColour;
	typedef Ptr<NodeColour> NodeColourPtr;
	typedef Ptr<const NodeColour> NodeColourPtrConst;

	class NodeImagePlainColour;
	typedef Ptr<NodeImagePlainColour> NodeImagePlainColourPtr;
	typedef Ptr<const NodeImagePlainColour> NodeImagePlainColourPtrConst;


	//! Node that multiplies the colors of an image, channel by channel.
	//! \ingroup model
	class MUTABLETOOLS_API NodeImagePlainColour : public NodeImage
	{
	public:

		NodeImagePlainColour();


		//-----------------------------------------------------------------------------------------
		// Node Interface
		//-----------------------------------------------------------------------------------------

        const FNodeType* GetType() const override;
		static const FNodeType* GetStaticType();

		//-----------------------------------------------------------------------------------------
		// Own Interface
		//-----------------------------------------------------------------------------------------

		//! Colour of the image.
		NodeColourPtr GetColour() const;
		void SetColour( NodeColourPtr );

		//! New size or relative factor
		void SetSize( int x, int y );

		//-----------------------------------------------------------------------------------------
		// Interface pattern
		//-----------------------------------------------------------------------------------------
		class Private;
		Private* GetPrivate() const;

	protected:

		//! Forbidden. Manage with the Ptr<> template.
		~NodeImagePlainColour();

	private:

		Private* m_pD;

	};


}
