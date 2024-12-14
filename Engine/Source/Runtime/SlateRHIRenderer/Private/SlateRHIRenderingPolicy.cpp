// Copyright Epic Games, Inc. All Rights Reserved.

#include "SlateRHIRenderingPolicy.h"
#include "UniformBuffer.h"
#include "Shader.h"
#include "ShowFlags.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/App.h"
#include "EngineGlobals.h"
#include "RHIStaticStates.h"
#include "RHIUtilities.h"
#include "GlobalRenderResources.h"
#include "Interfaces/SlateRHIRenderingPolicyInterface.h"
#include "SceneView.h"
#include "SceneUtils.h"
#include "Engine/Engine.h"
#include "SlateShaders.h"
#include "Rendering/SlateRenderer.h"
#include "SlateRHIRenderer.h"
#include "SlateMaterialShader.h"
#include "SlateUTextureResource.h"
#include "SlateMaterialResource.h"
#include "SlateUpdatableBuffer.h"
#include "SlatePostProcessor.h"
#include "Materials/MaterialRenderProxy.h"
#include "Modules/ModuleManager.h"
#include "PipelineStateCache.h"
#include "Math/RandomStream.h"
#include "DeviceProfiles/DeviceProfile.h"
#include "DeviceProfiles/DeviceProfileManager.h"
#include "Types/SlateConstants.h"
#include "RenderGraphUtils.h"
#include "SceneRenderTargetParameters.h"
#include "SceneInterface.h"
#include "Containers/StaticBitArray.h"
#include "MeshPassProcessor.h"

extern void UpdateNoiseTextureParameters(FViewUniformShaderParameters& ViewUniformShaderParameters);

DECLARE_CYCLE_STAT(TEXT("Update Buffers RT"), STAT_SlateUpdateBufferRTTime, STATGROUP_Slate);
DECLARE_CYCLE_STAT(TEXT("Update Buffers RT"), STAT_SlateUpdateBufferRTTimeLambda, STATGROUP_Slate);

DECLARE_DWORD_COUNTER_STAT(TEXT("Num Layers"), STAT_SlateNumLayers, STATGROUP_Slate);
DECLARE_DWORD_COUNTER_STAT(TEXT("Num Batches"), STAT_SlateNumBatches, STATGROUP_Slate);
DECLARE_DWORD_COUNTER_STAT(TEXT("Num Vertices"), STAT_SlateVertexCount, STATGROUP_Slate);

DECLARE_DWORD_COUNTER_STAT(TEXT("Clips (Scissor)"), STAT_SlateScissorClips, STATGROUP_Slate);
DECLARE_DWORD_COUNTER_STAT(TEXT("Clips (Stencil)"), STAT_SlateStencilClips, STATGROUP_Slate);

#if WITH_SLATE_DEBUGGING
static int32 GSlateEnableDrawEvents = 1;
#else
static int32 GSlateEnableDrawEvents = 0;
#endif
static FAutoConsoleVariableRef CVarGSlateEnableDrawEvents(TEXT("Slate.EnableDrawEvents"), GSlateEnableDrawEvents, TEXT("."), ECVF_Default);

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
	#define SLATE_DRAW_EVENT( RHICmdList, EventName             ) SCOPED_CONDITIONAL_DRAW_EVENT( RHICmdList, EventName, (GSlateEnableDrawEvents != 0));
	#define SLATE_DRAW_EVENTF(RHICmdList, EventName, Format, ...) SCOPED_CONDITIONAL_DRAW_EVENTF(RHICmdList, EventName, (GSlateEnableDrawEvents != 0), Format, ##__VA_ARGS__);
#else
	#define SLATE_DRAW_EVENT( RHICmdList, EventName             )
	#define SLATE_DRAW_EVENTF(RHICmdList, EventName, Format, ...)
#endif

#if WITH_SLATE_VISUALIZERS
extern TAutoConsoleVariable<int32> CVarShowSlateOverdraw;
extern TAutoConsoleVariable<int32> CVarShowSlateBatching;
#endif

//////////////////////////////////////////////////////////////////////////

FSlateRHIRenderingPolicy::FSlateRHIRenderingPolicy(TSharedRef<FSlateFontServices> InSlateFontServices, TSharedRef<FSlateRHIResourceManager> InResourceManager)
	: FSlateRenderingPolicy(InSlateFontServices, 0)
	, ResourceManager(InResourceManager)
{}

void FSlateRHIRenderingPolicy::AddSceneAt(FSceneInterface* Scene, int32 Index)
{
	ResourceManager->AddSceneAt(Scene, Index);
}

void FSlateRHIRenderingPolicy::ClearScenes()
{
	ResourceManager->ClearScenes();
}

//////////////////////////////////////////////////////////////////////////

TConstArrayView<FTextureLODGroup> GetTextureLODGroups()
{
	if (UDeviceProfileManager::DeviceProfileManagerSingleton)
	{
		if (UDeviceProfile* Profile = UDeviceProfileManager::DeviceProfileManagerSingleton->GetActiveProfile())
		{
			return Profile->GetTextureLODSettings()->TextureLODGroups;
		}
	}
	return {};
}

ETextureSamplerFilter GetSamplerFilter(TConstArrayView<FTextureLODGroup> TextureLODGroups, const UTexture* Texture)
{
	// Default to point filtering.
	ETextureSamplerFilter Filter = ETextureSamplerFilter::Point;

	switch (Texture->Filter)
	{
	case TF_Nearest: 
		Filter = ETextureSamplerFilter::Point; 
		break;
	case TF_Bilinear:
		Filter = ETextureSamplerFilter::Bilinear; 
		break;
	case TF_Trilinear: 
		Filter = ETextureSamplerFilter::Trilinear; 
		break;

		// TF_Default
	default:
		// Use LOD group value to find proper filter setting.
		if (Texture->LODGroup < TextureLODGroups.Num())
		{
			Filter = TextureLODGroups[Texture->LODGroup].Filter;
		}
	}

	return Filter;
}

FRHISamplerState* GetSamplerState(ESlateBatchDrawFlag DrawFlags, ETextureSamplerFilter Filter)
{
	FRHISamplerState* SamplerState = nullptr;

	if (EnumHasAllFlags(DrawFlags, (ESlateBatchDrawFlag::TileU | ESlateBatchDrawFlag::TileV)))
	{
		switch (Filter)
		{
		case ETextureSamplerFilter::Point:
			SamplerState = TStaticSamplerState<SF_Point, AM_Wrap, AM_Wrap, AM_Wrap>::GetRHI();
			break;
		case ETextureSamplerFilter::AnisotropicPoint:
			SamplerState = TStaticSamplerState<SF_AnisotropicPoint, AM_Wrap, AM_Wrap, AM_Wrap>::GetRHI();
			break;
		case ETextureSamplerFilter::Trilinear:
			SamplerState = TStaticSamplerState<SF_Trilinear, AM_Wrap, AM_Wrap, AM_Wrap>::GetRHI();
			break;
		case ETextureSamplerFilter::AnisotropicLinear:
			SamplerState = TStaticSamplerState<SF_AnisotropicLinear, AM_Wrap, AM_Wrap, AM_Wrap>::GetRHI();
			break;
		default:
			SamplerState = TStaticSamplerState<SF_Bilinear, AM_Wrap, AM_Wrap, AM_Wrap>::GetRHI();
			break;
		}
	}
	else if (EnumHasAllFlags(DrawFlags, ESlateBatchDrawFlag::TileU))
	{
		switch (Filter)
		{
		case ETextureSamplerFilter::Point:
			SamplerState = TStaticSamplerState<SF_Point, AM_Wrap, AM_Clamp, AM_Wrap>::GetRHI();
			break;
		case ETextureSamplerFilter::AnisotropicPoint:
			SamplerState = TStaticSamplerState<SF_AnisotropicPoint, AM_Wrap, AM_Clamp, AM_Wrap>::GetRHI();
			break;
		case ETextureSamplerFilter::Trilinear:
			SamplerState = TStaticSamplerState<SF_Trilinear, AM_Wrap, AM_Clamp, AM_Wrap>::GetRHI();
			break;
		case ETextureSamplerFilter::AnisotropicLinear:
			SamplerState = TStaticSamplerState<SF_AnisotropicLinear, AM_Wrap, AM_Clamp, AM_Wrap>::GetRHI();
			break;
		default:
			SamplerState = TStaticSamplerState<SF_Bilinear, AM_Wrap, AM_Clamp, AM_Wrap>::GetRHI();
			break;
		}
	}
	else if (EnumHasAllFlags(DrawFlags, ESlateBatchDrawFlag::TileV))
	{
		switch (Filter)
		{
		case ETextureSamplerFilter::Point:
			SamplerState = TStaticSamplerState<SF_Point, AM_Clamp, AM_Wrap, AM_Wrap>::GetRHI();
			break;
		case ETextureSamplerFilter::AnisotropicPoint:
			SamplerState = TStaticSamplerState<SF_AnisotropicPoint, AM_Clamp, AM_Wrap, AM_Wrap>::GetRHI();
			break;
		case ETextureSamplerFilter::Trilinear:
			SamplerState = TStaticSamplerState<SF_Trilinear, AM_Clamp, AM_Wrap, AM_Wrap>::GetRHI();
			break;
		case ETextureSamplerFilter::AnisotropicLinear:
			SamplerState = TStaticSamplerState<SF_AnisotropicLinear, AM_Clamp, AM_Wrap, AM_Wrap>::GetRHI();
			break;
		default:
			SamplerState = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Wrap, AM_Wrap>::GetRHI();
			break;
		}
	}
	else
	{
		switch (Filter)
		{
		case ETextureSamplerFilter::Point:
			SamplerState = TStaticSamplerState<SF_Point, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
			break;
		case ETextureSamplerFilter::AnisotropicPoint:
			SamplerState = TStaticSamplerState<SF_AnisotropicPoint, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
			break;
		case ETextureSamplerFilter::Trilinear:
			SamplerState = TStaticSamplerState<SF_Trilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
			break;
		case ETextureSamplerFilter::AnisotropicLinear:
			SamplerState = TStaticSamplerState<SF_AnisotropicLinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
			break;
		default:
			SamplerState = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
			break;
		}
	}

	return SamplerState;
}

