// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MuR/ExtensionData.h"
#include "MuR/Ptr.h"
#include "MuR/RefCounted.h"
#include "MuT/NodeExtensionData.h"


namespace mu
{

	//! Node that outputs a constant ExtensionData
	//! \ingroup model
	class MUTABLETOOLS_API NodeExtensionDataConstant : public NodeExtensionData
	{
	public:

		Ptr<const ExtensionData> Value;

	public:

		//-----------------------------------------------------------------------------------------
		// Node Interface
		//-----------------------------------------------------------------------------------------
		virtual const FNodeType* GetType() const override { return GetStaticType(); }
		static const FNodeType* GetStaticType() { return &StaticType; }

		//-----------------------------------------------------------------------------------------
		// Own Interface
		//-----------------------------------------------------------------------------------------

		/** Deprecated. Access Value attribute directly. */
		void SetValue(const Ptr<const ExtensionData>& In) { Value = In; }

	protected:

		//! Forbidden. Manage with the Ptr<> template.
		~NodeExtensionDataConstant() {}

	private:

		static FNodeType StaticType;

	};


}
