// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include <2D/BlendModes.h>

#include "CoreMinimal.h"
#include "Job/Job.h"
#include "FxMat/FxMaterial.h"
#include "Model/Mix/MixUpdateCycle.h"
#include <DataDrivenShaderPlatformInfo.h>
#include "T_Filter.generated.h"

//////////////////////////////////////////////////////////////////////////
/// Edge detection
//////////////////////////////////////////////////////////////////////////
class TEXTUREGRAPHENGINE_API FSH_EdgeDetect : public FSH_Base
{
public:
	DECLARE_GLOBAL_SHADER(FSH_EdgeDetect);
	SHADER_USE_PARAMETER_STRUCT(FSH_EdgeDetect, FSH_Base);
	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT(FTileInfo, TileInfo)
		SHADER_PARAMETER_STRUCT(FStandardSamplerStates, SamplerStates)
		SHADER_PARAMETER_TEXTURE(Texture2D, SourceTexture)
		SHADER_PARAMETER(float, Thickness)
	END_SHADER_PARAMETER_STRUCT()

public:
	TEXTURE_ENGINE_DEFAULT_PERMUTATION;
	TEXTUREGRAPH_ENGINE_DEFAULT_COMPILATION_ENV;
};

UENUM(BlueprintType)
namespace EWarp
{
	enum Type : int
	{
		Directional = 0				UMETA(DisplayName = "Directional"),
		Normal = 1					UMETA(DisplayName = "Normal"),
		Sine = 2					UMETA(DisplayName = "Sine"),
	};
}

//////////////////////////////////////////////////////////////////////////
/// Directional warp
//////////////////////////////////////////////////////////////////////////
class TEXTUREGRAPHENGINE_API FSH_DirectionalWarp : public FSH_Base
{
public:
	DECLARE_GLOBAL_SHADER(FSH_DirectionalWarp);
	SHADER_USE_PARAMETER_STRUCT(FSH_DirectionalWarp, FSH_Base);
	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT(FTileInfo, TileInfo)
		SHADER_PARAMETER_STRUCT(FStandardSamplerStates, SamplerStates)
		SHADER_PARAMETER_TEXTURE(Texture2D, SourceTexture)
		SHADER_PARAMETER_TEXTURE(Texture2D, Mask)
		SHADER_PARAMETER(float, AngleRad)
		SHADER_PARAMETER(float, Intensity)
	END_SHADER_PARAMETER_STRUCT()

public:
	TEXTURE_ENGINE_DEFAULT_PERMUTATION;
	TEXTUREGRAPH_ENGINE_DEFAULT_COMPILATION_ENV;
};

//////////////////////////////////////////////////////////////////////////
/// Directional warp
//////////////////////////////////////////////////////////////////////////
class TEXTUREGRAPHENGINE_API FSH_NormalWarp : public FSH_Base
{
public:
	DECLARE_GLOBAL_SHADER(FSH_NormalWarp);
	SHADER_USE_PARAMETER_STRUCT(FSH_NormalWarp, FSH_Base);
	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT(FTileInfo, TileInfo)
		SHADER_PARAMETER_STRUCT(FStandardSamplerStates, SamplerStates)
		SHADER_PARAMETER_TEXTURE(Texture2D, SourceTexture)
		SHADER_PARAMETER_TEXTURE(Texture2D, Mask)
		SHADER_PARAMETER(float, Intensity)
	END_SHADER_PARAMETER_STRUCT()

public:
	TEXTURE_ENGINE_DEFAULT_PERMUTATION;
	TEXTUREGRAPH_ENGINE_DEFAULT_COMPILATION_ENV;
};

//////////////////////////////////////////////////////////////////////////
/// Directional warp
//////////////////////////////////////////////////////////////////////////
class TEXTUREGRAPHENGINE_API FSH_SineWarp : public FSH_Base
{
public:
	DECLARE_GLOBAL_SHADER(FSH_SineWarp);
	SHADER_USE_PARAMETER_STRUCT(FSH_SineWarp, FSH_Base);
	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT(FTileInfo, TileInfo)
		SHADER_PARAMETER_STRUCT(FStandardSamplerStates, SamplerStates)
		SHADER_PARAMETER_TEXTURE(Texture2D, SourceTexture)
		SHADER_PARAMETER_TEXTURE(Texture2D, Mask)
		SHADER_PARAMETER(float, Intensity)
		SHADER_PARAMETER(float, PhaseU)
		SHADER_PARAMETER(float, PhaseV)
	END_SHADER_PARAMETER_STRUCT()

public:
	TEXTURE_ENGINE_DEFAULT_PERMUTATION;
	TEXTUREGRAPH_ENGINE_DEFAULT_COMPILATION_ENV;
};

//////////////////////////////////////////////////////////////////////////
class TEXTUREGRAPHENGINE_API T_Filter
{
public:
	//////////////////////////////////////////////////////////////////////////
	/// Static functions
	//////////////////////////////////////////////////////////////////////////
	static TiledBlobPtr				CreateEdgeDetect(MixUpdateCyclePtr InCycle, BufferDescriptor DesiredDesc, TiledBlobPtr SourceTexture, float Thickness, int32 InTargetId);
	static TiledBlobPtr				CreateDirectionalWarp(MixUpdateCyclePtr InCycle, BufferDescriptor DesiredDesc, TiledBlobPtr SourceTexture, TiledBlobPtr Mask, float Intensity, float AngleRad, int32 InTargetId);
	static TiledBlobPtr				CreateNormalWarp(MixUpdateCyclePtr InCycle, BufferDescriptor DesiredDesc, TiledBlobPtr SourceTexture, TiledBlobPtr Mask, float Intensity, int32 InTargetId);
	static TiledBlobPtr				CreateSineWarp(MixUpdateCyclePtr InCycle, BufferDescriptor DesiredDesc, TiledBlobPtr SourceTexture, TiledBlobPtr Mask, float Intensity, float PhaseU, float PhaseV, int32 InTargetId);
};
