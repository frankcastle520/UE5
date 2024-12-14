// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	MetalAmplificationShader.h: Metal RHI Amplification Shader Class Definition.
=============================================================================*/

#pragma once

#include "MetalRHIPrivate.h"
#include "Shaders/Types/Templates/MetalBaseShader.h"

//------------------------------------------------------------------------------

#pragma mark - Metal RHI Amplification Shader Class


#if PLATFORM_SUPPORTS_MESH_SHADERS
class FMetalAmplificationShader : public TMetalBaseShader<FRHIAmplificationShader, SF_Amplification>
{
public:
    FMetalAmplificationShader(FMetalDevice& Device, TArrayView<const uint8> InCode);
    FMetalAmplificationShader(FMetalDevice& Device, TArrayView<const uint8> InCode, MTLLibraryPtr InLibrary);

    MTLFunctionPtr GetFunction();
};
#endif