/** Returns the pixel shader that should be used for the specified ShaderType and DrawEffects */
TShaderRef<FSlateElementPS> GetTexturePixelShader(FGlobalShaderMap* ShaderMap, ESlateShader ShaderType, ESlateDrawEffect DrawEffects, bool bUseTextureGrayscale, bool bIsVirtualTexture)
{
	TShaderRef<FSlateElementPS> PixelShader;

#if WITH_SLATE_VISUALIZERS
	if (CVarShowSlateOverdraw.GetValueOnRenderThread() != 0)
	{
		PixelShader = TShaderMapRef<FSlateDebugOverdrawPS>(ShaderMap);
	}
	else
#endif
	{
	const bool bDrawDisabled = EnumHasAllFlags( DrawEffects, ESlateDrawEffect::DisabledEffect );
	const bool bUseTextureAlpha = !EnumHasAllFlags( DrawEffects, ESlateDrawEffect::IgnoreTextureAlpha );

	if (bDrawDisabled)
	{
		switch (ShaderType)
		{
		default:
		case ESlateShader::Default:
			if (bUseTextureAlpha)
			{
				if (bIsVirtualTexture)
				{
					if (bUseTextureGrayscale)
					{
						PixelShader = TShaderMapRef<TSlateElementPS<ESlateShader::Default, true, true, true, true> >(ShaderMap);
					}
					else
					{
						PixelShader = TShaderMapRef<TSlateElementPS<ESlateShader::Default, true, true, false, true> >(ShaderMap);
					}
				}
				else
				{
					if (bUseTextureGrayscale)
					{
						PixelShader = TShaderMapRef<TSlateElementPS<ESlateShader::Default, true, true, true, false> >(ShaderMap);
					}
					else
					{
						PixelShader = TShaderMapRef<TSlateElementPS<ESlateShader::Default, true, true, false, false> >(ShaderMap);
					}
				}
			}
			else
			{
				if (bIsVirtualTexture)
				{
					if (bUseTextureGrayscale)
					{
						PixelShader = TShaderMapRef<TSlateElementPS<ESlateShader::Default, true, false, true, true> >(ShaderMap);
					}
					else
					{
						PixelShader = TShaderMapRef<TSlateElementPS<ESlateShader::Default, true, false, false, true> >(ShaderMap);
					}
				}
				else
				{
					if (bUseTextureGrayscale)
					{
						PixelShader = TShaderMapRef<TSlateElementPS<ESlateShader::Default, true, false, true, false> >(ShaderMap);
					}
					else
					{
						PixelShader = TShaderMapRef<TSlateElementPS<ESlateShader::Default, true, false, false, false> >(ShaderMap);
					}
				}
			}
			break;
		case ESlateShader::Border:
			if ( bUseTextureAlpha )
			{
				PixelShader = TShaderMapRef<TSlateElementPS<ESlateShader::Border, true, true> >(ShaderMap);
			}
			else
			{
				PixelShader = TShaderMapRef<TSlateElementPS<ESlateShader::Border, true, false> >(ShaderMap);
			}
			break;
		case ESlateShader::GrayscaleFont:
			PixelShader = TShaderMapRef<TSlateElementPS<ESlateShader::GrayscaleFont, true> >(ShaderMap);
			break;
		case ESlateShader::ColorFont:
			PixelShader = TShaderMapRef<TSlateElementPS<ESlateShader::ColorFont, true> >(ShaderMap);
			break;
		case ESlateShader::LineSegment:
			PixelShader = TShaderMapRef<TSlateElementPS<ESlateShader::LineSegment, true> >(ShaderMap);
			break;
		case ESlateShader::RoundedBox:
			PixelShader = TShaderMapRef<TSlateElementPS<ESlateShader::RoundedBox, true> >(ShaderMap);
			break;
		case ESlateShader::SdfFont:
			PixelShader = TShaderMapRef<TSlateElementPS<ESlateShader::SdfFont, true> >(ShaderMap);
			break;
		case ESlateShader::MsdfFont:
			PixelShader = TShaderMapRef<TSlateElementPS<ESlateShader::MsdfFont, true> >(ShaderMap);
			break;
		}
	}
	else
	{
		switch (ShaderType)
		{
		default:
		case ESlateShader::Default:
			if (bUseTextureAlpha)
			{
				if (bIsVirtualTexture)
				{
					if (bUseTextureGrayscale)
					{
						PixelShader = TShaderMapRef<TSlateElementPS<ESlateShader::Default, false, true, true, true> >(ShaderMap);
					}
					else
					{
						PixelShader = TShaderMapRef<TSlateElementPS<ESlateShader::Default, false, true, false, true> >(ShaderMap);
					}
				}
				else
				{
					if (bUseTextureGrayscale)
					{
						PixelShader = TShaderMapRef<TSlateElementPS<ESlateShader::Default, false, true, true, false> >(ShaderMap);
					}
					else
					{
						PixelShader = TShaderMapRef<TSlateElementPS<ESlateShader::Default, false, true, false, false> >(ShaderMap);
					}
				}
			}
			else
			{
				if (bIsVirtualTexture)
				{
					if (bUseTextureGrayscale)
					{
						PixelShader = TShaderMapRef<TSlateElementPS<ESlateShader::Default, false, false, true, true> >(ShaderMap);
					}
					else
					{
						PixelShader = TShaderMapRef<TSlateElementPS<ESlateShader::Default, false, false, false, true> >(ShaderMap);
					}
				}
				else
				{
					if (bUseTextureGrayscale)
					{
						PixelShader = TShaderMapRef<TSlateElementPS<ESlateShader::Default, false, false, true, false> >(ShaderMap);
					}
					else
					{
						PixelShader = TShaderMapRef<TSlateElementPS<ESlateShader::Default, false, false, false, false> >(ShaderMap);
					}
				}
			}
			break;
		case ESlateShader::Border:
			if (bUseTextureAlpha)
			{
				PixelShader = TShaderMapRef<TSlateElementPS<ESlateShader::Border, false, true> >(ShaderMap);
			}
			else
			{
				PixelShader = TShaderMapRef<TSlateElementPS<ESlateShader::Border, false, false> >(ShaderMap);
			}
			break;
		case ESlateShader::GrayscaleFont:
			PixelShader = TShaderMapRef<TSlateElementPS<ESlateShader::GrayscaleFont, false> >(ShaderMap);
			break;
		case ESlateShader::ColorFont:
			PixelShader = TShaderMapRef<TSlateElementPS<ESlateShader::ColorFont, false> >(ShaderMap);
			break;
		case ESlateShader::LineSegment:
			PixelShader = TShaderMapRef<TSlateElementPS<ESlateShader::LineSegment, false> >(ShaderMap);
			break;
		case ESlateShader::RoundedBox:
			PixelShader = TShaderMapRef<TSlateElementPS<ESlateShader::RoundedBox, false> >(ShaderMap);
			break;
		case ESlateShader::SdfFont:
			PixelShader = TShaderMapRef<TSlateElementPS<ESlateShader::SdfFont, false> >(ShaderMap);
			break;
		case ESlateShader::MsdfFont:
			PixelShader = TShaderMapRef<TSlateElementPS<ESlateShader::MsdfFont, false> >(ShaderMap);
			break;
		}
	}
	}

	return PixelShader;
}

void ChooseMaterialShaderTypes(ESlateShader ShaderType, bool bUseInstancing, FMaterialShaderTypes& OutShaderTypes)
{
	switch (ShaderType)
	{
	case ESlateShader::Default:
		OutShaderTypes.AddShaderType<TSlateMaterialShaderPS<ESlateShader::Default>>();
		break;
	case ESlateShader::Border:
		OutShaderTypes.AddShaderType<TSlateMaterialShaderPS<ESlateShader::Border>>();
		break;
	case ESlateShader::GrayscaleFont:
		OutShaderTypes.AddShaderType<TSlateMaterialShaderPS<ESlateShader::GrayscaleFont>>();
		break;
	case ESlateShader::ColorFont:
		OutShaderTypes.AddShaderType<TSlateMaterialShaderPS<ESlateShader::ColorFont>>();
		break;
	case ESlateShader::Custom:
		OutShaderTypes.AddShaderType<TSlateMaterialShaderPS<ESlateShader::Custom>>();
		break;
	case ESlateShader::RoundedBox:
		OutShaderTypes.AddShaderType<TSlateMaterialShaderPS<ESlateShader::RoundedBox>>();
		break;
	case ESlateShader::SdfFont:
		OutShaderTypes.AddShaderType<TSlateMaterialShaderPS<ESlateShader::SdfFont>>();
		break;
	case ESlateShader::MsdfFont:
		OutShaderTypes.AddShaderType<TSlateMaterialShaderPS<ESlateShader::MsdfFont>>();
		break;
	default:
		checkf(false, TEXT("Unsupported Slate shader type for use with materials"));
		break;
	}

	if (bUseInstancing)
	{
		OutShaderTypes.AddShaderType<TSlateMaterialShaderVS<true>>();
	}
	else
	{
		OutShaderTypes.AddShaderType<TSlateMaterialShaderVS<false>>();
	}
}

EPrimitiveType GetRHIPrimitiveType(ESlateDrawPrimitive SlateType)
{
	switch(SlateType)
	{
	case ESlateDrawPrimitive::LineList:
		return PT_LineList;
	case ESlateDrawPrimitive::TriangleList:
	default:
		return PT_TriangleList;
	}
};

