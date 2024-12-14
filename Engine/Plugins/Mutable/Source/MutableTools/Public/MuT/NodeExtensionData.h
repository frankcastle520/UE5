// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MuR/Ptr.h"
#include "MuR/RefCounted.h"
#include "MuT/Node.h"
#include "MuT/NodeObject.h"


namespace mu
{


	//! Node that evaluates to an ExtensionData
	class MUTABLETOOLS_API NodeExtensionData : public Node
	{
	public:

		//-----------------------------------------------------------------------------------------
		// Node Interface
		//-----------------------------------------------------------------------------------------
		virtual const FNodeType* GetType() const override { return GetStaticType(); }
		static const FNodeType* GetStaticType() { return &StaticType; }

	protected:

		//! Forbidden. Manage with the Ptr<> template.
		inline ~NodeExtensionData() {}

	private:

		static FNodeType StaticType;

	};



}
