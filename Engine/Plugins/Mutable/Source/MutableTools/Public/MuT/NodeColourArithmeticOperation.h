// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MuR/Ptr.h"
#include "MuR/RefCounted.h"
#include "MuT/NodeColour.h"


namespace mu
{

    /** Perform a per - component arithmetic operation between two colours. 
	*/
	class MUTABLETOOLS_API NodeColourArithmeticOperation : public NodeColour
	{
	public:

		/** Possible arithmetic operations. */
		enum class EOperation
		{
			Add,
			Subtract,
			Multiply,
			Divide
		};

		EOperation Operation;
		Ptr<NodeColour> A;
		Ptr<NodeColour> B;

	public:

		/** Node type hierarchy data. */
		virtual const FNodeType* GetType() const { return &StaticType; }
		static const FNodeType* GetStaticType() { return &StaticType; }

	protected:

		//! Forbidden. Manage with the Ptr<> template.
		~NodeColourArithmeticOperation() {}

	private:

		static FNodeType StaticType;

	};

	
}
