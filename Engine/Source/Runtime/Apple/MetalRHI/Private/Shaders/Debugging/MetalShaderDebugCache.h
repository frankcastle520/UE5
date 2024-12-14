// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	MetalShaderDebugCache.h: Metal RHI Shader Debug Cache.
=============================================================================*/

#pragma once

#include "HAL/Platform.h"

#if !UE_BUILD_SHIPPING

#include "MetalRHIPrivate.h"

struct FMetalShaderDebugCache
{
	static FMetalShaderDebugCache& Get()
	{
		static FMetalShaderDebugCache sSelf;
		return sSelf;
	}
	
	class FMetalShaderDebugZipFile* GetDebugFile(FString Path);
	NS::String* GetShaderCode(uint32 ShaderSrcLen, uint32 ShaderSrcCRC);
	
	FCriticalSection Mutex;
	TMap<FString, class FMetalShaderDebugZipFile*> DebugFiles;
};

#endif // !UE_BUILD_SHIPPING
