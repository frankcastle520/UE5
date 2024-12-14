// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	MetalShaderResources.h: Metal shader resource RHI definitions.
=============================================================================*/

#pragma once

#include "CrossCompilerCommon.h"

/**
* Shader related constants.
*/
enum
{
	METAL_MAX_UNIFORM_BUFFER_BINDINGS = 12,	// @todo-mobile: Remove me
	METAL_FIRST_UNIFORM_BUFFER = 0,			// @todo-mobile: Remove me
	METAL_MAX_COMPUTE_STAGE_UAV_UNITS = 8,	// @todo-mobile: Remove me
	METAL_UAV_NOT_SUPPORTED_FOR_GRAPHICS_UNIT = -1, // for now, only CS supports UAVs/ images
	METAL_MAX_BUFFERS = 31,
};

/**
* Buffer data-types for MetalRHI & MetalSL
*/
enum class EMetalBufferFormat : uint8
{
	Unknown					=0,
	
	R8Sint					=1,
	R8Uint					=2,
	R8Snorm					=3,
	R8Unorm					=4,
	
	R16Sint					=5,
	R16Uint					=6,
	R16Snorm				=7,
	R16Unorm				=8,
	R16Half					=9,
	
	R32Sint					=10,
	R32Uint					=11,
	R32Float				=12,
	
	RG8Sint					=13,
	RG8Uint					=14,
	RG8Snorm				=15,
	RG8Unorm				=16,
	
	RG16Sint				=17,
	RG16Uint				=18,
	RG16Snorm				=19,
	RG16Unorm				=20,
	RG16Half				=21,
	
	RG32Sint				=22,
	RG32Uint				=23,
	RG32Float				=24,
	
	RGB8Sint				=25,
	RGB8Uint				=26,
	RGB8Snorm				=27,
	RGB8Unorm				=28,
	
	RGB16Sint				=29,
	RGB16Uint				=30,
	RGB16Snorm				=31,
	RGB16Unorm				=32,
	RGB16Half				=33,
	
	RGB32Sint				=34,
	RGB32Uint				=35,
	RGB32Float				=36,
	
	RGBA8Sint				=37,
	RGBA8Uint				=38,
	RGBA8Snorm				=39,
	RGBA8Unorm				=40,
	
	BGRA8Unorm				=41,
	
	RGBA16Sint				=42,
	RGBA16Uint				=43,
	RGBA16Snorm				=44,
	RGBA16Unorm				=45,
	RGBA16Half				=46,
	
	RGBA32Sint				=47,
	RGBA32Uint				=48,
	RGBA32Float				=49,
	
	RGB10A2Unorm			=50,
	
	RG11B10Half 			=51,
	
	R5G6B5Unorm         	=52,
	B5G5R5A1Unorm           =53,

	Max						=54
};

enum class EMetalBindingsFlags : uint8
{
	PixelDiscard = 1 << 0,
	UseMetalShaderConverter = 1 << 1,
};

struct FMetalShaderBindings
{
	TArray<CrossCompiler::FPackedArrayInfo>			PackedGlobalArrays;
	TMap<uint8, TArray<uint8>>						ArgumentBufferMasks;
	CrossCompiler::FShaderBindingInOutMask			InOutMask;
    FString                                         IRConverterReflectionJSON;
    uint32                                          RSNumCBVs = 0;
    uint32                                          OutputSizeVS = 0;
    uint32                                          MaxInputPrimitivesPerMeshThreadgroupGS = 0;

	uint32 	ConstantBuffers = 0;
	uint32  ArgumentBuffers = 0;
	uint8	NumSamplers = 0;
	uint8	NumUniformBuffers = 0;
	uint8	NumUAVs = 0;
	EMetalBindingsFlags	Flags{};
	
	inline FArchive& Serialize(FArchive& Ar, FShaderResourceTable& SRT);
};

