// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "HAL/Platform.h"
#include "MuR/Ptr.h"
#include "MuR/RefCounted.h"
#include "MuT/Node.h"


namespace mu
{

    // Forward definitions
    class NodeRange;
    typedef Ptr<NodeRange> NodeRangePtr;
    typedef Ptr<const NodeRange> NodeRangePtrConst;


    //! %Base class of any node that outputs a range.
    class MUTABLETOOLS_API NodeRange : public Node
	{
	public:

		//-----------------------------------------------------------------------------------------
		// Node Interface
		//-----------------------------------------------------------------------------------------

		const FNodeType* GetType() const override;
		static const FNodeType* GetStaticType();


		//-----------------------------------------------------------------------------------------
		// Interface pattern
		//-----------------------------------------------------------------------------------------
		class Private;

	protected:

		//! Forbidden. Manage with the Ptr<> template.
        inline ~NodeRange() {}

	};


}

