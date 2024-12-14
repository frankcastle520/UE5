// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MuR/ExtensionData.h"
#include "MuR/Ptr.h"
#include "MuR/RefCounted.h"
#include "MuT/NodeExtensionData.h"

namespace mu
{

	class MUTABLETOOLS_API NodeExtensionDataVariation : public NodeExtensionData
	{
	public:

		Ptr<NodeExtensionData> DefaultValue;

		struct FVariation
		{
			Ptr<NodeExtensionData> Value;
			FString Tag;
		};

		TArray<FVariation> Variations;

	public:

		//-----------------------------------------------------------------------------------------
		// Node Interface
		//-----------------------------------------------------------------------------------------
		virtual const FNodeType* GetType() const override { return GetStaticType(); }
		static const FNodeType* GetStaticType() { return &StaticType; }

		//-----------------------------------------------------------------------------------------
		// Own Interface
		//-----------------------------------------------------------------------------------------
		void SetDefaultValue(Ptr<NodeExtensionData> InValue);

		void SetVariationCount(int32 InCount);
		int GetVariationCount() const;

		void SetVariationTag(int32 InIndex, const FString& InTag);

		void SetVariationValue(int32 InIndex, Ptr<NodeExtensionData> InValue);


	protected:

		//! Forbidden. Manage with the Ptr<> template.
		~NodeExtensionDataVariation() {}

	private:

		static FNodeType StaticType;

	};
}