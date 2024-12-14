// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GDTF/Geometries/DMXGDTFGeometry.h"

namespace UE::DMX::GDTF
{
	/** This type of geometry is used to describe device parts which have a shaper (XML node <FilterShaper>). */
	class DMXGDTF_API FDMXGDTFFilterShaperGeometry
		: public FDMXGDTFGeometry
	{
	public:
		FDMXGDTFFilterShaperGeometry(const TSharedRef<FDMXGDTFGeometryCollectBase>& InGeometryCollect)
			: FDMXGDTFGeometry(InGeometryCollect)
		{};

		//~ Begin FDMXGDTFNode interface
		virtual const TCHAR* GetXmlTag() const override { return TEXT("FilterShaper"); }
		//~ End FDMXGDTFNode interface
	};
}
