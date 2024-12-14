// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	MetalCompiledShaderCache.cpp: Metal RHI Compiled Shader Cache.
=============================================================================*/

#include "MetalCompiledShaderCache.h"
#include "MetalCompiledShaderKey.h"
#include "MetalCompiledShaderCache.h"

FMetalCompiledShaderCache& GetMetalCompiledShaderCache()
{
	static FMetalCompiledShaderCache CompiledShaderCache;
	return CompiledShaderCache;
}
