// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	MetalAmplificationShader.cpp: Metal RHI Amplification Shader Class Implementation.
=============================================================================*/


#include "MetalAmplificationShader.h"

//------------------------------------------------------------------------------

#pragma mark - Metal RHI Amplification Shader Class

#if PLATFORM_SUPPORTS_MESH_SHADERS
FMetalAmplificationShader::FMetalAmplificationShader(FMetalDevice& MetalDevice, TArrayView<const uint8> InCode) : TMetalBaseShader<FRHIAmplificationShader, SF_Amplification>(MetalDevice)
{
	FMetalCodeHeader Header;
	Init(InCode, Header, MTLLibraryPtr());
}

FMetalAmplificationShader::FMetalAmplificationShader(FMetalDevice& MetalDevice, TArrayView<const uint8> InCode, MTLLibraryPtr InLibrary) : TMetalBaseShader<FRHIAmplificationShader, SF_Amplification>(MetalDevice)
{
	FMetalCodeHeader Header;
	Init(InCode, Header, InLibrary);
}

MTLFunctionPtr FMetalAmplificationShader::GetFunction()
{
	return GetCompiledFunction();
}
#endif
