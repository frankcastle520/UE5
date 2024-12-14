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
	class NodeImageNormalComposite;
	typedef Ptr<NodeImageNormalComposite> NodeImageNormalCompositePtr;
	typedef Ptr<const NodeImageNormalComposite> NodeImageNormalCompositePtrConst;

	//! Node that inverts the colors of an image, channel by channel
	class MUTABLETOOLS_API NodeImageNormalComposite : public NodeImage
	{
	public:

		NodeImageNormalComposite();

		//-----------------------------------------------------------------------------------------
		// Node Interface
		//-----------------------------------------------------------------------------------------

		const FNodeType* GetType() const override;
		static const FNodeType* GetStaticType();

		//-----------------------------------------------------------------------------------------
		// Own Interface
		//-----------------------------------------------------------------------------------------

		NodeImagePtr GetBase() const;
		void SetBase(NodeImagePtr);

        NodeImagePtr GetNormal() const;
        void SetNormal(NodeImagePtr);

		float GetPower() const;
		void SetPower(float); 

        ECompositeImageMode GetMode() const;
        void SetMode(ECompositeImageMode);

		//-----------------------------------------------------------------------------------------
		// Interface pattern
		//-----------------------------------------------------------------------------------------
		class Private;
		Private* GetPrivate() const;

	protected:

		//! Forbidden. Manage with the Ptr<> template
		~NodeImageNormalComposite();

	private:

		Private* m_pD;
	};

}
