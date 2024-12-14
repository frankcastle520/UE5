// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MuR/Ptr.h"
#include "MuR/RefCounted.h"
#include "MuT/Node.h"
#include "MuT/NodeColour.h"
#include "MuT/NodeScalar.h"


namespace mu
{

	/** Obtain a colour by sampling an image at specific homogeneous coordinates.
	*/
	class MUTABLETOOLS_API NodeColourFromScalars : public NodeColour
	{
	public:

		Ptr<NodeScalar> X;
		Ptr<NodeScalar> Y;
		Ptr<NodeScalar> Z;
		Ptr<NodeScalar> W;

	public:

		// Node interface
		virtual const FNodeType* GetType() const { return &StaticType; }
		static const FNodeType* GetStaticType() { return &StaticType; }

	protected:

		//! Forbidden. Manage with the Ptr<> template.
		~NodeColourFromScalars() {}

	private:

		static FNodeType StaticType;

	};


}
