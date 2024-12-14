// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

class FDataDrivenShaderPlatformInfo final : public FGenericDataDrivenShaderPlatformInfo
{
public:
	IMPLEMENT_DDPSPI_SETTING(GetTargetsTiledGPU,				true);
	IMPLEMENT_DDPSPI_SETTING(GetSupportsUniformBufferObjects,	false);
	IMPLEMENT_DDPSPI_SETTING(GetSupportsManualVertexFetch,		false);

#if USE_STATIC_FEATURE_LEVEL_ENUMS
	IMPLEMENT_DDPSPI_SETTING_WITH_RETURN_TYPE(ERHIFeatureLevel::Type, GetMaxFeatureLevel, FStaticFeatureLevel(UE_IOS_STATIC_FEATURE_LEVEL));
	IMPLEMENT_DDPSPI_SETTING(GetIsMobile, (UE_IOS_STATIC_FEATURE_LEVEL == ERHIFeatureLevel::ES3_1));
#endif
};
