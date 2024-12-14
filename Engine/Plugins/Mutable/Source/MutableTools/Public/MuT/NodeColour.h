// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "HAL/Platform.h"
#include "MuR/Ptr.h"
#include "MuR/RefCounted.h"
#include "MuT/Node.h"


namespace mu
{

    /** Base class of any node that outputs a colour.
	*/
    class MUTABLETOOLS_API NodeColour : public Node
    {
    public:

        // Node Interface
		virtual const FNodeType* GetType() const override { return &StaticType; }
		static const FNodeType* GetStaticType() { return &StaticType; }

    protected:

        //! Forbidden. Manage with the Ptr<> template.
        inline ~NodeColour() {}

	private:

		static FNodeType StaticType;

    };

}

