// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
LightGridInjection.cpp
=============================================================================*/

#include "LightGrid.h"

#include "CoreMinimal.h"
#include "Stats/Stats.h"
#include "HAL/IConsoleManager.h"
#include "RHI.h"
#include "UniformBuffer.h"
#include "ShaderParameters.h"
#include "RendererInterface.h"
#include "EngineDefines.h"
#include "PrimitiveSceneProxy.h"
#include "Shader.h"
#include "SceneUtils.h"
#include "PostProcess/SceneRenderTargets.h"
#include "LightSceneInfo.h"
#include "RectLightSceneProxy.h"
#include "GlobalShader.h"
#include "SceneRendering.h"
#include "DeferredShadingRenderer.h"
#include "BasePassRendering.h"
#include "RendererModule.h"
#include "ScenePrivate.h"
#include "ClearQuad.h"
#include "VolumetricFog.h"
#include "VolumetricCloudRendering.h"
#include "Components/LightComponent.h"
#include "Engine/MapBuildDataRegistry.h"
#include "PixelShaderUtils.h"
#include "ShaderPrint.h"
#include "ShaderPrintParameters.h"
#include "RenderUtils.h"
#include "MegaLights/MegaLights.h"
#include "LightGridDefinitions.h"
#include "VolumetricFog.h"

int32 GLightGridPixelSize = 64;
FAutoConsoleVariableRef CVarLightGridPixelSize(
	TEXT("r.Forward.LightGridPixelSize"),
	GLightGridPixelSize,
	TEXT("Size of a cell in the light grid, in pixels."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

int32 GLightGridSizeZ = 32;
FAutoConsoleVariableRef CVarLightGridSizeZ(
	TEXT("r.Forward.LightGridSizeZ"),
	GLightGridSizeZ,
	TEXT("Number of Z slices in the light grid."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

int32 GForwardLightGridDebug = 0;
FAutoConsoleVariableRef CVarLightGridDebug(
	TEXT("r.Forward.LightGridDebug"),
	GForwardLightGridDebug,
	TEXT("Whether to display on screen culledlight per tile.\n")
	TEXT(" 0: off (default)\n")
	TEXT(" 1: on - showing light count onto the depth buffer\n")
	TEXT(" 2: on - showing max light count per tile accoung for each slice but the last one (culling there is too conservative)\n")
	TEXT(" 3: on - showing max light count per tile accoung for each slice and the last one \n"),
	ECVF_RenderThreadSafe
);

int32 GForwardLightGridDebugMaxThreshold = 8;
FAutoConsoleVariableRef CVarLightGridDebugMaxThreshold(
	TEXT("r.Forward.LightGridDebug.MaxThreshold"),
	GForwardLightGridDebugMaxThreshold,
	TEXT("Maximum light threshold for heat map visualization. (default = 8)\n"),
	ECVF_RenderThreadSafe
);

int32 GLightGridHZBCull = 1;
FAutoConsoleVariableRef CVarLightGridHZBCull(
	TEXT("r.Forward.LightGridHZBCull"),
	GLightGridHZBCull,
	TEXT("Whether to use HZB culling to skip occluded grid cells."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

int32 GLightGridRefineRectLightBounds = 1;
FAutoConsoleVariableRef CVarLightGridRefineRectLightBounds(
	TEXT("r.Forward.LightGridDebug.RectLightBounds"),
	GLightGridRefineRectLightBounds,
	TEXT("Whether to refine rect light bounds (should only be disabled for debugging purposes)."),
	ECVF_RenderThreadSafe
);

int32 GMaxCulledLightsPerCell = 32;
FAutoConsoleVariableRef CVarMaxCulledLightsPerCell(
	TEXT("r.Forward.MaxCulledLightsPerCell"),
	GMaxCulledLightsPerCell,
	TEXT("Controls how much memory is allocated for each cell for light culling.  When r.Forward.LightLinkedListCulling is enabled, this is used to compute a global max instead of a per-cell limit on culled lights."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

int32 GLightLinkedListCulling = 1;
FAutoConsoleVariableRef CVarLightLinkedListCulling(
	TEXT("r.Forward.LightLinkedListCulling"),
	GLightLinkedListCulling,
	TEXT("Uses a reverse linked list to store culled lights, removing the fixed limit on how many lights can affect a cell - it becomes a global limit instead."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

int32 GLightCullingQuality = 1;
FAutoConsoleVariableRef CVarLightCullingQuality(
	TEXT("r.LightCulling.Quality"),
	GLightCullingQuality,
	TEXT("Whether to run compute light culling pass.\n")
	TEXT(" 0: off \n")
	TEXT(" 1: on (default)\n"),
	ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<int32> CVarLightCullingWorkloadDistributionMode(
	TEXT("r.LightCulling.WorkloadDistributionMode"),
	0,
	TEXT("0 - single thread per cell.\n")
	TEXT("1 - thread group per cell (64 threads).\n")
	TEXT("2 - thread group per cell (32 threads if supported, otherwise single thread).\n")
	TEXT("(This cvar only applies to fine light grid. When using two levels, coarse grid always uses thread group per cell."),
	ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<bool> CVarLightCullingTwoLevel(
	TEXT("r.LightCulling.TwoLevel"),
	true,
	TEXT("Whether to build light grid in two passes."),
	ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<int32> CVarLightCullingTwoLevelThreshold(
	TEXT("r.LightCulling.TwoLevel.Threshold"),
	128,
	TEXT("Threshold used to determine whether to use two level culling basedon the number of lights in view."),
	ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<int32> CVarLightCullingTwoLevelExponent(
	TEXT("r.LightCulling.TwoLevel.Exponent"),
	2,
	TEXT("Exponent used to derive the coarse grid size (base 2)."),
	ECVF_RenderThreadSafe
);

float GLightCullingMaxDistanceOverrideKilometers = -1.0f;
FAutoConsoleVariableRef CVarLightCullingMaxDistanceOverride(
	TEXT("r.LightCulling.MaxDistanceOverrideKilometers"),
	GLightCullingMaxDistanceOverrideKilometers,
	TEXT("Used to override the maximum far distance at which we can store data in the light grid.\n If this is increase, you might want to update r.Forward.LightGridSizeZ to a reasonable value according to your use case light count and distribution.")
	TEXT(" <=0: off \n")
	TEXT(" >0: the far distance in kilometers.\n"),
	ECVF_RenderThreadSafe
);

bool ShouldVisualizeLightGrid()
{
	return GForwardLightGridDebug > 0;
}

// If this is changed, the LIGHT_GRID_USES_16BIT_BUFFERS define from LightGridCommon.ush should also be updated.
bool LightGridUses16BitBuffers(EShaderPlatform Platform)
{
	// CulledLightDataGrid, is typically 16bit elements to save on memory and bandwidth. So to not introduce any regressions it will stay as texel buffer on all platforms, except mobile and Metal (which does not support type conversions).
	return RHISupportsBufferLoadTypeConversion(Platform) && !IsMobilePlatform(Platform);
}

void SetupDummyForwardLightUniformParameters(FRDGBuilder& GraphBuilder, FForwardLightData& ForwardLightData, EShaderPlatform ShaderPlatform)
{
	const FRDGSystemTextures& SystemTextures = FRDGSystemTextures::Get(GraphBuilder);
	
	ForwardLightData.DirectionalLightShadowmapAtlas = SystemTextures.Black;
	ForwardLightData.DirectionalLightStaticShadowmap = GBlackTexture->TextureRHI;

	FRDGBufferRef ForwardLocalLightBuffer = GSystemTextures.GetDefaultStructuredBuffer(GraphBuilder, sizeof(FVector4f));
	ForwardLightData.ForwardLocalLightBuffer = GraphBuilder.CreateSRV(ForwardLocalLightBuffer);

	FRDGBufferRef NumCulledLightsGrid = GSystemTextures.GetDefaultStructuredBuffer(GraphBuilder, sizeof(uint32));
	ForwardLightData.NumCulledLightsGrid = GraphBuilder.CreateSRV(NumCulledLightsGrid);

	const bool bLightGridUses16BitBuffers = LightGridUses16BitBuffers(ShaderPlatform);
	FRDGBufferSRVRef CulledLightDataGridSRV = nullptr;
	if (bLightGridUses16BitBuffers)
	{
		FRDGBufferRef CulledLightDataGrid = GSystemTextures.GetDefaultBuffer(GraphBuilder, sizeof(uint16));
		CulledLightDataGridSRV = GraphBuilder.CreateSRV(CulledLightDataGrid, PF_R16_UINT);
	}
	else
	{
		FRDGBufferRef CulledLightDataGrid = GSystemTextures.GetDefaultStructuredBuffer(GraphBuilder, sizeof(uint32));
		CulledLightDataGridSRV = GraphBuilder.CreateSRV(CulledLightDataGrid);
	}
	ForwardLightData.CulledLightDataGrid32Bit = CulledLightDataGridSRV;
	ForwardLightData.CulledLightDataGrid16Bit = CulledLightDataGridSRV;

	ForwardLightData.LightFunctionAtlasLightIndex = 0;

	ForwardLightData.bAffectsTranslucentLighting = 0;
}

TRDGUniformBufferRef<FForwardLightData> CreateDummyForwardLightUniformBuffer(FRDGBuilder& GraphBuilder, EShaderPlatform ShaderPlatform)
{
	FForwardLightData* ForwardLightData = GraphBuilder.AllocParameters<FForwardLightData>();
	SetupDummyForwardLightUniformParameters(GraphBuilder, *ForwardLightData, ShaderPlatform);
	return GraphBuilder.CreateUniformBuffer(ForwardLightData);
}

void SetDummyForwardLightUniformBufferOnViews(FRDGBuilder& GraphBuilder, EShaderPlatform ShaderPlatform, TArray<FViewInfo>& Views)
{
	TRDGUniformBufferRef<FForwardLightData> ForwardLightUniformBuffer = CreateDummyForwardLightUniformBuffer(GraphBuilder, ShaderPlatform);
	for (auto& View : Views)
	{
		View.ForwardLightingResources.SetUniformBuffer(ForwardLightUniformBuffer);
	}
}

IMPLEMENT_GLOBAL_SHADER_PARAMETER_STRUCT(FForwardLightData, "ForwardLightData");

FForwardLightData::FForwardLightData()
{
	FMemory::Memzero(*this);
	ShadowmapSampler = TStaticSamplerState<SF_Point,AM_Clamp,AM_Clamp,AM_Clamp>::GetRHI();
	DirectionalLightStaticShadowmap = GBlackTexture->TextureRHI;
	StaticShadowmapSampler = TStaticSamplerState<SF_Bilinear,AM_Clamp,AM_Clamp,AM_Clamp>::GetRHI();
}

int32 NumCulledLightsGridStride = 2;
int32 NumCulledGridPrimitiveTypes = 2;
int32 LightLinkStride = 2;

// 65k indexable light limit
typedef uint16 FLightIndexType;
// UINT_MAX indexable light limit
typedef uint32 FLightIndexType32;


class FLightGridInjectionCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FLightGridInjectionCS);
	SHADER_USE_PARAMETER_STRUCT(FLightGridInjectionCS, FGlobalShader)
public:
	class FUseLinkedList : SHADER_PERMUTATION_BOOL("USE_LINKED_CULL_LIST");
	class FRefineRectLightBounds : SHADER_PERMUTATION_BOOL("REFINE_RECTLIGHT_BOUNDS");
	class FUseHZBCull : SHADER_PERMUTATION_BOOL("USE_HZB_CULL");
	class FUseParentLightGrid : SHADER_PERMUTATION_BOOL("USE_PARENT_LIGHT_GRID");
	class FUseThreadGroupPerCell : SHADER_PERMUTATION_BOOL("USE_THREAD_GROUP_PER_CELL");
	class FUseThreadGroupSize32 : SHADER_PERMUTATION_BOOL("USE_THREAD_GROUP_SIZE_32");
	using FPermutationDomain = TShaderPermutationDomain<FUseLinkedList, FRefineRectLightBounds, FUseHZBCull, FUseParentLightGrid, FUseThreadGroupPerCell, FUseThreadGroupSize32>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_REF(FReflectionCaptureShaderData, ReflectionCapture)
		SHADER_PARAMETER_STRUCT_REF(FMobileReflectionCaptureShaderData, MobileReflectionCaptureData)
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, RWNumCulledLightsGrid)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, RWCulledLightDataGrid32Bit)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, RWCulledLightDataGrid16Bit)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, RWNextCulledLightLink)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, RWNextCulledLightData)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, RWCulledLightLinks)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>, LightViewSpacePositionAndRadius)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>, LightViewSpaceDirAndPreprocAngle)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>, LightViewSpaceRectPlanes)

		SHADER_PARAMETER(FIntVector, CulledGridSize)
		SHADER_PARAMETER(uint32, NumReflectionCaptures)
		SHADER_PARAMETER(FVector3f, LightGridZParams)
		SHADER_PARAMETER(uint32, NumLocalLights)
		SHADER_PARAMETER(uint32, NumGridCells)
		SHADER_PARAMETER(uint32, MaxCulledLightsPerCell)
		SHADER_PARAMETER(uint32, NumAvailableLinks)
		SHADER_PARAMETER(uint32, LightGridPixelSizeShift)
		SHADER_PARAMETER(uint32, MegaLightsSupportedStartIndex)

		SHADER_PARAMETER(uint32, LightGridZSliceScale)
		SHADER_PARAMETER(uint32, LightGridCullMarginXY)
		SHADER_PARAMETER(uint32, LightGridCullMarginZ)
		SHADER_PARAMETER(FVector3f, LightGridCullMarginZParams)
		SHADER_PARAMETER(uint32, LightGridCullMaxZ)

		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, ParentNumCulledLightsGrid)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, ParentCulledLightDataGrid32Bit)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, ParentCulledLightDataGrid16Bit)
		SHADER_PARAMETER(FIntVector, ParentGridSize)
		SHADER_PARAMETER(uint32, NumParentGridCells)
		SHADER_PARAMETER(uint32, ParentGridSizeFactor)

		SHADER_PARAMETER(FVector2f, HZBSize)
		SHADER_PARAMETER(FVector2f, HZBViewSize)
		SHADER_PARAMETER(FIntRect, HZBViewRect)

		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, HZBTexture)
		SHADER_PARAMETER_SAMPLER(SamplerState, HZBSampler)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return true;
	}

	static FIntVector GetGroupSize(FPermutationDomain PermutationVector)
	{
		if (PermutationVector.Get<FUseThreadGroupSize32>())
		{
			return FIntVector(4, 4, 2);
		}
		else
		{
			return FIntVector(4, 4, 4);
		}
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		FForwardLightingParameters::ModifyCompilationEnvironment(Parameters.Platform, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("LIGHT_LINK_STRIDE"), LightLinkStride);

		FPermutationDomain PermutationVector(Parameters.PermutationId);

		FIntVector GroupSize = GetGroupSize(PermutationVector);

		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE"), GroupSize.X * GroupSize.Y * GroupSize.Z);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE_X"), GroupSize.X);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE_Y"), GroupSize.Y);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE_Z"), GroupSize.Z);
	}
};

