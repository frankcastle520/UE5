// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	MetalMeshShader.h: Metal RHI Mesh Shader Class Definition.
=============================================================================*/

#pragma once

#include "MetalRHIPrivate.h"
#include "Shaders/Types/Templates/MetalBaseShader.h"


//------------------------------------------------------------------------------

#pragma mark - Metal RHI Mesh Shader Class


#if PLATFORM_SUPPORTS_MESH_SHADERS
class FMetalMeshShader : public TMetalBaseShader<FRHIMeshShader, SF_Mesh>
{
public:
    FMetalMeshShader(FMetalDevice& Device, TArrayView<const uint8> InCode);
    FMetalMeshShader(FMetalDevice& Device, TArrayView<const uint8> InCode, MTLLibraryPtr InLibrary);

    MTLFunctionPtr GetFunction();
};
#endif
