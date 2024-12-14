// Copyright Epic Games, Inc. All Rights Reserved.

#include "GDTF/Geometries/DMXGDTFLaserProtocol.h"

#include "Serialization/DMXGDTFNodeInitializer.h"
#include "Serialization/DMXGDTFXmlNodeBuilder.h"

namespace UE::DMX::GDTF
{
	FDMXGDTFLaserProtocol::FDMXGDTFLaserProtocol(const TSharedRef<FDMXGDTFLaserGeometry>& InLaserGeometry)
		: OuterLaserGeometry(InLaserGeometry)
	{}

	void FDMXGDTFLaserProtocol::Initialize(const FXmlNode& XmlNode)
	{
		FDMXGDTFNodeInitializer(SharedThis(this), XmlNode)
			.GetAttribute(TEXT("Name"), Name);
	}
	
	FXmlNode* FDMXGDTFLaserProtocol::CreateXmlNode(FXmlNode& Parent)
	{
		const FDMXGDTFXmlNodeBuilder ChildBuilder = FDMXGDTFXmlNodeBuilder(Parent, *this)
			.SetAttribute(TEXT("Name"), Name);

		return ChildBuilder.GetIntermediateXmlNode();
	}
}
