// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GDTF/Geometries/DMXGDTFGeometry.h"

namespace UE::DMX::GDTF
{
	class FDMXGDTFGeometryCollectBase;

	/** This type of geometry defines device parts with a rotation axis (XML node <Axis>).  */
	class DMXGDTF_API FDMXGDTFAxisGeometry
		: public FDMXGDTFGeometry
	{
	public:
		FDMXGDTFAxisGeometry(const TSharedRef<FDMXGDTFGeometryCollectBase>& InGeometryCollect)
			: FDMXGDTFGeometry(InGeometryCollect)
		{};

		//~ Begin FDMXGDTFNode interface
		virtual const TCHAR* GetXmlTag() const override { return TEXT("Axis"); }
		//~ End FDMXGDTFNode interface
	};
}