FRHIBlendState* GetMaterialBlendState(FSlateShaderResource* TextureMaskResource, const FMaterial* Material)
{
	if (TextureMaskResource && IsOpaqueOrMaskedBlendMode(*Material))
	{
		// Font materials require some form of translucent blending
		return TStaticBlendState<CW_RGBA, BO_Add, BF_SourceAlpha, BF_InverseSourceAlpha, BO_Add, BF_InverseDestAlpha, BF_One>::GetRHI();
	}
	else
	{
		switch (Material->GetBlendMode())
		{
		default:
		case BLEND_Opaque:
			return TStaticBlendState<>::GetRHI();
			break;
		case BLEND_Masked:
			return TStaticBlendState<>::GetRHI();
			break;
		case BLEND_Translucent:
			return TStaticBlendState<CW_RGBA, BO_Add, BF_SourceAlpha, BF_InverseSourceAlpha, BO_Add, BF_InverseDestAlpha, BF_One>::GetRHI();
			break;
		case BLEND_Additive:
			// Add to the existing scene color
			return TStaticBlendState<CW_RGBA, BO_Add, BF_One, BF_One, BO_Add, BF_One, BF_One>::GetRHI();
			break;
		case BLEND_Modulate:
			// Modulate with the existing scene color
			return TStaticBlendState<CW_RGB, BO_Add, BF_Zero, BF_SourceColor>::GetRHI();
			break;
		case BLEND_AlphaComposite:
			// Blend with existing scene color. New color is already pre-multiplied by alpha.
			return TStaticBlendState<CW_RGBA, BO_Add, BF_One, BF_InverseSourceAlpha, BO_Add, BF_One, BF_InverseSourceAlpha>::GetRHI();
			break;
		case BLEND_AlphaHoldout:
			// Blend by holding out the matte shape of the source alpha
			return TStaticBlendState<CW_RGBA, BO_Add, BF_Zero, BF_InverseSourceAlpha, BO_Add, BF_Zero, BF_InverseSourceAlpha>::GetRHI();
			break;
		};
	}
}

//////////////////////////////////////////////////////////////////////////

FSlateElementsBuffers BuildSlateElementsBuffers(FRDGBuilder& GraphBuilder, FSlateBatchData& BatchData)
{
	FSlateElementsBuffers ElementsBuffers;

	if (BatchData.GetRenderBatches().IsEmpty())
	{
		return ElementsBuffers;
	}

	{
		const FSlateVertexArray& Data = BatchData.GetFinalVertexData();

		FRDGBufferDesc BufferDesc;
		BufferDesc.Usage = EBufferUsageFlags::VertexBuffer | EBufferUsageFlags::Volatile;
		BufferDesc.BytesPerElement = sizeof(Data[0]);
		BufferDesc.NumElements = BatchData.GetMaxNumFinalVertices();

		if (BufferDesc.NumElements > 0)
		{
			ElementsBuffers.VertexBuffer = GraphBuilder.CreateBuffer(BufferDesc, TEXT("SlateElementsVertexBuffer"));
			GraphBuilder.QueueBufferUpload(ElementsBuffers.VertexBuffer, Data.GetData(), Data.Num() * BufferDesc.BytesPerElement, ERDGInitialDataFlags::NoCopy);
		}
	}

	{
		const FSlateIndexArray& Data = BatchData.GetFinalIndexData();

		FRDGBufferDesc BufferDesc;
		BufferDesc.Usage = EBufferUsageFlags::IndexBuffer | EBufferUsageFlags::Volatile;
		BufferDesc.BytesPerElement = sizeof(Data[0]);
		BufferDesc.NumElements = BatchData.GetMaxNumFinalIndices();

		if (BufferDesc.NumElements > 0)
		{
			ElementsBuffers.IndexBuffer = GraphBuilder.CreateBuffer(BufferDesc, TEXT("SlateElementIndexBuffer"));
			GraphBuilder.QueueBufferUpload(ElementsBuffers.IndexBuffer, Data.GetData(), Data.Num() * BufferDesc.BytesPerElement, ERDGInitialDataFlags::NoCopy);
		}
	}

	SET_DWORD_STAT(STAT_SlateNumLayers, BatchData.GetNumLayers());
	SET_DWORD_STAT(STAT_SlateNumBatches, BatchData.GetNumFinalBatches());
	SET_DWORD_STAT(STAT_SlateVertexCount, BatchData.GetFinalVertexData().Num());

	return ElementsBuffers;
}

//////////////////////////////////////////////////////////////////////////

BEGIN_UNIFORM_BUFFER_STRUCT(FSlateViewUniformParameters, )
	SHADER_PARAMETER(FMatrix44f, ViewProjection)
END_UNIFORM_BUFFER_STRUCT()

IMPLEMENT_STATIC_UNIFORM_BUFFER_SLOT(SlateView);
IMPLEMENT_STATIC_UNIFORM_BUFFER_STRUCT(FSlateViewUniformParameters, "SlateView", SlateView);

struct FSlateSceneViewAllocateInputs
{
	FIntPoint TextureExtent             = FIntPoint::ZeroValue;
	FIntRect ViewRect;
	FMatrix44f ViewProjectionMatrix     = FMatrix44f::Identity;
	FIntPoint CursorPosition            = FIntPoint::ZeroValue;
	FGameTime Time;
	float ViewportScaleUI               = 1.0f;
};

struct FSlateSceneView
{
	const FSceneInterface* Scene = nullptr;
	ERHIFeatureLevel::Type FeatureLevel = ERHIFeatureLevel::Num;
};

class FSlateSceneViewAllocator
{
	RDG_FRIEND_ALLOCATOR_FRIEND(FSlateSceneViewAllocator);
public:
	static FSlateSceneViewAllocator* Create(FRDGBuilder& GraphBuilder, FSlateRHIResourceManager& ResourceManager, const FSlateSceneViewAllocateInputs& Inputs)
	{
		return GraphBuilder.AllocObject<FSlateSceneViewAllocator>(ResourceManager, Inputs);
	}

	const TUniformBufferRef<FViewUniformShaderParameters>& GetViewUniformBuffer(const FSlateSceneView* View) const
	{
		return UniformBuffers[(int32)View->FeatureLevel];
	}

	const FSlateSceneView* BeginAllocateSceneView(FRDGBuilder& GraphBuilder, int32 SceneViewIndex)
	{
		if (!SceneViews.IsValidIndex(SceneViewIndex))
		{
			SceneViewIndex = SceneViewWithNullSceneIndex;
		}

		const FSlateSceneView& SceneView = SceneViews[SceneViewIndex];
		const ERHIFeatureLevel::Type FeatureLevel = SceneView.FeatureLevel;

		if (TUniformBufferRef<FViewUniformShaderParameters>& UniformBuffer = UniformBuffers[(int32)FeatureLevel]; !UniformBuffer)
		{
			UniformBuffer = CreateUniformBuffer(FeatureLevel, AllocateInputs);
		}

		return &SceneView;
	}

private:
	FSlateSceneViewAllocator(FSlateRHIResourceManager& ResourceManager, const FSlateSceneViewAllocateInputs& Inputs)
		: SceneViewWithNullSceneIndex(ResourceManager.GetSceneCount())
		, NumScenes(SceneViewWithNullSceneIndex + 1)
		, AllocateInputs(Inputs)
	{
		SceneViews.SetNum(NumScenes);
		SceneViews.Last().FeatureLevel = GMaxRHIFeatureLevel;

		for (int32 Index = 0; Index < SceneViewWithNullSceneIndex; ++Index)
		{
			const FSceneInterface* Scene = ResourceManager.GetSceneAt(Index);
			SceneViews[Index].Scene = Scene;
			SceneViews[Index].FeatureLevel = Scene->GetFeatureLevel();
		}
	}

	static TUniformBufferRef<FViewUniformShaderParameters> CreateUniformBuffer(ERHIFeatureLevel::Type FeatureLevel, const FSlateSceneViewAllocateInputs& Inputs)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(FSlateSceneViewAllocator::CreateUniformBuffer);

		static const FEngineShowFlags DefaultShowFlags(ESFIM_Game);

		FIntRect ViewRect = Inputs.ViewRect;

		// The window we are rendering to might not have a viewport, so use the full output instead.
		if (ViewRect.IsEmpty())
		{
			ViewRect.Max = Inputs.TextureExtent;
		}

		FViewMatrices::FMinimalInitializer Initializer;
		Initializer.ProjectionMatrix = FMatrix(Inputs.ViewProjectionMatrix);
		Initializer.ConstrainedViewRect = ViewRect;

		const FViewMatrices ViewMatrices(Initializer);

		const FSetupViewUniformParametersInputs SetupViewUniformParameterInputs =
		{
			  .EngineShowFlags  = &DefaultShowFlags
			, .UnscaledViewRect = ViewRect
			, .Time             = Inputs.Time
			, .CursorPosition   = Inputs.CursorPosition
		};

		FViewUniformShaderParameters ViewUniformShaderParameters;
		ViewUniformShaderParameters.VTFeedbackBuffer = GEmptyStructuredBufferWithUAV->UnorderedAccessViewRHI;

		SetupCommonViewUniformBufferParameters(ViewUniformShaderParameters, Inputs.TextureExtent, 1, ViewRect, ViewMatrices, ViewMatrices, SetupViewUniformParameterInputs);

		// Update Viewport Scale UI from any external sources (Material editor, UMG zoom scale / etc).
		ViewUniformShaderParameters.ViewportScaleUI = Inputs.ViewportScaleUI;

		// Always Update cursor position in realtime for slate
		ViewUniformShaderParameters.CursorPosition = Inputs.CursorPosition;

		// Slate materials need this scale to be positive, otherwise it can fail in querying scene textures (e.g., custom stencil)
		ViewUniformShaderParameters.BufferToSceneTextureScale = FVector2f(1.0f, 1.0f);

