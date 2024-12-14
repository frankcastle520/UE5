// Copyright Epic Games, Inc. All Rights Reserved.

/*==============================================================================
	MetalDynamicRHIModule.h: Metal Dynamic RHI Module Class.
==============================================================================*/

#pragma once

#include "DynamicRHI.h"

//------------------------------------------------------------------------------

#pragma mark - Metal Dynamic RHI Module Class


class FMetalDynamicRHIModule : public IDynamicRHIModule
{
public:
	virtual bool IsSupported() override final;

	virtual FDynamicRHI* CreateRHI(ERHIFeatureLevel::Type RequestedFeatureLevel = ERHIFeatureLevel::Num) override final;
};
