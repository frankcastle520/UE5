// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	PSOPrecache.h
=============================================================================*/

#pragma once

#include "RHIDefinitions.h"
#include "RHIFeatureLevel.h"
#include "RHIResources.h"
#include "Engine/EngineTypes.h"
#include "PipelineStateCache.h"
#include "PSOPrecacheFwd.h"
#include "Shader.h"

class FVertexFactoryType;

/**
 * Parameters which are needed to collect all possible PSOs used by the PSO collectors
 */
struct FPSOPrecacheParams
{
	FPSOPrecacheParams()
	{
		PrimitiveType = (uint8)PT_TriangleList;
		bDefaultMaterial = false;
		bCanvasMaterial = false;
		bRenderInMainPass = true;
		bRenderInDepthPass = true;
		bStaticLighting = true;
		bCastShadow = true;
		bRenderCustomDepth = false;
		bAffectDynamicIndirectLighting = true;
		bReverseCulling = false;
		bDisableBackFaceCulling = false;
		bCastShadowAsTwoSided = false;
		bForceLODModel = false;
		Mobility = (uint8)EComponentMobility::Static;
		bAnyMaterialHasWorldPositionOffset = false;
		StencilWriteMask = (uint8)EStencilMask::SM_Default;
		BasePassPixelFormat = (uint16)PF_Unknown;
		Unused = 0;
	}

	bool operator==(const FPSOPrecacheParams& Other) const
	{
		return Data == Other.Data;
	}

	bool operator!=(const FPSOPrecacheParams& rhs) const
	{
		return !(*this == rhs);
	}

	friend uint32 GetTypeHash(const FPSOPrecacheParams& Params)
	{
		return GetTypeHash(Params.Data);
	}

	void SetMobility(EComponentMobility::Type InMobility)
	{
		Mobility = (uint8)InMobility;
	}

	TEnumAsByte<EComponentMobility::Type> GetMobility() const
	{
		return TEnumAsByte<EComponentMobility::Type>((EComponentMobility::Type)Mobility);
	}

	bool IsMoveable() const
	{
		return Mobility == EComponentMobility::Movable || Mobility == EComponentMobility::Stationary;
	}

	void SetStencilWriteMask(EStencilMask InStencilMask)
	{
		StencilWriteMask = (uint8)InStencilMask;
	}

	TEnumAsByte<EStencilMask> GetStencilWriteMask() const
	{
		return TEnumAsByte<EStencilMask>((EStencilMask)StencilWriteMask);
	}

	void SetBassPixelFormat(EPixelFormat InBasePassPixelFormat)
	{
		BasePassPixelFormat = (uint16)InBasePassPixelFormat;
	}

	EPixelFormat GetBassPixelFormat() const
	{
		return (EPixelFormat)BasePassPixelFormat;
	}

	union
	{
		struct
		{
			uint64 PrimitiveType : 6;
			
			uint64 bDefaultMaterial : 1;
			uint64 bCanvasMaterial : 1;
			
			uint64 bRenderInMainPass : 1;
			uint64 bRenderInDepthPass : 1;
			uint64 bStaticLighting : 1;
			uint64 bCastShadow : 1;
			uint64 bRenderCustomDepth : 1;
			
			uint64 bUsesIndirectLightingCache : 1;
			uint64 bAffectDynamicIndirectLighting : 1;
			uint64 bReverseCulling : 1;
			uint64 bDisableBackFaceCulling : 1;
			uint64 bCastShadowAsTwoSided : 1;
			uint64 bForceLODModel : 1;
			
			uint64 Mobility : 4;
			uint64 bAnyMaterialHasWorldPositionOffset : 1;
			uint64 StencilWriteMask : 4;

			uint64 BasePassPixelFormat : 16;

			uint64 Unused : 20;
		};
		uint64 Data;
	};

};

// Unique ID to find the FVertexDeclarationElementList - these can be shared
using FVertexDeclarationElementListID = uint16;

