// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "HAL/Platform.h"
#include "MuR/Ptr.h"
#include "MuR/RefCounted.h"
#include "MuT/Node.h"
#include "MuT/NodeLOD.h"


namespace mu
{

	/** */
	class MUTABLETOOLS_API NodeComponent : public Node
	{
	public:

		TArray<Ptr<NodeLOD>> LODs;

	public:

        // Node interface
		virtual const FNodeType* GetType() const override { return &StaticType; }
		static const FNodeType* GetStaticType() { return &StaticType; }

        //-----------------------------------------------------------------------------------------
        // Own interface
        //-----------------------------------------------------------------------------------------

		virtual const class NodeComponentNew* GetParentComponentNew() const = 0;

	protected:

		//! Forbidden. Manage with the Ptr<> template.
		inline ~NodeComponent() {}

	private:

		static FNodeType StaticType;

	};


}