IMPLEMENT_GLOBAL_SHADER(FLightGridInjectionCS, "/Engine/Private/LightGridInjection.usf", "LightGridInjectionCS", SF_Compute);


/**
 */
FORCEINLINE float GetTanRadAngleOrZero(float coneAngle)
{
	if (coneAngle < PI / 2.001f)
	{
		return FMath::Tan(coneAngle);
	}

	return 0.0f;
}


FVector GetLightGridZParams(float NearPlane, float FarPlane)
{
	// S = distribution scale
	// B, O are solved for given the z distances of the first+last slice, and the # of slices.
	//
	// slice = log2(z*B + O) * S

	// Don't spend lots of resolution right in front of the near plane
	double NearOffset = .095 * 100;
	// Space out the slices so they aren't all clustered at the near plane
	double S = 4.05;

	double N = NearPlane + NearOffset;
	double F = FarPlane;

	double O = (F - N * exp2((GLightGridSizeZ - 1) / S)) / (F - N);
	double B = (1 - O) / N;

	return FVector(B, O, S);
}

uint32 PackRG16(float In0, float In1)
{
	return uint32(FFloat16(In0).Encoded) | (uint32(FFloat16(In1).Encoded) << 16);
}

static uint32 PackRGB10(float In0, float In1, float In2)
{
	return 
		(uint32(FMath::Clamp(In0 * 1023u, 0u, 1023u))      )|
		(uint32(FMath::Clamp(In1 * 1023u, 0u, 1023u)) << 10)|
		(uint32(FMath::Clamp(In2 * 1023u, 0u, 1023u)) << 20);
}

static FVector2f PackLightColor(const FVector3f& LightColor)
{
	FVector3f LightColorDir;
	float LightColorLength;
	LightColor.ToDirectionAndLength(LightColorDir, LightColorLength);

	FVector2f LightColorPacked;
	uint32 LightColorDirPacked = 
		((static_cast<uint32>(LightColorDir.X * 0x3FF) & 0x3FF) <<  0) |
		((static_cast<uint32>(LightColorDir.Y * 0x3FF) & 0x3FF) << 10) |
		((static_cast<uint32>(LightColorDir.Z * 0x3FF) & 0x3FF) << 20);

	LightColorPacked.X = LightColorLength / 0x3FF;
	*(uint32*)(&LightColorPacked.Y) = LightColorDirPacked;

	return LightColorPacked;
}

static uint32 PackVirtualShadowMapIdAndPrevLocalLightIndex(int32 VirtualShadowMapId, int32 PrevLocalLightIndex)
{
	// NOTE: Both of these could possibly be INDEX_NONE, which needs to be represented
	// We map all negative numbers to 0, and add one to any positive ones
	uint32 VSMPacked = VirtualShadowMapId < 0 ? 0 : uint32(VirtualShadowMapId + 1);
	uint32 PrevPacked = PrevLocalLightIndex < 0 ? 0 : uint32(PrevLocalLightIndex + 1);

	// Pack to 16 bits each
	check(VSMPacked <= MAX_uint16);
	check(PrevPacked <= MAX_uint16);
	return (VSMPacked << 16) | (PrevPacked & 0xFFFF);
}

static void PackLocalLightData(
	FForwardLocalLightData& Out,
	const FViewInfo& View,
	const FSimpleLightEntry& SimpleLight,
	const FSimpleLightPerViewEntry& SimpleLightPerViewData)
{
	// Put simple lights in all lighting channels
	FLightingChannels SimpleLightLightingChannels;
	SimpleLightLightingChannels.bChannel0 = SimpleLightLightingChannels.bChannel1 = SimpleLightLightingChannels.bChannel2 = true;

	const uint32 SimpleLightLightingChannelMask = GetLightingChannelMaskForStruct(SimpleLightLightingChannels);
	const FVector3f LightTranslatedWorldPosition(View.ViewMatrices.GetPreViewTranslation() + SimpleLightPerViewData.Position);

	// No shadowmap channels for simple lights
	uint32 ShadowMapChannelMask = 0;
	ShadowMapChannelMask |= SimpleLightLightingChannelMask << 8;

	// Pack both values into a single float to keep float4 alignment
	const float SimpleLightSourceLength = 0;
	const uint32 PackedW = PackRG16(SimpleLightSourceLength, SimpleLight.VolumetricScatteringIntensity);

	// Pack both values into a single float to keep float4 alignment
	const float SourceRadius = 0;
	const float SourceSoftRadius = 0;
	const uint32 PackedZ = PackRG16(SourceRadius, SourceSoftRadius);

	// Pack both rect light data (barn door length is initialized to -2 
	const uint32 RectPackedX = 0;
	const uint32 RectPackedY = 0;
	const uint32 RectPackedZ = FFloat16(-2.f).Encoded;

	// Pack specular scale and IES profile index
	const float SpecularScale = SimpleLight.SpecularScale;
	const float DiffuseScale  = SimpleLight.DiffuseScale;
	const float IESAtlasIndex = INDEX_NONE;

	// Offset IESAtlasIndex here in order to preserve INDEX_NONE = -1 after encoding
	const uint32 SpecularScale_DiffuseScale_IESData = PackRGB10(SpecularScale, DiffuseScale, (IESAtlasIndex + 1) * (1.f / 1023.f));

	const FVector3f LightColor = (FVector3f)SimpleLight.Color * FLightRenderParameters::GetLightExposureScale(View.GetLastEyeAdaptationExposure(), SimpleLight.InverseExposureBlend);
	const FVector2f LightColorPacked = PackLightColor(LightColor);

	const uint32 VirtualShadowMapIdAndPrevLocalLightIndex = PackVirtualShadowMapIdAndPrevLocalLightIndex(INDEX_NONE, INDEX_NONE);

	Out.LightPositionAndInvRadius							= FVector4f(LightTranslatedWorldPosition, 1.0f / FMath::Max(SimpleLight.Radius, KINDA_SMALL_NUMBER));
	Out.LightColorAndIdAndFalloffExponent					= FVector4f(LightColorPacked.X, LightColorPacked.Y, INDEX_NONE, SimpleLight.Exponent);
	Out.LightDirectionAndShadowMapChannelMask				= FVector4f(FVector3f(1, 0, 0), FMath::AsFloat(ShadowMapChannelMask));
	Out.SpotAnglesAndSourceRadiusPacked						= FVector4f(-2, 1, FMath::AsFloat(PackedZ), FMath::AsFloat(PackedW));
	Out.LightTangentAndIESDataAndSpecularScale				= FVector4f(1.0f, 0.0f, 0.0f, FMath::AsFloat(SpecularScale_DiffuseScale_IESData));
	Out.RectDataAndVirtualShadowMapIdOrPrevLocalLightIndex	= FVector4f(FMath::AsFloat(RectPackedX), FMath::AsFloat(RectPackedY), FMath::AsFloat(RectPackedZ), FMath::AsFloat(VirtualShadowMapIdAndPrevLocalLightIndex));
}