/**
 * Wraps vertex factory data used during PSO precaching - optional element list ID can be used if manual vertex fetch is not possible for the given vertex factory type
 */
struct FPSOPrecacheVertexFactoryData
{
	FPSOPrecacheVertexFactoryData() = default;
	FPSOPrecacheVertexFactoryData(const FVertexFactoryType* InVertexFactoryType) : VertexFactoryType(InVertexFactoryType), CustomDefaultVertexDeclaration(nullptr) {}
	ENGINE_API FPSOPrecacheVertexFactoryData(const FVertexFactoryType* InVertexFactoryType, const FVertexDeclarationElementList& ElementList);

	const FVertexFactoryType* VertexFactoryType = nullptr;

	// Custom vertex declaration used for EVertexInputStreamType::Default if provided - the others are directly retrieved from the type if needed
	FRHIVertexDeclaration* CustomDefaultVertexDeclaration = nullptr;

	bool operator==(const FPSOPrecacheVertexFactoryData& Other) const
	{
		return VertexFactoryType == Other.VertexFactoryType && CustomDefaultVertexDeclaration == Other.CustomDefaultVertexDeclaration;
	}

	bool operator!=(const FPSOPrecacheVertexFactoryData& rhs) const
	{
		return !(*this == rhs);
	}

	friend uint32 GetTypeHash(const FPSOPrecacheVertexFactoryData& Params)
	{
		return HashCombine(PointerHash(Params.VertexFactoryType), PointerHash(Params.CustomDefaultVertexDeclaration));
	}
};

typedef TArray<FPSOPrecacheVertexFactoryData, TInlineAllocator<2> > FPSOPrecacheVertexFactoryDataList;

struct FPSOPrecacheVertexFactoryDataPerMaterialIndex
{
	int16 MaterialIndex;
	FPSOPrecacheVertexFactoryDataList VertexFactoryDataList;
};

typedef TArray<FPSOPrecacheVertexFactoryDataPerMaterialIndex, TInlineAllocator<4> > FPSOPrecacheVertexFactoryDataPerMaterialIndexList;

struct FMaterialInterfacePSOPrecacheParams
{
	EPSOPrecachePriority Priority = EPSOPrecachePriority::Medium;
	UMaterialInterface* MaterialInterface = nullptr;
	FPSOPrecacheParams PSOPrecacheParams;
	FPSOPrecacheVertexFactoryDataList VertexFactoryDataList;
};

extern ENGINE_API void AddMaterialInterfacePSOPrecacheParamsToList(const FMaterialInterfacePSOPrecacheParams& EntryToAdd, FMaterialInterfacePSOPrecacheParamsList& List);

struct FShaderPreloadData
{
	FShaderPreloadData() = default;

	FShaderPreloadData(const TShaderRef<FShader>& InShader)
	{
		Shaders.Emplace(InShader);
	}

	FShaderPreloadData(TArray<TShaderRef<FShader>, TInlineAllocator<3>>&& InShaders)
		: Shaders(MoveTemp(InShaders))
	{
	}

	// Can have 3 shaders at most (vertex, geometry, pixel).
	TArray<TShaderRef<FShader>, TInlineAllocator<3>> Shaders;
};

enum class EPSOPrecacheMode : uint8
{
	PSO = 0,
	PreloadShader = 1,
};

extern ENGINE_API EPSOPrecacheMode GetPSOPrecacheMode();

/**
 * Wrapper class around the initializer to collect some extra validation data during PSO collection on the different collectors
 */
struct FPSOPrecacheData
{
	FPSOPrecacheData() : Type(EType::Graphics), bRequired(true)
#if PSO_PRECACHING_VALIDATE
		, PSOCollectorIndex(INDEX_NONE)
		, bDefaultMaterial(0)
		, VertexFactoryType(nullptr)
#endif // PSO_PRECACHING_VALIDATE
	{
	}

	enum class EType : uint8
	{
		Graphics,
		Compute,
	};
	EType Type;

	// Is the PSO required to be able render the object or can it provide a fallback path
	// (proxy creation won't wait for these PSOs if enabled)
	bool bRequired;

