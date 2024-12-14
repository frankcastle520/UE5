// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MuR/Ptr.h"
#include "MuR/RefCounted.h"
#include "MuT/Node.h"
#include "MuT/NodeComponent.h"


namespace mu
{

	/** Select different component subgraphs based on active tags. */
    class MUTABLETOOLS_API NodeComponentVariation : public NodeComponent
    {
	public:

		Ptr<NodeComponent> DefaultComponent;

		struct FVariation
		{
			Ptr<NodeComponent> Component;
			FString Tag;
		};

		TArray<FVariation> Variations;

    public:

		// Node Interface
		virtual const FNodeType* GetType() const override { return &StaticType; }
		static const FNodeType* GetStaticType() { return &StaticType; }

		// NodeComponent interface
		virtual const class NodeComponentNew* GetParentComponentNew() const override { check(false); return nullptr; }

    protected:

        //! Forbidden. Manage with the Ptr<> template.
		~NodeComponentVariation() {}

	private:

		static FNodeType StaticType;
	
	};


} // namespace mu