static void PackLocalLightData(
	FForwardLocalLightData& Out, 
	const FViewInfo& View,
	const FLightRenderParameters& LightParameters,
	const uint32 LightTypeAndShadowMapChannelMaskAndLightFunctionIndexPacked,
	const int32 LightSceneId,
	const int32 VirtualShadowMapId,
	const int32 PrevLocalLightIndex,
	const float VolumetricScatteringIntensity)
{
	const FVector3f LightTranslatedWorldPosition(View.ViewMatrices.GetPreViewTranslation() + LightParameters.WorldPosition);

	// Pack both values into a single float to keep float4 alignment
	const uint32 PackedW = PackRG16(LightParameters.SourceLength, VolumetricScatteringIntensity);

	// Pack both SourceRadius and SoftSourceRadius
	const uint32 PackedZ = PackRG16(LightParameters.SourceRadius, LightParameters.SoftSourceRadius);
	
	// Pack rect light data
	uint32 RectPackedX = PackRG16(LightParameters.RectLightAtlasUVOffset.X, LightParameters.RectLightAtlasUVOffset.Y);
	uint32 RectPackedY = PackRG16(LightParameters.RectLightAtlasUVScale.X, LightParameters.RectLightAtlasUVScale.Y);
	uint32 RectPackedZ = 0;
	RectPackedZ |= FFloat16(LightParameters.RectLightBarnLength).Encoded;									// 16 bits
	RectPackedZ |= uint32(FMath::Clamp(LightParameters.RectLightBarnCosAngle,  0.f, 1.0f) * 0x3FF) << 16;	// 10 bits
	RectPackedZ |= uint32(FMath::Clamp(LightParameters.RectLightAtlasMaxLevel, 0.f, 63.f)) << 26;			//  6 bits

	// Pack specular scale and IES profile index
	// Offset IESAtlasIndex here in order to preserve INDEX_NONE = -1 after encoding
	// IESAtlasIndex requires scaling because PackRGB10 expects inputs to be [0:1]
	const uint32 SpecularScale_DiffuseScale_IESData = PackRGB10(LightParameters.SpecularScale, LightParameters.DiffuseScale, (LightParameters.IESAtlasIndex + 1) * (1.f / 1023.f)); // pack atlas id here? 16bit specular 8bit IES and 8 bit LightFunction

	const FVector2f LightColorPacked = PackLightColor(FVector3f(LightParameters.Color));

	const uint32 VirtualShadowMapIdAndPrevLocalLightIndex = 
		PackVirtualShadowMapIdAndPrevLocalLightIndex(VirtualShadowMapId, PrevLocalLightIndex);

	// NOTE: SpotAngles needs full-precision for VSM one pass projection
	Out.LightPositionAndInvRadius							= FVector4f(LightTranslatedWorldPosition, LightParameters.InvRadius);
	Out.LightColorAndIdAndFalloffExponent					= FVector4f(LightColorPacked.X, LightColorPacked.Y, LightSceneId, LightParameters.FalloffExponent);
	Out.LightDirectionAndShadowMapChannelMask				= FVector4f(LightParameters.Direction, FMath::AsFloat(LightTypeAndShadowMapChannelMaskAndLightFunctionIndexPacked));
	Out.SpotAnglesAndSourceRadiusPacked						= FVector4f(LightParameters.SpotAngles.X, LightParameters.SpotAngles.Y, FMath::AsFloat(PackedZ), FMath::AsFloat(PackedW));
	Out.LightTangentAndIESDataAndSpecularScale				= FVector4f(LightParameters.Tangent, FMath::AsFloat(SpecularScale_DiffuseScale_IESData));
	Out.RectDataAndVirtualShadowMapIdOrPrevLocalLightIndex	= FVector4f(FMath::AsFloat(RectPackedX), FMath::AsFloat(RectPackedY), FMath::AsFloat(RectPackedZ), FMath::AsFloat(VirtualShadowMapIdAndPrevLocalLightIndex));
}

static const uint32 NUM_PLANES_PER_RECT_LIGHT = 4;

static void CalculateRectLightCullingPlanes(const FRectLightSceneProxy* RectProxy, TArray<FPlane, TInlineAllocator<NUM_PLANES_PER_RECT_LIGHT>>& OutPlanes)
{
	const float BarnMaxAngle = GetRectLightBarnDoorMaxAngle();
	const float AngleRad = FMath::DegreesToRadians(FMath::Clamp(RectProxy->BarnDoorAngle, 0.f, BarnMaxAngle));

	// horizontal barn doors
	{
		float HorizontalBarnExtent;
		float HorizontalBarnDepth;
		CalculateRectLightCullingBarnExtentAndDepth(RectProxy->SourceWidth, RectProxy->BarnDoorLength, AngleRad, RectProxy->Radius, HorizontalBarnExtent, HorizontalBarnDepth);

		TStaticArray<FVector, 8> Corners;
		CalculateRectLightBarnCorners(RectProxy->SourceWidth, RectProxy->SourceHeight, HorizontalBarnExtent, HorizontalBarnDepth, Corners);

		OutPlanes.Add(FPlane(Corners[1], Corners[0], Corners[3])); // right
		OutPlanes.Add(FPlane(Corners[5], Corners[7], Corners[4])); // left
	}
	
	// vertical barn doors
	{
		float VerticalBarnExtent;
		float VerticalBarnDepth;
		CalculateRectLightCullingBarnExtentAndDepth(RectProxy->SourceHeight, RectProxy->BarnDoorLength, AngleRad, RectProxy->Radius, VerticalBarnExtent, VerticalBarnDepth);

		TStaticArray<FVector, 8> Corners;
		CalculateRectLightBarnCorners(RectProxy->SourceWidth, RectProxy->SourceHeight, VerticalBarnExtent, VerticalBarnDepth, Corners);

		OutPlanes.Add(FPlane(Corners[4], Corners[6], Corners[0])); // top
		OutPlanes.Add(FPlane(Corners[1], Corners[3], Corners[5])); // bottom
	}

	check(OutPlanes.Num() == NUM_PLANES_PER_RECT_LIGHT);
}

struct FLightGrid
{
	FRDGBufferSRVRef CulledLightDataGridSRV = nullptr;
	FRDGBufferSRVRef NumCulledLightsGridSRV = nullptr;
};

