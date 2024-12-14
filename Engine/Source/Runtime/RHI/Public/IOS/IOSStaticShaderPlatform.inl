// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Misc/AssertionMacros.h"

#if defined(UE_IOS_SHADER_PLATFORM_METAL)
	#define UE_IOS_STATIC_SHADER_PLATFORM SP_METAL
#elif defined(UE_IOS_SHADER_PLATFORM_METAL_SIM)
	#define UE_IOS_STATIC_SHADER_PLATFORM SP_METAL_SIM
#elif defined(UE_IOS_SHADER_PLATFORM_METAL_MRT)
	#define UE_IOS_STATIC_SHADER_PLATFORM SP_METAL_MRT
#else
	#error "Unknown IOS static shader platform"
#endif

struct FStaticShaderPlatform
{
	inline FStaticShaderPlatform(const EShaderPlatform InPlatform)
	{
		checkSlow(UE_IOS_STATIC_SHADER_PLATFORM == InPlatform);
	}

	inline operator EShaderPlatform() const
	{
		return UE_IOS_STATIC_SHADER_PLATFORM;
	}

	inline bool operator == (const EShaderPlatform Other) const
	{
		return Other == UE_IOS_STATIC_SHADER_PLATFORM;
	}
	
	inline bool operator != (const EShaderPlatform Other) const
	{
		return Other != UE_IOS_STATIC_SHADER_PLATFORM;
	}
};
