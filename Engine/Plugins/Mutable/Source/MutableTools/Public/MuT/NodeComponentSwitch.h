// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MuR/Ptr.h"
#include "MuR/RefCounted.h"
#include "MuT/Node.h"
#include "MuT/NodeComponent.h"
#include "MuT/NodeScalar.h"


namespace mu
{

    /** This node selects an output component from a set of input components based on a parameter. 
	*/
    class MUTABLETOOLS_API NodeComponentSwitch : public NodeComponent
	{
	public:

		Ptr<NodeScalar> Parameter;
		TArray<Ptr<NodeComponent>> Options;

	public:

		/** Node type hierarchy data. */
		virtual const FNodeType* GetType() const { return &StaticType; }
		static const FNodeType* GetStaticType() { return &StaticType; }

		// NodeComponent interface
		virtual const class NodeComponentNew* GetParentComponentNew() const override { check(false); return nullptr; }

	protected:

		//! Forbidden. Manage with the Ptr<> template.
		~NodeComponentSwitch() {}

	private:

		static FNodeType StaticType;

	};


}