FLightGrid LightGridInjection(
	FRDGBuilder& GraphBuilder,
	FViewInfo& View,
	FIntVector GridSize,
	uint32 LightGridPixelSizeShift,
	uint32 ZSliceScale,
	uint32 MaxNumCells,
	FVector3f ZParams,
	uint32 LightGridCullMarginXY,
	uint32 LightGridCullMarginZ,
	FVector3f LightGridCullMarginZParams,
	uint32 LightGridCullMaxZ,
	uint32 NumLocalLights,
	uint32 NumReflectionCaptures,
	uint32 MegaLightsSupportedStartIndex,
	bool bUse16BitBuffers,
	bool bRefineRectLightBounds,
	FRDGBufferSRVRef LightViewSpacePositionAndRadiusSRV,
	FRDGBufferSRVRef LightViewSpaceDirAndPreprocAngleSRV,
	FRDGBufferSRVRef LightViewSpaceRectPlanesSRV,
	FLightGridViewState* LightGridViewState,
	bool bThreadGroupPerCell,
	bool bThreadGroupSize32,
	// parent params
	FRDGBufferSRVRef ParentNumCulledLightsGridSRV,
	FRDGBufferSRVRef ParentCulledLightDataGridSRV,
	uint32 ParentGridSizeFactor)
{
	const uint32 NumCulledLightEntries = MaxNumCells * GMaxCulledLightsPerCell;

	uint32 NumCulledLightLinks = MaxNumCells * GMaxCulledLightsPerCell;
	
	if (bThreadGroupPerCell)
	{
		ensureMsgf(NumLocalLights <= LIGHT_GRID_CELL_WRITER_MAX_NUM_PRIMITIVES, TEXT("NumLocalLights limited to 16M by FCellWriter."));
		ensureMsgf(NumReflectionCaptures <= LIGHT_GRID_CELL_WRITER_MAX_NUM_PRIMITIVES, TEXT("NumLocalLights limited to 16M by FCellWriter."));

		NumCulledLightLinks = FMath::Min(NumCulledLightLinks, (uint32)LIGHT_GRID_CELL_WRITER_MAX_NUM_LINKS); // limited to 16M by FCellWriter (will cause warning if exceeded, see FLightGridViewState::Update())
	}

	const FIntVector ParentGridSize = FIntVector::DivideAndRoundUp(GridSize, ParentGridSizeFactor);

	FRDGBufferRef CulledLightLinksBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32), NumCulledLightLinks * LightLinkStride), TEXT("CulledLightLinks"));
	FRDGBufferRef NextCulledLightLinkBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32), 1), TEXT("NextCulledLightLink"));
	FRDGBufferRef NextCulledLightDataBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32), 1), TEXT("NextCulledLightData"));
	FRDGBufferUAVRef NextCulledLightDataUAV = GraphBuilder.CreateUAV(NextCulledLightDataBuffer);
	FRDGBufferRef NumCulledLightsGrid = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32), MaxNumCells * NumCulledLightsGridStride), TEXT("NumCulledLightsGrid"));
	FRDGBufferUAVRef NumCulledLightsGridUAV = GraphBuilder.CreateUAV(NumCulledLightsGrid);

	FRDGBufferSRVRef CulledLightDataGridSRV;
	FRDGBufferUAVRef CulledLightDataGridUAV;
	if (bUse16BitBuffers)
	{
		const SIZE_T LightIndexTypeSize = sizeof(FLightIndexType);
		const EPixelFormat CulledLightDataGridFormat = PF_R16_UINT;
		FRDGBufferRef CulledLightDataGrid = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateBufferDesc(LightIndexTypeSize, NumCulledLightEntries), TEXT("CulledLightDataGrid"));
		CulledLightDataGridSRV = GraphBuilder.CreateSRV(CulledLightDataGrid, CulledLightDataGridFormat);
		CulledLightDataGridUAV = GraphBuilder.CreateUAV(CulledLightDataGrid, CulledLightDataGridFormat);
	}
	else
	{
		const SIZE_T LightIndexTypeSize = sizeof(FLightIndexType32);
		const EPixelFormat CulledLightDataGridFormat = PF_R32_UINT;
		FRDGBufferRef CulledLightDataGrid = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(LightIndexTypeSize, NumCulledLightEntries), TEXT("CulledLightDataGrid"));
		CulledLightDataGridSRV = GraphBuilder.CreateSRV(CulledLightDataGrid);
		CulledLightDataGridUAV = GraphBuilder.CreateUAV(CulledLightDataGrid);
	}

	FLightGridInjectionCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FLightGridInjectionCS::FParameters>();

	PassParameters->View = View.ViewUniformBuffer;

	if (IsMobilePlatform(View.GetShaderPlatform()))
	{
		PassParameters->MobileReflectionCaptureData = View.MobileReflectionCaptureUniformBuffer;
	}
	else
	{
		PassParameters->ReflectionCapture = View.ReflectionCaptureUniformBuffer;
	}

	PassParameters->RWNumCulledLightsGrid = NumCulledLightsGridUAV;
	PassParameters->RWCulledLightDataGrid32Bit = CulledLightDataGridUAV;
	PassParameters->RWCulledLightDataGrid16Bit = CulledLightDataGridUAV;
	PassParameters->RWNextCulledLightLink = GraphBuilder.CreateUAV(NextCulledLightLinkBuffer);
	PassParameters->RWNextCulledLightData = NextCulledLightDataUAV;
	PassParameters->RWCulledLightLinks = GraphBuilder.CreateUAV(CulledLightLinksBuffer);
	PassParameters->CulledGridSize = GridSize;
	PassParameters->LightGridZParams = ZParams;
	PassParameters->NumReflectionCaptures = NumReflectionCaptures;
	PassParameters->NumLocalLights = NumLocalLights;
	PassParameters->MaxCulledLightsPerCell = GMaxCulledLightsPerCell;
	PassParameters->NumAvailableLinks = NumCulledLightLinks;
	PassParameters->NumGridCells = GridSize.X * GridSize.Y * GridSize.Z;
	PassParameters->LightGridPixelSizeShift = LightGridPixelSizeShift;
	PassParameters->LightGridZSliceScale = ZSliceScale;
	PassParameters->LightGridCullMarginXY = LightGridCullMarginXY;
	PassParameters->LightGridCullMarginZ = LightGridCullMarginZ;
	PassParameters->LightGridCullMarginZParams = LightGridCullMarginZParams;
	PassParameters->LightGridCullMaxZ = LightGridCullMaxZ;
	PassParameters->MegaLightsSupportedStartIndex = MegaLightsSupportedStartIndex;

	PassParameters->ParentNumCulledLightsGrid = ParentNumCulledLightsGridSRV;
	PassParameters->ParentCulledLightDataGrid32Bit = ParentCulledLightDataGridSRV;
	PassParameters->ParentCulledLightDataGrid16Bit = ParentCulledLightDataGridSRV;
	PassParameters->ParentGridSize = ParentGridSize;
	PassParameters->NumParentGridCells = ParentGridSize.X * ParentGridSize.Y * ParentGridSize.Z;
	PassParameters->ParentGridSizeFactor = ParentGridSizeFactor;

	PassParameters->LightViewSpacePositionAndRadius = LightViewSpacePositionAndRadiusSRV;
	PassParameters->LightViewSpaceDirAndPreprocAngle = LightViewSpaceDirAndPreprocAngleSRV;
	PassParameters->LightViewSpaceRectPlanes = LightViewSpaceRectPlanesSRV;

	{
		PassParameters->HZBTexture = View.HZB;
		PassParameters->HZBSampler = TStaticSamplerState< SF_Point, AM_Clamp, AM_Clamp, AM_Clamp >::GetRHI();
		PassParameters->HZBSize = FVector2f(View.HZBMipmap0Size);
		PassParameters->HZBViewSize = FVector2f(View.ViewRect.Size());
		PassParameters->HZBViewRect = FIntRect(0, 0, View.ViewRect.Width(), View.ViewRect.Height());
	}

	FLightGridInjectionCS::FPermutationDomain PermutationVector;
	PermutationVector.Set<FLightGridInjectionCS::FUseLinkedList>(GLightLinkedListCulling != 0);
	PermutationVector.Set<FLightGridInjectionCS::FRefineRectLightBounds>(bRefineRectLightBounds);
	PermutationVector.Set<FLightGridInjectionCS::FUseHZBCull>(GLightGridHZBCull != 0 && View.HZB != nullptr);
	PermutationVector.Set<FLightGridInjectionCS::FUseParentLightGrid>(ParentNumCulledLightsGridSRV != nullptr && ParentCulledLightDataGridSRV != nullptr);
	PermutationVector.Set<FLightGridInjectionCS::FUseThreadGroupPerCell>(bThreadGroupPerCell);
	PermutationVector.Set<FLightGridInjectionCS::FUseThreadGroupSize32>(bThreadGroupSize32);
	auto ComputeShader = View.ShaderMap->GetShader<FLightGridInjectionCS>(PermutationVector);

	AddClearUAVPass(GraphBuilder, PassParameters->RWNextCulledLightLink, 0);
	AddClearUAVPass(GraphBuilder, NextCulledLightDataUAV, 0);
	AddClearUAVPass(GraphBuilder, NumCulledLightsGridUAV, 0);

	FIntVector NumGroups;
	if (bThreadGroupPerCell)
	{
		NumGroups = GridSize;
	}
	else
	{
		NumGroups = FComputeShaderUtils::GetGroupCount(GridSize, FLightGridInjectionCS::GetGroupSize(PermutationVector));
	}

	FComputeShaderUtils::AddPass(
		GraphBuilder,
		RDG_EVENT_NAME("LightGridInject %s %s",
			PermutationVector.Get<FLightGridInjectionCS::FUseLinkedList>() ? TEXT("LinkedList") : TEXT("NoLinkedList"),
			PermutationVector.Get<FLightGridInjectionCS::FUseThreadGroupPerCell>() ? TEXT("ThreadGroup") : TEXT("SingleThread")),
		ComputeShader,
		PassParameters,
		NumGroups);

	FLightGrid Output;
	Output.CulledLightDataGridSRV = CulledLightDataGridSRV;
	Output.NumCulledLightsGridSRV = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(NumCulledLightsGrid));

	if (LightGridViewState != nullptr)
	{
		LightGridViewState->FeedbackStatus(GraphBuilder, View, NextCulledLightDataBuffer, NumCulledLightEntries, NextCulledLightLinkBuffer, NumCulledLightLinks);
	}

	return Output;
}

