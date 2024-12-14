// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MuR/ExtensionData.h"
#include "MuR/Ptr.h"
#include "MuR/RefCounted.h"
#include "MuT/NodeExtensionData.h"
#include "MuT/NodeScalar.h"

namespace mu
{

	class MUTABLETOOLS_API NodeExtensionDataSwitch : public NodeExtensionData
	{
	public:

		Ptr<NodeScalar> Parameter;
		TArray<Ptr<NodeExtensionData>> Options;

	public:

		//-----------------------------------------------------------------------------------------
		// Node Interface
		//-----------------------------------------------------------------------------------------
		virtual const FNodeType* GetType() const override { return GetStaticType(); }
		static const FNodeType* GetStaticType() { return &StaticType; }

		//-----------------------------------------------------------------------------------------
		// Own Interface
		//-----------------------------------------------------------------------------------------
		Ptr<NodeScalar> GetParameter() const;
		void SetParameter(Ptr<NodeScalar> InParameter);

		void SetOptionCount(int);

		Ptr<NodeExtensionData> GetOption(int32 t) const;
		void SetOption(int32 t, Ptr<NodeExtensionData>);


	protected:

		//! Forbidden. Manage with the Ptr<> template.
		~NodeExtensionDataSwitch() {}

	private:

		static FNodeType StaticType;

	};
}