		ViewUniformShaderParameters.MobilePreviewMode = (FeatureLevel == ERHIFeatureLevel::ES3_1) && GMaxRHIFeatureLevel > ERHIFeatureLevel::ES3_1 ? 1.0f : 0.0f;

		UpdateNoiseTextureParameters(ViewUniformShaderParameters);
		return TUniformBufferRef<FViewUniformShaderParameters>::CreateUniformBufferImmediate(ViewUniformShaderParameters, UniformBuffer_SingleFrame);
	}

	const int32 SceneViewWithNullSceneIndex;
	const int32 NumScenes;
	const FSlateSceneViewAllocateInputs AllocateInputs;
	TArray<FSlateSceneView, FRDGArrayAllocator> SceneViews;
	TStaticArray<TUniformBufferRef<FViewUniformShaderParameters>, (int32)ERHIFeatureLevel::Num> UniformBuffers;
};

//////////////////////////////////////////////////////////////////////////

bool GetSlateClippingPipelineState(const FSlateClippingOp* ClippingStateOp, FRHIDepthStencilState*& OutDepthStencilState, uint8& OutStencilRef)
{
	if (ClippingStateOp && ClippingStateOp->Method == EClippingMethod::Stencil)
	{
		// Setup the stenciling state to be read only now, disable depth writes, and restore the color buffer
		// because we're about to go back to rendering widgets "normally", but with the added effect that now
		// we have the stencil buffer bound with a bunch of clipping zones rendered into it.
		OutDepthStencilState = 
			TStaticDepthStencilState<
				/*bEnableDepthWrite*/ false
			, /*DepthTest*/ CF_Always
			, /*bEnableFrontFaceStencil*/ true
			, /*FrontFaceStencilTest*/ CF_Equal
			, /*FrontFaceStencilFailStencilOp*/ SO_Keep
			, /*FrontFaceDepthFailStencilOp*/ SO_Keep
			, /*FrontFacePassStencilOp*/ SO_Keep
			, /*bEnableBackFaceStencil*/ true
			, /*BackFaceStencilTest*/ CF_Equal
			, /*BackFaceStencilFailStencilOp*/ SO_Keep
			, /*BackFaceDepthFailStencilOp*/ SO_Keep
			, /*BackFacePassStencilOp*/ SO_Keep
			, /*StencilReadMask*/ 0xFF
			, /*StencilWriteMask*/ 0xFF>::GetRHI();

		// Set a StencilRef equal to the number of stenciling/clipping masks, so unless the pixel we're rendering
		// to is on top of a stencil pixel with the same number it's going to get rejected, thereby clipping
		// everything except for the cross-section of all the stenciling quads.
		OutStencilRef = ClippingStateOp->MaskingId + ClippingStateOp->Data_Stencil.Zones.Num();
		return true;
	}
	else
	{
		OutDepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();
		OutStencilRef = 0;
		return false;
	}
}

void SetSlateClipping(FRHICommandList& RHICmdList, const FSlateClippingOp* ClippingStateOp, FIntRect ViewportRect)
{
	check(RHICmdList.IsInsideRenderPass());

	if (ClippingStateOp)
	{
		const FVector2f ElementOffset = ClippingStateOp->Offset;

		const auto ClampRectToViewport = [ElementOffset, ViewportRect] (FSlateRect ScissorRect)
		{
			ScissorRect.Left   = FMath::Clamp(ScissorRect.Left + ElementOffset.X, (float)ViewportRect.Min.X, (float)ViewportRect.Max.X);
			ScissorRect.Top    = FMath::Clamp(ScissorRect.Top  + ElementOffset.Y, (float)ViewportRect.Min.Y, (float)ViewportRect.Max.Y);
			ScissorRect.Right  = FMath::Clamp(ScissorRect.Right,  ScissorRect.Left, (float)ViewportRect.Max.X);
			ScissorRect.Bottom = FMath::Clamp(ScissorRect.Bottom, ScissorRect.Top,  (float)ViewportRect.Max.Y);
			return ScissorRect;
		};

		if (ClippingStateOp->Method == EClippingMethod::Scissor)
		{
			const FSlateRect ScissorRect = ClampRectToViewport(ClippingStateOp->Data_Scissor.Rect);
			RHICmdList.SetScissorRect(true, ScissorRect.Left, ScissorRect.Top, ScissorRect.Right, ScissorRect.Bottom);
		}
		else
		{
			check(ClippingStateOp->Method == EClippingMethod::Stencil);

			const TConstArrayView<FSlateClippingZone> Zones = ClippingStateOp->Data_Stencil.Zones;
			check(Zones.Num() > 0);

			// There might be some large - useless stencils, especially in the first couple of stencils if large
			// widgets that clip also contain render targets, so, by setting the scissor to the AABB of the final
			// stencil, we can cut out a lot of work that can't possibly be useful. We also round it, because if we
			// don't it can over-eagerly slice off pixels it shouldn't.

			const FSlateRect ScissorRect = ClampRectToViewport(Zones.Last().GetBoundingBox().Round());
			RHICmdList.SetScissorRect(true, ScissorRect.Left, ScissorRect.Top, ScissorRect.Right, ScissorRect.Bottom);

			const uint8 MaskingId = ClippingStateOp->MaskingId;

			FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(GMaxRHIShaderPlatform);
			TShaderMapRef<FSlateMaskingVS> VertexShader(ShaderMap);
			TShaderMapRef<FSlateMaskingPS> PixelShader(ShaderMap);

			// Start by setting up the stenciling states so that we can write representations of the clipping zones into the stencil buffer only.
			FGraphicsPipelineStateInitializer WriteMaskPSOInit;
			RHICmdList.ApplyCachedRenderTargets(WriteMaskPSOInit);
			WriteMaskPSOInit.BlendState = TStaticBlendStateWriteMask<CW_NONE, CW_NONE, CW_NONE, CW_NONE, CW_NONE, CW_NONE, CW_NONE, CW_NONE>::GetRHI();
			WriteMaskPSOInit.RasterizerState = TStaticRasterizerState<>::GetRHI();
			WriteMaskPSOInit.DepthStencilState =
				TStaticDepthStencilState<
				  /*bEnableDepthWrite*/ false
				, /*DepthTest*/ CF_Always
				, /*bEnableFrontFaceStencil*/ true
				, /*FrontFaceStencilTest*/ CF_Always
				, /*FrontFaceStencilFailStencilOp*/ SO_Keep
				, /*FrontFaceDepthFailStencilOp*/ SO_Keep
				, /*FrontFacePassStencilOp*/ SO_Replace
				, /*bEnableBackFaceStencil*/ true
				, /*BackFaceStencilTest*/ CF_Always
				, /*BackFaceStencilFailStencilOp*/ SO_Keep
				, /*BackFaceDepthFailStencilOp*/ SO_Keep
				, /*BackFacePassStencilOp*/ SO_Replace
				, /*StencilReadMask*/ 0xFF
				, /*StencilWriteMask*/ 0xFF>::GetRHI();

			WriteMaskPSOInit.BoundShaderState.VertexDeclarationRHI = GSlateMaskingVertexDeclaration.VertexDeclarationRHI;
			WriteMaskPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
			WriteMaskPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();
			WriteMaskPSOInit.PrimitiveType = PT_TriangleStrip;

			// Draw the first stencil using SO_Replace, so that we stomp any pixel with a MaskingID + 1.
			SetGraphicsPipelineState(RHICmdList, WriteMaskPSOInit, MaskingId + 1);

			// Simple 2D orthographic projection from screen space to NDC space.
			const FVector2f A
			(
				2.0f /  ViewportRect.Width(),
				2.0f / -ViewportRect.Height()
			);

			const FVector2f B
			(
				(ViewportRect.Min.X + ViewportRect.Max.X) / -ViewportRect.Width(),
				(ViewportRect.Min.Y + ViewportRect.Max.Y) /  ViewportRect.Height()
			);

			const auto TransformVertex = [A, B, ElementOffset](FVector2f P)
			{
				return FVector2f((P.X + ElementOffset.X) * A.X + B.X, (P.Y + ElementOffset.Y) * A.Y + B.Y);
			};

			const auto SetMaskingParameters = [VertexShader, TransformVertex] (FRHIBatchedShaderParameters& BatchedParameters, const FSlateClippingZone& Zone)
			{
				FSlateMaskingVS::FParameters Parameters;
				Parameters.MaskRectPacked[0] = FVector4f(TransformVertex(Zone.TopLeft), TransformVertex(Zone.TopRight));
				Parameters.MaskRectPacked[1] = FVector4f(TransformVertex(Zone.BottomLeft), TransformVertex(Zone.BottomRight));
				SetShaderParameters(BatchedParameters, VertexShader, Parameters);
			};

			{
				FRHIBatchedShaderParameters& BatchedParameters = RHICmdList.GetScratchShaderParameters();
				SetMaskingParameters(BatchedParameters, Zones[0]);
				RHICmdList.SetBatchedShaderParameters(VertexShader.GetVertexShader(), BatchedParameters);
				RHICmdList.SetStreamSource(0, GSlateStencilClipVertexBuffer.VertexBufferRHI, 0);
				RHICmdList.DrawPrimitive(0, 2, 1);
			}

			// Now setup the pipeline to use SO_SaturatedIncrement, since we've established the initial
			// stencil with SO_Replace, we can safely use SO_SaturatedIncrement, to build up the stencil
			// to the required mask of MaskingID + StencilQuads.Num(), thereby ensuring only the union of
			// all stencils will render pixels.
			WriteMaskPSOInit.DepthStencilState =
				TStaticDepthStencilState<
				/*bEnableDepthWrite*/ false
				, /*DepthTest*/ CF_Always
				, /*bEnableFrontFaceStencil*/ true
				, /*FrontFaceStencilTest*/ CF_Always
				, /*FrontFaceStencilFailStencilOp*/ SO_Keep
				, /*FrontFaceDepthFailStencilOp*/ SO_Keep
				, /*FrontFacePassStencilOp*/ SO_SaturatedIncrement
				, /*bEnableBackFaceStencil*/ true
				, /*BackFaceStencilTest*/ CF_Always
				, /*BackFaceStencilFailStencilOp*/ SO_Keep
				, /*BackFaceDepthFailStencilOp*/ SO_Keep
				, /*BackFacePassStencilOp*/ SO_SaturatedIncrement
				, /*StencilReadMask*/ 0xFF
				, /*StencilWriteMask*/ 0xFF>::GetRHI();

			SetGraphicsPipelineState(RHICmdList, WriteMaskPSOInit, 0);

			// Next write the number of quads representing the number of clipping zones have on top of each other.
			for (int32 MaskIndex = 1; MaskIndex < Zones.Num(); MaskIndex++)
			{
				FRHIBatchedShaderParameters& BatchedParameters = RHICmdList.GetScratchShaderParameters();
				SetMaskingParameters(BatchedParameters, Zones[MaskIndex]);
				RHICmdList.SetBatchedShaderParameters(VertexShader.GetVertexShader(), BatchedParameters);
				RHICmdList.SetStreamSource(0, GSlateStencilClipVertexBuffer.VertexBufferRHI, 0);
				RHICmdList.DrawPrimitive(0, 2, 1);
			}
		}
	}
	else
	{
		RHICmdList.SetScissorRect(false, 0.0f, 0.0f, 0.0f, 0.0f);
	}
}

