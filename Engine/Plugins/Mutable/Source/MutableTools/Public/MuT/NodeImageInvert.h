// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MuR/Ptr.h"
#include "MuR/RefCounted.h"
#include "MuT/Node.h"
#include "MuT/NodeImage.h"


namespace mu
{

	// Forward definitions
	class NodeImageInvert;
	typedef Ptr<NodeImageInvert> NodeImageInvertPtr;
	typedef Ptr<const NodeImageInvert> NodeImageInvertPtrConst;

	//! Node that inverts the colors of an image, channel by channel
	class MUTABLETOOLS_API NodeImageInvert : public NodeImage
	{
	public:

		NodeImageInvert();


		//-----------------------------------------------------------------------------------------
		// Node Interface
		//-----------------------------------------------------------------------------------------

		const FNodeType* GetType() const override;
		static const FNodeType* GetStaticType();

		//-----------------------------------------------------------------------------------------
		// Own Interface
		//-----------------------------------------------------------------------------------------

		//! Base image to invert.
		NodeImagePtr GetBase() const;
		void SetBase(NodeImagePtr);

		//-----------------------------------------------------------------------------------------
		// Interface pattern
		//-----------------------------------------------------------------------------------------
		class Private;
		Private* GetPrivate() const;

	protected:

		//! Forbidden. Manage with the Ptr<> template
		~NodeImageInvert();

	private:

		Private* m_pD;
	};
}