inline FArchive& FMetalShaderBindings::Serialize(FArchive& Ar, FShaderResourceTable& SRT)
{
	Ar << PackedGlobalArrays;
	Ar << SRT;
	Ar << ConstantBuffers;
	Ar << InOutMask;
	Ar << ArgumentBuffers;
	if (ArgumentBuffers)
	{
		Ar << ArgumentBufferMasks;
	}
	Ar << NumSamplers;
	Ar << NumUniformBuffers;
	Ar << NumUAVs;
	Ar << Flags;
	if (EnumHasAnyFlags(Flags, EMetalBindingsFlags::UseMetalShaderConverter))
	{
		Ar << IRConverterReflectionJSON;
		Ar << RSNumCBVs;
		Ar << OutputSizeVS;
		Ar << MaxInputPrimitivesPerMeshThreadgroupGS;
	}
	return Ar;
}

enum class EMetalOutputWindingMode : uint8
{
	Clockwise = 0,
	CounterClockwise = 1,
};

enum class EMetalPartitionMode : uint8
{
	Pow2 = 0,
	Integer = 1,
	FractionalOdd = 2,
	FractionalEven = 3,
};

enum class EMetalComponentType : uint8
{
	Uint = 0,
	Int,
	Half,
	Float,
	Bool,
	Max
};

struct FMetalRayTracingHeader
{
	uint32 InstanceIndexBuffer;

	bool IsValid() const
	{
		return InstanceIndexBuffer != UINT32_MAX;
	}

	FMetalRayTracingHeader()
		: InstanceIndexBuffer(UINT32_MAX)
	{

	}

	friend FArchive& operator<<(FArchive& Ar, FMetalRayTracingHeader& Header)
	{
		Ar << Header.InstanceIndexBuffer;
		return Ar;
	}
};

struct FMetalAttribute
{
	uint32 Index;
	uint32 Components;
	uint32 Offset;
	EMetalComponentType Type;
	uint32 Semantic;
	
	FMetalAttribute()
	: Index(0)
	, Components(0)
	, Offset(0)
	, Type(EMetalComponentType::Uint)
	, Semantic(0)
	{
		
	}
	
	friend FArchive& operator<<(FArchive& Ar, FMetalAttribute& Attr)
	{
		Ar << Attr.Index;
		Ar << Attr.Type;
		Ar << Attr.Components;
		Ar << Attr.Offset;
		Ar << Attr.Semantic;
		return Ar;
	}
};

struct FMetalCodeHeader
{
	FMetalShaderBindings Bindings;

	uint32 SourceLen;
	uint32 SourceCRC;
	uint32 Version;
	uint32 NumThreadsX;
	uint32 NumThreadsY;
	uint32 NumThreadsZ;
	uint32 CompileFlags;
	FMetalRayTracingHeader RayTracing;
	uint8 Frequency;
	int8 SideTable;
	uint8 bDeviceFunctionConstants;
	
	FMetalCodeHeader()
	: SourceLen(0)
	, SourceCRC(0)
	, Version(0)
	, NumThreadsX(0)
	, NumThreadsY(0)
	, NumThreadsZ(0)
	, CompileFlags(0)
	, Frequency(0)
	, SideTable(-1)
	, bDeviceFunctionConstants(0)
	{
	}

	inline FArchive& Serialize(FArchive& Ar, FShaderResourceTable& SRT);
};


inline FArchive& FMetalCodeHeader::Serialize(FArchive& Ar, FShaderResourceTable& SRT)
{
	Bindings.Serialize(Ar, SRT);
	
	Ar << SourceLen;
	Ar << SourceCRC;
	Ar << Version;
	Ar << Frequency;
	if (Frequency == SF_Compute || IsRayTracingShaderFrequency((EShaderFrequency)Frequency))
	{
		Ar << NumThreadsX;
		Ar << NumThreadsY;
		Ar << NumThreadsZ;
		Ar << RayTracing;
	}
	Ar << CompileFlags;
	Ar << SideTable;
	Ar << bDeviceFunctionConstants;
	
    return Ar;
}

struct FMetalShaderLibraryHeader
{
	FString Format;
	uint32 NumLibraries;
	uint32 NumShadersPerLibrary;
	
	friend FArchive& operator<<(FArchive& Ar, FMetalShaderLibraryHeader& Header)
	{
		return Ar << Header.Format << Header.NumLibraries << Header.NumShadersPerLibrary;
	}
};