FComputeLightGridOutput FSceneRenderer::ComputeLightGrid(FRDGBuilder& GraphBuilder, bool bCullLightsToGrid, const FSortedLightSetSceneInfo& SortedLightSet)
{
	FComputeLightGridOutput Result = {};

	RDG_CSV_STAT_EXCLUSIVE_SCOPE(GraphBuilder, ComputeLightGrid);
	QUICK_SCOPE_CYCLE_COUNTER(STAT_ComputeLightGrid);
	RDG_EVENT_SCOPE(GraphBuilder, "ComputeLightGrid");

	const bool bAllowStaticLighting = IsStaticLightingAllowed();
	const bool bLightGridUses16BitBuffers = LightGridUses16BitBuffers(ShaderPlatform);
	const bool bRenderRectLightsAsSpotLights = RenderRectLightsAsSpotLights(FeatureLevel);

	const FRDGSystemTextures& SystemTextures = FRDGSystemTextures::Get(GraphBuilder);

#if WITH_EDITOR
	bool bMultipleDirLightsConflictForForwardShading = false;
#endif

	for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
	{
		FViewInfo& View = Views[ViewIndex];

		View.ForwardLightingResources.SelectedForwardDirectionalLightProxy = nullptr;

		FForwardLightData* ForwardLightData = GraphBuilder.AllocParameters<FForwardLightData>();
		ForwardLightData->DirectionalLightShadowmapAtlas = SystemTextures.Black;
		ForwardLightData->DirectionalLightStaticShadowmap = GBlackTexture->TextureRHI;

		TArray<FForwardLocalLightData, SceneRenderingAllocator> ForwardLocalLightData;
		TArray<int32, SceneRenderingAllocator>  LocalLightVisibleLightInfosIndex;

		TArray<FVector4f, SceneRenderingAllocator> ViewSpacePosAndRadiusData;
		TArray<FVector4f, SceneRenderingAllocator> ViewSpaceDirAndPreprocAngleData;
		TArray<FVector4f, SceneRenderingAllocator> ViewSpaceRectPlanesData;

		float FurthestLight = 1000;

		int32 ConflictingLightCountForForwardShading = 0;

		// Track the end markers for different types
		int32 SimpleLightsEnd = 0;
		int32 ClusteredSupportedEnd = 0;
		int32 MegaLightsSupportedStart = 0;

		bool bHasRectLights = false;
		bool bHasTexturedLights = false;

		const float Exposure = View.GetLastEyeAdaptationExposure();

		if (bCullLightsToGrid)
		{
			// Simple lights are copied without view dependent checks, so same in and out
			SimpleLightsEnd = SortedLightSet.SimpleLightsEnd;
			// 1. insert simple lights
			if (SimpleLightsEnd > 0)
			{
				ForwardLocalLightData.Reserve(SimpleLightsEnd);
				LocalLightVisibleLightInfosIndex.Reserve(SimpleLightsEnd);

				ViewSpacePosAndRadiusData.Reserve(SimpleLightsEnd);
				ViewSpaceDirAndPreprocAngleData.Reserve(SimpleLightsEnd);
				ViewSpaceRectPlanesData.Reserve(SimpleLightsEnd * NUM_PLANES_PER_RECT_LIGHT);

				const FSimpleLightArray& SimpleLights = SortedLightSet.SimpleLights;

				// Pack both values into a single float to keep float4 alignment
				const FFloat16 SimpleLightSourceLength16f = FFloat16(0);
				FLightingChannels SimpleLightLightingChannels;

				// Put simple lights in all lighting channels
				SimpleLightLightingChannels.bChannel0 = SimpleLightLightingChannels.bChannel1 = SimpleLightLightingChannels.bChannel2 = true;
				const uint32 SimpleLightLightingChannelMask = GetLightingChannelMaskForStruct(SimpleLightLightingChannels);

				// Now using the sorted lights, and keep track of ranges as we go.
				for (int32 SortedIndex = 0; SortedIndex < SimpleLightsEnd; ++SortedIndex)
				{
					check(SortedLightSet.SortedLights[SortedIndex].LightSceneInfo == nullptr);
					check(!SortedLightSet.SortedLights[SortedIndex].SortKey.Fields.bIsNotSimpleLight);


					int32 SimpleLightIndex = SortedLightSet.SortedLights[SortedIndex].SimpleLightIndex;

					ForwardLocalLightData.AddUninitialized(1);
					FForwardLocalLightData& LightData = ForwardLocalLightData.Last();

					// Simple lights have no 'VisibleLight' info
					LocalLightVisibleLightInfosIndex.Add(INDEX_NONE);

					const FSimpleLightEntry& SimpleLight = SimpleLights.InstanceData[SimpleLightIndex];
					const FSimpleLightPerViewEntry& SimpleLightPerViewData = SimpleLights.GetViewDependentData(SimpleLightIndex, ViewIndex, Views.Num());
					PackLocalLightData(LightData, View, SimpleLight, SimpleLightPerViewData);

					FVector4f ViewSpacePosAndRadius(FVector4f(View.ViewMatrices.GetViewMatrix().TransformPosition(SimpleLightPerViewData.Position)), SimpleLight.Radius);
					ViewSpacePosAndRadiusData.Add(ViewSpacePosAndRadius);
					ViewSpaceDirAndPreprocAngleData.AddZeroed();
					ViewSpaceRectPlanesData.AddZeroed(NUM_PLANES_PER_RECT_LIGHT);
				}
			}

			const uint32 LightShaderParameterFlags = bRenderRectLightsAsSpotLights ? ELightShaderParameterFlags::RectAsSpotLight : 0u;
			float SelectedForwardDirectionalLightIntensitySq = 0.0f;
			int32 SelectedForwardDirectionalLightPriority = -1;
			const TArray<FSortedLightSceneInfo, SceneRenderingAllocator>& SortedLights = SortedLightSet.SortedLights;
			ClusteredSupportedEnd = SimpleLightsEnd;
			MegaLightsSupportedStart = MAX_int32;
			// Next add all the other lights, track the end index for clustered supporting lights
			for (int32 SortedIndex = SimpleLightsEnd; SortedIndex < SortedLights.Num(); ++SortedIndex)
			{
				const FSortedLightSceneInfo& SortedLightInfo = SortedLights[SortedIndex];
				const FLightSceneInfo* const LightSceneInfo = SortedLightInfo.LightSceneInfo;
				const FLightSceneProxy* LightProxy = LightSceneInfo->Proxy;

				if (LightSceneInfo->ShouldRenderLight(View))
				{
					FLightRenderParameters LightParameters;
					LightProxy->GetLightShaderParameters(LightParameters, LightShaderParameterFlags);

					if (LightProxy->IsInverseSquared())
					{
						LightParameters.FalloffExponent = 0;
					}

					// When rendering reflection captures, the direct lighting of the light is actually the indirect specular from the main view
					if (View.bIsReflectionCapture)
					{
						LightParameters.Color *= LightProxy->GetIndirectLightingScale();
					}

					uint32 LightTypeAndShadowMapChannelMaskPacked = LightSceneInfo->PackLightTypeAndShadowMapChannelMask(bAllowStaticLighting, SortedLightInfo.SortKey.Fields.bLightFunction);

					const bool bDynamicShadows = ViewFamily.EngineShowFlags.DynamicShadows && VisibleLightInfos.IsValidIndex(LightSceneInfo->Id);
					const int32 VirtualShadowMapId = bDynamicShadows ? VisibleLightInfos[LightSceneInfo->Id].GetVirtualShadowMapId( &View ) : INDEX_NONE;

					if ((SortedLightInfo.SortKey.Fields.LightType == LightType_Point && ViewFamily.EngineShowFlags.PointLights) ||
						(SortedLightInfo.SortKey.Fields.LightType == LightType_Spot && ViewFamily.EngineShowFlags.SpotLights) ||
						(SortedLightInfo.SortKey.Fields.LightType == LightType_Rect && ViewFamily.EngineShowFlags.RectLights))
					{
						int32 PrevLocalLightIndex = INDEX_NONE;
						if (View.ViewState)
						{
							PrevLocalLightIndex = View.ViewState->LightSceneIdToLocalLightIndex.FindOrAdd(LightSceneInfo->Id, INDEX_NONE);
							View.ViewState->LightSceneIdToLocalLightIndex[LightSceneInfo->Id] = ForwardLocalLightData.Num();
						}

						ForwardLocalLightData.AddUninitialized(1);
						FForwardLocalLightData& LightData = ForwardLocalLightData.Last();
						LocalLightVisibleLightInfosIndex.Add(LightSceneInfo->Id);

						// Track the last one to support clustered deferred
						if (!SortedLightInfo.SortKey.Fields.bClusteredDeferredNotSupported)
						{
							ClusteredSupportedEnd = FMath::Max(ClusteredSupportedEnd, ForwardLocalLightData.Num());
						}

						if (SortedLightInfo.SortKey.Fields.bHandledByMegaLights && MegaLightsSupportedStart == MAX_int32)
						{
							MegaLightsSupportedStart = ForwardLocalLightData.Num() - 1;
						}
						const float LightFade = GetLightFadeFactor(View, LightProxy);
						LightParameters.Color *= LightFade;
						LightParameters.Color *= LightParameters.GetLightExposureScale(Exposure);

						float VolumetricScatteringIntensity = LightProxy->GetVolumetricScatteringIntensity();
						if (LightNeedsSeparateInjectionIntoVolumetricFogForOpaqueShadow(View, LightSceneInfo, VisibleLightInfos[LightSceneInfo->Id], *Scene))
						{
							// Disable this lights forward shading volumetric scattering contribution
							VolumetricScatteringIntensity = 0;
						}

						PackLocalLightData(
							LightData,
							View,
							LightParameters,
							LightTypeAndShadowMapChannelMaskPacked,
							LightSceneInfo->Id,
							VirtualShadowMapId,
							PrevLocalLightIndex,
							VolumetricScatteringIntensity);

						const FSphere BoundingSphere = LightProxy->GetBoundingSphere();
						const float Distance = View.ViewMatrices.GetViewMatrix().TransformPosition(BoundingSphere.Center).Z + BoundingSphere.W;
						FurthestLight = FMath::Max(FurthestLight, Distance);

						const FVector3f LightViewPosition = FVector4f(View.ViewMatrices.GetViewMatrix().TransformPosition(LightParameters.WorldPosition)); // LWC_TODO: precision loss
						const FVector3f LightViewDirection = FVector4f(View.ViewMatrices.GetViewMatrix().TransformVector((FVector)LightParameters.Direction)); // LWC_TODO: precision loss

						// Note: inverting radius twice seems stupid (but done in shader anyway otherwise)
						FVector4f ViewSpacePosAndRadius(LightViewPosition, 1.0f / LightParameters.InvRadius);
						ViewSpacePosAndRadiusData.Add(ViewSpacePosAndRadius);

						const bool bIsRectLight = !bRenderRectLightsAsSpotLights && LightProxy->IsRectLight();
						const bool bUseTightRectLightCulling = bIsRectLight && LightParameters.RectLightBarnLength > 0.5f && LightParameters.RectLightBarnCosAngle > FMath::Cos(FMath::DegreesToRadians(GetRectLightBarnDoorMaxAngle()));

						// Pack flags in the LSB of PreProcAngle
						const float PreProcAngle = SortedLightInfo.SortKey.Fields.LightType == LightType_Spot ? GetTanRadAngleOrZero(LightProxy->GetOuterConeAngle()) : 0.0f;
						const uint32 PackedPreProcAngleAndFlags = (FMath::AsUInt(PreProcAngle) & 0xFFFFFFF8) | (LightProxy->HasSourceTexture() ? 0x4 : 0) | (bUseTightRectLightCulling ? 0x2 : 0) | (bIsRectLight ? 0x1 : 0);
						FVector4f ViewSpaceDirAndPreprocAngleAndFlags(LightViewDirection, FMath::AsFloat(PackedPreProcAngleAndFlags)); // LWC_TODO: precision loss
						ViewSpaceDirAndPreprocAngleData.Add(ViewSpaceDirAndPreprocAngleAndFlags);

						if (bUseTightRectLightCulling)
						{
							const FRectLightSceneProxy* RectProxy = (const FRectLightSceneProxy*)LightProxy;

							TArray<FPlane, TInlineAllocator<NUM_PLANES_PER_RECT_LIGHT>> Planes;

							CalculateRectLightCullingPlanes(RectProxy, Planes);

							for (FPlane& Plane : Planes)
							{
								const FPlane4f ViewPlane(Plane.TransformBy(LightProxy->GetLightToWorld() * View.ViewMatrices.GetViewMatrix()));
								ViewSpaceRectPlanesData.Add(FVector4f(FVector3f(ViewPlane), -ViewPlane.W));
							}
						}
						else
						{
							ViewSpaceRectPlanesData.AddZeroed(NUM_PLANES_PER_RECT_LIGHT);
						}

						bHasRectLights |= bIsRectLight;
						bHasTexturedLights |= LightProxy->HasSourceTexture();
					}
					// On mobile there is a separate FMobileDirectionalLightShaderParameters UB which holds all directional light data.
					else if (SortedLightInfo.SortKey.Fields.LightType == LightType_Directional && ViewFamily.EngineShowFlags.DirectionalLights && !IsMobilePlatform(View.GetShaderPlatform()))
					{
						// The selected forward directional light is also used for volumetric lighting using ForwardLightData UB.
						// Also some people noticed that depending on the order a two directional lights are made visible in a level, the selected light for volumetric fog lighting will be different.
						// So to be clear and avoid such issue, we select the most intense directional light for forward shading and volumetric lighting.
						const float LightIntensitySq = FVector3f(LightParameters.Color).SizeSquared();
						const int32 LightForwardShadingPriority = LightProxy->GetDirectionalLightForwardShadingPriority();
#if WITH_EDITOR
						if (LightForwardShadingPriority > SelectedForwardDirectionalLightPriority)
						{
							// Reset the count if the new light has a higher priority than the previous one.
							ConflictingLightCountForForwardShading = 1;
						}
						else if (LightForwardShadingPriority == SelectedForwardDirectionalLightPriority)
						{
							// Accumulate new light if also has the highest priority value.
							ConflictingLightCountForForwardShading++;
						}
#endif
						if (LightForwardShadingPriority > SelectedForwardDirectionalLightPriority
							|| (LightForwardShadingPriority == SelectedForwardDirectionalLightPriority && LightIntensitySq > SelectedForwardDirectionalLightIntensitySq))
						{

							SelectedForwardDirectionalLightPriority = LightForwardShadingPriority;
							SelectedForwardDirectionalLightIntensitySq = LightIntensitySq;
							View.ForwardLightingResources.SelectedForwardDirectionalLightProxy = LightProxy;

							ForwardLightData->HasDirectionalLight = 1;
							ForwardLightData->DirectionalLightColor = FVector3f(LightParameters.Color);
							ForwardLightData->DirectionalLightVolumetricScatteringIntensity = LightProxy->GetVolumetricScatteringIntensity();
							ForwardLightData->DirectionalLightSpecularScale = FMath::Clamp(LightProxy->GetSpecularScale(), 0.f, 1.f);
							ForwardLightData->DirectionalLightDiffuseScale = FMath::Clamp(LightProxy->GetDiffuseScale(), 0.f, 1.f);
							ForwardLightData->DirectionalLightDirection = LightParameters.Direction;
							ForwardLightData->DirectionalLightSourceRadius = LightParameters.SourceRadius;
							ForwardLightData->DirectionalLightSoftSourceRadius = LightParameters.SoftSourceRadius;
							ForwardLightData->DirectionalLightShadowMapChannelMask = LightTypeAndShadowMapChannelMaskPacked;
							ForwardLightData->DirectionalLightVSM = INDEX_NONE;
							ForwardLightData->LightFunctionAtlasLightIndex = LightParameters.LightFunctionAtlasLightIndex;
							ForwardLightData->bAffectsTranslucentLighting = LightParameters.bAffectsTranslucentLighting;

							const FVector2D FadeParams = LightProxy->GetDirectionalLightDistanceFadeParameters(View.GetFeatureLevel(), LightSceneInfo->IsPrecomputedLightingValid(), View.MaxShadowCascades);

							ForwardLightData->DirectionalLightDistanceFadeMAD = FVector2f(FadeParams.Y, -FadeParams.X * FadeParams.Y);	// LWC_TODO: Precision loss

							const FMatrix TranslatedWorldToWorld = FTranslationMatrix(-View.ViewMatrices.GetPreViewTranslation());

							if (bDynamicShadows)
							{
								const TArray<FProjectedShadowInfo*, SceneRenderingAllocator>& DirectionalLightShadowInfos = VisibleLightInfos[LightSceneInfo->Id].AllProjectedShadows;

								ForwardLightData->DirectionalLightVSM = VirtualShadowMapId;

								ForwardLightData->NumDirectionalLightCascades = 0;
								// Unused cascades should compare > all scene depths
								ForwardLightData->CascadeEndDepths = FVector4f(MAX_FLT, MAX_FLT, MAX_FLT, MAX_FLT);

								for (const FProjectedShadowInfo* ShadowInfo : DirectionalLightShadowInfos)
								{
									if (ShadowInfo->DependentView)
									{
										// when rendering stereo views, allow using the shadows rendered for the primary view as 'close enough'
										if (ShadowInfo->DependentView != &View && ShadowInfo->DependentView != View.GetPrimaryView())
										{
											continue;
										}
									}

									const int32 CascadeIndex = ShadowInfo->CascadeSettings.ShadowSplitIndex;

									if (ShadowInfo->IsWholeSceneDirectionalShadow() && !ShadowInfo->HasVirtualShadowMap() && ShadowInfo->bAllocated && CascadeIndex < GMaxForwardShadowCascades)
									{
										const FMatrix WorldToShadow = ShadowInfo->GetWorldToShadowMatrix(ForwardLightData->DirectionalLightShadowmapMinMax[CascadeIndex]);
										const FMatrix44f TranslatedWorldToShadow = FMatrix44f(TranslatedWorldToWorld * WorldToShadow);

										ForwardLightData->NumDirectionalLightCascades++;
										ForwardLightData->DirectionalLightTranslatedWorldToShadowMatrix[CascadeIndex] = TranslatedWorldToShadow;
										ForwardLightData->CascadeEndDepths[CascadeIndex] = ShadowInfo->CascadeSettings.SplitFar;

										if (CascadeIndex == 0)
										{
											ForwardLightData->DirectionalLightShadowmapAtlas = GraphBuilder.RegisterExternalTexture(ShadowInfo->RenderTargets.DepthTarget);
											ForwardLightData->DirectionalLightDepthBias = ShadowInfo->GetShaderDepthBias();
											FVector2D AtlasSize = ForwardLightData->DirectionalLightShadowmapAtlas->Desc.Extent;
											ForwardLightData->DirectionalLightShadowmapAtlasBufferSize = FVector4f(AtlasSize.X, AtlasSize.Y, 1.0f / AtlasSize.X, 1.0f / AtlasSize.Y);
										}
									}
								}
							}

							const FStaticShadowDepthMap* StaticShadowDepthMap = LightSceneInfo->Proxy->GetStaticShadowDepthMap();
							const uint32 bStaticallyShadowedValue = LightSceneInfo->IsPrecomputedLightingValid() 
																	&& StaticShadowDepthMap 
																	&& StaticShadowDepthMap->Data 
																	&& !StaticShadowDepthMap->Data->WorldToLight.ContainsNaN()
																	&& StaticShadowDepthMap->TextureRHI ? 1 : 0;
							ForwardLightData->DirectionalLightUseStaticShadowing = bStaticallyShadowedValue;
							if (bStaticallyShadowedValue)
							{
								const FMatrix44f TranslatedWorldToShadow = FMatrix44f(TranslatedWorldToWorld * StaticShadowDepthMap->Data->WorldToLight);
								ForwardLightData->DirectionalLightStaticShadowBufferSize = FVector4f(StaticShadowDepthMap->Data->ShadowMapSizeX, StaticShadowDepthMap->Data->ShadowMapSizeY, 1.0f / StaticShadowDepthMap->Data->ShadowMapSizeX, 1.0f / StaticShadowDepthMap->Data->ShadowMapSizeY);
								ForwardLightData->DirectionalLightTranslatedWorldToStaticShadow = TranslatedWorldToShadow;
								ForwardLightData->DirectionalLightStaticShadowmap = StaticShadowDepthMap->TextureRHI;
							}
							else
							{
								ForwardLightData->DirectionalLightStaticShadowBufferSize = FVector4f(0, 0, 0, 0);
								ForwardLightData->DirectionalLightTranslatedWorldToStaticShadow = FMatrix44f::Identity;
								ForwardLightData->DirectionalLightStaticShadowmap = GWhiteTexture->TextureRHI;
							}
						}
					}
				}
			}
		}

#if WITH_EDITOR
		// For any views, if there are more than two light that compete for the forward shaded light, we report it.
		bMultipleDirLightsConflictForForwardShading |= ConflictingLightCountForForwardShading >= 2;
#endif

		// Store off the number of lights before we add a fake entry
		const int32 NumLocalLightsFinal = ForwardLocalLightData.Num();

		// Some platforms index the StructuredBuffer in the shader based on the stride specified at buffer creation time, not from the stride specified in the shader.
		// ForwardLocalLightBuffer is a StructuredBuffer<float4> in the shader, so create the buffer with a stride of sizeof(float4)
		static_assert(sizeof(FForwardLocalLightData) % sizeof(FVector4f) == 0, "ForwardLocalLightBuffer is used as a StructuredBuffer<float4> in the shader");
		const uint32 ForwardLocalLightDataSizeNumFloat4 = (NumLocalLightsFinal * sizeof(FForwardLocalLightData)) / sizeof(FVector4f);

		FRDGBufferRef ForwardLocalLightBuffer = CreateStructuredBuffer(GraphBuilder, TEXT("ForwardLocalLightBuffer"),
			TConstArrayView<FVector4f>(reinterpret_cast<const FVector4f*>(ForwardLocalLightData.GetData()), ForwardLocalLightDataSizeNumFloat4));

		View.ForwardLightingResources.LocalLightVisibleLightInfosIndex = LocalLightVisibleLightInfosIndex;

		View.bLightGridHasRectLights = bHasRectLights;
		View.bLightGridHasTexturedLights = bHasTexturedLights;

		const FIntPoint LightGridSizeXY = FIntPoint::DivideAndRoundUp(View.ViewRect.Size(), GLightGridPixelSize);
		ForwardLightData->ForwardLocalLightBuffer = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(ForwardLocalLightBuffer));
		ForwardLightData->NumLocalLights = NumLocalLightsFinal;
		ForwardLightData->NumReflectionCaptures = View.NumBoxReflectionCaptures + View.NumSphereReflectionCaptures;
		ForwardLightData->NumGridCells = LightGridSizeXY.X * LightGridSizeXY.Y * GLightGridSizeZ;
		ForwardLightData->CulledGridSize = FIntVector(LightGridSizeXY.X, LightGridSizeXY.Y, GLightGridSizeZ);
		ForwardLightData->MaxCulledLightsPerCell = GLightLinkedListCulling ? NumLocalLightsFinal: GMaxCulledLightsPerCell;
		ForwardLightData->LightGridPixelSizeShift = FMath::FloorLog2(GLightGridPixelSize);
		ForwardLightData->SimpleLightsEndIndex = SimpleLightsEnd;
		ForwardLightData->ClusteredDeferredSupportedEndIndex = ClusteredSupportedEnd;
		ForwardLightData->MegaLightsSupportedStartIndex = FMath::Min<int32>(MegaLightsSupportedStart, NumLocalLightsFinal);
		ForwardLightData->DirectLightingShowFlag = ViewFamily.EngineShowFlags.DirectLighting ? 1 : 0;

		// Clamp far plane to something reasonable
		const float KilometersToCentimeters = 100000.0f;
		const float LightCullingMaxDistance = GLightCullingMaxDistanceOverrideKilometers <= 0.0f ? (float)UE_OLD_HALF_WORLD_MAX / 5.0f : GLightCullingMaxDistanceOverrideKilometers * KilometersToCentimeters;
		float FarPlane = FMath::Min(FMath::Max(FurthestLight, View.FurthestReflectionCaptureDistance), LightCullingMaxDistance);
		FVector ZParams = GetLightGridZParams(View.NearClippingDistance, FarPlane + 10.f);
		ForwardLightData->LightGridZParams = (FVector3f)ZParams;

		const uint64 NumIndexableLights = !bLightGridUses16BitBuffers ? (1llu << (sizeof(FLightIndexType32) * 8llu)) : (1llu << (sizeof(FLightIndexType) * 8llu));

		if ((uint64)ForwardLocalLightData.Num() > NumIndexableLights)
		{
			static bool bWarned = false;

			if (!bWarned)
			{
				UE_LOG(LogRenderer, Warning, TEXT("Exceeded indexable light count, glitches will be visible (%u / %llu)"), ForwardLocalLightData.Num(), NumIndexableLights);
				bWarned = true;
			}
		}

		check(ViewSpacePosAndRadiusData.Num() == ForwardLocalLightData.Num());
		check(ViewSpaceDirAndPreprocAngleData.Num() == ForwardLocalLightData.Num());
		check(ViewSpaceRectPlanesData.Num() == ForwardLocalLightData.Num() * NUM_PLANES_PER_RECT_LIGHT);

		FRDGBufferRef LightViewSpacePositionAndRadius = CreateStructuredBuffer(GraphBuilder, TEXT("ViewSpacePosAndRadiusData"), TConstArrayView<FVector4f>(ViewSpacePosAndRadiusData));
		FRDGBufferRef LightViewSpaceDirAndPreprocAngle = CreateStructuredBuffer(GraphBuilder, TEXT("ViewSpaceDirAndPreprocAngleData"), TConstArrayView<FVector4f>(ViewSpaceDirAndPreprocAngleData));
		FRDGBufferRef LightViewSpaceRectPlanes = CreateStructuredBuffer(GraphBuilder, TEXT("ViewSpaceRectPlanesData"), TConstArrayView<FVector4f>(ViewSpaceRectPlanesData));

		FRDGBufferSRVRef LightViewSpacePositionAndRadiusSRV = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(LightViewSpacePositionAndRadius));
		FRDGBufferSRVRef LightViewSpaceDirAndPreprocAngleSRV = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(LightViewSpaceDirAndPreprocAngle));
		FRDGBufferSRVRef LightViewSpaceRectPlanesSRV = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(LightViewSpaceRectPlanes));

		// Allocate buffers using the scene render targets size so we won't reallocate every frame with dynamic resolution
		const FIntPoint MaxLightGridSizeXY = FIntPoint::DivideAndRoundUp(View.GetSceneTexturesConfig().Extent, GLightGridPixelSize);

		const int32 MaxNumCells = MaxLightGridSizeXY.X * MaxLightGridSizeXY.Y * GLightGridSizeZ * NumCulledGridPrimitiveTypes;

		uint32 LightGridCullMarginXY = MegaLights::IsEnabled(ViewFamily) ? MegaLights::GetSampleMargin() : 0;
		uint32 LightGridCullMarginZ = 0;
		FVector3f LightGridCullMarginZParams = FVector3f::ZeroVector;
		uint32 LightGridCullMaxZ = 0;
		if (ShouldRenderVolumetricFog())
		{
			uint32 MarginInVolumetricFogGridCells = 1 + (MegaLights::IsEnabled(ViewFamily) && MegaLights::UseVolume() ? MegaLights::GetSampleMargin() : 0);
			LightGridCullMarginXY = MarginInVolumetricFogGridCells * GetVolumetricFogGridPixelSize();
			LightGridCullMarginZ = MarginInVolumetricFogGridCells;

			FVolumetricFogGlobalData VolumetricFogParamaters;
			SetupVolumetricFogGlobalData(View, VolumetricFogParamaters);
			LightGridCullMarginZParams = VolumetricFogParamaters.GridZParams;
			LightGridCullMaxZ = VolumetricFogParamaters.ViewGridSize.Z;
		}

		RDG_EVENT_SCOPE(GraphBuilder, "CullLights %ux%ux%u NumLights %u NumCaptures %u",
			ForwardLightData->CulledGridSize.X,
			ForwardLightData->CulledGridSize.Y,
			ForwardLightData->CulledGridSize.Z,
			ForwardLightData->NumLocalLights,
			ForwardLightData->NumReflectionCaptures);

		FLightGrid ParentLightGrid;
		uint32 ParentLightGridFactor = 1;

		if(CVarLightCullingTwoLevel.GetValueOnRenderThread() && (int32)ForwardLightData->NumLocalLights > CVarLightCullingTwoLevelThreshold.GetValueOnRenderThread())
		{
			ParentLightGridFactor = (uint32)FMath::Pow(2.0f, FMath::Clamp(CVarLightCullingTwoLevelExponent.GetValueOnRenderThread(), 1, 4));

			FIntVector ParentLightGridSize = FIntVector::DivideAndRoundUp(ForwardLightData->CulledGridSize, ParentLightGridFactor);

			ParentLightGrid = LightGridInjection(
				GraphBuilder,
				View,
				ParentLightGridSize,
				FMath::FloorLog2(GLightGridPixelSize * ParentLightGridFactor),
				ParentLightGridFactor,
				MaxNumCells, // TODO: could potentially be reduced on coarse grid
				ForwardLightData->LightGridZParams,
				LightGridCullMarginXY,
				LightGridCullMarginZ,
				LightGridCullMarginZParams,
				LightGridCullMaxZ,
				ForwardLightData->NumLocalLights,
				ForwardLightData->NumReflectionCaptures,
				ForwardLightData->MegaLightsSupportedStartIndex,
				bLightGridUses16BitBuffers,
				bHasRectLights && (GLightGridRefineRectLightBounds != 0),
				LightViewSpacePositionAndRadiusSRV,
				LightViewSpaceDirAndPreprocAngleSRV,
				LightViewSpaceRectPlanesSRV,
				View.ViewState != nullptr ? &View.ViewState->LightGrid : nullptr,
				/*bThreadGroupPerCell*/ true,
				/*bThreadGroupSize32*/ false,
				nullptr,
				nullptr,
				1);
		}

		const int32 WorkloadDistributionMode = CVarLightCullingWorkloadDistributionMode.GetValueOnRenderThread();

		uint32 NumThreadsPerCell = 1;
		
		if (WorkloadDistributionMode == 1) // thread group per cell (64 threads)
		{
			NumThreadsPerCell = 64;
		}
		else if (WorkloadDistributionMode == 2 && GRHIMinimumWaveSize <= 32) // thread group per cell (32 threads if supported, otherwise single thread).
		{
			NumThreadsPerCell = 32;
		}

		FLightGrid LightGrid = LightGridInjection(
			GraphBuilder,
			View,
			ForwardLightData->CulledGridSize,
			ForwardLightData->LightGridPixelSizeShift,
			1,
			MaxNumCells,
			ForwardLightData->LightGridZParams,
			LightGridCullMarginXY,
			LightGridCullMarginZ,
			LightGridCullMarginZParams,
			LightGridCullMaxZ,
			ForwardLightData->NumLocalLights,
			ForwardLightData->NumReflectionCaptures,
			ForwardLightData->MegaLightsSupportedStartIndex,
			bLightGridUses16BitBuffers,
			bHasRectLights && (GLightGridRefineRectLightBounds != 0),
			LightViewSpacePositionAndRadiusSRV,
			LightViewSpaceDirAndPreprocAngleSRV,
			LightViewSpaceRectPlanesSRV,
			View.ViewState != nullptr ? &View.ViewState->LightGrid : nullptr,
			NumThreadsPerCell > 1,
			NumThreadsPerCell == 32,
			ParentLightGrid.NumCulledLightsGridSRV,
			ParentLightGrid.CulledLightDataGridSRV,
			ParentLightGridFactor);

		ForwardLightData->CulledLightDataGrid32Bit = LightGrid.CulledLightDataGridSRV;
		ForwardLightData->CulledLightDataGrid16Bit = LightGrid.CulledLightDataGridSRV;
		ForwardLightData->NumCulledLightsGrid = LightGrid.NumCulledLightsGridSRV;
		View.ForwardLightingResources.SetUniformBuffer(GraphBuilder.CreateUniformBuffer(ForwardLightData));
	}

