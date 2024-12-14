// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	MetalPixelShader.cpp: Metal RHI Pixel Shader Class Implementation.
=============================================================================*/

#include "MetalPixelShader.h"

//------------------------------------------------------------------------------

#pragma mark - Metal RHI Pixel Shader Class


FMetalPixelShader::FMetalPixelShader(FMetalDevice& MetalDevice, TArrayView<const uint8> InCode)
	: TMetalBaseShader<FRHIPixelShader, SF_Pixel>(MetalDevice)
{
	FMetalCodeHeader Header;
	Init(InCode, Header, MTLLibraryPtr());
}

FMetalPixelShader::FMetalPixelShader(FMetalDevice& MetalDevice, TArrayView<const uint8> InCode, MTLLibraryPtr InLibrary)
	: TMetalBaseShader<FRHIPixelShader, SF_Pixel>(MetalDevice)
{
	FMetalCodeHeader Header;
	Init(InCode, Header, InLibrary);
}

MTLFunctionPtr FMetalPixelShader::GetFunction()
{
	return GetCompiledFunction();
}