enum class ESlateClippingStencilAction : uint8
{
	None,
	Write,
	Clear
};

struct FSlateClippingCreateContext
{
	uint32 NumStencils = 0;
	uint32 NumScissors = 0;
	uint32 MaskingId = 0;
	ESlateClippingStencilAction StencilAction = ESlateClippingStencilAction::None;
};

const FSlateClippingOp* CreateSlateClipping(FRDGBuilder& GraphBuilder, const FVector2f ElementsOffset, const FSlateClippingState* ClippingState, FSlateClippingCreateContext& Context)
{
	Context.StencilAction = ESlateClippingStencilAction::None;

	if (ClippingState)
	{
		if (ClippingState->GetClippingMethod() == EClippingMethod::Scissor)
		{
			Context.NumScissors++;

			const FSlateClippingZone& ScissorRect = ClippingState->ScissorRect.GetValue();

			return FSlateClippingOp::Scissor(GraphBuilder, ElementsOffset, FSlateRect(ScissorRect.TopLeft.X, ScissorRect.TopLeft.Y, ScissorRect.BottomRight.X, ScissorRect.BottomRight.Y));
		}
		else
		{
			Context.NumStencils++;

			TConstArrayView<FSlateClippingZone> StencilQuads = ClippingState->StencilQuads;
			check(StencilQuads.Num() > 0);

			// Reset the masking ID back to zero if stencil is going to overflow.
			if (Context.MaskingId + StencilQuads.Num() > 255)
			{
				Context.MaskingId = 0;
			}

			// Mark stencil for clear when the masking id is 0.
			Context.StencilAction = Context.MaskingId == 0 ? ESlateClippingStencilAction::Clear : ESlateClippingStencilAction::Write;

			const FSlateClippingOp* Op = FSlateClippingOp::Stencil(GraphBuilder, ElementsOffset, StencilQuads, Context.MaskingId);
			Context.MaskingId += StencilQuads.Num();
			return Op;
		}
	}
	return nullptr;
}

//////////////////////////////////////////////////////////////////////////

enum class ESlateRenderBatchType
{
	CustomDrawer,
	PostProcess,
	Primitive,
	MAX
};

inline ESlateRenderBatchType GetSlateRenderBatchType(const FSlateRenderBatch& DrawBatch)
{
	if (DrawBatch.CustomDrawer != nullptr)
	{
		return ESlateRenderBatchType::CustomDrawer;
	}

	if (DrawBatch.ShaderType == ESlateShader::PostProcess)
	{
		return ESlateRenderBatchType::PostProcess;
	}

	return ESlateRenderBatchType::Primitive;
}

class FSlateDrawShaderBindings : public FMeshDrawSingleShaderBindings
{
	FSlateDrawShaderBindings(const TShaderRef<FShader>& InShader, const FMeshDrawShaderBindingsLayout& InLayout, uint8* InData)
		: FMeshDrawSingleShaderBindings(InLayout, InData)
		, Shader(InShader)
	{}

public:
	static FSlateDrawShaderBindings* Create(FRDGBuilder& GraphBuilder, const TShaderRef<FShader>& Shader)
	{
		const FMeshDrawShaderBindingsLayout Layout(Shader);
		const uint32 DataSize = Layout.GetDataSizeBytes();
		uint8* Data = (uint8*)GraphBuilder.Alloc(DataSize);
		FMemory::Memzero(Data, DataSize);
		return new (GraphBuilder.Alloc(sizeof(FSlateDrawShaderBindings))) FSlateDrawShaderBindings(Shader, Layout, Data);
	}

	void SetOnCommandList(FRHICommandList& RHICmdList) const
	{
		FRHIBatchedShaderParameters& BatchedParameters = RHICmdList.GetScratchShaderParameters();
		FReadOnlyMeshDrawSingleShaderBindings::SetShaderBindings(BatchedParameters, FReadOnlyMeshDrawSingleShaderBindings(*this));
		RHICmdList.SetBatchedShaderParameters(Shader.GetGraphicsShader(), BatchedParameters);
	}

	TShaderRef<FShader> Shader;
};

struct FSlateRenderBatchOp
{
	FSlateRenderBatchOp* Next;
	const FSlateRenderBatch* RenderBatch;
	const FSlateClippingOp* ClippingStateOp;
	FSlateDrawShaderBindings* VertexBindings;
	FSlateDrawShaderBindings* PixelBindings;
	FRHIBuffer* InstanceBuffer;
	FRHIBlendState* BlendState;
	ESlateShaderResource::Type ShaderResourceType;
};

//////////////////////////////////////////////////////////////////////////

struct FSlateRenderBatchCreateInputs
{
	FGlobalShaderMap* ShaderMap;
	FSlateSceneViewAllocator* SceneViewAllocator;
	TConstArrayView<FTextureLODGroup> TextureLODGroups;
	float DisplayGamma;
	float DisplayContrast;
	float EngineGamma;
#if WITH_SLATE_VISUALIZERS
	FLinearColor BatchColor;
#endif
};

struct FSlateRenderBatchDrawState
{
	const FSlateClippingOp* LastClippingOp = nullptr;
	FGraphicsPipelineStateInitializer GraphicsPSOInit;
	uint8 StencilRef = 0;
};

