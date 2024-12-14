// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MuR/Ptr.h"
#include "MuR/RefCounted.h"
#include "MuT/Node.h"
#include "MuT/NodeColour.h"
#include "MuT/NodeScalar.h"


namespace mu
{

    /** This node selects an output Colour from a set of input Colours based on a parameter. 
	*/
    class MUTABLETOOLS_API NodeColourSwitch : public NodeColour
	{
	public:

		Ptr<NodeScalar> Parameter;
		TArray<Ptr<NodeColour>> Options;

	public:

		/** Node type hierarchy data. */
		virtual const FNodeType* GetType() const { return &StaticType; }
		static const FNodeType* GetStaticType() { return &StaticType; }

	protected:

		//! Forbidden. Manage with the Ptr<> template.
		~NodeColourSwitch() {}

	private:

		static FNodeType StaticType;

	};


}
