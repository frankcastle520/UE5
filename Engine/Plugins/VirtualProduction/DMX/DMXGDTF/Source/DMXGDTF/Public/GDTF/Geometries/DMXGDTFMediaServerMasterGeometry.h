// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GDTF/Geometries/DMXGDTFGeometry.h"

namespace UE::DMX::GDTF
{
	/** This type of geometry is used to describe the master control of one or several media devices (XML node <MediaServerMaster>). */
	class DMXGDTF_API FDMXGDTFMediaServerMasterGeometry
		: public FDMXGDTFGeometry
	{
	public:
		FDMXGDTFMediaServerMasterGeometry(const TSharedRef<FDMXGDTFGeometryCollectBase>& InGeometryCollect)
			: FDMXGDTFGeometry(InGeometryCollect)
		{};

		//~ Begin FDMXGDTFNode interface
		virtual const TCHAR* GetXmlTag() const override { return TEXT("MediaServerMaster"); }
		//~ End FDMXGDTFNode interface
	};
}