#if WITH_EDITOR
	if (bMultipleDirLightsConflictForForwardShading)
	{
		OnGetOnScreenMessages.AddLambda([](FScreenMessageWriter& ScreenMessageWriter)->void
		{
			static const FText Message = NSLOCTEXT("Renderer", "MultipleDirLightsConflictForForwardShading", "Multiple directional lights are competing to be the single one used for forward shading, translucent, water or volumetric fog. Please adjust their ForwardShadingPriority.\nAs a fallback, the main directional light will be selected based on overall brightness.");
			ScreenMessageWriter.DrawLine(Message, 10, FColor::Orange);
		});
	}
#endif

	return Result;
}

FComputeLightGridOutput FDeferredShadingSceneRenderer::GatherLightsAndComputeLightGrid(FRDGBuilder& GraphBuilder, bool bNeedLightGrid, const FSortedLightSetSceneInfo& SortedLightSet)
{
	SCOPED_NAMED_EVENT(GatherLightsAndComputeLightGrid, FColor::Emerald);
	FComputeLightGridOutput Result = {};

	if (!bNeedLightGrid)
	{
		SetDummyForwardLightUniformBufferOnViews(GraphBuilder, ShaderPlatform, Views);
		return Result;
	}

	bool bAnyViewUsesForwardLighting = false;
	bool bAnyViewUsesLumen = false;
	bool bAnyViewUsesRayTracing = false;
	for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
	{
		const FViewInfo& View = Views[ViewIndex];
		bAnyViewUsesForwardLighting |= View.bTranslucentSurfaceLighting || ShouldRenderVolumetricFog() || View.bHasSingleLayerWaterMaterial 
			|| VolumetricCloudWantsToSampleLocalLights(Scene, ViewFamily.EngineShowFlags) || ShouldVisualizeLightGrid() || ShouldRenderLocalFogVolume(Scene, ViewFamily);
		bAnyViewUsesLumen |= GetViewPipelineState(View).DiffuseIndirectMethod == EDiffuseIndirectMethod::Lumen || GetViewPipelineState(View).ReflectionsMethod == EReflectionsMethod::Lumen;
		bAnyViewUsesRayTracing |= IsRayTracingEnabled() && View.IsRayTracingAllowedForView();
	}
	
	const bool bCullLightsToGrid = GLightCullingQuality 
		&& (IsForwardShadingEnabled(ShaderPlatform) || bAnyViewUsesForwardLighting || bAnyViewUsesRayTracing || ShouldUseClusteredDeferredShading() ||
			bAnyViewUsesLumen || ViewFamily.EngineShowFlags.VisualizeMeshDistanceFields || VirtualShadowMapArray.IsEnabled() || MegaLights::IsEnabled(ViewFamily));

	// Store this flag if lights are injected in the grids, check with 'AreLightsInLightGrid()'
	bAreLightsInLightGrid = bCullLightsToGrid;
	
	Result = ComputeLightGrid(GraphBuilder, bCullLightsToGrid, SortedLightSet);

	return Result;
}