FSlateRenderBatchOp* CreateSlateRenderBatchOp(
	FRDGBuilder& GraphBuilder,
	const FSlateRenderBatchCreateInputs& Inputs,
	const FSlateRenderBatch* RenderBatch,
	const FSlateClippingOp* ClippingStateOp)
{
	const FSlateShaderResource* ShaderResource   = RenderBatch->ShaderResource;
	const ESlateBatchDrawFlag DrawFlags          = RenderBatch->DrawFlags;
	const ESlateDrawEffect DrawEffects           = RenderBatch->DrawEffects;
	const ESlateShader ShaderType                = RenderBatch->ShaderType;
	const FShaderParams& ShaderParams            = RenderBatch->ShaderParams;

	check(ShaderResource == nullptr || !ShaderResource->Debug_IsDestroyed());
	const ESlateShaderResource::Type ResourceType = ShaderResource ? ShaderResource->GetType() : ESlateShaderResource::Invalid;

	const bool bUseInstancing = RenderBatch->InstanceCount > 0 && RenderBatch->InstanceData != nullptr;

	const float FinalGamma = EnumHasAnyFlags(DrawFlags, ESlateBatchDrawFlag::ReverseGamma) ? (1.0f / Inputs.EngineGamma) : EnumHasAnyFlags(DrawFlags, ESlateBatchDrawFlag::NoGamma) ? 1.0f : Inputs.DisplayGamma;
	const float FinalContrast = EnumHasAnyFlags(DrawFlags, ESlateBatchDrawFlag::NoGamma) ? 1 : Inputs.DisplayContrast;

	FRHIBlendState* BlendState = nullptr;
	FSlateDrawShaderBindings* PixelBindings = nullptr;
	FSlateDrawShaderBindings* VertexBindings = nullptr;

	if (ResourceType == ESlateShaderResource::Material)
	{
		// Skip material render batches when the engine is not available.
		if (!GEngine)
		{
			return nullptr;
		}

		FSlateMaterialResource* MaterialShaderResource = (FSlateMaterialResource*)ShaderResource;
		MaterialShaderResource->CheckForStaleResources();

		const FMaterialRenderProxy* MaterialRenderProxy = MaterialShaderResource->GetRenderProxy();

		if (!MaterialRenderProxy)
		{
			return nullptr;
		}

		const FSlateSceneView* SceneView = Inputs.SceneViewAllocator->BeginAllocateSceneView(GraphBuilder, RenderBatch->SceneIndex);
		const ERHIFeatureLevel::Type SceneFeatureLevel = SceneView->FeatureLevel;
		const FSceneInterface* Scene = SceneView->Scene;
		const TUniformBufferRef<FViewUniformShaderParameters>& ViewUniformBuffer = Inputs.SceneViewAllocator->GetViewUniformBuffer(SceneView);

		TShaderRef<FSlateMaterialShaderVS> VertexShader;
		TShaderRef<FSlateMaterialShaderPS> PixelShader;

		FMaterialShaderTypes ShaderTypesToGet;
		ChooseMaterialShaderTypes(ShaderType, bUseInstancing, ShaderTypesToGet);
		const FMaterial* EffectiveMaterial = nullptr;

		while (MaterialRenderProxy)
		{
			const FMaterial* Material = MaterialRenderProxy->UpdateUniformExpressionCacheIfNeeded(GraphBuilder.RHICmdList, SceneFeatureLevel);
			FMaterialShaders Shaders;
			if (Material && Material->TryGetShaders(ShaderTypesToGet, nullptr, Shaders))
			{
				EffectiveMaterial = Material;
				Shaders.TryGetVertexShader(VertexShader);
				Shaders.TryGetPixelShader(PixelShader);
				break;
			}

			MaterialRenderProxy = MaterialRenderProxy->GetFallback(SceneFeatureLevel);
		}

		if (!VertexShader.IsValid() || !PixelShader.IsValid())
		{
			return nullptr;
		}

		VertexBindings = FSlateDrawShaderBindings::Create(GraphBuilder, VertexShader);
		VertexShader->SetMaterialShaderParameters(*VertexBindings, Scene, ViewUniformBuffer, MaterialRenderProxy, EffectiveMaterial);

		const bool bDrawDisabled = EnumHasAllFlags(RenderBatch->DrawEffects, ESlateDrawEffect::DisabledEffect);

		PixelBindings = FSlateDrawShaderBindings::Create(GraphBuilder, PixelShader);
		PixelShader->SetMaterialShaderParameters(*PixelBindings, Scene, ViewUniformBuffer, MaterialRenderProxy, EffectiveMaterial, ShaderParams);
		PixelShader->SetDisplayGammaAndContrast(*PixelBindings, FinalGamma, FinalContrast);
		PixelShader->SetDrawFlags(*PixelBindings, bDrawDisabled);

		auto* MaskResource = static_cast<TSlateTexture<FTextureRHIRef>*>(MaterialShaderResource->GetTextureMaskResource());

		if (MaskResource)
		{
			PixelShader->SetAdditionalTexture(*PixelBindings, MaskResource->GetTypedResource(), TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI());
		}

		BlendState = GetMaterialBlendState(MaskResource, EffectiveMaterial);
	}
	else
	{
		check(!bUseInstancing);

		TShaderRef<FSlateElementPS> PixelShader;

#if WITH_SLATE_VISUALIZERS
		TShaderRef<FSlateDebugBatchingPS> BatchingPixelShader;
		if (CVarShowSlateBatching.GetValueOnRenderThread() != 0)
		{
			BatchingPixelShader = TShaderMapRef<FSlateDebugBatchingPS>(Inputs.ShaderMap);
			PixelShader = BatchingPixelShader;
		}
		else
#endif
		{
			bool bIsVirtualTexture = false;

			// check if texture is using BC4 compression and set shader to render grayscale
			bool bUseTextureGrayscale = false;

			if (ShaderResource != nullptr && ResourceType == ESlateShaderResource::TextureObject)
			{
				FSlateBaseUTextureResource* TextureObjectResource = const_cast<FSlateBaseUTextureResource*>(static_cast<const FSlateBaseUTextureResource*>(ShaderResource));

				if (UTexture* TextureObj = TextureObjectResource->GetTextureObject())
				{
					bIsVirtualTexture = TextureObj->IsCurrentlyVirtualTextured();

					if (TextureObj->CompressionSettings == TC_Alpha)
					{
						bUseTextureGrayscale = true;
					}
				}
			}

			PixelShader = GetTexturePixelShader(Inputs.ShaderMap, ShaderType, DrawEffects, bUseTextureGrayscale, bIsVirtualTexture);
		}

#if WITH_SLATE_VISUALIZERS
		if (BatchingPixelShader.IsValid())
		{
			BlendState = TStaticBlendState<CW_RGBA, BO_Add, BF_SourceAlpha, BF_InverseSourceAlpha, BO_Add, BF_One, BF_InverseSourceAlpha>::GetRHI();
		}
		else if (CVarShowSlateOverdraw.GetValueOnRenderThread() != 0)
		{
			BlendState = TStaticBlendState<CW_RGB, BO_Add, BF_One, BF_One, BO_Add, BF_Zero, BF_InverseSourceAlpha>::GetRHI();
		}
		else
#endif
		{
			BlendState =
				EnumHasAllFlags(DrawFlags, ESlateBatchDrawFlag::NoBlending)
				? TStaticBlendState<>::GetRHI()
				: (EnumHasAllFlags(DrawFlags, ESlateBatchDrawFlag::PreMultipliedAlpha)
					? TStaticBlendState<CW_RGBA, BO_Add, BF_One, BF_InverseSourceAlpha, BO_Add, BF_One, BF_InverseSourceAlpha>::GetRHI()
					: TStaticBlendState<CW_RGBA, BO_Add, BF_SourceAlpha, BF_InverseSourceAlpha, BO_Add, BF_One, BF_InverseSourceAlpha>::GetRHI())
				;
		}

		FRHISamplerState* SamplerState = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
		FRHITexture* TextureRHI = GWhiteTexture->TextureRHI;
		bool bIsVirtualTexture = false;
		FTextureResource* TextureResource = nullptr;

		if (ShaderResource)
		{
			ETextureSamplerFilter Filter = ETextureSamplerFilter::Bilinear;

			if (ResourceType == ESlateShaderResource::TextureObject)
			{
				FSlateBaseUTextureResource* TextureObjectResource = (FSlateBaseUTextureResource*)ShaderResource;
				if (UTexture* TextureObj = TextureObjectResource->GetTextureObject())
				{
					TextureObjectResource->CheckForStaleResources();

					TextureRHI = TextureObjectResource->AccessRHIResource();

					// This can upset some RHIs, so use transparent black texture until it's valid.
					// these can be temporarily invalid when recreating them / invalidating their streaming
					// state.
					if (TextureRHI == nullptr)
					{
						// We use transparent black here, because it's about to become valid - probably, and flashing white
						// wouldn't be ideal.
						TextureRHI = GTransparentBlackTexture->TextureRHI;
					}

					TextureResource = TextureObj->GetResource();

					Filter = GetSamplerFilter(Inputs.TextureLODGroups, TextureObj);
					bIsVirtualTexture = TextureObj->IsCurrentlyVirtualTextured();
				}
			}
			else
			{
				FRHITexture* NativeTextureRHI = ((TSlateTexture<FTextureRHIRef>*)ShaderResource)->GetTypedResource();
				// Atlas textures that have no content are never initialized but null textures are invalid on many platforms.
				TextureRHI = NativeTextureRHI ? NativeTextureRHI : (FRHITexture*)GWhiteTexture->TextureRHI;
			}

			SamplerState = GetSamplerState(DrawFlags, Filter);
		}

		PixelBindings = FSlateDrawShaderBindings::Create(GraphBuilder, PixelShader);

#if WITH_SLATE_VISUALIZERS
		if (BatchingPixelShader.IsValid())
		{
			BatchingPixelShader->SetBatchColor(*PixelBindings, Inputs.BatchColor);
		}
#endif

		if (bIsVirtualTexture && TextureResource != nullptr)
		{
			PixelShader->SetVirtualTextureParameters(*PixelBindings, static_cast<FVirtualTexture2DResource*>(TextureResource));
		}
		else
		{
			PixelShader->SetTexture(*PixelBindings, TextureRHI, SamplerState);
		}

		PixelShader->SetShaderParams(*PixelBindings, ShaderParams);
		PixelShader->SetDisplayGammaAndInvertAlphaAndContrast(*PixelBindings, FinalGamma, EnumHasAllFlags(DrawEffects, ESlateDrawEffect::InvertAlpha) ? 1.0f : 0.0f, FinalContrast);
	}

	FSlateRenderBatchOp* RenderBatchOp = GraphBuilder.AllocPOD<FSlateRenderBatchOp>();
	RenderBatchOp->RenderBatch        = RenderBatch;
	RenderBatchOp->ClippingStateOp    = ClippingStateOp;
	RenderBatchOp->ShaderResourceType = ResourceType;
	RenderBatchOp->VertexBindings     = VertexBindings;
	RenderBatchOp->PixelBindings      = PixelBindings;
	RenderBatchOp->InstanceBuffer     = bUseInstancing ? RenderBatch->InstanceData->GetRHI() : nullptr;
	RenderBatchOp->BlendState         = BlendState;
	RenderBatchOp->Next               = nullptr;
	return RenderBatchOp;
}

struct FSlateRenderBatchDrawInputs
{
	FGlobalShaderMap* ShaderMap;
	FSlateElementsBuffers ElementsBuffers;
	FIntRect ElementsViewRect;
	bool bWireframe;
};

