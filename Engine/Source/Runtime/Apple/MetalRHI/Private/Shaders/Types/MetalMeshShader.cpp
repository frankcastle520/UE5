// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	MetalMeshShader.cpp: Metal RHI Mesh Shader Class Implementation.
=============================================================================*/

#include "MetalMeshShader.h"

//------------------------------------------------------------------------------

#pragma mark - Metal RHI Mesh Shader Class

#if PLATFORM_SUPPORTS_MESH_SHADERS
FMetalMeshShader::FMetalMeshShader(FMetalDevice& MetalDevice, TArrayView<const uint8> InCode)
	: TMetalBaseShader<FRHIMeshShader, SF_Mesh>(MetalDevice)
{
	FMetalCodeHeader Header;
	Init(InCode, Header, MTLLibraryPtr());
}

FMetalMeshShader::FMetalMeshShader(FMetalDevice& MetalDevice, TArrayView<const uint8> InCode, MTLLibraryPtr InLibrary)
	: TMetalBaseShader<FRHIMeshShader, SF_Mesh>(MetalDevice)
{
	FMetalCodeHeader Header;
	Init(InCode, Header, InLibrary);
}

MTLFunctionPtr FMetalMeshShader::GetFunction()
{
	return GetCompiledFunction();
}
#endif