void FDeferredShadingSceneRenderer::RenderForwardShadowProjections(
	FRDGBuilder& GraphBuilder,
	const FMinimalSceneTextures& SceneTextures,
	FRDGTextureRef& OutForwardScreenSpaceShadowMask,
	FRDGTextureRef& OutForwardScreenSpaceShadowMaskSubPixel)
{
	CheckShadowDepthRenderCompleted();

	const bool bIsHairEnable = HairStrands::HasViewHairStrandsData(Views);
	bool bScreenShadowMaskNeeded = false;

	FRDGTextureRef SceneDepthTexture = SceneTextures.Depth.Target;

	for (auto LightIt = Scene->Lights.CreateConstIterator(); LightIt; ++LightIt)
	{
		const FLightSceneInfoCompact& LightSceneInfoCompact = *LightIt;
		const FLightSceneInfo* const LightSceneInfo = LightSceneInfoCompact.LightSceneInfo;
		const FVisibleLightInfo& VisibleLightInfo = VisibleLightInfos[LightSceneInfo->Id];

		bScreenShadowMaskNeeded |= VisibleLightInfo.ShadowsToProject.Num() > 0 || VisibleLightInfo.CapsuleShadowsToProject.Num() > 0 || LightSceneInfo->Proxy->GetLightFunctionMaterial() != nullptr;
	}

	if (bScreenShadowMaskNeeded)
	{
		RDG_CSV_STAT_EXCLUSIVE_SCOPE(GraphBuilder, RenderForwardShadingShadowProjections);

		FRDGTextureMSAA ForwardScreenSpaceShadowMask;
		FRDGTextureMSAA ForwardScreenSpaceShadowMaskSubPixel;

		{
			FRDGTextureDesc Desc(FRDGTextureDesc::Create2D(SceneTextures.Config.Extent, PF_B8G8R8A8, FClearValueBinding::White, TexCreate_RenderTargetable | TexCreate_ShaderResource));
			Desc.NumSamples = SceneDepthTexture->Desc.NumSamples;
			ForwardScreenSpaceShadowMask = CreateTextureMSAA(GraphBuilder, Desc, TEXT("ShadowMaskTextureMS"), TEXT("ShadowMaskTexture"), GFastVRamConfig.ScreenSpaceShadowMask);
			if (bIsHairEnable)
			{
				Desc.NumSamples = 1;
				ForwardScreenSpaceShadowMaskSubPixel = CreateTextureMSAA(GraphBuilder, Desc, TEXT("ShadowMaskSubPixelTextureMS"), TEXT("ShadowMaskSubPixelTexture"), GFastVRamConfig.ScreenSpaceShadowMask);
			}
		}

		RDG_EVENT_SCOPE_STAT(GraphBuilder, ShadowProjection, "ShadowProjectionOnOpaque");
		RDG_GPU_STAT_SCOPE(GraphBuilder, ShadowProjection);

		// All shadows render with min blending
		AddClearRenderTargetPass(GraphBuilder, ForwardScreenSpaceShadowMask.Target);
		if (bIsHairEnable)
		{
			AddClearRenderTargetPass(GraphBuilder, ForwardScreenSpaceShadowMaskSubPixel.Target);
		}

		const bool bProjectingForForwardShading = true;

		for (auto LightIt = Scene->Lights.CreateConstIterator(); LightIt; ++LightIt)
		{
			const FLightSceneInfoCompact& LightSceneInfoCompact = *LightIt;
			const FLightSceneInfo* const LightSceneInfo = LightSceneInfoCompact.LightSceneInfo;
			FVisibleLightInfo& VisibleLightInfo = VisibleLightInfos[LightSceneInfo->Id];

			const bool bIssueLightDrawEvent = VisibleLightInfo.ShadowsToProject.Num() > 0 || VisibleLightInfo.CapsuleShadowsToProject.Num() > 0;

			FString LightNameWithLevel;
			GetLightNameForDrawEvent(LightSceneInfo->Proxy, LightNameWithLevel);
			RDG_EVENT_SCOPE_CONDITIONAL(GraphBuilder, bIssueLightDrawEvent, "%s", *LightNameWithLevel);

			if (VisibleLightInfo.ShadowsToProject.Num() > 0)
			{
				RenderShadowProjections(
					GraphBuilder,
					SceneTextures,
					ForwardScreenSpaceShadowMask.Target,
					ForwardScreenSpaceShadowMaskSubPixel.Target,
					LightSceneInfo,
					bProjectingForForwardShading);

				if (bIsHairEnable)
				{
					RenderHairStrandsShadowMask(GraphBuilder, Views, LightSceneInfo, VisibleLightInfos, bProjectingForForwardShading, ForwardScreenSpaceShadowMask.Target);
				}
			}

			RenderCapsuleDirectShadows(GraphBuilder, *LightSceneInfo, ForwardScreenSpaceShadowMask.Target, VisibleLightInfo.CapsuleShadowsToProject, bProjectingForForwardShading);

			if (LightSceneInfo->GetDynamicShadowMapChannel() >= 0 && LightSceneInfo->GetDynamicShadowMapChannel() < 4)
			{
				RenderLightFunction(
					GraphBuilder,
					SceneTextures,
					LightSceneInfo,
					ForwardScreenSpaceShadowMask.Target,
					true, true, false);
			}
		}

		auto* PassParameters = GraphBuilder.AllocParameters<FRenderTargetParameters>();
		PassParameters->RenderTargets[0] = FRenderTargetBinding(ForwardScreenSpaceShadowMask.Target, ForwardScreenSpaceShadowMask.Resolve, ERenderTargetLoadAction::ELoad);
		OutForwardScreenSpaceShadowMask = ForwardScreenSpaceShadowMask.Resolve;

		if (bIsHairEnable)
		{
			OutForwardScreenSpaceShadowMaskSubPixel = ForwardScreenSpaceShadowMaskSubPixel.Target;
		}

		GraphBuilder.AddPass(RDG_EVENT_NAME("ResolveScreenSpaceShadowMask"), PassParameters, ERDGPassFlags::Raster, [](FRDGAsyncTask, FRHICommandList&) {});
	}
}