void DrawSlateRenderBatch(
	FRHICommandList& RHICmdList,
	FSlateRenderBatchDrawState& State,
	const FSlateRenderBatchDrawInputs& Inputs,
	const FSlateRenderBatchOp& RenderBatchOp)
{
	const FSlateClippingOp* ClippingStateOp = RenderBatchOp.ClippingStateOp;
	const FSlateRenderBatch& RenderBatch    = *RenderBatchOp.RenderBatch;

	if (State.LastClippingOp != ClippingStateOp)
	{
		GetSlateClippingPipelineState(ClippingStateOp, State.GraphicsPSOInit.DepthStencilState, State.StencilRef);
		SetSlateClipping(RHICmdList, ClippingStateOp, Inputs.ElementsViewRect);
		State.LastClippingOp = ClippingStateOp;
	}

	FRHIBuffer* ElementsVertexBuffer = Inputs.ElementsBuffers.VertexBuffer->GetRHI();
	FRHIBuffer* ElementsIndexBuffer  = Inputs.ElementsBuffers.IndexBuffer->GetRHI();

	State.GraphicsPSOInit.BlendState = RenderBatchOp.BlendState;

	if (EnumHasAllFlags(RenderBatch.DrawFlags, ESlateBatchDrawFlag::Wireframe))
	{
		State.GraphicsPSOInit.RasterizerState = TStaticRasterizerState<FM_Wireframe>::GetRHI();
	}
	else
	{
		State.GraphicsPSOInit.RasterizerState = TStaticRasterizerState<FM_Solid>::GetRHI();
	}

	check(RenderBatch.NumIndices > 0);
	const uint32 PrimitiveCount = RenderBatch.DrawPrimitiveType == ESlateDrawPrimitive::LineList ? RenderBatch.NumIndices / 2 : RenderBatch.NumIndices / 3;

	if (RenderBatchOp.ShaderResourceType == ESlateShaderResource::Material)
	{
		State.GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = RenderBatchOp.InstanceBuffer ? GSlateInstancedVertexDeclaration.VertexDeclarationRHI : GSlateVertexDeclaration.VertexDeclarationRHI;
		State.GraphicsPSOInit.BoundShaderState.VertexShaderRHI = RenderBatchOp.VertexBindings->Shader.GetVertexShader();
		State.GraphicsPSOInit.BoundShaderState.PixelShaderRHI  = RenderBatchOp.PixelBindings->Shader.GetPixelShader();
		State.GraphicsPSOInit.PrimitiveType = GetRHIPrimitiveType(RenderBatch.DrawPrimitiveType);

		SetGraphicsPipelineState(RHICmdList, State.GraphicsPSOInit, State.StencilRef);

		RenderBatchOp.VertexBindings->SetOnCommandList(RHICmdList);
		RenderBatchOp.PixelBindings->SetOnCommandList(RHICmdList);

		RHICmdList.SetStreamSource(0, ElementsVertexBuffer, RenderBatch.VertexOffset * sizeof(FSlateVertex));

		if (RenderBatchOp.InstanceBuffer)
		{
			RHICmdList.SetStreamSource(1, RenderBatchOp.InstanceBuffer, RenderBatch.InstanceOffset * sizeof(FSlateInstanceBufferData::ElementType));
			RHICmdList.DrawIndexedPrimitive(ElementsIndexBuffer, 0, 0, RenderBatch.NumVertices, RenderBatch.IndexOffset, PrimitiveCount, RenderBatch.InstanceCount);
		}
		else
		{
			RHICmdList.SetStreamSource(1, nullptr, 0);
			RHICmdList.DrawIndexedPrimitive(ElementsIndexBuffer, 0, 0, RenderBatch.NumVertices, RenderBatch.IndexOffset, PrimitiveCount, 1);
		}
	}
	else
	{
		if (EnumHasAllFlags(RenderBatch.DrawFlags, ESlateBatchDrawFlag::Wireframe) || Inputs.bWireframe)
		{
			State.GraphicsPSOInit.RasterizerState = TStaticRasterizerState<FM_Wireframe>::GetRHI();

			if (Inputs.bWireframe)
			{
				State.GraphicsPSOInit.BlendState = TStaticBlendState<>::GetRHI();
			}
		}
		else
		{
			State.GraphicsPSOInit.RasterizerState = TStaticRasterizerState<FM_Solid>::GetRHI();
		}

		TShaderMapRef<FSlateElementVS> GlobalVertexShader(Inputs.ShaderMap);

		State.GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GSlateVertexDeclaration.VertexDeclarationRHI;
		State.GraphicsPSOInit.BoundShaderState.VertexShaderRHI = GlobalVertexShader.GetVertexShader();
		State.GraphicsPSOInit.BoundShaderState.PixelShaderRHI = RenderBatchOp.PixelBindings->Shader.GetPixelShader();
		State.GraphicsPSOInit.PrimitiveType = GetRHIPrimitiveType(RenderBatch.DrawPrimitiveType);

		SetGraphicsPipelineState(RHICmdList, State.GraphicsPSOInit, State.StencilRef);

		RenderBatchOp.PixelBindings->SetOnCommandList(RHICmdList);

		RHICmdList.SetStreamSource(0, ElementsVertexBuffer, RenderBatch.VertexOffset * sizeof(FSlateVertex));
		RHICmdList.DrawIndexedPrimitive(ElementsIndexBuffer, 0, 0, RenderBatch.NumVertices, RenderBatch.IndexOffset, PrimitiveCount, RenderBatch.InstanceCount);
	}
}

BEGIN_SHADER_PARAMETER_STRUCT(FSlateRenderBatchParameters, )
	SHADER_PARAMETER_STRUCT_INCLUDE(FSceneTextureExtractsParameters, SceneTextures)
	RDG_BUFFER_ACCESS(ElementsVertexBuffer, ERHIAccess::VertexOrIndexBuffer)
	RDG_BUFFER_ACCESS(ElementsIndexBuffer, ERHIAccess::VertexOrIndexBuffer)
	SHADER_PARAMETER_STRUCT_REF(FSlateViewUniformParameters, SlateView)
	RENDER_TARGET_BINDING_SLOTS()
END_SHADER_PARAMETER_STRUCT()

void AddSlateDrawElementsPass(
	FRDGBuilder& GraphBuilder,
	const FSlateRHIRenderingPolicy& RenderingPolicy,
	const FSlateDrawElementsPassInputs& Inputs,
	TConstArrayView<FSlateRenderBatch> RenderBatches,
	int32 FirstBatchIndex)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(AddSlateDrawElements);
	
	const FIntPoint ElementsTextureExtent = Inputs.ElementsTexture->Desc.Extent;
	const FScreenPassTexture ElementsTexture(Inputs.ElementsTexture);

	const float EngineGamma  = GEngine ? GEngine->GetDisplayGamma() : 2.2f;
	const float DisplayGamma = Inputs.bAllowGammaCorrection && !Inputs.bElementsTextureIsHDRDisplay ? EngineGamma : 1.0f;

	FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(GMaxRHIShaderPlatform);

	FSlateRHIResourceManager& ResourceManager = RenderingPolicy.GetResourceManagerRHI();

	const FSlateSceneViewAllocateInputs SceneViewAllocateInputs =
	{
		  .TextureExtent        = ElementsTextureExtent
		, .ViewRect             = Inputs.SceneViewRect
		, .ViewProjectionMatrix = Inputs.ElementsMatrix
		, .CursorPosition       = Inputs.CursorPosition
		, .Time                 = Inputs.Time
		, .ViewportScaleUI      = Inputs.ViewportScaleUI
	};

	FSlateSceneViewAllocator* SceneViewAllocator = FSlateSceneViewAllocator::Create(GraphBuilder, ResourceManager, SceneViewAllocateInputs);

#if WITH_SLATE_VISUALIZERS
	FRandomStream BatchColors(1337);
