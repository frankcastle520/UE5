// Copyright Epic Games, Inc. All Rights Reserved.

#include "GDTF/FTPresets/DMXGDTFFTPreset.h"

#include "GDTF/DMXGDTFFixtureType.h"
#include "Serialization/DMXGDTFNodeInitializer.h"
#include "Serialization/DMXGDTFXmlNodeBuilder.h"

namespace UE::DMX::GDTF
{
	FDMXGDTFFTPreset::FDMXGDTFFTPreset(const TSharedRef<FDMXGDTFFixtureType>& InFixtureType)
		: OuterFixtureType(InFixtureType)
	{}

	void FDMXGDTFFTPreset::Initialize(const FXmlNode& XmlNode)
	{
		FDMXGDTFNodeInitializer(SharedThis(this), XmlNode);
	}

	FXmlNode* FDMXGDTFFTPreset::CreateXmlNode(FXmlNode& Parent)
	{
		const FDMXGDTFXmlNodeBuilder ChildBuilder = FDMXGDTFXmlNodeBuilder(Parent, *this);

		return ChildBuilder.GetIntermediateXmlNode();
	}
}