class FDebugLightGridPS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FDebugLightGridPS);
	SHADER_USE_PARAMETER_STRUCT(FDebugLightGridPS, FGlobalShader);

	using FPermutationDomain = TShaderPermutationDomain<>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, ViewUniformBuffer)
		SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FForwardLightData, Forward)
		SHADER_PARAMETER_STRUCT_INCLUDE(ShaderPrint::FShaderParameters, ShaderPrintParameters)
		SHADER_PARAMETER_TEXTURE(Texture2D, MiniFontTexture)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, DepthTexture)
		SHADER_PARAMETER(uint32, DebugMode)
		SHADER_PARAMETER(uint32, MaxThreshold)
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()

	static FPermutationDomain RemapPermutation(FPermutationDomain PermutationVector)
	{
		return PermutationVector;
	}

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return EnumHasAllFlags(Parameters.Flags, EShaderPermutationFlags::HasEditorOnlyData) && ShaderPrint::IsSupported(Parameters.Platform);
	}

	static EShaderPermutationPrecacheRequest ShouldPrecachePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return EShaderPermutationPrecacheRequest::NotPrecached;
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		ShaderPrint::ModifyCompilationEnvironment(Parameters, OutEnvironment);

		// Stay debug and skip optimizations to reduce compilation time on this long shader.
		OutEnvironment.CompilerFlags.Add(CFLAG_Debug);
		OutEnvironment.CompilerFlags.Add(CFLAG_AllowTypedUAVLoads);
		OutEnvironment.SetDefine(TEXT("SHADER_DEBUG_LIGHT_GRID_PS"), 1);
		OutEnvironment.SetDefine(TEXT("LIGHT_LINK_STRIDE"), LightLinkStride);
		FForwardLightingParameters::ModifyCompilationEnvironment(Parameters.Platform, OutEnvironment);
	}
};
IMPLEMENT_GLOBAL_SHADER(FDebugLightGridPS, "/Engine/Private/LightGridInjection.usf", "DebugLightGridPS", SF_Pixel);

FScreenPassTexture AddVisualizeLightGridPass(FRDGBuilder& GraphBuilder, const FViewInfo& View, FScreenPassTexture& ScreenPassSceneColor, FRDGTextureRef SceneDepthTexture)
{
	if (ShaderPrint::IsSupported(View.Family->GetShaderPlatform()))
	{
		RDG_EVENT_SCOPE(GraphBuilder, "VisualizeLightGrid");

		// Force ShaderPrint on.
		ShaderPrint::SetEnabled(true);

		ShaderPrint::RequestSpaceForLines(128);
		ShaderPrint::RequestSpaceForCharacters(128);

		FDebugLightGridPS::FPermutationDomain PermutationVector;
		TShaderMapRef<FDebugLightGridPS> PixelShader(View.ShaderMap, PermutationVector);
		FDebugLightGridPS::FParameters* PassParameters = GraphBuilder.AllocParameters<FDebugLightGridPS::FParameters>();
		PassParameters->ViewUniformBuffer = View.ViewUniformBuffer;
		PassParameters->Forward = View.ForwardLightingResources.ForwardLightUniformBuffer;
		ShaderPrint::SetParameters(GraphBuilder, View.ShaderPrintData, PassParameters->ShaderPrintParameters);
		PassParameters->DepthTexture = SceneDepthTexture ? SceneDepthTexture : GSystemTextures.GetMaxFP16Depth(GraphBuilder);
		PassParameters->MiniFontTexture = GetMiniFontTexture();
		PassParameters->RenderTargets[0] = FRenderTargetBinding(ScreenPassSceneColor.Texture, ERenderTargetLoadAction::ELoad);
		PassParameters->DebugMode = GForwardLightGridDebug;
		PassParameters->MaxThreshold = GForwardLightGridDebugMaxThreshold;

		FRHIBlendState* PreMultipliedColorTransmittanceBlend = TStaticBlendState<CW_RGB, BO_Add, BF_One, BF_SourceAlpha, BO_Add, BF_Zero, BF_One>::GetRHI();

		FPixelShaderUtils::AddFullscreenPass<FDebugLightGridPS>(GraphBuilder, View.ShaderMap, RDG_EVENT_NAME("DebugLightGridCS"), PixelShader, PassParameters,
			ScreenPassSceneColor.ViewRect, PreMultipliedColorTransmittanceBlend);
	}

	return MoveTemp(ScreenPassSceneColor);
}

class FLightGridFeedbackStatusCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FLightGridFeedbackStatusCS);
	SHADER_USE_PARAMETER_STRUCT(FLightGridFeedbackStatusCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, NextCulledLightDataBuffer)
		SHADER_PARAMETER(uint32, NumCulledLightDataEntries)

		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, NextCulledLightLinkBuffer)
		SHADER_PARAMETER(uint32, NumAvailableLinks)

		SHADER_PARAMETER_STRUCT_INCLUDE(GPUMessage::FParameters, GPUMessageParams)
		SHADER_PARAMETER(uint32, StatusMessageId)
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FLightGridFeedbackStatusCS, "/Engine/Private/LightGridInjection.usf", "FeedbackStatusCS", SF_Compute);

FLightGridViewState::FLightGridViewState()
{
#if !UE_BUILD_SHIPPING
	StatusFeedbackSocket = GPUMessage::RegisterHandler(TEXT("LightGrid.StatusFeedback"),
		[this](GPUMessage::FReader Message)
		{
			const uint32 AllocatedEntries = Message.Read<uint32>(0);
			const uint32 MaxEntries = Message.Read<uint32>(0);

			const uint32 AllocatedLinks = Message.Read<uint32>(0);
			const uint32 MaxLinks = Message.Read<uint32>(0);

			if (AllocatedEntries > MaxEntries)
			{
				bool bWarn = MaxEntries > MaxEntriesHighWaterMark;

				if (bWarn)
				{
					UE_LOG(LogRenderer, Warning, TEXT(	"Building light grid exceeded number of available entries (%u / %u). "
														"Increase r.Forward.MaxCulledLightsPerCell to prevent potential visual artifacts."), AllocatedEntries, MaxEntries);
				}

				MaxEntriesHighWaterMark = FMath::Max(MaxEntriesHighWaterMark, MaxEntries);
			}

			if (AllocatedLinks > MaxLinks)
			{
				static bool bWarn = true;

				if (bWarn)
				{
					UE_LOG(LogRenderer, Warning, TEXT("Building light grid exceeded number of available links, glitches will be visible (%u / %u)."), AllocatedLinks, MaxLinks);
					bWarn = false;
				}
			}
		});
#endif
}

void FLightGridViewState::FeedbackStatus(FRDGBuilder& GraphBuilder, FViewInfo& View, FRDGBufferRef NextCulledLightDataBuffer, uint32 NumCulledLightDataEntries, FRDGBufferRef NextCulledLightLinkBuffer, uint32 NumCulledLightLinks)
{
#if !UE_BUILD_SHIPPING
	FLightGridFeedbackStatusCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FLightGridFeedbackStatusCS::FParameters>();

	PassParameters->NextCulledLightDataBuffer = GraphBuilder.CreateSRV(NextCulledLightDataBuffer);
	PassParameters->NumCulledLightDataEntries = NumCulledLightDataEntries;

	PassParameters->NextCulledLightLinkBuffer = GraphBuilder.CreateSRV(NextCulledLightLinkBuffer);
	PassParameters->NumAvailableLinks = NumCulledLightLinks;

	PassParameters->GPUMessageParams = GPUMessage::GetShaderParameters(GraphBuilder);
	PassParameters->StatusMessageId = GetStatusMessageId();

	auto ComputeShader = View.ShaderMap->GetShader<FLightGridFeedbackStatusCS>();

	FComputeShaderUtils::AddPass(
		GraphBuilder,
		RDG_EVENT_NAME("LightGridFeedbackStatus"),
		ComputeShader,
		PassParameters,
		FIntVector(1, 1, 1)
	);
#endif // !UE_BUILD_SHIPPING
}