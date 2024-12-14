// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MuR/Ptr.h"
#include "MuR/RefCounted.h"
#include "MuT/Node.h"
#include "MuT/NodeColour.h"


namespace mu
{

	/** Select different color subgraphs based on active tags. */
    class MUTABLETOOLS_API NodeColourVariation : public NodeColour
    {
	public:

		Ptr<NodeColour> DefaultColour;

		struct FVariation
		{
			Ptr<NodeColour> Colour;
			FString Tag;
		};

		TArray<FVariation> Variations;

    public:

		// Node Interface
		virtual const FNodeType* GetType() const override { return &StaticType; }
		static const FNodeType* GetStaticType() { return &StaticType; }

    protected:

        //! Forbidden. Manage with the Ptr<> template.
		~NodeColourVariation() {}

	private:

		static FNodeType StaticType;
	
	};


} // namespace mu