	union
	{
		FGraphicsPipelineStateInitializer GraphicsPSOInitializer;
		FRHIComputeShader* ComputeShader;
	};

	FShaderPreloadData ShaderPreloadData;

	void SetComputeShader(const TShaderRef<FShader>& InComputeShader)
	{
		if (GetPSOPrecacheMode() == EPSOPrecacheMode::PreloadShader)
		{
			ShaderPreloadData.Shaders.Emplace(InComputeShader);
		}
		else
		{
			ComputeShader = InComputeShader.GetComputeShader();
		}
	}

#if PSO_PRECACHING_VALIDATE
	int32 PSOCollectorIndex : 31;
	int32 bDefaultMaterial : 1;
	const FVertexFactoryType* VertexFactoryType;
#endif // PSO_PRECACHING_VALIDATE
};

typedef TArray<FPSOPrecacheData> FPSOPrecacheDataArray;
typedef TArray<FPSOPrecacheRequestResult, TInlineAllocator<4> > FPSOPrecacheRequestResultArray;

struct FMaterialPSOPrecacheParams
{
	ERHIFeatureLevel::Type FeatureLevel = ERHIFeatureLevel::Num;
	FMaterial* Material = nullptr;
	FPSOPrecacheVertexFactoryData VertexFactoryData;
	FPSOPrecacheParams PrecachePSOParams;

	bool operator==(const FMaterialPSOPrecacheParams& Other) const
	{
		return FeatureLevel == Other.FeatureLevel &&
			Material == Other.Material &&
			VertexFactoryData == Other.VertexFactoryData &&
			PrecachePSOParams == Other.PrecachePSOParams;
	}

	bool operator!=(const FMaterialPSOPrecacheParams& rhs) const
	{
		return !(*this == rhs);
	}

	friend uint32 GetTypeHash(const FMaterialPSOPrecacheParams& Params)
	{
		return HashCombine(GetTypeHash(Params.FeatureLevel), HashCombine(PointerHash(Params.Material),
			HashCombine(GetTypeHash(Params.VertexFactoryData), GetTypeHash(Params.PrecachePSOParams))));
	}
};



/**
 * Precaching PSOs for components?
 */
extern ENGINE_API bool IsComponentPSOPrecachingEnabled();

/**
 * Precaching PSOs for resources?
 */
extern ENGINE_API bool IsResourcePSOPrecachingEnabled();

/**
 * Boost drawn PSO precache request priority
 */
extern ENGINE_API bool ShouldBoostPSOPrecachePriorityOnDraw();

/**
 * Dynamically preload shaders
 */
extern ENGINE_API bool IsPSOShaderPreloadingEnabled();

enum class EPSOPrecacheProxyCreationStrategy : uint8
{
	// Always create the render proxy regardless of whether the PSO has finished precaching or not. 
	// This will introduce a blocking wait when the proxy is rendered if the PSO is not ready.
	AlwaysCreate = 0, 

	// Delay the creation of the render proxy until the PSO has finished precaching. 
	// This effectively skips drawing components until the PSO is ready, when the proxy will be created.
	DelayUntilPSOPrecached = 1, 
	
	// Create a render proxy that uses the default material if the PSO has not finished precaching by creation time.
	// The proxy will be re-created with the actual materials once the PSO is ready.
	// Currently implemented only for static and skinned mesh components, while Niagara components will skip render proxy creation altogether.
	UseDefaultMaterialUntilPSOPrecached = 2
};

extern ENGINE_API EPSOPrecacheProxyCreationStrategy GetPSOPrecacheProxyCreationStrategy();

/**
 * Delay component proxy creation when it's requested PSOs are still precaching
 */
extern ENGINE_API bool ProxyCreationWhenPSOReady();

/**
 * Boost the primitive's precache PSO jobs to highest priority.
 */
extern ENGINE_API void BoostPrecachedPSORequestsOnDraw(const class FPrimitiveSceneInfo* SceneInfo);