#endif

	const FSlateRenderBatchCreateInputs RenderBatchCreateInputs
	{
		  .ShaderMap             = ShaderMap
		, .SceneViewAllocator    = SceneViewAllocator
		, .TextureLODGroups      = GetTextureLODGroups()
		, .DisplayGamma          = DisplayGamma
		, .DisplayContrast       = GSlateContrast
		, .EngineGamma           = EngineGamma
#if WITH_SLATE_VISUALIZERS
		, .BatchColor            = FLinearColor(BatchColors.GetUnitVector())
#endif
	};

	// Draw inputs are passed into RDG lambdas and need to be allocated by RDG.
	FSlateRenderBatchDrawInputs* RenderBatchDrawInputs = GraphBuilder.AllocPOD<FSlateRenderBatchDrawInputs>();

	*RenderBatchDrawInputs =
	{
		  .ShaderMap             = ShaderMap
		, .ElementsBuffers       = Inputs.ElementsBuffers
		, .ElementsViewRect      = ElementsTexture.ViewRect
		, .bWireframe            = Inputs.bWireframe
	};

	ERenderTargetLoadAction ElementsLoadAction = Inputs.ElementsLoadAction;

	const auto ConsumeLoadAction = [] (ERenderTargetLoadAction& InOutLoadAction)
	{
		ERenderTargetLoadAction LoadAction = InOutLoadAction;
		InOutLoadAction = ERenderTargetLoadAction::ELoad;
		return LoadAction;
	};

	TUniformBufferRef<FSlateViewUniformParameters> SlateViewUniformBuffer;

	FSlateRenderBatchParameters* NoneStencilActionPassParameters = GraphBuilder.AllocParameters<FSlateRenderBatchParameters>();
	NoneStencilActionPassParameters->SceneTextures = GetSceneTextureExtracts().GetShaderParameters();
	NoneStencilActionPassParameters->ElementsVertexBuffer = Inputs.ElementsBuffers.VertexBuffer;
	NoneStencilActionPassParameters->ElementsIndexBuffer  = Inputs.ElementsBuffers.IndexBuffer;
	NoneStencilActionPassParameters->RenderTargets[0] = FRenderTargetBinding(Inputs.ElementsTexture, ERenderTargetLoadAction::ELoad);

	{
		FSlateViewUniformParameters UniformParameters;
		UniformParameters.ViewProjection = Inputs.ElementsMatrix;
		NoneStencilActionPassParameters->SlateView = TUniformBufferRef<FSlateViewUniformParameters>::CreateUniformBufferImmediate(UniformParameters, UniformBuffer_SingleFrame);
	}

	FSlateRenderBatchParameters* ClearStencilActionPassParameters = nullptr;
	FSlateRenderBatchParameters* WriteStencilActionPassParameters = nullptr;

	if (Inputs.StencilTexture)
	{
		WriteStencilActionPassParameters = GraphBuilder.AllocParameters<FSlateRenderBatchParameters>(NoneStencilActionPassParameters);
		WriteStencilActionPassParameters->RenderTargets.DepthStencil = FDepthStencilBinding(Inputs.StencilTexture, ERenderTargetLoadAction::ENoAction, ERenderTargetLoadAction::ELoad, FExclusiveDepthStencil::DepthNop_StencilWrite);

		ClearStencilActionPassParameters = GraphBuilder.AllocParameters<FSlateRenderBatchParameters>(WriteStencilActionPassParameters);
		ClearStencilActionPassParameters->RenderTargets.DepthStencil.SetStencilLoadAction(ERenderTargetLoadAction::EClear);
	}

	FSlateRenderBatchParameters* LastPassParameters  = NoneStencilActionPassParameters;
	const FSlateClippingState*   LastClippingState   = nullptr;
	const FSlateClippingOp*      LastClippingOp      = nullptr;

	FSlateClippingCreateContext ClippingCreateContext;

	FSlateRenderBatchOp* RenderBatchHeadOp = nullptr;
	FSlateRenderBatchOp* RenderBatchTailOp = nullptr;
	int32 NumRenderBatchOps = 0;

	const auto FlushDrawElementsPass = [&]
	{
		if (!NumRenderBatchOps)
		{
			return;
		}

		if (ERenderTargetLoadAction LoadAction = ConsumeLoadAction(ElementsLoadAction); LoadAction != ERenderTargetLoadAction::ELoad)
		{
			// Load action differs from the default read one, so make a copy and modify.
			LastPassParameters = GraphBuilder.AllocParameters<FSlateRenderBatchParameters>(LastPassParameters);
			LastPassParameters->RenderTargets[0].SetLoadAction(LoadAction);
		}

		FRDGPass* Pass = GraphBuilder.AddPass(RDG_EVENT_NAME("ElementBatch"), LastPassParameters, ERDGPassFlags::Raster, [Inputs = RenderBatchDrawInputs, RenderBatchHeadOp] (FRDGAsyncTask, FRHICommandList& RHICmdList)
		{
			RHICmdList.SetViewport(Inputs->ElementsViewRect.Min.X, Inputs->ElementsViewRect.Min.Y, 0.0f, Inputs->ElementsViewRect.Max.X, Inputs->ElementsViewRect.Max.Y, 1.0f);
			RHICmdList.SetScissorRect(false, 0, 0, 0, 0);

			FSlateRenderBatchDrawState DrawState;
			RHICmdList.ApplyCachedRenderTargets(DrawState.GraphicsPSOInit);
			DrawState.GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();

			const FSlateRenderBatchOp* RenderBatchOp = RenderBatchHeadOp;
			const FSlateRenderBatchOp* LastRenderBatchOp = nullptr;

			for (; RenderBatchOp != nullptr; RenderBatchOp = RenderBatchOp->Next)
			{
				DrawSlateRenderBatch(RHICmdList, DrawState, *Inputs, *RenderBatchOp);
				LastRenderBatchOp = RenderBatchOp;
			}
		});

		GraphBuilder.SetPassWorkload(Pass, NumRenderBatchOps);
		RenderBatchHeadOp = RenderBatchTailOp = nullptr;
		NumRenderBatchOps = 0;
	};

	int32 NextRenderBatchIndex = FirstBatchIndex;

	while (NextRenderBatchIndex != INDEX_NONE)
	{
		const FSlateRenderBatch& NextRenderBatch = RenderBatches[NextRenderBatchIndex];

		NextRenderBatchIndex = NextRenderBatch.NextBatchIndex;

		FSlateRenderBatchParameters* NextPassParameters  = LastPassParameters;
		const FSlateClippingState*   NextClippingState   = NextRenderBatch.ClippingState;
		const FSlateClippingOp*      NextClippingOp      = LastClippingOp;

		if (NextClippingState != LastClippingState)
		{
			NextClippingOp = CreateSlateClipping(GraphBuilder, Inputs.ElementsOffset, NextClippingState, ClippingCreateContext);

			switch (ClippingCreateContext.StencilAction)
			{
			case ESlateClippingStencilAction::Clear:
				NextPassParameters = ClearStencilActionPassParameters;
				break;

			case ESlateClippingStencilAction::Write:
				NextPassParameters = WriteStencilActionPassParameters;
				break;

			case ESlateClippingStencilAction::None:
				NextPassParameters = NoneStencilActionPassParameters;
				break;
			}

			LastClippingState = NextClippingState;
			LastClippingOp = NextClippingOp;
		}

		const ESlateRenderBatchType NextRenderBatchType = GetSlateRenderBatchType(NextRenderBatch);

		// Flush all primitive render batches when we encounter one that can't be added.
		if (NextRenderBatchType != ESlateRenderBatchType::Primitive || NextPassParameters != LastPassParameters)
		{
			FlushDrawElementsPass();
		}

		LastPassParameters = NextPassParameters;

		switch (NextRenderBatchType)
		{
		case ESlateRenderBatchType::CustomDrawer:
		{
			// Clear the color texture if we haven't done it yet.
			if (ConsumeLoadAction(ElementsLoadAction) == ERenderTargetLoadAction::EClear)
			{
				AddClearRenderTargetPass(GraphBuilder, Inputs.ElementsTexture);
			}

			ICustomSlateElement::FDrawPassInputs DrawInputs;
			DrawInputs.ElementsMatrix = Inputs.ElementsMatrix;
			DrawInputs.ElementsOffset = Inputs.ElementsOffset;
			DrawInputs.OutputTexture = ElementsTexture.Texture;
			DrawInputs.SceneViewRect = Inputs.SceneViewRect;
			DrawInputs.HDRDisplayColorGamut = Inputs.HDRDisplayColorGamut;
			DrawInputs.UsedSlatePostBuffers = Inputs.UsedSlatePostBuffers;
			DrawInputs.bOutputIsHDRDisplay = Inputs.bElementsTextureIsHDRDisplay;
			DrawInputs.bWireFrame = Inputs.bWireframe;

			NextRenderBatch.CustomDrawer->Draw_RenderThread(GraphBuilder, DrawInputs);

			// Reset cached clipping state since custom draws mutate render state.
			LastClippingState = nullptr;
			break;
		}
		case ESlateRenderBatchType::PostProcess:
		{
			const FShaderParams& ShaderParams = NextRenderBatch.ShaderParams;

			FSlatePostProcessBlurPassInputs BlurInputs;

			if (Inputs.SceneViewportTexture && Inputs.SceneViewportTexture != Inputs.ElementsTexture)
			{
				// Blur uses the scene viewport texture output as blur input and composites UI separately.
				BlurInputs.InputTexture = Inputs.SceneViewportTexture;

				if (HasBeenProduced(Inputs.ElementsTexture))
				{
					BlurInputs.SDRCompositeUITexture = Inputs.ElementsTexture;
				}
			}
			else
			{
				// UI elements and scene are already composited together.
				BlurInputs.InputTexture = Inputs.ElementsTexture;
			}

			BlurInputs.InputRect = FIntRect(ShaderParams.PixelParams.X, ShaderParams.PixelParams.Y, ShaderParams.PixelParams.Z, ShaderParams.PixelParams.W);
			BlurInputs.OutputTexture = Inputs.SceneViewportTexture ? Inputs.SceneViewportTexture : Inputs.ElementsTexture;
			BlurInputs.OutputRect = BlurInputs.InputRect;
			BlurInputs.OutputLoadAction = ConsumeLoadAction(ElementsLoadAction);
			BlurInputs.ClippingOp = NextClippingOp;
			BlurInputs.ClippingStencilBinding = &NextPassParameters->RenderTargets.DepthStencil;
			BlurInputs.ClippingElementsViewRect = RenderBatchDrawInputs->ElementsViewRect;
			BlurInputs.KernelSize = ShaderParams.PixelParams2.X;
			BlurInputs.Strength = ShaderParams.PixelParams2.Y;
			BlurInputs.DownsampleAmount = ShaderParams.PixelParams2.Z;
			BlurInputs.CornerRadius = ShaderParams.PixelParams3;

			AddSlatePostProcessBlurPass(GraphBuilder, BlurInputs);
			break;
		}
		case ESlateRenderBatchType::Primitive:
		{
			if (FSlateRenderBatchOp* RenderBatchOp = CreateSlateRenderBatchOp(GraphBuilder, RenderBatchCreateInputs, &NextRenderBatch, NextClippingOp))
			{
				if (!RenderBatchTailOp)
				{
					RenderBatchHeadOp = RenderBatchTailOp = RenderBatchOp;
				}
				else
				{
					RenderBatchTailOp->Next = RenderBatchOp;
					RenderBatchTailOp = RenderBatchOp;
				}
				NumRenderBatchOps++;
			}

			break;
		}
		default: checkNoEntry();
		}
	}

	if (NumRenderBatchOps > 0)
	{
		FlushDrawElementsPass();
	}

	// If no batches were rendered at all, then we might need to just clear the render target.
	if (ConsumeLoadAction(ElementsLoadAction) == ERenderTargetLoadAction::EClear)
	{
		AddClearRenderTargetPass(GraphBuilder, Inputs.ElementsTexture);
	}
	else
	{
		// Don't do color correction on iOS or Android, we don't have the GPU overhead for it.
#if !(PLATFORM_IOS || PLATFORM_ANDROID)
		if (Inputs.bAllowColorDeficiencyCorrection && GSlateColorDeficiencyType != EColorVisionDeficiency::NormalVision && GSlateColorDeficiencySeverity > 0)
		{
			FSlatePostProcessColorDeficiencyPassInputs ColorDeficiencyInputs;
			ColorDeficiencyInputs.InputTexture = ElementsTexture;
			ColorDeficiencyInputs.OutputTexture = ElementsTexture;

			AddSlatePostProcessColorDeficiencyPass(GraphBuilder, ColorDeficiencyInputs);
		}
#endif
	}
	
	INC_DWORD_STAT_BY(STAT_SlateScissorClips, ClippingCreateContext.NumScissors);
	INC_DWORD_STAT_BY(STAT_SlateStencilClips, ClippingCreateContext.NumStencils);
}