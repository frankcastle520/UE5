// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
LandscapeEditLayers.cpp: Landscape editing layers mode
=============================================================================*/

#include "LandscapeEdit.h"
#include "Landscape.h"
#include "LandscapeEditLayer.h"
#include "LandscapeProxy.h"
#include "LandscapeStreamingProxy.h"
#include "LandscapeInfo.h"
#include "LandscapeComponent.h"
#include "LandscapeLayerInfoObject.h"
#include "LandscapeDataAccess.h"
#include "LandscapePrivate.h"
#include "LandscapeEditReadback.h"
#include "LandscapeEditResources.h"
#include "LandscapeEditResourcesSubsystem.h"
#include "LandscapeEditLayerRendererPrivate.h"
#include "LandscapeEditTypes.h"
#include "LandscapeUtils.h"
#include "LandscapeUtilsPrivate.h"
#include "LandscapeSubsystem.h"
#include "LandscapeTextureStreamingManager.h"

#include "Application/SlateApplicationBase.h"
#include "Shader.h"
#include "GlobalShader.h"
#include "ShaderParameterUtils.h"
#include "EngineModule.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/TextureRenderTarget2DArray.h"
#include "Logging/MessageLog.h"
#include "RenderCaptureInterface.h"
#include "RenderGraph.h"
#include "RenderGraphUtils.h"
#include "PixelShaderUtils.h"
#include "SystemTextures.h"
#include "Rendering/Texture2DResource.h"
#include "SceneView.h"
#include "MaterialCachedData.h"
#include "ContentStreaming.h"
#include "Templates/TypeHash.h"
#include "RHIResourceUtils.h"

#if WITH_EDITOR
#include "AssetCompilingManager.h"
#include "LandscapeEditorModule.h"
#include "LandscapeToolInterface.h"
#include "ComponentRecreateRenderStateContext.h"
#include "LandscapeBlueprintBrushBase.h"
#include "Materials/MaterialInstanceConstant.h"
#include "LandscapeMaterialInstanceConstant.h"
#include "Materials/MaterialExpressionLandscapeVisibilityMask.h"
#include "ShaderCompiler.h"
#include "Algo/Accumulate.h"
#include "Algo/Count.h"
#include "Algo/AnyOf.h"
#include "Algo/AllOf.h"
#include "Algo/Unique.h"
#include "Algo/Transform.h"
#include "Algo/ForEach.h"
#include "Algo/RemoveIf.h"
#include "LandscapeSettings.h"
#include "LandscapeRender.h"
#include "LandscapeInfoMap.h"
#include "Misc/MessageDialog.h"
#include "GlobalMergeLegacySupportUtil.h" // ILandscapeBrushRenderCallAdapter_GlobalMergeLegacySupport
#include "GameFramework/WorldSettings.h"
#include "UObject/UObjectThreadContext.h"
#include "LandscapeSplinesComponent.h"
#include "Misc/FileHelper.h"
#include "Misc/MapErrors.h"
#include "Misc/UObjectToken.h"
#include "Misc/ScopeExit.h"
#include "Misc/ScopedSlowTask.h"
#include "TextureCompiler.h"
#include "Editor.h"
#include "LandscapeNotification.h"
#include "LandscapeSubsystem.h"
#include "ObjectCacheContext.h"
#include "Components/RuntimeVirtualTextureComponent.h"
#include "UnrealEdGlobals.h" // GUnrealEd
#include "Editor/UnrealEdEngine.h"
#include "VisualLogger/VisualLogger.h"
#endif

#define LOCTEXT_NAMESPACE "Landscape"

// TODO [jonathan.bard] this define is for when(/if) we implement the uber landscape material in the editor, where weightmaps are *not* RGBA packed but stored in a plain texture array. 
//  This will allow several simplifications and optimizations to edit layers 
#define SUPPORTS_LANDSCAPE_EDITORONLY_UBER_MATERIAL 0

// Channel remapping
extern const size_t ChannelOffsets[4];

ENGINE_API extern bool GDisableAutomaticTextureMaterialUpdateDependencies;

// GPU profiling stats
DECLARE_GPU_STAT_NAMED(LandscapeLayers_Clear, TEXT("Landscape Layer Clear"));
DECLARE_GPU_STAT_NAMED(LandscapeLayers_Render, TEXT("Landscape Layer Render"));
DECLARE_GPU_STAT_NAMED(LandscapeLayers_CopyTexture, TEXT("Landscape Layer Copy Texture"));
DECLARE_GPU_STAT_NAMED(LandscapeLayers_CopyTexturePS, TEXT("Landscape Layer Copy Texture PS"));
DECLARE_GPU_STAT_NAMED(LandscapeLayers_ExtractLayers, TEXT("Landscape Extract Layers"));
DECLARE_GPU_STAT_NAMED(LandscapeLayers_PackLayers, TEXT("Landscape Pack Layers"));

#if WITH_EDITOR
static TAutoConsoleVariable<int32> CVarForceLayersUpdate(
	TEXT("landscape.ForceLayersUpdate"),
	0,
	TEXT("This will force landscape edit layers to be update every frame, rather than when requested only."));

int32 RenderCaptureLayersNextHeightmapDraws = 0;
static FAutoConsoleVariableRef CVarRenderCaptureLayersNextHeightmapDraws(
	TEXT("landscape.RenderCaptureLayersNextHeightmapDraws"),
	RenderCaptureLayersNextHeightmapDraws,
	TEXT("Trigger N render captures during the next heightmap draw calls."));

int32 RenderCaptureLayersNextWeightmapDraws = 0;
static FAutoConsoleVariableRef CVarRenderCaptureLayersNextWeightmapDraws(
	TEXT("landscape.RenderCaptureLayersNextWeightmapDraws"),
	RenderCaptureLayersNextWeightmapDraws,
	TEXT("Trigger N render captures during the next weightmap draw calls."));

static TAutoConsoleVariable<int32> CVarOutputLayersRTContent(
	TEXT("landscape.OutputLayersRTContent"),
	0,
	TEXT("This will output the content of render target. This is used for debugging only."));

static TAutoConsoleVariable<int32> CVarOutputLayersWeightmapsRTContent(
	TEXT("landscape.OutputLayersWeightmapsRTContent"),
	0,
	TEXT("This will output the content of render target used for weightmap. This is used for debugging only."));

static TAutoConsoleVariable<int32> CVarLandscapeSimulatePhysics(
	TEXT("landscape.SimulatePhysics"),
	0,
	TEXT("This will enable physic simulation on worlds containing landscape."));

static TAutoConsoleVariable<int32> CVarLandscapeLayerOptim(
	TEXT("landscape.Optim"),
	1,
	TEXT("This will enable landscape layers optim."));

static TAutoConsoleVariable<int32> CVarLandscapeLayerBrushOptim(
	TEXT("landscape.BrushOptim"),
	0,
	TEXT("This will enable landscape layers optim."));

static TAutoConsoleVariable<int32> CVarLandscapeDumpHeightmapDiff(
	TEXT("landscape.DumpHeightmapDiff"),
	0,
	TEXT("This will save images for readback heightmap textures that have changed in the last edit layer blend phase. (= 0 No Diff, 1 = Mip 0 Diff, 2 = All Mips Diff"));

static TAutoConsoleVariable<int32> CVarLandscapeDumpWeightmapDiff(
	TEXT("landscape.DumpWeightmapDiff"),
	0,
	TEXT("This will save images for readback weightmap textures that have changed in the last edit layer blend phase. (= 0 No Diff, 1 = Mip 0 Diff, 2 = All Mips Diff"));

static TAutoConsoleVariable<bool> CVarLandscapeDumpDiffDetails(
	TEXT("landscape.DumpDiffDetails"),
	false,
	TEXT("When dumping diffs for heightmap (landscape.DumpHeightmapDiff) or weightmap (landscape.DumpWeightmapDiff), dumps additional details about the pixels being different"));

TAutoConsoleVariable<int32> CVarLandscapeDirtyHeightmapHeightThreshold(
	TEXT("landscape.DirtyHeightmapHeightThreshold"),
	0,
	TEXT("Threshold to avoid imprecision issues on certain GPUs when detecting when a heightmap height changes, i.e. only a height difference > than this threshold (N over 16-bits uint height) will be detected as a change."));

TAutoConsoleVariable<int32> CVarLandscapeDirtyHeightmapNormalThreshold(
	TEXT("landscape.DirtyHeightmapNormalThreshold"),
	0,
	TEXT("Threshold to avoid imprecision issues on certain GPUs when detecting when a heightmap normal changes, i.e. only a normal channel difference > than this threshold (N over each 8-bits uint B & A channels independently) will be detected as a change."));

TAutoConsoleVariable<int32> CVarLandscapeDirtyWeightmapThreshold(
	TEXT("landscape.DirtyWeightmapThreshold"),
	0,
	TEXT("Threshold to avoid imprecision issues on certain GPUs when detecting when a weightmap changes, i.e. only a difference > than this threshold (N over each 8-bits uint weightmap channel)."));

TAutoConsoleVariable<int32> CVarLandscapeShowDirty(
	TEXT("landscape.ShowDirty"),
	0,
	TEXT("This will highlight the data that has changed during the layer blend phase."));

TAutoConsoleVariable<int32> CVarLandscapeTrackDirty(
	TEXT("landscape.TrackDirty"),
	0,
	TEXT("This will track the accumulation of data changes during the layer blend phase."));

TAutoConsoleVariable<int32> CVarLandscapeForceFlush(
	TEXT("landscape.ForceFlush"),
	0,
	TEXT("This will force a render flush every frame when landscape editing."));

TAutoConsoleVariable<int32> CVarLandscapeValidateProxyWeightmapUsages(
	TEXT("landscape.ValidateProxyWeightmapUsages"),
	1,
	TEXT("This will validate that weightmap usages in landscape proxies and their components don't get desynchronized with the landscape component layer allocations."));

TAutoConsoleVariable<int32> CVarLandscapeRemoveEmptyPaintLayersOnEdit(
	TEXT("landscape.RemoveEmptyPaintLayersOnEdit"),
	// TODO [jonathan.bard] : this has been disabled for now, since it can lead to a permanent dirty-on-load state for landscape, where the edit layers will do a new weightmap allocation for the missing layer
	//  (e.g. if a BP brush writes to it), only to remove it after readback, which will lead to the actor to be marked dirty. We need to separate the final from the source weightmap data to avoid this issue : 
	0, 
	TEXT("This will analyze weightmaps on readback and remove unneeded allocations (for unpainted layers)."));

TAutoConsoleVariable<float> CVarLandscapeBatchedMergeVisualLogOffsetIncrement(
	TEXT("landscape.BatchedMerge.VisualLog.OffsetIncrement"),
	5000.0f,
	TEXT("Offset (in unreal units) for visualizing each operation of the batched merge in the viewport via the visual logger."));

TAutoConsoleVariable<float> CVarLandscapeBatchedMergeVisualLogAlpha(
	TEXT("landscape.BatchedMerge.VisualLog.Alpha"),
	0.5f,
	TEXT("Alpha value to use when visualizing batched merge info in the viewport via the visual logger ([0.0, 1.0] range)"));

TAutoConsoleVariable<int32> CVarLandscapeBatchedMergeVisualLogShowMergeType(
	TEXT("landscape.BatchedMerge.VisualLog.ShowMergeType"),
	3,
	TEXT("Filter what to visualize in the visual logger when using batched merge (0 = no visual log, 1 = show heightmaps only, 2 = show weightmaps only, 3 = show all"));

TAutoConsoleVariable<bool> CVarLandscapeBatchedMergeVisualLogShowMergeProcess(
	TEXT("landscape.BatchedMerge.VisualLog.ShowMergeProcess"),
	false,
	TEXT("Allows to visualize the merge process in the visual logger (batches, affected components, etc.)"));

TAutoConsoleVariable<int32> CVarLandscapeBatchedMergeVisualLogShowRenderItemsType(
	TEXT("landscape.BatchedMerge.VisualLog.ShowRenderItemsType"),
	0,
	TEXT("Allows to visualize the edit layer renderers' render items in the visual logger when using batched merge (0 = no visual log, 1 = show input areas, 2 = show output areas, 3 = show all)"));

TAutoConsoleVariable<FString> CVarLandscapeBatchedMergeVisualLogShowRenderItemsEditLayerRendererFilter(
	TEXT("landscape.BatchedMerge.VisualLog.ShowRenderItemsEditLayerRendererFilter"),
	"",
	TEXT("Allows to filter the elements added to the visual log to only those pertaining to a given edit layer renderer : use in conjunction with landscape.BatchedMerge.VisualLog.ShowRenderItemsType (empty : display all elements, otherwise, only display the items related to the edit layer renderer if its name matches (partial match)"));

TAutoConsoleVariable<bool> CVarLandscapeBatchedMergeVisualLogShowAllRenderItems(
	TEXT("landscape.BatchedMerge.VisualLog.ShowAllRenderItems"),
	false,
	TEXT("Allows to visualize all render items : use in conjunction with landscape.BatchedMerge.VisualLog.ShowRenderItemsType (if true, all render items will be displayed. If false, only those that participate to the render will be"));

TAutoConsoleVariable<int32> CVarLandscapeBatchedMergeVisualLogShowComponentDependencies(
	TEXT("landscape.BatchedMerge.VisualLog.ShowComponentDependencies"),
	0,
	TEXT("Allows to visualize the dependencies between landscape components when using batched merge (0 = no visual log, 1 = show component coordinates + area affecting component, 2 = show component coordinates + renderer name + area affecting component)"));

TAutoConsoleVariable<FString> CVarLandscapeBatchedMergeVisualLogShowComponentDependenciesFilter(
	TEXT("landscape.BatchedMerge.VisualLog.ShowComponentDependenciesFilter"),
	"",
	TEXT("Allows to visualize all the dependency graph for the component specified : use the \"X= Y=\" format to specify the component for which to show the dependencies"));


void OnLandscapeEditLayersLocalMergeChanged(IConsoleVariable* CVar)
{
	for (TObjectIterator<UWorld> It(/*AdditionalExclusionFlags = */RF_ClassDefaultObject, /*bIncludeDerivedClasses = */true, /*InInternalExclusionFlags = */EInternalObjectFlags::Garbage); It; ++It)
	{
		UWorld* CurrentWorld = *It;
		if (!CurrentWorld->IsGameWorld())
		{
			ULandscapeInfoMap& LandscapeInfoMap = ULandscapeInfoMap::GetLandscapeInfoMap(CurrentWorld);
			for (auto& Pair : LandscapeInfoMap.Map)
			{
				if (ULandscapeInfo* LandscapeInfo = Pair.Value)
				{
					if (ALandscape* Landscape = LandscapeInfo->LandscapeActor.Get())
					{
						Landscape->RequestLayersInitialization(true);
					}
				}
			}
		}
	}
}

int32 LandscapeEditLayersLocalMerge = 0;
static FAutoConsoleVariableRef CVarLandscapeEditLayersLocalMerge(
	TEXT("landscape.EditLayersLocalMerge.Enable"),
	LandscapeEditLayersLocalMerge,
	TEXT("Setting this to 1 will allow the local merge algorithm (that merges layers at the landscape component level) to be used on landscapes that support it. This is a temporary measure while waiting for non-compatible landscapes to be deprecated.\n\
		Setting this to 2 will allow the batched merge algorithm (that merges layers in batches). Supports all landscape types. This is also a temporary measure but will be the default once it's stable."),
	FConsoleVariableDelegate::CreateStatic(OnLandscapeEditLayersLocalMergeChanged));

TAutoConsoleVariable<int32> CVarLandscapeEditLayersMaxComponentsPerHeightmapResolveBatch(
	TEXT("landscape.EditLayersLocalMerge.MaxComponentsPerHeightmapResolveBatch"),
	16,
	TEXT("Number of components being rendered in a single batch when resolving heightmaps. The higher the number, the more heightmaps can be resolved in a single batch (and the higher the GPU memory consumption since more transient textures will be needed in memory at a time)"),
	ECVF_RenderThreadSafe);

TAutoConsoleVariable<int32> CVarLandscapeEditLayersMaxComponentsPerWeightmapResolveBatch(
	TEXT("landscape.EditLayersLocalMerge.MaxComponentsPerWeightmapResolveBatch"),
	16,
	TEXT("Number of components being rendered in a single batch when resolving weightmaps. The higher the number, the more weightmaps can be resolved in a single batch (and the higher the GPU memory consumption since more transient textures will be needed in memory at a time)"),
	ECVF_RenderThreadSafe);

TAutoConsoleVariable<int32> CVarLandscapeEditLayersMaxResolutionPerRenderBatch(
	TEXT("landscape.EditLayersLocalMerge.MaxResolutionPerRenderBatch"),
	1024,
	TEXT("Maximum supported resolution for merging edit layers in a single batch. The higher the number, the more components can be resolved in a single batch (and the higher the GPU memory consumption since merging requires several temporary textures)"),
	ECVF_RenderThreadSafe);

TAutoConsoleVariable<int32> CVarLandscapeEditLayersClearBeforeEachWriteToScratch(
	TEXT("landscape.EditLayersLocalMerge.ClearBeforeEachWriteToScratch"),
	0,
	TEXT("Debug to help with RenderDoc debugging : clear each time we're about to write on a scratch render target (since those are reused and can be used to write RTs of different resolutions"),
	ECVF_RenderThreadSafe);

struct FLandscapeDirty
{
	FLandscapeDirty()
		: ClearDiffConsoleCommand(
			TEXT("Landscape.ClearDirty"),
			TEXT("Clears all Landscape Dirty Debug Data"),
			FConsoleCommandDelegate::CreateRaw(this, &FLandscapeDirty::ClearDirty))
	{
	}

private:
	FAutoConsoleCommand ClearDiffConsoleCommand;

	void ClearDirty()
	{
		bool bCleared = false;
		for (TObjectIterator<UWorld> It(/*AdditionalExclusionFlags = */RF_ClassDefaultObject, /*bIncludeDerivedClasses = */true, /*InInternalExclusionFlags = */EInternalObjectFlags::Garbage); It; ++It)
		{
			UWorld* CurrentWorld = *It;
			if (!CurrentWorld->IsGameWorld())
			{
				ULandscapeInfoMap& LandscapeInfoMap = ULandscapeInfoMap::GetLandscapeInfoMap(CurrentWorld);
				for (auto& Pair : LandscapeInfoMap.Map)
				{
					if (Pair.Value && Pair.Value->SupportsLandscapeEditing())
					{
						Pair.Value->ClearDirtyData();
						bCleared = true;
					}
				}
			}
		}

		UE_LOG(LogLandscape, Verbose, TEXT("Landscape.Dirty: %s"), bCleared ? TEXT("Cleared") : TEXT("Landscape.Dirty: Nothing to clear"));
	}
};

FLandscapeDirty GLandscapeDebugDirty;

/**
 * Mapping between heightmaps/weightmaps and components.
 * It's not safe to persist this across frames, so we recalculate at the start of each update.
 */
struct FTextureToComponentHelper
{
	// Partial refresh flags : allows to recompute only a subset of the helper information :
	enum class ERefreshFlags
	{
		None = 0,
		RefreshComponents = (1 << 0),
		RefreshHeightmaps = (1 << 1),
		RefreshWeightmaps = (1 << 2),
		RefreshAll = ~0,
	};
	FRIEND_ENUM_CLASS_FLAGS(ERefreshFlags);

	FTextureToComponentHelper(const ULandscapeInfo& InLandscapeInfo)
		: LandscapeInfo(&InLandscapeInfo)
	{
		Refresh(ERefreshFlags::RefreshAll);
	}

	void Refresh(ERefreshFlags InRefreshFlags)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(TextureToComponentHelper_Refresh);
		// Compute the list of components in this landscape : 
		if (EnumHasAnyFlags(InRefreshFlags, ERefreshFlags::RefreshComponents))
		{
			// When components are refreshed, all other info has to be :
			check(EnumHasAllFlags(InRefreshFlags, ERefreshFlags::RefreshAll));

			LandscapeComponents.Reset();
			LandscapeInfo->ForAllLandscapeComponents([&](ULandscapeComponent* Component)
			{
				LandscapeComponents.Add(Component);
			});
		}

		if (EnumHasAnyFlags(InRefreshFlags, ERefreshFlags::RefreshHeightmaps | ERefreshFlags::RefreshWeightmaps))
		{
			// Cleanup our heightmap/weightmap info:
			if (EnumHasAnyFlags(InRefreshFlags, ERefreshFlags::RefreshHeightmaps))
			{
				Heightmaps.Reset();
				HeightmapToComponents.Reset();
			}

			if (EnumHasAnyFlags(InRefreshFlags, ERefreshFlags::RefreshWeightmaps))
			{
				Weightmaps.Reset();
				WeightmapToComponents.Reset();
				WeightmapToChannelMask.Reset();
			}

			// Iterate on all tracked landscape components and keep track of components/heightmaps/weightmaps relationship :
			for (ULandscapeComponent* Component : LandscapeComponents)
			{
				if (EnumHasAnyFlags(InRefreshFlags, ERefreshFlags::RefreshHeightmaps))
				{
					UTexture2D* Heightmap = Component->GetHeightmap();
					check(Heightmap != nullptr);

					Heightmaps.Add(Heightmap);
					HeightmapToComponents.FindOrAdd(Heightmap).Add(Component);
				}

				if (EnumHasAnyFlags(InRefreshFlags, ERefreshFlags::RefreshWeightmaps))
				{
					const TArray<UTexture2D*>& WeightmapTextures = Component->GetWeightmapTextures();
					const TArray<FWeightmapLayerAllocationInfo>& AllocInfos = Component->GetWeightmapLayerAllocations();

					for (FWeightmapLayerAllocationInfo const& AllocInfo : AllocInfos)
					{
						if (AllocInfo.IsAllocated() && AllocInfo.WeightmapTextureIndex < WeightmapTextures.Num())
						{
							UTexture2D* Weightmap = WeightmapTextures[AllocInfo.WeightmapTextureIndex];
							check(Weightmap != nullptr);

							Weightmaps.Add(Weightmap);

							WeightmapToComponents.FindOrAdd(Weightmap).AddUnique(Component);
							uint8& WeightmapChannelMask = WeightmapToChannelMask.FindOrAdd(Weightmap, 0);
							WeightmapChannelMask |= (1 << AllocInfo.WeightmapTextureChannel);
						}
					}
				}
			}
		}
	}

	const ULandscapeInfo* LandscapeInfo = nullptr;
	TArray< ULandscapeComponent* > LandscapeComponents;
	TSet< UTexture2D* > Heightmaps;
	TMap< UTexture2D*, TArray<ULandscapeComponent*> > HeightmapToComponents;
	TSet< UTexture2D* > Weightmaps;
	// Key = texture, Value = list of components that use this texture in their weightmap layer allocations 
	TMap< UTexture2D*, TArray<ULandscapeComponent*> > WeightmapToComponents;
	// Key = texture, Value = final channel mask for this texture (i.e. which channel is actually allocated in any component)
	TMap<UTexture2D*, uint8> WeightmapToChannelMask;
};
ENUM_CLASS_FLAGS(FTextureToComponentHelper::ERefreshFlags);

// Must match EEditLayerHeightmapBlendMode in LandscapeLayersHeightmapsPS.usf
enum class ELandscapeEditLayerHeightmapBlendMode : uint32
{
	Additive = 0,
	AlphaBlend,

	Num,
};

#endif // WITH_EDITOR

namespace UE::Landscape::Private
{

#if WITH_EDITOR
	static FFileHelper::EColorChannel GetWeightmapColorChannel(const FWeightmapLayerAllocationInfo& AllocInfo)
	{
		FFileHelper::EColorChannel ColorChannelMapping[] = { FFileHelper::EColorChannel::R, FFileHelper::EColorChannel::G, FFileHelper::EColorChannel::B, FFileHelper::EColorChannel::A };
		FFileHelper::EColorChannel ColorChannel = FFileHelper::EColorChannel::All;
	
		if (ensure(AllocInfo.WeightmapTextureChannel < 4))
		{
			ColorChannel = ColorChannelMapping[AllocInfo.WeightmapTextureChannel];
		}

		return ColorChannel;
	}

	static ELandscapeEditLayerHeightmapBlendMode LandscapeBlendModeToEditLayerBlendMode(ELandscapeBlendMode InLandscapeBlendMode)
	{
		switch (InLandscapeBlendMode)
		{
		case LSBM_AdditiveBlend:
			return ELandscapeEditLayerHeightmapBlendMode::Additive;
		case LSBM_AlphaBlend:
			return ELandscapeEditLayerHeightmapBlendMode::AlphaBlend;
		default:
			check(false);
		}

		return ELandscapeEditLayerHeightmapBlendMode::Num;
	}
#endif // WITH_EDITOR
	

	// ----------------------------------------------------------------------------------
	// Texture channel swizzling : 
	enum class ERGBAChannel : uint8 { R, G, B, A };

	static constexpr uint8 BuildChannelSwizzleMask(ERGBAChannel InChannelR = ERGBAChannel::R, ERGBAChannel InChannelG = ERGBAChannel::G, ERGBAChannel InChannelB = ERGBAChannel::B, ERGBAChannel InChannelA = ERGBAChannel::A)
	{
		return (static_cast<uint8>(InChannelR) << 0)
			| (static_cast<uint8>(InChannelG) << 2)
			| (static_cast<uint8>(InChannelB) << 4)
			| (static_cast<uint8>(InChannelA) << 6);
	}

	static ERGBAChannel ExtractDestinationChannelFromSwizzleMask(ERGBAChannel InSourceChannel, uint8 InSwizzleMask)
	{
		uint8 SourceChannelIndex = static_cast<uint8>(InSourceChannel);
		return static_cast<ERGBAChannel>((((uint8)3 << (SourceChannelIndex * 2)) & InSwizzleMask) >> (SourceChannelIndex * 2));
	}

	FString GetChannelSwizzleMaskDescription(uint8 InSwizzleMask, int32 InNumChannels = 4)
	{
		static constexpr auto ChannelToChar = [](ERGBAChannel InChannel) { return (InChannel == ERGBAChannel::R) ? TCHAR('R') : (InChannel == ERGBAChannel::G) ? TCHAR('G') : (InChannel == ERGBAChannel::B) ? TCHAR('B') : TCHAR('A'); };
		check(InNumChannels <= 4);
		const TCHAR ChannelsChar[4] =
		{
			ChannelToChar(ExtractDestinationChannelFromSwizzleMask(ERGBAChannel::R, InSwizzleMask)),
			ChannelToChar(ExtractDestinationChannelFromSwizzleMask(ERGBAChannel::G, InSwizzleMask)),
			ChannelToChar(ExtractDestinationChannelFromSwizzleMask(ERGBAChannel::B, InSwizzleMask)),
			ChannelToChar(ExtractDestinationChannelFromSwizzleMask(ERGBAChannel::A, InSwizzleMask))
		};
		FString Result;
		Result.AppendChars(ChannelsChar, InNumChannels);
		return Result;
	}

	static constexpr uint8 RGBAToRGBASwizzleMask = BuildChannelSwizzleMask(ERGBAChannel::R, ERGBAChannel::G, ERGBAChannel::B, ERGBAChannel::A);
}

// Vertex format and vertex buffer

struct FLandscapeLayersVertex
{
	FVector2f Position;
	FVector2f UV;
};

struct FLandscapeLayersTriangle
{
	FLandscapeLayersVertex V0;
	FLandscapeLayersVertex V1;
	FLandscapeLayersVertex V2;
};

class FLandscapeLayersVertexDeclaration : public FRenderResource
{
public:
	FVertexDeclarationRHIRef VertexDeclarationRHI;

	/** Destructor. */
	virtual ~FLandscapeLayersVertexDeclaration() {}

	virtual void InitRHI(FRHICommandListBase& RHICmdList)
	{
		FVertexDeclarationElementList Elements;
		constexpr uint16 Stride = sizeof(FLandscapeLayersVertex);
		Elements.Add(FVertexElement(0, STRUCT_OFFSET(FLandscapeLayersVertex, Position), VET_Float2, 0, Stride));
		Elements.Add(FVertexElement(0, STRUCT_OFFSET(FLandscapeLayersVertex, UV), VET_Float2, 1, Stride));
		VertexDeclarationRHI = PipelineStateCache::GetOrCreateVertexDeclaration(Elements);
	}

	virtual void ReleaseRHI()
	{
		VertexDeclarationRHI.SafeRelease();
	}
};

class FLandscapeLayersVertexBuffer : public FVertexBuffer
{
public:
	void Init(const TArray<FLandscapeLayersTriangle>& InTriangleList)
	{
		TriangleList = InTriangleList;
	}

private:

	/** Initialize the RHI for this rendering resource */
	void InitRHI(FRHICommandListBase& RHICmdList) override
	{
		TArray<FLandscapeLayersVertex> Vertices;
		Vertices.SetNumUninitialized(TriangleList.Num() * 3);

		for (int32 i = 0; i < TriangleList.Num(); ++i)
		{
			Vertices[i * 3 + 0] = TriangleList[i].V0;
			Vertices[i * 3 + 1] = TriangleList[i].V1;
			Vertices[i * 3 + 2] = TriangleList[i].V2;
		}

		// Create vertex buffer. Fill buffer with initial data upon creation
		VertexBufferRHI = UE::RHIResourceUtils::CreateVertexBufferFromArray(RHICmdList, TEXT("FLandscapeLayersVertexBuffer"), EBufferUsageFlags::Static, MakeConstArrayView(Vertices));
	}

	TArray<FLandscapeLayersTriangle> TriangleList;
};

// ----------------------------------------------------------------------------------
// /Engine/Private/LandscapeLayersVS.usf shaders

class FLandscapeLayersVS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FLandscapeLayersVS)

public:

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return UE::Landscape::DoesPlatformSupportEditLayers(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
	}

	FLandscapeLayersVS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{
		TransformParam.Bind(Initializer.ParameterMap, TEXT("Transform"), SPF_Mandatory);
	}

	FLandscapeLayersVS()
	{}

	void SetParameters(FRHIBatchedShaderParameters& BatchedParameters, const FMatrix44f& InProjectionMatrix)
	{
		SetShaderValue(BatchedParameters, TransformParam, InProjectionMatrix);
	}

private:
	LAYOUT_FIELD(FShaderParameter, TransformParam);
};

IMPLEMENT_GLOBAL_SHADER(FLandscapeLayersVS, "/Engine/Private/LandscapeLayersVS.usf", "VSMain", SF_Vertex);

// ----------------------------------------------------------------------------------
// /Engine/Private/LandscapeLayersPS.usf shaders

struct FLandscapeLayersHeightmapShaderParameters
{
	FLandscapeLayersHeightmapShaderParameters()
		: ReadHeightmap1(nullptr)
		, ReadHeightmap2(nullptr)
		, HeightmapSize(0, 0)
		, ApplyLayerModifiers(false)
		, SetAlphaOne(false)
		, LayerAlpha(1.0f)
		, LayerVisible(true)
		, LayerBlendMode(LSBM_AdditiveBlend)
		, GenerateNormals(false)
		, OverrideBoundaryNormals(false) // HACK [chris.tchou] remove once we have a better boundary normal solution
		, GridSize(0.0f, 0.0f, 0.0f)
		, CurrentMipSize(0, 0)
		, ParentMipSize(0, 0)
		, CurrentMipComponentVertexCount(0)
	{}

	UTexture* ReadHeightmap1;
	UTexture* ReadHeightmap2;
	FIntPoint HeightmapSize;
	bool ApplyLayerModifiers;
	bool SetAlphaOne;
	float LayerAlpha;
	bool LayerVisible;
	ELandscapeBlendMode LayerBlendMode;
	bool GenerateNormals;
	bool OverrideBoundaryNormals; // HACK [chris.tchou] remove once we have a better boundary normal solution
	FVector GridSize;
	FIntPoint CurrentMipSize;
	FIntPoint ParentMipSize;
	int32 CurrentMipComponentVertexCount;
};

class FLandscapeLayersHeightmapPS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FLandscapeLayersHeightmapPS);
public:

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return UE::Landscape::DoesPlatformSupportEditLayers(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
	}

	FLandscapeLayersHeightmapPS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{
		ReadTexture1Param.Bind(Initializer.ParameterMap, TEXT("ReadTexture1"));
		ReadTexture2Param.Bind(Initializer.ParameterMap, TEXT("ReadTexture2"));
		ReadTexture1SamplerParam.Bind(Initializer.ParameterMap, TEXT("ReadTexture1Sampler"));
		ReadTexture2SamplerParam.Bind(Initializer.ParameterMap, TEXT("ReadTexture2Sampler"));

		LayerInfoParam.Bind(Initializer.ParameterMap, TEXT("LayerInfo"));
		OutputConfigParam.Bind(Initializer.ParameterMap, TEXT("OutputConfig"));
		TextureSizeParam.Bind(Initializer.ParameterMap, TEXT("TextureSize"));
		LandscapeGridScaleParam.Bind(Initializer.ParameterMap, TEXT("LandscapeGridScale"));
		ComponentVertexCountParam.Bind(Initializer.ParameterMap, TEXT("CurrentMipComponentVertexCount"));
	}

	FLandscapeLayersHeightmapPS()
	{}

	void SetParameters(FRHIBatchedShaderParameters& BatchedParameters, const FLandscapeLayersHeightmapShaderParameters& InParams)
	{
		SetTextureParameter(BatchedParameters, ReadTexture1Param, ReadTexture1SamplerParam, TStaticSamplerState<SF_Point, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI(), InParams.ReadHeightmap1->GetResource()->TextureRHI);
		SetTextureParameter(BatchedParameters, ReadTexture2Param, ReadTexture2SamplerParam, 
			(InParams.GenerateNormals && InParams.OverrideBoundaryNormals) 					// HACK [chris.tchou] remove once we have a better boundary normal solution
				? TStaticSamplerState<SF_Point, AM_Wrap, AM_Wrap, AM_Wrap>::GetRHI()		// override boundary normals requires wrap mode
				: TStaticSamplerState<SF_Point, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI(),
			InParams.ReadHeightmap2 != nullptr ? InParams.ReadHeightmap2->GetResource()->TextureRHI : GWhiteTexture->TextureRHI);

		FVector4f LayerInfo(InParams.LayerAlpha, InParams.LayerVisible ? 1.0f : 0.0f, InParams.LayerBlendMode == LSBM_AlphaBlend ? 1.0f : 0.f, 0.f);
		FVector4f OutputConfig(InParams.ApplyLayerModifiers ? 1.0f : 0.0f, InParams.SetAlphaOne ? 1.0f : 0.0f,
			(InParams.ReadHeightmap2 && !InParams.OverrideBoundaryNormals) ? 1.0f : 0.0f,			// HACK [chris.tchou] remove once we have a better boundary normal solution
			InParams.GenerateNormals ? (InParams.OverrideBoundaryNormals ? 2.0f : 1.0f) : 0.0f);	// HACK [chris.tchou] remove once we have a better boundary normal solution
		FVector2f TextureSize(static_cast<float>(InParams.HeightmapSize.X), static_cast<float>(InParams.HeightmapSize.Y));

		SetShaderValue(BatchedParameters, LayerInfoParam, LayerInfo);
		SetShaderValue(BatchedParameters, OutputConfigParam, OutputConfig);
		SetShaderValue(BatchedParameters, TextureSizeParam, TextureSize);
		SetShaderValue(BatchedParameters, LandscapeGridScaleParam, FVector3f(InParams.GridSize));
		SetShaderValue(BatchedParameters, ComponentVertexCountParam, (float)InParams.CurrentMipComponentVertexCount);
	}

private:
	LAYOUT_FIELD(FShaderResourceParameter, ReadTexture1Param);
	LAYOUT_FIELD(FShaderResourceParameter, ReadTexture2Param);
	LAYOUT_FIELD(FShaderResourceParameter, ReadTexture1SamplerParam);
	LAYOUT_FIELD(FShaderResourceParameter, ReadTexture2SamplerParam);
	LAYOUT_FIELD(FShaderParameter, LayerInfoParam);
	LAYOUT_FIELD(FShaderParameter, OutputConfigParam);
	LAYOUT_FIELD(FShaderParameter, TextureSizeParam);
	LAYOUT_FIELD(FShaderParameter, LandscapeGridScaleParam);
	LAYOUT_FIELD(FShaderParameter, ComponentVertexCountParam);
};

IMPLEMENT_GLOBAL_SHADER(FLandscapeLayersHeightmapPS, "/Engine/Private/LandscapeLayersPS.usf", "PSHeightmapMain", SF_Pixel);

class FLandscapeLayersHeightmapMipsPS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FLandscapeLayersHeightmapMipsPS);
public:

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return UE::Landscape::DoesPlatformSupportEditLayers(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
	}

	FLandscapeLayersHeightmapMipsPS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{
		ReadTexture1Param.Bind(Initializer.ParameterMap, TEXT("ReadTexture1"));
		ReadTexture1SamplerParam.Bind(Initializer.ParameterMap, TEXT("ReadTexture1Sampler"));
		CurrentMipSizeParam.Bind(Initializer.ParameterMap, TEXT("CurrentMipTextureSize"));
		ParentMipSizeParam.Bind(Initializer.ParameterMap, TEXT("ParentMipTextureSize"));
		CurrentMipComponentVertexCountParam.Bind(Initializer.ParameterMap, TEXT("CurrentMipComponentVertexCount"));
	}

	FLandscapeLayersHeightmapMipsPS()
	{}

	void SetParameters(FRHIBatchedShaderParameters& BatchedParameters, const FLandscapeLayersHeightmapShaderParameters& InParams)
	{
		SetTextureParameter(BatchedParameters, ReadTexture1Param, ReadTexture1SamplerParam, TStaticSamplerState<SF_Point, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI(), InParams.ReadHeightmap1->GetResource()->TextureRHI);

		SetShaderValue(BatchedParameters, CurrentMipSizeParam, FVector2f(static_cast<float>(InParams.CurrentMipSize.X), static_cast<float>(InParams.CurrentMipSize.Y)));
		SetShaderValue(BatchedParameters, ParentMipSizeParam, FVector2f(static_cast<float>(InParams.ParentMipSize.X), static_cast<float>(InParams.ParentMipSize.Y)));
		SetShaderValue(BatchedParameters, CurrentMipComponentVertexCountParam, (float)InParams.CurrentMipComponentVertexCount);
	}

private:
	LAYOUT_FIELD(FShaderResourceParameter, ReadTexture1Param);
	LAYOUT_FIELD(FShaderResourceParameter, ReadTexture1SamplerParam);
	LAYOUT_FIELD(FShaderParameter, CurrentMipSizeParam);
	LAYOUT_FIELD(FShaderParameter, ParentMipSizeParam);
	LAYOUT_FIELD(FShaderParameter, CurrentMipComponentVertexCountParam);
};

IMPLEMENT_GLOBAL_SHADER(FLandscapeLayersHeightmapMipsPS, "/Engine/Private/LandscapeLayersPS.usf", "PSHeightmapMainMips", SF_Pixel);

struct FLandscapeLayersWeightmapShaderParameters
{
	FLandscapeLayersWeightmapShaderParameters()
		: ReadWeightmap1(nullptr)
		, ReadWeightmap2(nullptr)
		, ApplyLayerModifiers(false)
		, LayerAlpha(1.0f)
		, LayerVisible(true)
		, LayerBlendMode(LSBM_AdditiveBlend)
		, OutputAsSubstractive(false)
		, CurrentMipSize(0, 0)
		, ParentMipSize(0, 0)
		, CurrentMipComponentVertexCount(0)
	{}

	UTexture* ReadWeightmap1;
	UTexture* ReadWeightmap2;
	bool ApplyLayerModifiers;
	float LayerAlpha;
	bool LayerVisible;
	ELandscapeBlendMode LayerBlendMode;
	bool OutputAsSubstractive;
	FIntPoint CurrentMipSize;
	FIntPoint ParentMipSize;
	int32 CurrentMipComponentVertexCount;
};

class FLandscapeLayersWeightmapPS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FLandscapeLayersWeightmapPS);
public:

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return UE::Landscape::DoesPlatformSupportEditLayers(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
	}

	FLandscapeLayersWeightmapPS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{
		ReadTexture1Param.Bind(Initializer.ParameterMap, TEXT("ReadTexture1"));
		ReadTexture2Param.Bind(Initializer.ParameterMap, TEXT("ReadTexture2"));
		ReadTexture1SamplerParam.Bind(Initializer.ParameterMap, TEXT("ReadTexture1Sampler"));
		ReadTexture2SamplerParam.Bind(Initializer.ParameterMap, TEXT("ReadTexture2Sampler"));
		LayerInfoParam.Bind(Initializer.ParameterMap, TEXT("LayerInfo"));
		OutputConfigParam.Bind(Initializer.ParameterMap, TEXT("OutputConfig"));
		ComponentVertexCountParam.Bind(Initializer.ParameterMap, TEXT("CurrentMipComponentVertexCount"));
	}

	FLandscapeLayersWeightmapPS()
	{}

	void SetParameters(FRHIBatchedShaderParameters& BatchedParameters, const FLandscapeLayersWeightmapShaderParameters& InParams)
	{
		SetTextureParameter(BatchedParameters, ReadTexture1Param, ReadTexture1SamplerParam, TStaticSamplerState<SF_Point, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI(), InParams.ReadWeightmap1->GetResource()->TextureRHI);
		SetTextureParameter(BatchedParameters, ReadTexture2Param, ReadTexture2SamplerParam, TStaticSamplerState<SF_Point, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI(), InParams.ReadWeightmap2 != nullptr ? InParams.ReadWeightmap2->GetResource()->TextureRHI : GWhiteTexture->TextureRHI);

		FVector4f LayerInfo(InParams.LayerAlpha, InParams.LayerVisible ? 1.0f : 0.0f, InParams.LayerBlendMode == LSBM_AlphaBlend ? 1.0f : 0.f, 0.f);
		FVector4f OutputConfig(InParams.ApplyLayerModifiers ? 1.0f : 0.0f, InParams.OutputAsSubstractive ? 1.0f : 0.0f, InParams.ReadWeightmap2 != nullptr ? 1.0f : 0.0f, 0.0f);

		SetShaderValue(BatchedParameters, LayerInfoParam, LayerInfo);
		SetShaderValue(BatchedParameters, OutputConfigParam, OutputConfig);
		SetShaderValue(BatchedParameters, ComponentVertexCountParam, (float)InParams.CurrentMipComponentVertexCount);
	}

private:
	LAYOUT_FIELD(FShaderResourceParameter, ReadTexture1Param);
	LAYOUT_FIELD(FShaderResourceParameter, ReadTexture2Param);
	LAYOUT_FIELD(FShaderResourceParameter, ReadTexture1SamplerParam);
	LAYOUT_FIELD(FShaderResourceParameter, ReadTexture2SamplerParam);
	LAYOUT_FIELD(FShaderParameter, LayerInfoParam);
	LAYOUT_FIELD(FShaderParameter, OutputConfigParam);
	LAYOUT_FIELD(FShaderParameter, ComponentVertexCountParam);
};

IMPLEMENT_GLOBAL_SHADER(FLandscapeLayersWeightmapPS, "/Engine/Private/LandscapeLayersPS.usf", "PSWeightmapMain", SF_Pixel);

class FLandscapeLayersWeightmapMipsPS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FLandscapeLayersWeightmapMipsPS);
public:

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return UE::Landscape::DoesPlatformSupportEditLayers(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
	}

	FLandscapeLayersWeightmapMipsPS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{
		ReadTexture1Param.Bind(Initializer.ParameterMap, TEXT("ReadTexture1"));
		ReadTexture1SamplerParam.Bind(Initializer.ParameterMap, TEXT("ReadTexture1Sampler"));
		CurrentMipSizeParam.Bind(Initializer.ParameterMap, TEXT("CurrentMipTextureSize"));
		ParentMipSizeParam.Bind(Initializer.ParameterMap, TEXT("ParentMipTextureSize"));
		CurrentMipComponentVertexCountParam.Bind(Initializer.ParameterMap, TEXT("CurrentMipComponentVertexCount"));
	}

	FLandscapeLayersWeightmapMipsPS()
	{}

	void SetParameters(FRHIBatchedShaderParameters& BatchedParameters, const FLandscapeLayersWeightmapShaderParameters& InParams)
	{
		SetTextureParameter(BatchedParameters, ReadTexture1Param, ReadTexture1SamplerParam, TStaticSamplerState<SF_Point, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI(), InParams.ReadWeightmap1->GetResource()->TextureRHI);

		SetShaderValue(BatchedParameters, CurrentMipSizeParam, FVector2f(static_cast<float>(InParams.CurrentMipSize.X), static_cast<float>(InParams.CurrentMipSize.Y)));
		SetShaderValue(BatchedParameters, ParentMipSizeParam, FVector2f(static_cast<float>(InParams.ParentMipSize.X), static_cast<float>(InParams.ParentMipSize.Y)));
		SetShaderValue(BatchedParameters, CurrentMipComponentVertexCountParam, (float)InParams.CurrentMipComponentVertexCount);
	}

private:
	LAYOUT_FIELD(FShaderResourceParameter, ReadTexture1Param);
	LAYOUT_FIELD(FShaderResourceParameter, ReadTexture1SamplerParam);
	LAYOUT_FIELD(FShaderParameter, CurrentMipSizeParam);
	LAYOUT_FIELD(FShaderParameter, ParentMipSizeParam);
	LAYOUT_FIELD(FShaderParameter, CurrentMipComponentVertexCountParam);
};

IMPLEMENT_GLOBAL_SHADER(FLandscapeLayersWeightmapMipsPS, "/Engine/Private/LandscapeLayersPS.usf", "PSWeightmapMainMips", SF_Pixel);

class FLandscapeCopyTextureVS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FLandscapeCopyTextureVS);

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& InParameters)
	{
		return UE::Landscape::DoesPlatformSupportEditLayers(InParameters.Platform);
	}

	FLandscapeCopyTextureVS()
	{};

	FLandscapeCopyTextureVS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{}
};

class FLandscapeCopyTexturePS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FLandscapeCopyTexturePS);
public:

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return UE::Landscape::DoesPlatformSupportEditLayers(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
	}

	FLandscapeCopyTexturePS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{
		ReadTexture1Param.Bind(Initializer.ParameterMap, TEXT("ReadTexture1"));
		ReadTexture1SamplerParam.Bind(Initializer.ParameterMap, TEXT("ReadTexture1Sampler"));
		SourceOffsetAndSizeUVParam.Bind(Initializer.ParameterMap, TEXT("SourceOffsetAndSizeUV"));
		ChannelSwizzleMaskParam.Bind(Initializer.ParameterMap, TEXT("ChannelSwizzleMask"));
	}

	FLandscapeCopyTexturePS()
	{}

	void SetParameters(FRHIBatchedShaderParameters& BatchedParameters, FRHITexture* InSourceTextureRHI,
		const FIntPoint& InSourcePosition = FIntPoint::ZeroValue, const FIntPoint& InCopySizePixels = FIntPoint::ZeroValue, uint8 InChannelSwizzleMask = UE::Landscape::Private::RGBAToRGBASwizzleMask)
	{
		FVector2f SourceSize(InSourceTextureRHI->GetSizeXY());
		FVector2f SourceOffsetUV = FVector2f(InSourcePosition) / SourceSize;
		FVector2f FinalCopySizePixels;
		FinalCopySizePixels.X = (InCopySizePixels.X > 0) ? InCopySizePixels.X : SourceSize.X;
		FinalCopySizePixels.Y = (InCopySizePixels.Y > 0) ? InCopySizePixels.Y : SourceSize.Y;
		FVector2f CopySizeUV = FinalCopySizePixels / SourceSize;
		SetTextureParameter(BatchedParameters, ReadTexture1Param, ReadTexture1SamplerParam, TStaticSamplerState<SF_Point, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI(), InSourceTextureRHI);
		SetShaderValue(BatchedParameters, SourceOffsetAndSizeUVParam, FVector4f(SourceOffsetUV.X, SourceOffsetUV.Y, CopySizeUV.X, CopySizeUV.Y));
		SetShaderValue(BatchedParameters, ChannelSwizzleMaskParam, static_cast<uint32>(InChannelSwizzleMask));
	}

private:
	LAYOUT_FIELD(FShaderResourceParameter, ReadTexture1Param);
	LAYOUT_FIELD(FShaderResourceParameter, ReadTexture1SamplerParam);
	LAYOUT_FIELD(FShaderParameter, SourceOffsetAndSizeUVParam);
	LAYOUT_FIELD(FShaderParameter, ChannelSwizzleMaskParam);
};

IMPLEMENT_GLOBAL_SHADER(FLandscapeCopyTextureVS, "/Engine/Private/LandscapeLayersPS.usf", "CopyTextureVS", SF_Vertex);
IMPLEMENT_GLOBAL_SHADER(FLandscapeCopyTexturePS, "/Engine/Private/LandscapeLayersPS.usf", "CopyTexturePS", SF_Pixel);

// ----------------------------------------------------------------------------------
// /Engine/Private/LandscapeLayersCS.usf shaders

int32 GLandscapeLayerWeightmapThreadGroupSizeX = 16;
int32 GLandscapeLayerWeightmapThreadGroupSizeY = 16;

struct FLandscapeLayerWeightmapExtractMaterialLayersComponentData
{
	FIntPoint ComponentVertexPosition;	// Section base converted to vertex instead of quad
	uint32 DestinationPaintLayerIndex;	// correspond to which layer info object index the data should be stored in the texture 2d array
	uint32 WeightmapChannelToProcess;	// correspond to which RGBA channel to process
	FIntPoint AtlasTexturePositionOutput;	// This represent the location we will write layer information
};

class FLandscapeLayerWeightmapExtractMaterialLayersComputeShaderResource : public FRenderResource
{
public:
	FLandscapeLayerWeightmapExtractMaterialLayersComputeShaderResource(const TArray<FLandscapeLayerWeightmapExtractMaterialLayersComponentData>& InComponentsData)
		: OriginalComponentsData(InComponentsData)
		, ComponentsDataCount(OriginalComponentsData.Num())
	{}

	~FLandscapeLayerWeightmapExtractMaterialLayersComputeShaderResource()
	{
		ComponentsData.SafeRelease();
		ComponentsDataSRV.SafeRelease();
	}

	/** Called when the resource is initialized. This is only called by the rendering thread. */
	virtual void InitRHI(FRHICommandListBase& RHICmdList) override
	{
		FRHIResourceCreateInfo CreateInfo(TEXT("FLandscapeLayerWeightmapExtractMaterialLayersComputeShaderResource"));
		ComponentsData = RHICmdList.CreateStructuredBuffer(sizeof(FLandscapeLayerWeightmapExtractMaterialLayersComponentData), OriginalComponentsData.Num() * sizeof(FLandscapeLayerWeightmapExtractMaterialLayersComponentData), BUF_ShaderResource | BUF_Volatile, CreateInfo);
		ComponentsDataSRV = RHICmdList.CreateShaderResourceView(ComponentsData);

		uint8* Buffer = (uint8*)RHICmdList.LockBuffer(ComponentsData, 0, OriginalComponentsData.Num() * sizeof(FLandscapeLayerWeightmapExtractMaterialLayersComponentData), RLM_WriteOnly);
		FMemory::Memcpy(Buffer, OriginalComponentsData.GetData(), OriginalComponentsData.Num() * sizeof(FLandscapeLayerWeightmapExtractMaterialLayersComponentData));
		RHICmdList.UnlockBuffer(ComponentsData);
	}

	virtual void ReleaseRHI() override
	{
		ComponentsData.SafeRelease();
		ComponentsDataSRV.SafeRelease();
	}

	int32 GetComponentsDataCount() const
	{
		return ComponentsDataCount;
	}

private:
	friend class FLandscapeLayerWeightmapExtractMaterialLayersCS;

	FBufferRHIRef ComponentsData;
	FShaderResourceViewRHIRef ComponentsDataSRV;
	TArray<FLandscapeLayerWeightmapExtractMaterialLayersComponentData> OriginalComponentsData;
	int32 ComponentsDataCount;
};

struct FLandscapeLayerWeightmapExtractMaterialLayersComputeShaderParameters
{
	FLandscapeLayerWeightmapExtractMaterialLayersComputeShaderParameters()
		: ComponentWeightmapResource(nullptr)
		, ComputeShaderResource(nullptr)
		, AtlasWeightmapsPerLayer(nullptr)
		, ComponentSize(0)
	{}

	FLandscapeTexture2DResource* ComponentWeightmapResource;
	FLandscapeLayerWeightmapExtractMaterialLayersComputeShaderResource* ComputeShaderResource;
	FLandscapeTexture2DArrayResource* AtlasWeightmapsPerLayer;
	uint32 ComponentSize;
};

class FLandscapeLayerWeightmapExtractMaterialLayersCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FLandscapeLayerWeightmapExtractMaterialLayersCS);
public:

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return UE::Landscape::DoesPlatformSupportEditLayers(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZEX"), GLandscapeLayerWeightmapThreadGroupSizeX);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZEY"), GLandscapeLayerWeightmapThreadGroupSizeY);
	}

	FLandscapeLayerWeightmapExtractMaterialLayersCS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{
		ComponentWeightmapParam.Bind(Initializer.ParameterMap, TEXT("InComponentWeightMaps"));
		AtlasPaintListsParam.Bind(Initializer.ParameterMap, TEXT("OutAtlasPaintLayers"));
		ComponentsDataParam.Bind(Initializer.ParameterMap, TEXT("InExtractLayersComponentsData"));
		ComponentSizeParam.Bind(Initializer.ParameterMap, TEXT("ComponentSize"));
	}

	FLandscapeLayerWeightmapExtractMaterialLayersCS()
	{}

	void SetParameters(FRHIBatchedShaderParameters& BatchedParameters, const FLandscapeLayerWeightmapExtractMaterialLayersComputeShaderParameters& InParams)
	{
		SetTextureParameter(BatchedParameters, ComponentWeightmapParam, InParams.ComponentWeightmapResource->TextureRHI);
		SetUAVParameter(BatchedParameters, AtlasPaintListsParam, InParams.AtlasWeightmapsPerLayer->GetTextureUAV(/*InMipLevel = */0));
		SetSRVParameter(BatchedParameters, ComponentsDataParam, InParams.ComputeShaderResource->ComponentsDataSRV);
		SetShaderValue(BatchedParameters, ComponentSizeParam, InParams.ComponentSize);
	}

	void UnsetParameters(FRHIBatchedShaderUnbinds& BatchedUnbinds)
	{
		UnsetUAVParameter(BatchedUnbinds, AtlasPaintListsParam);
	}

private:
	LAYOUT_FIELD(FShaderResourceParameter, ComponentWeightmapParam);
	LAYOUT_FIELD(FShaderResourceParameter, AtlasPaintListsParam);
	LAYOUT_FIELD(FShaderResourceParameter, ComponentsDataParam);
	LAYOUT_FIELD(FShaderParameter, ComponentSizeParam);
};

IMPLEMENT_GLOBAL_SHADER(FLandscapeLayerWeightmapExtractMaterialLayersCS, "/Engine/Private/LandscapeLayersCS.usf", "ComputeWeightmapPerPaintLayer", SF_Compute);

class FLandscapeLayerWeightmapExtractMaterialLayersCSDispatch_RenderThread
{
public:
	FLandscapeLayerWeightmapExtractMaterialLayersCSDispatch_RenderThread(const FLandscapeLayerWeightmapExtractMaterialLayersComputeShaderParameters& InShaderParams)
		: ShaderParams(InShaderParams)
	{}

	void ExtractLayers(FRHICommandListImmediate& InRHICmdList)
	{
		RHI_BREADCRUMB_EVENT_STAT(InRHICmdList, LandscapeLayers_ExtractLayers, "LandscapeLayers_ExtractLayers");
		SCOPED_GPU_STAT(InRHICmdList, LandscapeLayers_ExtractLayers);

		TShaderMapRef<FLandscapeLayerWeightmapExtractMaterialLayersCS> ComputeShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
		SetComputePipelineState(InRHICmdList, ComputeShader.GetComputeShader());

		SetShaderParametersLegacyCS(InRHICmdList, ComputeShader, ShaderParams);

		// In case the CS is executed twice in a row, we need a barrier since we want to prevent UAV overlaps:
		InRHICmdList.Transition(FRHITransitionInfo(ShaderParams.AtlasWeightmapsPerLayer->TextureRHI, ERHIAccess::UAVMask, ERHIAccess::UAVMask));

		uint32 ThreadGroupCountX = FMath::CeilToInt((float)ShaderParams.ComponentSize / (float)GLandscapeLayerWeightmapThreadGroupSizeX);
		uint32 ThreadGroupCountY = FMath::CeilToInt((float)ShaderParams.ComponentSize / (float)GLandscapeLayerWeightmapThreadGroupSizeY);
		check(ThreadGroupCountX > 0 && ThreadGroupCountY > 0);

		DispatchComputeShader(InRHICmdList, ComputeShader.GetShader(), ThreadGroupCountX, ThreadGroupCountY, ShaderParams.ComputeShaderResource->GetComponentsDataCount());

		UnsetShaderParametersLegacyCS(InRHICmdList, ComputeShader);

		ShaderParams.ComputeShaderResource->ReleaseResource();
		delete ShaderParams.ComputeShaderResource;
	}

private:
	FLandscapeLayerWeightmapExtractMaterialLayersComputeShaderParameters ShaderParams;
};

struct FLandscapeLayerWeightmapPackMaterialLayersComponentData
{
	int32 ComponentVertexPositionX[4];		// Section base converted to vertex instead of quad
	int32 ComponentVertexPositionY[4];		// Section base converted to vertex instead of quad
	int32 SourcePaintLayerIndex[4];			// correspond to which layer info object index the data should be loaded from the texture 2d array
	int32 WeightmapChannelToProcess[4];		// correspond to which RGBA channel to process
};

class FLandscapeLayerWeightmapPackMaterialLayersComputeShaderResource : public FRenderResource
{
public:
	FLandscapeLayerWeightmapPackMaterialLayersComputeShaderResource(const TArray<FLandscapeLayerWeightmapPackMaterialLayersComponentData>& InComponentsData, const TArray<float>& InWeightmapWeightBlendModeData, const TArray<FVector2f>& InTextureOutputOffset)
		: OriginalComponentsData(InComponentsData)
		, ComponentsDataCount(OriginalComponentsData.Num())
		, OriginalWeightmapWeightBlendModeData(InWeightmapWeightBlendModeData)
		, OriginalTextureOutputOffset(InTextureOutputOffset)
	{}

	~FLandscapeLayerWeightmapPackMaterialLayersComputeShaderResource()
	{
		ComponentsData.SafeRelease();
		ComponentsDataSRV.SafeRelease();
		WeightmapWeightBlendModeSRV.SafeRelease();
		WeightmapTextureOutputOffsetSRV.SafeRelease();
	}

	/** Called when the resource is initialized. This is only called by the rendering thread. */
	virtual void InitRHI(FRHICommandListBase& RHICmdList) override
	{
		FRHIResourceCreateInfo CreateInfo(TEXT("ComponentsData"));
		uint32 ComponentsDataMemSize = OriginalComponentsData.Num() * sizeof(FLandscapeLayerWeightmapPackMaterialLayersComponentData);
		ComponentsData = RHICmdList.CreateStructuredBuffer(sizeof(FLandscapeLayerWeightmapPackMaterialLayersComponentData), ComponentsDataMemSize, BUF_ShaderResource | BUF_Volatile, CreateInfo);
		ComponentsDataSRV = RHICmdList.CreateShaderResourceView(ComponentsData);

		uint8* Buffer = (uint8*)RHICmdList.LockBuffer(ComponentsData, 0, ComponentsDataMemSize, RLM_WriteOnly);
		FMemory::Memcpy(Buffer, OriginalComponentsData.GetData(), ComponentsDataMemSize);
		RHICmdList.UnlockBuffer(ComponentsData);

		FRHIResourceCreateInfo WeightBlendCreateInfo(TEXT("WeightmapWeightBlendMode"));
		uint32 WeightBlendMemSize = OriginalWeightmapWeightBlendModeData.Num() * sizeof(float);
		WeightmapWeightBlendMode = RHICmdList.CreateVertexBuffer(WeightBlendMemSize, BUF_ShaderResource | BUF_Volatile, WeightBlendCreateInfo);
		WeightmapWeightBlendModeSRV = RHICmdList.CreateShaderResourceView(WeightmapWeightBlendMode, sizeof(float), PF_R32_FLOAT);

		void* WeightmapWeightBlendModePtr = RHICmdList.LockBuffer(WeightmapWeightBlendMode, 0, WeightBlendMemSize, RLM_WriteOnly);
		FMemory::Memcpy(WeightmapWeightBlendModePtr, OriginalWeightmapWeightBlendModeData.GetData(), WeightBlendMemSize);
		RHICmdList.UnlockBuffer(WeightmapWeightBlendMode);

		FRHIResourceCreateInfo TextureOutputOffsetCreateInfo(TEXT("WeightmapTextureOutputOffset"));
		uint32 TextureOutputOffsetMemSize = OriginalTextureOutputOffset.Num() * sizeof(FVector2f);
		WeightmapTextureOutputOffset = RHICmdList.CreateVertexBuffer(TextureOutputOffsetMemSize, BUF_ShaderResource | BUF_Volatile, TextureOutputOffsetCreateInfo);
		WeightmapTextureOutputOffsetSRV = RHICmdList.CreateShaderResourceView(WeightmapTextureOutputOffset, sizeof(FVector2f), PF_G32R32F);

		void* TextureOutputOffsetPtr = RHICmdList.LockBuffer(WeightmapTextureOutputOffset, 0, TextureOutputOffsetMemSize, RLM_WriteOnly);
		FMemory::Memcpy(TextureOutputOffsetPtr, OriginalTextureOutputOffset.GetData(), TextureOutputOffsetMemSize);
		RHICmdList.UnlockBuffer(WeightmapTextureOutputOffset);
	}

	virtual void ReleaseRHI() override
	{
		ComponentsData.SafeRelease();
		ComponentsDataSRV.SafeRelease();
		WeightmapWeightBlendModeSRV.SafeRelease();
		WeightmapTextureOutputOffsetSRV.SafeRelease();
	}

	int32 GetComponentsDataCount() const
	{
		return ComponentsDataCount;
	}

private:
	friend class FLandscapeLayerWeightmapPackMaterialLayersCS;

	FBufferRHIRef ComponentsData;
	FShaderResourceViewRHIRef ComponentsDataSRV;
	TArray<FLandscapeLayerWeightmapPackMaterialLayersComponentData> OriginalComponentsData;
	int32 ComponentsDataCount;

	TArray<float> OriginalWeightmapWeightBlendModeData;
	FBufferRHIRef WeightmapWeightBlendMode;
	FShaderResourceViewRHIRef WeightmapWeightBlendModeSRV;

	TArray<FVector2f> OriginalTextureOutputOffset;
	FBufferRHIRef WeightmapTextureOutputOffset;
	FShaderResourceViewRHIRef WeightmapTextureOutputOffsetSRV;
};

struct FLandscapeLayerWeightmapPackMaterialLayersComputeShaderParameters
{
	FLandscapeLayerWeightmapPackMaterialLayersComputeShaderParameters()
		: ComponentWeightmapResource(nullptr)
		, ComputeShaderResource(nullptr)
		, AtlasWeightmapsPerLayer(nullptr)
		, ComponentSize(0)
	{}

	FLandscapeTexture2DResource* ComponentWeightmapResource;
	FLandscapeLayerWeightmapPackMaterialLayersComputeShaderResource* ComputeShaderResource;
	FLandscapeTexture2DArrayResource* AtlasWeightmapsPerLayer;
	uint32 ComponentSize;
};

class FLandscapeLayerWeightmapPackMaterialLayersCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FLandscapeLayerWeightmapPackMaterialLayersCS);
public:

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return UE::Landscape::DoesPlatformSupportEditLayers(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZEX"), GLandscapeLayerWeightmapThreadGroupSizeX);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZEY"), GLandscapeLayerWeightmapThreadGroupSizeY);
	}

	FLandscapeLayerWeightmapPackMaterialLayersCS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{
		ComponentWeightmapParam.Bind(Initializer.ParameterMap, TEXT("OutComponentWeightMaps"));
		AtlasPaintListsParam.Bind(Initializer.ParameterMap, TEXT("InAtlasPaintLayers"));
		ComponentsDataParam.Bind(Initializer.ParameterMap, TEXT("InPackLayersComponentsData"));
		ComponentSizeParam.Bind(Initializer.ParameterMap, TEXT("ComponentSize"));
		WeightmapWeightBlendModeParam.Bind(Initializer.ParameterMap, TEXT("InWeightmapWeightBlendMode"));
		WeightmapTextureOutputOffsetParam.Bind(Initializer.ParameterMap, TEXT("InWeightmapTextureOutputOffset"));
	}

	FLandscapeLayerWeightmapPackMaterialLayersCS()
	{}

	void SetParameters(FRHIBatchedShaderParameters& BatchedParameters, const FLandscapeLayerWeightmapPackMaterialLayersComputeShaderParameters& InParams)
	{
		SetUAVParameter(BatchedParameters, ComponentWeightmapParam, InParams.ComponentWeightmapResource->GetTextureUAV(/*InMipLevel = */0));
		SetTextureParameter(BatchedParameters, AtlasPaintListsParam, InParams.AtlasWeightmapsPerLayer->TextureRHI);
		SetSRVParameter(BatchedParameters, ComponentsDataParam, InParams.ComputeShaderResource->ComponentsDataSRV);
		SetShaderValue(BatchedParameters, ComponentSizeParam, InParams.ComponentSize);
		SetSRVParameter(BatchedParameters, WeightmapWeightBlendModeParam, InParams.ComputeShaderResource->WeightmapWeightBlendModeSRV);
		SetSRVParameter(BatchedParameters, WeightmapTextureOutputOffsetParam, InParams.ComputeShaderResource->WeightmapTextureOutputOffsetSRV);
	}

	void UnsetParameters(FRHIBatchedShaderUnbinds& BatchedUnbinds)
	{
		UnsetUAVParameter(BatchedUnbinds, ComponentWeightmapParam);
	}

private:
	LAYOUT_FIELD(FShaderResourceParameter, ComponentWeightmapParam);
	LAYOUT_FIELD(FShaderResourceParameter, AtlasPaintListsParam);
	LAYOUT_FIELD(FShaderResourceParameter, ComponentsDataParam);
	LAYOUT_FIELD(FShaderParameter, ComponentSizeParam);
	LAYOUT_FIELD(FShaderResourceParameter, WeightmapWeightBlendModeParam);
	LAYOUT_FIELD(FShaderResourceParameter, WeightmapTextureOutputOffsetParam);
};

IMPLEMENT_GLOBAL_SHADER(FLandscapeLayerWeightmapPackMaterialLayersCS, "/Engine/Private/LandscapeLayersCS.usf", "PackPaintLayerToWeightmap", SF_Compute);

class FLandscapeLayerWeightmapPackMaterialLayersCSDispatch_RenderThread
{
public:
	FLandscapeLayerWeightmapPackMaterialLayersCSDispatch_RenderThread(const FLandscapeLayerWeightmapPackMaterialLayersComputeShaderParameters& InShaderParams)
		: ShaderParams(InShaderParams)
	{}

	void PackLayers(FRHICommandListImmediate& InRHICmdList)
	{
		RHI_BREADCRUMB_EVENT_STAT(InRHICmdList, LandscapeLayers_PackLayers, "LandscapeLayers_PackLayers");
		SCOPED_GPU_STAT(InRHICmdList, LandscapeLayers_PackLayers);

		TShaderMapRef<FLandscapeLayerWeightmapPackMaterialLayersCS> ComputeShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
		SetComputePipelineState(InRHICmdList, ComputeShader.GetComputeShader());

		SetShaderParametersLegacyCS(InRHICmdList, ComputeShader, ShaderParams);

		uint32 ThreadGroupCountX = FMath::CeilToInt((float)ShaderParams.ComponentSize / (float)GLandscapeLayerWeightmapThreadGroupSizeX);
		uint32 ThreadGroupCountY = FMath::CeilToInt((float)ShaderParams.ComponentSize / (float)GLandscapeLayerWeightmapThreadGroupSizeY);
		check(ThreadGroupCountX > 0 && ThreadGroupCountY > 0);

		DispatchComputeShader(InRHICmdList, ComputeShader.GetShader(), ThreadGroupCountX, ThreadGroupCountY, ShaderParams.ComputeShaderResource->GetComponentsDataCount());

		UnsetShaderParametersLegacyCS(InRHICmdList, ComputeShader);

		ShaderParams.ComputeShaderResource->ReleaseResource();
		delete ShaderParams.ComputeShaderResource;
	}

private:
	FLandscapeLayerWeightmapPackMaterialLayersComputeShaderParameters ShaderParams;
};

// ----------------------------------------------------------------------------------
// /Engine/Private/Landscape/LandscapeLayersHeightmapsPS.usf shaders : 

class FLandscapeLayersHeightmapsMergeEditLayersPS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FLandscapeLayersHeightmapsMergeEditLayersPS);
	SHADER_USE_PARAMETER_STRUCT(FLandscapeLayersHeightmapsMergeEditLayersPS, FGlobalShader);

public:
	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(uint32, InNumEditLayers)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2DArray<float4>, InEditLayersTextures)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FLandscapeEditLayerHeightmapMergeInfo>, InEditLayersMergeInfos)
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& InParameters)
	{
		return UE::Landscape::DoesPlatformSupportEditLayers(InParameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& InParameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		OutEnvironment.SetDefine(TEXT("MERGE_EDIT_LAYERS"), 1);
	}

	static void MergeEditLayers(FRDGBuilder& GraphBuilder, FParameters* InParameters, const FIntPoint& InTextureSize)
	{
		FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(GMaxRHIFeatureLevel);
		TShaderMapRef<FLandscapeLayersHeightmapsMergeEditLayersPS> PixelShader(ShaderMap);

		FPixelShaderUtils::AddFullscreenPass(
			GraphBuilder,
			ShaderMap,
			RDG_EVENT_NAME("LandscapeLayers_MergeEditLayers"),
			PixelShader,
			InParameters,
			FIntRect(0, 0, InTextureSize.X, InTextureSize.Y));
	}
};

IMPLEMENT_GLOBAL_SHADER(FLandscapeLayersHeightmapsMergeEditLayersPS, "/Engine/Private/Landscape/LandscapeLayersHeightmapsPS.usf", "MergeEditLayers", SF_Pixel);

class FLandscapeLayersHeightmapsStitchHeightmapPS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FLandscapeLayersHeightmapsStitchHeightmapPS);
	SHADER_USE_PARAMETER_STRUCT(FLandscapeLayersHeightmapsStitchHeightmapPS, FGlobalShader);

public:
	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FUintVector2, InSourceTextureSize)
		SHADER_PARAMETER(uint32, InNumSubsections)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2DArray<float2>, InSourceHeightmaps)
		SHADER_PARAMETER_SCALAR_ARRAY(uint32, InNeighborHeightmapIndices, [9])
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return UE::Landscape::DoesPlatformSupportEditLayers(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		OutEnvironment.SetDefine(TEXT("STITCH_HEIGHTMAP"), 1);
	}

	static void StitchHeightmap(FRDGBuilder& GraphBuilder, FParameters* InParameters)
	{
		FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(GMaxRHIFeatureLevel);
		TShaderMapRef<FLandscapeLayersHeightmapsStitchHeightmapPS> PixelShader(ShaderMap);

		FPixelShaderUtils::AddFullscreenPass(
			GraphBuilder,
			ShaderMap,
			RDG_EVENT_NAME("LandscapeLayers_StitchHeightmap"),
			PixelShader,
			InParameters,
			FIntRect(0, 0, InParameters->InSourceTextureSize.X, InParameters->InSourceTextureSize.Y));
	}
};

IMPLEMENT_GLOBAL_SHADER(FLandscapeLayersHeightmapsStitchHeightmapPS, "/Engine/Private/Landscape/LandscapeLayersHeightmapsPS.usf", "StitchHeightmap", SF_Pixel);

class FLandscapeLayersHeightmapsFinalizeHeightmapPS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FLandscapeLayersHeightmapsFinalizeHeightmapPS);
	SHADER_USE_PARAMETER_STRUCT(FLandscapeLayersHeightmapsFinalizeHeightmapPS, FGlobalShader);

public:
	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FUintVector2, InSourceTextureSize)
		SHADER_PARAMETER(uint32, InNumSubsections)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2DArray<float2>, InSourceHeightmaps)
		SHADER_PARAMETER_SCALAR_ARRAY(uint32, InNeighborHeightmapIndices, [9])
		SHADER_PARAMETER(FUintVector4, InDestinationTextureSubregion)
		SHADER_PARAMETER(FVector3f, InLandscapeGridScale)
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return UE::Landscape::DoesPlatformSupportEditLayers(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		OutEnvironment.SetDefine(TEXT("FINALIZE_HEIGHTMAP"), 1);
	}

	static void FinalizeHeightmap(FRDGBuilder& GraphBuilder, FParameters* InParameters)
	{
		FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(GMaxRHIFeatureLevel);
		TShaderMapRef<FLandscapeLayersHeightmapsFinalizeHeightmapPS> PixelShader(ShaderMap);

		FPixelShaderUtils::AddFullscreenPass(
			GraphBuilder,
			ShaderMap,
			RDG_EVENT_NAME("LandscapeLayers_FinalizeHeightmap"),
			PixelShader,
			InParameters,
			FIntRect(InParameters->InDestinationTextureSubregion.X, InParameters->InDestinationTextureSubregion.Y, InParameters->InDestinationTextureSubregion.Z, InParameters->InDestinationTextureSubregion.W));
	}
};

IMPLEMENT_GLOBAL_SHADER(FLandscapeLayersHeightmapsFinalizeHeightmapPS, "/Engine/Private/Landscape/LandscapeLayersHeightmapsPS.usf", "FinalizeHeightmap", SF_Pixel);

class FLandscapeLayersHeightmapsGenerateMipsPS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FLandscapeLayersHeightmapsGenerateMipsPS);
	SHADER_USE_PARAMETER_STRUCT(FLandscapeLayersHeightmapsGenerateMipsPS, FGlobalShader);

public:
	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FUintVector2, InCurrentMipSubregionSize)
		SHADER_PARAMETER(uint32, InNumSubsections)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2D<float4>, InSourceHeightmap)
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return UE::Landscape::DoesPlatformSupportEditLayers(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		OutEnvironment.SetDefine(TEXT("GENERATE_MIPS"), 1);
	}

	static void GenerateSingleMip(FRDGBuilder& GraphBuilder, FParameters* InParameters)
	{
		FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(GMaxRHIFeatureLevel);
		TShaderMapRef<FLandscapeLayersHeightmapsGenerateMipsPS> PixelShader(ShaderMap);

		FIntVector MipSize = InParameters->RenderTargets[0].GetTexture()->Desc.GetSize();

		FPixelShaderUtils::AddFullscreenPass(
			GraphBuilder,
			ShaderMap,
			RDG_EVENT_NAME("LandscapeLayers_GenerateMip"),
			PixelShader,
			InParameters,
			FIntRect(0, 0, MipSize.X, MipSize.Y));
	}
};

IMPLEMENT_GLOBAL_SHADER(FLandscapeLayersHeightmapsGenerateMipsPS, "/Engine/Private/Landscape/LandscapeLayersHeightmapsPS.usf", "GenerateMips", SF_Pixel);

// ----------------------------------------------------------------------------------
// /Engine/Private/Landscape/LandscapeLayersWeightmapsPS.usf shaders :

class FLandscapeLayersWeightmapsMergeEditLayersPS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FLandscapeLayersWeightmapsMergeEditLayersPS);
	SHADER_USE_PARAMETER_STRUCT(FLandscapeLayersWeightmapsMergeEditLayersPS, FGlobalShader);

public:
	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(uint32, InNumEditLayers)
		SHADER_PARAMETER(uint32, InStartIndexInEditLayersMergeInfos)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2DArray<float4>, InPackedWeightmaps)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FLandscapeEditLayerWeightmapMergeInfo>, InEditLayersMergeInfos)
		RENDER_TARGET_BINDING_SLOTS()
		END_SHADER_PARAMETER_STRUCT()

		static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& InParameters)
	{
		return UE::Landscape::DoesPlatformSupportEditLayers(InParameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& InParameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		OutEnvironment.SetDefine(TEXT("MERGE_EDIT_LAYERS"), 1);
	}

	static void MergeEditLayers(FRDGBuilder& GraphBuilder, FParameters* InParameters)
	{
		FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(GMaxRHIFeatureLevel);
		TShaderMapRef<FLandscapeLayersWeightmapsMergeEditLayersPS> PixelShader(ShaderMap);

		FIntVector TextureSize = InParameters->RenderTargets[0].GetTexture()->Desc.GetSize();

		FPixelShaderUtils::AddFullscreenPass(
			GraphBuilder,
			ShaderMap,
			RDG_EVENT_NAME("LandscapeLayers_MergeEditLayers"),
			PixelShader,
			InParameters,
			FIntRect(0, 0, TextureSize.X, TextureSize.Y));
	}
};

IMPLEMENT_GLOBAL_SHADER(FLandscapeLayersWeightmapsMergeEditLayersPS, "/Engine/Private/Landscape/LandscapeLayersWeightmapsPS.usf", "MergeEditLayers", SF_Pixel);

class FLandscapeLayersWeightmapsFinalizeWeightmapPS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FLandscapeLayersWeightmapsFinalizeWeightmapPS);
	SHADER_USE_PARAMETER_STRUCT(FLandscapeLayersWeightmapsFinalizeWeightmapPS, FGlobalShader);

public:
	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(uint32, InValidTextureChannelsMask)
		SHADER_PARAMETER(FUintVector4, InPerChannelPaintLayerIndexInWeightmaps)
		SHADER_PARAMETER(FUintVector4, InPerChannelStartPaintLayerIndex)
		SHADER_PARAMETER(FUintVector4, InPerChannelNumPaintLayers)
		SHADER_PARAMETER_RDG_TEXTURE_SRV_ARRAY(Texture2D<float4>, InPerChannelPaintLayerWeightmaps, [4])
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, InPaintLayerInfoIndices)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<UE::Landscape::EditLayers::FWeightmapPaintLayerInfo>, InPaintLayerInfos)
		RENDER_TARGET_BINDING_SLOTS()
		END_SHADER_PARAMETER_STRUCT()

		static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return UE::Landscape::DoesPlatformSupportEditLayers(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		OutEnvironment.SetDefine(TEXT("FINALIZE_WEIGHTMAP"), 1);
	}

	static void FinalizeWeightmap(FRDGBuilder& GraphBuilder, FParameters* InParameters)
	{
		FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(GMaxRHIFeatureLevel);
		TShaderMapRef<FLandscapeLayersWeightmapsFinalizeWeightmapPS> PixelShader(ShaderMap);

		FIntVector TextureSize = InParameters->RenderTargets[0].GetTexture()->Desc.GetSize();

		FPixelShaderUtils::AddFullscreenPass(
			GraphBuilder,
			ShaderMap,
			RDG_EVENT_NAME("LandscapeLayers_PackWeightmap"),
			PixelShader,
			InParameters,
			FIntRect(0, 0, TextureSize.X, TextureSize.Y));
	}
};

IMPLEMENT_GLOBAL_SHADER(FLandscapeLayersWeightmapsFinalizeWeightmapPS, "/Engine/Private/Landscape/LandscapeLayersWeightmapsPS.usf", "FinalizeWeightmap", SF_Pixel);

class FLandscapeLayersWeightmapsGenerateMipsPS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FLandscapeLayersWeightmapsGenerateMipsPS);
	SHADER_USE_PARAMETER_STRUCT(FLandscapeLayersWeightmapsGenerateMipsPS, FGlobalShader);

public:
	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FUintVector2, InCurrentMipSize)
		SHADER_PARAMETER(uint32, InNumSubsections)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2D<float4>, InSourceWeightmap)
		RENDER_TARGET_BINDING_SLOTS()
		END_SHADER_PARAMETER_STRUCT()

		static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return UE::Landscape::DoesPlatformSupportEditLayers(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		OutEnvironment.SetDefine(TEXT("GENERATE_MIPS"), 1);
	}

	static void GenerateSingleMip(FRDGBuilder& GraphBuilder, FParameters* InParameters)
	{
		FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(GMaxRHIFeatureLevel);
		TShaderMapRef<FLandscapeLayersWeightmapsGenerateMipsPS> PixelShader(ShaderMap);

		FIntVector MipSize = InParameters->RenderTargets[0].GetTexture()->Desc.GetSize();

		FPixelShaderUtils::AddFullscreenPass(
			GraphBuilder,
			ShaderMap,
			RDG_EVENT_NAME("LandscapeLayers_GenerateMip"),
			PixelShader,
			InParameters,
			FIntRect(0, 0, MipSize.X, MipSize.Y));
	}
};

IMPLEMENT_GLOBAL_SHADER(FLandscapeLayersWeightmapsGenerateMipsPS, "/Engine/Private/Landscape/LandscapeLayersWeightmapsPS.usf", "GenerateMips", SF_Pixel);


// ----------------------------------------------------------------------------------
// /Engine/Private/Landscape/LandscapeEditLayersHeightmaps.usf shaders :

class FLandscapeEditLayersHeightmapsMergeEditLayerPS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FLandscapeEditLayersHeightmapsMergeEditLayerPS);
	SHADER_USE_PARAMETER_STRUCT(FLandscapeEditLayersHeightmapsMergeEditLayerPS, FGlobalShader);

public:
	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(uint32, InEditLayerBlendMode)
		SHADER_PARAMETER(float, InEditLayerAlpha)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2D<float4>, InCurrentEditLayerHeightmap)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2D<float4>, InPreviousEditLayersHeightmap)
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& InParameters)
	{
		return UE::Landscape::DoesPlatformSupportEditLayers(InParameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& InParameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		OutEnvironment.SetDefine(TEXT("MERGE_EDIT_LAYER"), 1);
	}

	static void MergeEditLayerPS(FRDGBuilder& GraphBuilder, FParameters* InParameters, const FName& InTargetLayerName, const FIntPoint& InTextureSize)
	{
		FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(GMaxRHIFeatureLevel);
		TShaderMapRef<FLandscapeEditLayersHeightmapsMergeEditLayerPS> PixelShader(ShaderMap);

		FPixelShaderUtils::AddFullscreenPass(
			GraphBuilder,
			ShaderMap,
			RDG_EVENT_NAME("LandscapeLayers_MergeEditLayerPS (%s)", *InTargetLayerName.ToString()),
			PixelShader,
			InParameters,
			FIntRect(0, 0, InTextureSize.X, InTextureSize.Y), 
			/*BlendState = */nullptr, 
			/*RasterizerState = */nullptr, 
			/*DepthStencilState = */nullptr, 
			/*StencilRef = */0, 
			ERDGPassFlags::NeverCull); // Use NeverCull because it renders a texture for which tracking is disabled
	}
};

IMPLEMENT_GLOBAL_SHADER(FLandscapeEditLayersHeightmapsMergeEditLayerPS, "/Engine/Private/Landscape/LandscapeEditLayersHeightmaps.usf", "MergeEditLayerPS", SF_Pixel);

class FLandscapeEditLayersHeightmapsGenerateNormalsPS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FLandscapeEditLayersHeightmapsGenerateNormalsPS);
	SHADER_USE_PARAMETER_STRUCT(FLandscapeEditLayersHeightmapsGenerateNormalsPS, FGlobalShader);

public:
	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FUintVector4, InTextureSize)
		SHADER_PARAMETER(FVector3f, InLandscapeGridScale)
		SHADER_PARAMETER(uint32, InComponentSizeQuads)
		SHADER_PARAMETER(FUintVector2, InNumComponents)
		SHADER_PARAMETER_SAMPLER(SamplerState, InSourceHeightmapSampler)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2D<float4>, InSourceHeightmap)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2D<uint8>, InValidityTexture)
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& InParameters)
	{
		return UE::Landscape::DoesPlatformSupportEditLayers(InParameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& InParameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		OutEnvironment.SetDefine(TEXT("GENERATE_NORMALS"), 1);
		OutEnvironment.CompilerFlags.Remove(CFLAG_HLSL2021 | CFLAG_PrecompileWithDXC);
	}

	static void GenerateNormalsPS(FRDGBuilder& GraphBuilder, FParameters* InParameters, const FIntPoint& InTextureSize)
	{
		FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(GMaxRHIFeatureLevel);
		TShaderMapRef<FLandscapeEditLayersHeightmapsGenerateNormalsPS> PixelShader(ShaderMap);

		FPixelShaderUtils::AddFullscreenPass(
			GraphBuilder,
			ShaderMap,
			RDG_EVENT_NAME("LandscapeLayers_GenerateNormalsPS"),
			PixelShader,
			InParameters,
			FIntRect(0, 0, InTextureSize.X, InTextureSize.Y),
			/*BlendState = */nullptr, 
			/*RasterizerState = */nullptr, 
			/*DepthStencilState = */nullptr, 
			/*StencilRef = */0, 
			ERDGPassFlags::NeverCull); // Use NeverCull because it renders a texture for which tracking is disabled
	}
};

IMPLEMENT_GLOBAL_SHADER(FLandscapeEditLayersHeightmapsGenerateNormalsPS, "/Engine/Private/Landscape/LandscapeEditLayersHeightmaps.usf", "GenerateNormalsPS", SF_Pixel);

class FLandscapeEditLayersHeightmapsGenerateMipsPS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FLandscapeEditLayersHeightmapsGenerateMipsPS);
	SHADER_USE_PARAMETER_STRUCT(FLandscapeEditLayersHeightmapsGenerateMipsPS, FGlobalShader);

public:
	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FUintVector2, InCurrentMipSubsectionSize)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2D<float4>, InSourceHeightmap)
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& InParameters)
	{
		return UE::Landscape::DoesPlatformSupportEditLayers(InParameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& InParameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		OutEnvironment.SetDefine(TEXT("GENERATE_MIPS"), 1);
	}

	static void GenerateMipsPS(FRDGBuilder& GraphBuilder, FParameters* InParameters, const FIntPoint& InTextureSize)
	{
		FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(GMaxRHIFeatureLevel);
		TShaderMapRef<FLandscapeEditLayersHeightmapsGenerateMipsPS> PixelShader(ShaderMap);

		FPixelShaderUtils::AddFullscreenPass(
			GraphBuilder,
			ShaderMap,
			RDG_EVENT_NAME("LandscapeLayers_GenerateMipsPS"),
			PixelShader,
			InParameters,
			FIntRect(0, 0, InTextureSize.X, InTextureSize.Y),
			/*BlendState = */nullptr,
			/*RasterizerState = */nullptr,
			/*DepthStencilState = */nullptr,
			/*StencilRef = */0,
			ERDGPassFlags::NeverCull); // Use NeverCull because it renders a texture for which tracking is disabled
	}
};

IMPLEMENT_GLOBAL_SHADER(FLandscapeEditLayersHeightmapsGenerateMipsPS, "/Engine/Private/Landscape/LandscapeEditLayersHeightmaps.usf", "GenerateMipsPS", SF_Pixel);


// ----------------------------------------------------------------------------------
// /Engine/Private/Landscape/LandscapeEditLayersWeightmaps.usf shaders :

class FLandscapeEditLayersWeightmapsMergeEditLayerPS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FLandscapeEditLayersWeightmapsMergeEditLayerPS);
	SHADER_USE_PARAMETER_STRUCT(FLandscapeEditLayersWeightmapsMergeEditLayerPS, FGlobalShader);

public:
	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(uint32, InPaintLayerIndex)
		SHADER_PARAMETER(uint32, InEditLayerPaintLayerBlendMode)
		SHADER_PARAMETER(float, InEditLayerAlpha)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2DArray<float4>, InCurrentEditLayerWeightmaps)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2DArray<float4>, InPreviousEditLayersWeightmaps)
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& InParameters)
	{
		return UE::Landscape::DoesPlatformSupportEditLayers(InParameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& InParameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		OutEnvironment.SetDefine(TEXT("MERGE_EDIT_LAYER"), 1);
	}

	static void MergeEditLayerPS(FRDGBuilder& GraphBuilder, FParameters* InParameters, const FName& InTargetLayerName, const FIntPoint& InTextureSize)
	{
		FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(GMaxRHIFeatureLevel);
		TShaderMapRef<FLandscapeEditLayersWeightmapsMergeEditLayerPS> PixelShader(ShaderMap);

		FPixelShaderUtils::AddFullscreenPass(
			GraphBuilder,
			ShaderMap,
			RDG_EVENT_NAME("LandscapeLayers_MergeEditLayerPS (%s)", *InTargetLayerName.ToString()),
			PixelShader,
			InParameters,
			FIntRect(0, 0, InTextureSize.X, InTextureSize.Y), 
			/*BlendState = */nullptr,
			/*RasterizerState = */nullptr,
			/*DepthStencilState = */nullptr,
			/*StencilRef = */0,
			ERDGPassFlags::NeverCull); // Use NeverCull because it renders a texture for which tracking is disabled
	}
};

IMPLEMENT_GLOBAL_SHADER(FLandscapeEditLayersWeightmapsMergeEditLayerPS, "/Engine/Private/Landscape/LandscapeEditLayersWeightmaps.usf", "MergeEditLayerPS", SF_Pixel);

class FLandscapeEditLayersWeightmapsPerformLegacyWeightBlendingPS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FLandscapeEditLayersWeightmapsPerformLegacyWeightBlendingPS);
	SHADER_USE_PARAMETER_STRUCT(FLandscapeEditLayersWeightmapsPerformLegacyWeightBlendingPS, FGlobalShader);

public:
	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(uint32, InPaintLayerIndex)
		SHADER_PARAMETER(uint32, InNumPaintLayers)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<UE::Landscape::FWeightmapPaintLayerInfo>, InPaintLayerInfos)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2DArray<float4>, InCurrentEditLayerWeightmaps)
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& InParameters)
	{
		return UE::Landscape::DoesPlatformSupportEditLayers(InParameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& InParameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		OutEnvironment.SetDefine(TEXT("PERFORM_LEGACY_WEIGHT_BLENDING"), 1);
	}

	static void PerformLegacyWeightBlendingPS(FRDGBuilder& GraphBuilder, FParameters* InParameters, const FName& InTargetLayerName, const FIntPoint& InTextureSize)
	{
		FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(GMaxRHIFeatureLevel);
		TShaderMapRef<FLandscapeEditLayersWeightmapsPerformLegacyWeightBlendingPS> PixelShader(ShaderMap);

		FPixelShaderUtils::AddFullscreenPass(
			GraphBuilder,
			ShaderMap,
			RDG_EVENT_NAME("LandscapeLayers_PerformLegacyWeightBlendingPS (%s)", *InTargetLayerName.ToString()),
			PixelShader,
			InParameters,
			FIntRect(0, 0, InTextureSize.X, InTextureSize.Y), 
			/*BlendState = */nullptr,
			/*RasterizerState = */nullptr,
			/*DepthStencilState = */nullptr,
			/*StencilRef = */0,
			ERDGPassFlags::NeverCull); // Use NeverCull because it renders a texture for which tracking is disabled
	}
};

IMPLEMENT_GLOBAL_SHADER(FLandscapeEditLayersWeightmapsPerformLegacyWeightBlendingPS, "/Engine/Private/Landscape/LandscapeEditLayersWeightmaps.usf", "PerformLegacyWeightBlendingPS", SF_Pixel);

class FLandscapeEditLayersWeightmapsPackWeightmapPS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FLandscapeEditLayersWeightmapsPackWeightmapPS);
	SHADER_USE_PARAMETER_STRUCT(FLandscapeEditLayersWeightmapsPackWeightmapPS, FGlobalShader);

public:
	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntVector4, InSourceSliceIndices)
		SHADER_PARAMETER_ARRAY(FUintVector4, InSourcePixelOffsets, [4])
		SHADER_PARAMETER(FUintVector2, InSubsectionPixelOffset)
		SHADER_PARAMETER(uint32, InIsAdditive)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2DArray<float4>, InSourceWeightmaps)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2D<float4>, InWeightmapBeingPacked)
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& InParameters)
	{
		return UE::Landscape::DoesPlatformSupportEditLayers(InParameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& InParameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		OutEnvironment.SetDefine(TEXT("PACK_WEIGHTMAP"), 1);
	}

	static void PackWeightmapPS(FRDGBuilder& GraphBuilder, FParameters* InParameters, const FIntRect& InTextureRect)
	{
		FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(GMaxRHIFeatureLevel);
		TShaderMapRef<FLandscapeEditLayersWeightmapsPackWeightmapPS> PixelShader(ShaderMap);

		FPixelShaderUtils::AddFullscreenPass(
			GraphBuilder,
			ShaderMap,
			RDG_EVENT_NAME("LandscapeLayers_PackWeightmapPS"),
			PixelShader,
			InParameters,
			InTextureRect,
			/*BlendState = */nullptr,
			/*RasterizerState = */nullptr,
			/*DepthStencilState = */nullptr,
			/*StencilRef = */0,
			ERDGPassFlags::NeverCull); // Use NeverCull because it renders a texture for which tracking is disabled
	}
};

IMPLEMENT_GLOBAL_SHADER(FLandscapeEditLayersWeightmapsPackWeightmapPS, "/Engine/Private/Landscape/LandscapeEditLayersWeightmaps.usf", "PackWeightmapPS", SF_Pixel);

class FLandscapeEditLayersWeightmapsGenerateMipsPS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FLandscapeEditLayersWeightmapsGenerateMipsPS);
	SHADER_USE_PARAMETER_STRUCT(FLandscapeEditLayersWeightmapsGenerateMipsPS, FGlobalShader);

public:
	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FUintVector2, InCurrentMipSubsectionSize)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2D<float4>, InSourceWeightmap)
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& InParameters)
	{
		return UE::Landscape::DoesPlatformSupportEditLayers(InParameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& InParameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		OutEnvironment.SetDefine(TEXT("GENERATE_MIPS"), 1);
	}

	static void GenerateMipsPS(FRDGBuilder& GraphBuilder, FParameters* InParameters, const FIntPoint& InTextureSize)
	{
		FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(GMaxRHIFeatureLevel);
		TShaderMapRef<FLandscapeEditLayersWeightmapsGenerateMipsPS> PixelShader(ShaderMap);

		FPixelShaderUtils::AddFullscreenPass(
			GraphBuilder,
			ShaderMap,
			RDG_EVENT_NAME("LandscapeLayers_GenerateMipsPS"),
			PixelShader,
			InParameters,
			FIntRect(0, 0, InTextureSize.X, InTextureSize.Y), 
			/*BlendState = */nullptr,
			/*RasterizerState = */nullptr,
			/*DepthStencilState = */nullptr,
			/*StencilRef = */0,
			ERDGPassFlags::NeverCull); // Use NeverCull because it renders a texture for which tracking is disabled
	}
};

IMPLEMENT_GLOBAL_SHADER(FLandscapeEditLayersWeightmapsGenerateMipsPS, "/Engine/Private/Landscape/LandscapeEditLayersWeightmaps.usf", "GenerateMipsPS", SF_Pixel);


// ----------------------------------------------------------------------------------
// /Engine/Private/Landscape/LandscapeEditLayersUtils.usf shaders : 

class FCopyQuadsMultiSourcePS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FCopyQuadsMultiSourcePS);
	SHADER_USE_PARAMETER_STRUCT(FCopyQuadsMultiSourcePS, FGlobalShader);

	static constexpr int32 NumMultiSources = 63; // This has to match the number of input textures supported by CopyQuadsMultiSourcePS

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint2>, InQuadInfos)
		SHADER_PARAMETER_RDG_TEXTURE_ARRAY(Texture2D, InSourceTexture, [NumMultiSources])
	END_SHADER_PARAMETER_STRUCT()


	class FUseSwizleMask : SHADER_PERMUTATION_BOOL("USE_SWIZZLE_MASK");
	using FPermutationDomain = TShaderPermutationDomain<FUseSwizleMask>;

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& InParameters)
	{
		return UE::Landscape::DoesPlatformSupportEditLayers(InParameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		OutEnvironment.SetDefine(TEXT("COPY_QUADS_MULTISOURCE"), 1);
	}
};

IMPLEMENT_GLOBAL_SHADER(FCopyQuadsMultiSourcePS, "/Engine/Private/Landscape/LandscapeEditLayersUtils.usf", "CopyQuadsMultiSourcePS", SF_Pixel);

BEGIN_SHADER_PARAMETER_STRUCT(FCopyQuadsMultiSourcePSParameters, )
SHADER_PARAMETER_STRUCT_INCLUDE(FPixelShaderUtils::FRasterizeToRectsVS::FParameters, VS)
SHADER_PARAMETER_STRUCT_INCLUDE(FCopyQuadsMultiSourcePS::FParameters, PS)
RENDER_TARGET_BINDING_SLOTS()
END_SHADER_PARAMETER_STRUCT()


// ----------------------------------------------------------------------------------
// Copy texture render command

struct FLandscapeLayersCopyTextureParams
{
	FLandscapeLayersCopyTextureParams(UTexture* InSourceTexture, UTexture* InDestTexture)
	{
		if (InSourceTexture != nullptr)
		{
			SourceResourceDebugName = InSourceTexture->GetName();
			SourceResource = InSourceTexture->GetResource();
		}
		if (InDestTexture != nullptr)
		{
			DestResourceDebugName = InDestTexture->GetName();
			DestResource = InDestTexture->GetResource();
		}
	}

	FLandscapeLayersCopyTextureParams(const FString& InSourceResourceDebugName, FTextureResource* InSourceResource, const FString& InDestResourceDebugName, FTextureResource* InDestResource)
		: SourceResourceDebugName(InSourceResourceDebugName)
		, SourceResource(InSourceResource)
		, DestResourceDebugName(InDestResourceDebugName)
		, DestResource(InDestResource)
	{}

	FLandscapeLayersCopyTextureParams(const FLandscapeLayersCopyTextureParams&) = default;
	FLandscapeLayersCopyTextureParams(FLandscapeLayersCopyTextureParams&&) = default;
	FLandscapeLayersCopyTextureParams& operator=(FLandscapeLayersCopyTextureParams&&) = default;

	FString SourceResourceDebugName;
	FTextureResource* SourceResource = nullptr;
	FString DestResourceDebugName;
	FTextureResource* DestResource = nullptr;
	FIntPoint CopySize = FIntPoint(0, 0);
	FIntPoint SourcePosition = FIntPoint(0, 0);
	FIntPoint DestPosition = FIntPoint(0, 0);
	uint8 SourceMip = 0;
	uint8 DestMip = 0;
	uint32 SourceArrayIndex = 0;
	uint32 DestArrayIndex = 0;
	ERHIAccess SourceAccess = ERHIAccess::SRVMask;
	ERHIAccess DestAccess = ERHIAccess::SRVMask;
	// There's a shader-version of the copy that is able to swizzle RGBA channels : this mask allows to specify how : 
	uint8 ChannelSwizzleMask = UE::Landscape::Private::RGBAToRGBASwizzleMask;
};

class FLandscapeLayersCopyTexture_RenderThread
{
public:
	FLandscapeLayersCopyTexture_RenderThread(const FLandscapeLayersCopyTextureParams& InParams)
		: Params(InParams)
	{}

	const FLandscapeLayersCopyTextureParams& GetParams() const { return Params; }
	void Copy(FRHICommandListImmediate& InRHICmdList)
	{
		// We must use the PS version if swizzling channels or if the format is different (e.g. R8G8B8A8 to R8)
		if ((Params.SourceResource->TextureRHI->GetFormat() != Params.DestResource->TextureRHI->GetFormat())
			|| (Params.ChannelSwizzleMask != UE::Landscape::Private::RGBAToRGBASwizzleMask))
		{
			checkf(EnumHasAllFlags(Params.DestResource->TextureRHI->GetFlags(), ETextureCreateFlags::RenderTargetable), TEXT("Cannot request swizzling if the texture is not render-targetable"));
			checkf(!Params.DestResource->TextureRHI->GetDesc().IsTextureArray() || EnumHasAllFlags(Params.DestResource->TextureRHI->GetFlags(), ETextureCreateFlags::TargetArraySlicesIndependently), TEXT("Cannot request swizzling on a texture array if the slices are not individually render-targetable"));
			CopyInternalPS(InRHICmdList);
		}
		else
		{
			CopyInternal(InRHICmdList);
		}
	}

private:
	void CopyInternal(FRHICommandListImmediate& InRHICmdList)
	{
		// TODO [jonathan.bard] : make those perf tags optional : with the amount of textures we copy, it slows down texture copies quite a bit : 
		RHI_BREADCRUMB_EVENT_STAT(InRHICmdList, LandscapeLayers_CopyTexture
			, "LandscapeLayers_Copy %s -> %s, Mip (%d -> %d), Array Index (%d -> %d)"
			, Params.SourceResourceDebugName
			, Params.DestResourceDebugName
			, Params.SourceMip
			, Params.DestMip
			, Params.SourceArrayIndex
			, Params.DestArrayIndex
		);
		SCOPED_GPU_STAT(InRHICmdList, LandscapeLayers_CopyTexture);

		FIntPoint SourceSize(Params.SourceResource->GetSizeX() >> Params.SourceMip, Params.SourceResource->GetSizeY() >> Params.SourceMip);
		FIntPoint DestSize(Params.DestResource->GetSizeX() >> Params.DestMip, Params.DestResource->GetSizeY() >> Params.DestMip);

		FRHICopyTextureInfo Info;
		Info.NumSlices = 1;
		// If CopySize is passed, used that as the size (and don't adjust with the mip level : consider that the user has computed it properly) : 
		Info.Size.X = (Params.CopySize.X > 0) ? Params.CopySize.X : SourceSize.X;
		Info.Size.Y = (Params.CopySize.Y > 0) ? Params.CopySize.Y : SourceSize.Y;
		Info.Size.Z = 1;
		Info.SourcePosition.X = Params.SourcePosition.X;
		Info.SourcePosition.Y = Params.SourcePosition.Y;
		Info.DestPosition.X = Params.DestPosition.X;
		Info.DestPosition.Y = Params.DestPosition.Y;
		Info.SourceSliceIndex = Params.SourceArrayIndex;
		Info.DestSliceIndex = Params.DestArrayIndex;
		Info.SourceMipIndex = Params.SourceMip;
		Info.DestMipIndex = Params.DestMip;

		check((Info.SourcePosition.X >= 0) && (Info.SourcePosition.Y >= 0) && (Info.DestPosition.X >= 0) && (Info.DestPosition.Y >= 0));
		check(Info.SourcePosition.X + Info.Size.X <= SourceSize.X);
		check(Info.SourcePosition.Y + Info.Size.Y <= SourceSize.Y);
		check(Info.DestPosition.X + Info.Size.X <= DestSize.X);
		check(Info.DestPosition.Y + Info.Size.Y <= DestSize.Y);

		InRHICmdList.Transition(FRHITransitionInfo(Params.SourceResource->TextureRHI, Params.SourceAccess, ERHIAccess::CopySrc));
		InRHICmdList.Transition(FRHITransitionInfo(Params.DestResource->TextureRHI, Params.DestAccess, ERHIAccess::CopyDest));
		InRHICmdList.CopyTexture(Params.SourceResource->TextureRHI, Params.DestResource->TextureRHI, Info);
		InRHICmdList.Transition(FRHITransitionInfo(Params.SourceResource->TextureRHI, ERHIAccess::CopySrc, Params.SourceAccess));
		InRHICmdList.Transition(FRHITransitionInfo(Params.DestResource->TextureRHI, ERHIAccess::CopyDest, Params.DestAccess));
	}

	void CopyInternalPS(FRHICommandListImmediate& InRHICmdList)
	{
		using namespace UE::Landscape::Private;

		const int32 NumChannelsDest = GPixelFormats[Params.DestResource->TextureRHI->GetDesc().Format].NumComponents;

		// TODO [jonathan.bard] : make those perf tags optional : with the amount of textures we copy, it slows down texture copies quite a bit : 
		RHI_BREADCRUMB_EVENT_STAT(InRHICmdList, LandscapeLayers_CopyTexturePS
			, "LandscapeLayers_CopyPS %s -> %s, Mip (%d -> %d), Array Index (%d -> %d), [%s]"
			, Params.SourceResourceDebugName
			, Params.DestResourceDebugName
			, Params.SourceMip
			, Params.DestMip
			, Params.SourceArrayIndex
			, Params.DestArrayIndex
			, GetChannelSwizzleMaskDescription(Params.ChannelSwizzleMask, NumChannelsDest)
		);
		SCOPED_GPU_STAT(InRHICmdList, LandscapeLayers_CopyTexturePS);

		FIntPoint SourceSize(Params.SourceResource->GetSizeX() >> Params.SourceMip, Params.SourceResource->GetSizeY() >> Params.SourceMip);
		FIntPoint DestSize(Params.DestResource->GetSizeX() >> Params.DestMip, Params.DestResource->GetSizeY() >> Params.DestMip);

		// If CopySize is passed, used that as the size (and don't adjust with the mip level : consider that the user has computed it properly) : 
		FIntPoint Size;
		Size.X = (Params.CopySize.X > 0) ? Params.CopySize.X : SourceSize.X;
		Size.Y = (Params.CopySize.Y > 0) ? Params.CopySize.Y : SourceSize.Y;
		check((Params.SourceArrayIndex == 0) && (Params.SourceMip == 0) && (Params.DestMip == 0)); // The PS version of copy is not supported on texture arrays and mips for now

		InRHICmdList.Transition(FRHITransitionInfo(Params.SourceResource->TextureRHI, Params.SourceAccess, ERHIAccess::SRVGraphics));
		InRHICmdList.Transition(FRHITransitionInfo(Params.DestResource->TextureRHI, Params.DestAccess, ERHIAccess::RTV));

		int32 PassArraySlice = Params.DestResource->TextureRHI->GetDesc().IsTextureArray() ? Params.DestArrayIndex : -1; // Little hack to make sure we pass -1 to FRHIRenderPassInfo for a non-texture array resource as that's what it expects :
		FRHIRenderPassInfo RPInfo(Params.DestResource->TextureRHI, ERenderTargetActions::DontLoad_Store, /*ResolveRT = */nullptr, /*InMipIndex = */0, PassArraySlice);
		InRHICmdList.BeginRenderPass(RPInfo, TEXT("CopyTexture"));

		FGlobalShaderMap* GlobalShaderMap = GetGlobalShaderMap(GMaxRHIFeatureLevel);
		TShaderMapRef< FLandscapeCopyTextureVS > VertexShader(GlobalShaderMap);
		TShaderMapRef< FLandscapeCopyTexturePS > PixelShader(GlobalShaderMap);

		FGraphicsPipelineStateInitializer GraphicsPSOInit;
		InRHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);
		GraphicsPSOInit.BlendState = TStaticBlendState<>::GetRHI();
		GraphicsPSOInit.RasterizerState = TStaticRasterizerState<>::GetRHI();
		GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();
		GraphicsPSOInit.PrimitiveType = PT_TriangleList;
		GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GetVertexDeclarationFVector4();
		GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
		GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();
		SetGraphicsPipelineState(InRHICmdList, GraphicsPSOInit, 0);

		SetShaderParametersLegacyPS(InRHICmdList, PixelShader, Params.SourceResource->TextureRHI, Params.SourcePosition, Size, Params.ChannelSwizzleMask);

		InRHICmdList.SetViewport((float)Params.DestPosition.X, (float)Params.DestPosition.Y, 0.0f, (float)(Params.DestPosition.X + Size.X), (float)(Params.DestPosition.Y + Size.Y), 1.0f);
		InRHICmdList.DrawIndexedPrimitive(GTwoTrianglesIndexBuffer.IndexBufferRHI, 0, 0, 4, 0, 2, 1);

		InRHICmdList.EndRenderPass();

		InRHICmdList.Transition(FRHITransitionInfo(Params.SourceResource->TextureRHI, ERHIAccess::SRVGraphics, Params.SourceAccess));
		InRHICmdList.Transition(FRHITransitionInfo(Params.DestResource->TextureRHI, ERHIAccess::RTV, Params.DestAccess));
	}

private:
	FLandscapeLayersCopyTextureParams Params;
};

// ----------------------------------------------------------------------------------
// Clear command

class LandscapeLayersWeightmapClear_RenderThread
{
public:
	LandscapeLayersWeightmapClear_RenderThread(const FString& InDebugName, FTextureRenderTargetResource* InTextureResourceToClear)
		: DebugName(InDebugName)
		, RenderTargetResource(InTextureResourceToClear)
	{}

	virtual ~LandscapeLayersWeightmapClear_RenderThread()
	{}

	void Clear(FRHICommandListImmediate& InRHICmdList)
	{
		RHI_BREADCRUMB_EVENT_STAT(InRHICmdList, LandscapeLayers_Clear, "LandscapeLayers_Clear %s", DebugName);
		SCOPED_GPU_STAT(InRHICmdList, LandscapeLayers_Clear);
		TRACE_CPUPROFILER_EVENT_SCOPE(LandscapeLayersWeightmapClear_RenderThread::Clear);

		check(IsInRenderingThread());

		InRHICmdList.Transition(FRHITransitionInfo(RenderTargetResource->TextureRHI, ERHIAccess::SRVMask, ERHIAccess::RTV));
		FRHIRenderPassInfo RPInfo(RenderTargetResource->TextureRHI, ERenderTargetActions::Clear_Store);
		InRHICmdList.BeginRenderPass(RPInfo, TEXT("Clear"));
		InRHICmdList.EndRenderPass();
		InRHICmdList.Transition(FRHITransitionInfo(RenderTargetResource->TextureRHI, ERHIAccess::RTV, ERHIAccess::SRVMask));
	}

	FString DebugName;
	FTextureRenderTargetResource* RenderTargetResource;
};

// ----------------------------------------------------------------------------------
// Render command

template<typename ShaderDataType, typename ShaderPixelClass, typename ShaderPixelMipsClass>
class FLandscapeLayersRender_RenderThread
{
public:

	FLandscapeLayersRender_RenderThread(const FString& InDebugName, UTextureRenderTarget2D* InWriteRenderTarget, const FIntPoint& InWriteRenderTargetSize, const FIntPoint& InReadRenderTargetSize, const FMatrix& InProjectionMatrix,
		const ShaderDataType& InShaderParams, uint8 InCurrentMip, const TArray<FLandscapeLayersTriangle>& InTriangleList)
		: RenderTargetResource(InWriteRenderTarget->GameThread_GetRenderTargetResource())
		, WriteRenderTargetSize(InWriteRenderTargetSize)
		, ReadRenderTargetSize(InReadRenderTargetSize)
		, ProjectionMatrix(InProjectionMatrix)
		, ShaderParams(InShaderParams)
		, PrimitiveCount(InTriangleList.Num())
		, DebugName(InDebugName)
		, CurrentMip(InCurrentMip)
	{
		VertexBufferResource.Init(InTriangleList);
	}

	virtual ~FLandscapeLayersRender_RenderThread()
	{}

	void Render(FRHICommandListImmediate& InRHICmdList, bool InClearRT)
	{
		RHI_BREADCRUMB_EVENT_STAT(InRHICmdList, LandscapeLayers_Render, "LandscapeLayers_Render %s", DebugName);
		SCOPED_GPU_STAT(InRHICmdList, LandscapeLayers_Render);
		INC_DWORD_STAT(STAT_LandscapeLayersRegenerateDrawCalls);
		TRACE_CPUPROFILER_EVENT_SCOPE(FLandscapeLayersRender_RenderThread::Render);

		check(IsInRenderingThread());

		FSceneViewFamilyContext ViewFamily(FSceneViewFamily::ConstructionValues(RenderTargetResource, NULL, FEngineShowFlags(ESFIM_Game))
			.SetTime(FGameTime::GetTimeSinceAppStart()));

		FSceneViewInitOptions ViewInitOptions;
		ViewInitOptions.SetViewRectangle(FIntRect(0, 0, WriteRenderTargetSize.X, WriteRenderTargetSize.Y));
		ViewInitOptions.ViewOrigin = FVector::ZeroVector;
		ViewInitOptions.ViewRotationMatrix = FMatrix::Identity;
		ViewInitOptions.ProjectionMatrix = ProjectionMatrix;
		ViewInitOptions.ViewFamily = &ViewFamily;
		ViewInitOptions.BackgroundColor = FLinearColor::Black;
		ViewInitOptions.OverlayColor = FLinearColor::White;

		// Create and add the new view
		FSceneView* View = new FSceneView(ViewInitOptions);
		ViewFamily.Views.Add(View);

		InRHICmdList.Transition(FRHITransitionInfo(ViewFamily.RenderTarget->GetRenderTargetTexture(), ERHIAccess::SRVMask, ERHIAccess::RTV));

		// Init VB/IB Resource
		VertexDeclaration.InitResource(InRHICmdList);
		VertexBufferResource.InitResource(InRHICmdList);

		// Setup Pipeline
		FGraphicsPipelineStateInitializer GraphicsPSOInit;
		GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = VertexDeclaration.VertexDeclarationRHI;
		GraphicsPSOInit.PrimitiveType = PT_TriangleList;

		GraphicsPSOInit.BlendState = TStaticBlendState<CW_RGBA, BO_Add, BF_One, BF_Zero, BO_Add, BF_One, BF_Zero>::GetRHI();
		GraphicsPSOInit.RasterizerState = TStaticRasterizerState<>::GetRHI();
		GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();

		FRHIRenderPassInfo RenderPassInfo(ViewFamily.RenderTarget->GetRenderTargetTexture(), CurrentMip == 0 || InClearRT ? ERenderTargetActions::Clear_Store : ERenderTargetActions::Load_Store, nullptr, 0, 0);
		InRHICmdList.BeginRenderPass(RenderPassInfo, TEXT("DrawLayers"));

		if (CurrentMip == 0)
		{
			// Setup Shaders
			TShaderMapRef<FLandscapeLayersVS> VertexShader(GetGlobalShaderMap(View->GetFeatureLevel()));
			TShaderMapRef<ShaderPixelClass> PixelShader(GetGlobalShaderMap(View->GetFeatureLevel()));

			GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
			GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();

			InRHICmdList.SetViewport(static_cast<float>(View->UnscaledViewRect.Min.X), static_cast<float>(View->UnscaledViewRect.Min.Y), 0.0f,
			                         static_cast<float>(View->UnscaledViewRect.Max.X), static_cast<float>(View->UnscaledViewRect.Max.Y), 1.0f);

			InRHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);
			SetGraphicsPipelineState(InRHICmdList, GraphicsPSOInit, 0);

			// Set shader params
			SetShaderParametersLegacyVS(InRHICmdList, VertexShader, FMatrix44f(ProjectionMatrix));		// LWC_TODo: Precision loss?
			SetShaderParametersLegacyPS(InRHICmdList, PixelShader, ShaderParams);
		}
		else
		{
			// Setup Shaders
			TShaderMapRef<FLandscapeLayersVS> VertexShader(GetGlobalShaderMap(View->GetFeatureLevel()));
			TShaderMapRef<ShaderPixelMipsClass> PixelShader(GetGlobalShaderMap(View->GetFeatureLevel()));

			GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
			GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();

			InRHICmdList.SetViewport(0.0f, 0.0f, 0.0f, static_cast<float>(WriteRenderTargetSize.X), static_cast<float>(WriteRenderTargetSize.Y), 1.0f);

			InRHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);
			SetGraphicsPipelineState(InRHICmdList, GraphicsPSOInit, 0);

			// Set shader params
			SetShaderParametersLegacyVS(InRHICmdList, VertexShader, FMatrix44f(ProjectionMatrix));		// LWC_TODo: Precision loss?
			SetShaderParametersLegacyPS(InRHICmdList, PixelShader, ShaderParams);
		}

		InRHICmdList.SetScissorRect(false, 0, 0, 0, 0);
		InRHICmdList.SetStreamSource(0, VertexBufferResource.VertexBufferRHI, 0);

		InRHICmdList.DrawPrimitive(0, PrimitiveCount, 1);

		InRHICmdList.EndRenderPass();
		InRHICmdList.Transition(FRHITransitionInfo(ViewFamily.RenderTarget->GetRenderTargetTexture(), ERHIAccess::RTV, ERHIAccess::SRVMask));

		VertexDeclaration.ReleaseResource();
		VertexBufferResource.ReleaseResource();
	}

private:
	FTextureRenderTargetResource* RenderTargetResource;
	FIntPoint WriteRenderTargetSize;
	FIntPoint ReadRenderTargetSize;
	FMatrix ProjectionMatrix;
	ShaderDataType ShaderParams;
	FLandscapeLayersVertexBuffer VertexBufferResource;
	int32 PrimitiveCount;
	FLandscapeLayersVertexDeclaration VertexDeclaration;
	FString DebugName;
	uint8 CurrentMip;
};

typedef FLandscapeLayersRender_RenderThread<FLandscapeLayersHeightmapShaderParameters, FLandscapeLayersHeightmapPS, FLandscapeLayersHeightmapMipsPS> FLandscapeLayersHeightmapRender_RenderThread;
typedef FLandscapeLayersRender_RenderThread<FLandscapeLayersWeightmapShaderParameters, FLandscapeLayersWeightmapPS, FLandscapeLayersWeightmapMipsPS> FLandscapeLayersWeightmapRender_RenderThread;

#if WITH_EDITOR

bool ALandscape::IsMaterialResourceCompiled(FMaterialResource* InMaterialResource, bool bInWaitForCompilation)
{
	check(InMaterialResource);
	if (InMaterialResource->IsGameThreadShaderMapComplete())
	{
		return true;
	}

	{
		TRACE_CPUPROFILER_EVENT_SCOPE(Landscape_WaitForMaterialCompilation);
		InMaterialResource->SubmitCompileJobs_GameThread(EShaderCompileJobPriority::High);
		if (bInWaitForCompilation)
		{
			InMaterialResource->FinishCompilation();
		}
	}
	return InMaterialResource->IsGameThreadShaderMapComplete();
}

bool ALandscape::ComputeLandscapeLayerBrushInfo(FTransform& OutLandscapeTransform, FIntPoint& OutLandscapeSize, FIntPoint& OutLandscapeRenderTargetSize)
{
	ULandscapeInfo* Info = GetLandscapeInfo();
	if (Info == nullptr)
	{
		return false;
	}

	FIntRect LandscapeExtent;
	if (!Info->GetLandscapeExtent(LandscapeExtent.Min.X, LandscapeExtent.Min.Y, LandscapeExtent.Max.X, LandscapeExtent.Max.Y))
	{
		return false;
	}

	ALandscape* Landscape = GetLandscapeActor();
	if (Landscape == nullptr)
	{
		return false;
	}

	OutLandscapeTransform = Landscape->GetTransform();
	FVector OffsetVector(LandscapeExtent.Min.X, LandscapeExtent.Min.Y, 0.f);
	FVector Translation = OutLandscapeTransform.TransformFVector4(OffsetVector);
	OutLandscapeTransform.SetTranslation(Translation);
	OutLandscapeSize = LandscapeExtent.Max - LandscapeExtent.Min;

	const FIntPoint ComponentCounts = ComputeComponentCounts();
	OutLandscapeRenderTargetSize.X = FMath::RoundUpToPowerOfTwo(((SubsectionSizeQuads + 1) * NumSubsections) * ComponentCounts.X);
	OutLandscapeRenderTargetSize.Y = FMath::RoundUpToPowerOfTwo(((SubsectionSizeQuads + 1) * NumSubsections) * ComponentCounts.Y);

	return true;
}

ELandscapeEditLayersMergeMode ALandscape::GetEditLayersMergeMode()
{
	ELandscapeEditLayersMergeMode DesiredMode = static_cast<ELandscapeEditLayersMergeMode>(FMath::Min<uint8>(static_cast<uint8>(LandscapeEditLayersLocalMerge), static_cast<uint8>(ELandscapeEditLayersMergeMode::Invalid) - 1));

	if (DesiredMode == ELandscapeEditLayersMergeMode::LocalMerge)
{
	ALandscape* Landscape = GetLandscapeActor();
	check(Landscape);

	for (const FLandscapeLayer& Layer : Landscape->LandscapeEditLayers)
	{
		// No BP brush is supported for local merge of edit layers : 
		if (Layer.bVisible && Algo::AnyOf(Layer.Brushes, [](const FLandscapeLayerBrush& Brush) { return (Brush.GetBrush() != nullptr) && Brush.GetBrush()->IsVisible(); }))
		{
				return ELandscapeEditLayersMergeMode::GlobalMerge;
			}
		}
	}

	return DesiredMode;
}

bool ALandscape::HasNormalCaptureBPBrushLayer()
{
	ALandscape* Landscape = GetLandscapeActor();
	check(Landscape);

	for (const FLandscapeLayer& Layer : Landscape->LandscapeEditLayers)
	{
		if (Layer.bVisible && Algo::AnyOf(Layer.Brushes, [](const FLandscapeLayerBrush& Brush) { return (Brush.GetBrush() != nullptr) && Brush.GetBrush()->IsVisible() && Brush.GetBrush()->GetCaptureBoundaryNormals(); }))
		{
			return true;
		}
	}

	return false;
}

bool ALandscape::CreateLayersRenderingResource(bool bUseNormalCapture)
{
	ULandscapeInfo* Info = GetLandscapeInfo();
	if (Info == nullptr)
	{
		return false;
	}

	const FIntPoint ComponentCounts = ComputeComponentCounts();

	// No components, can't update the render targets	
	if (ComponentCounts.X <= 0 || ComponentCounts.Y <= 0)
	{
		return false;
	}

	ALandscape* Landscape = GetLandscapeActor();
	check(Landscape);

	if (GetEditLayersMergeMode() != ELandscapeEditLayersMergeMode::GlobalMerge)
	{
		ReleaseLayersRenderingResource();

		Landscape->HeightmapRTList.Empty();
		Landscape->WeightmapRTList.Empty();
	}
	else
	{
		// Heightmap mip size
		int32 ComponentVerts = ((SubsectionSizeQuads + 1) * NumSubsections);
		int32 CurrentMipSizeX = ComponentVerts * ComponentCounts.X;
		int32 CurrentMipSizeY = ComponentVerts * ComponentCounts.Y;

		// when using normal capture, we require an extra padding of 2 pixels --
		//   but we round up to a full component of padding because the mip downsample shader code currently requires perfect 2:1 ratios in resolutions between mips
		//   and keeping CurrentMipSizeX/Y a multiple of ComponentVerts ensures that (ComponentVerts is guaranteed to be a power of 2) 
		if (bUseNormalCapture)
		{
			int32 PaddingX = FMath::RoundUpToPowerOfTwo(CurrentMipSizeX) - CurrentMipSizeX;
			int32 PaddingY = FMath::RoundUpToPowerOfTwo(CurrentMipSizeY) - CurrentMipSizeY;
			if (PaddingX < 2)
			{
				CurrentMipSizeX += ComponentVerts;
			}
			if (PaddingY < 2)
			{
				CurrentMipSizeY += ComponentVerts;
			}
		}

		if (CurrentMipSizeX > GRHIGlobals.MaxTextureDimensions || CurrentMipSizeY > GRHIGlobals.MaxTextureDimensions)
		{
			if (!bWarnedGlobalMergeDimensionsExceeded)
			{
				UE_LOG(LogLandscape, Error, TEXT("Cannot initialize resources for Landscape Layer Merge because the current device does not support render targets of the required size.  Please reduce landscape size, or use a different render device, or try to enable local merge with `landscape.EditLayersLocalMerge.Enable 1` (local merge works only if no landscape blueprint brushes are used)"));
				bWarnedGlobalMergeDimensionsExceeded = true;
			}
			return false;
		}

		// once the issue is fixed, clear the warn flag
		bWarnedGlobalMergeDimensionsExceeded = false;

		bool bCreateFromScratch = (Landscape->HeightmapRTList.Num() == 0);
		if (bCreateFromScratch)
		{
			Landscape->HeightmapRTList.Init(nullptr, (int32)EHeightmapRTType::HeightmapRT_Count);
		}

		auto InitOrResizeRT = [](UTextureRenderTarget2D* RT, int32 ResX, int32 ResY, bool bInit)
		{
			if (bInit)
			{
				RT->InitAutoFormat(FMath::RoundUpToPowerOfTwo(ResX), FMath::RoundUpToPowerOfTwo(ResY));
				RT->UpdateResourceImmediate(true);
			}
			else
			{
				RT->ResizeTarget(FMath::RoundUpToPowerOfTwo(ResX), FMath::RoundUpToPowerOfTwo(ResY));
			}
		};

		for (int32 i = 0; i < (int32)EHeightmapRTType::HeightmapRT_Count; ++i)
		{
			if (bCreateFromScratch)
			{
				FText DisplayName = StaticEnum<EHeightmapRTType>()->GetDisplayValueAsText((EHeightmapRTType)i);
				FName RTName = MakeUniqueObjectName(GetTransientPackage(), UTextureRenderTarget2D::StaticClass(), FName(*DisplayName.ToString()));
				Landscape->HeightmapRTList[i] = NewObject<UTextureRenderTarget2D>(GetTransientPackage(), RTName, RF_Transient);
				check(Landscape->HeightmapRTList[i]);
				Landscape->HeightmapRTList[i]->RenderTargetFormat = RTF_RGBA8;
				Landscape->HeightmapRTList[i]->AddressX = TextureAddress::TA_Clamp;
				Landscape->HeightmapRTList[i]->AddressY = TextureAddress::TA_Clamp;
				Landscape->HeightmapRTList[i]->ClearColor = FLinearColor(0.0f, 0.0f, 0.0f, 0.0f);
			}

			if (i < (int32)EHeightmapRTType::HeightmapRT_Mip1) // Landscape size RT
			{
				InitOrResizeRT(Landscape->HeightmapRTList[i], CurrentMipSizeX, CurrentMipSizeY, bCreateFromScratch);
			}
			else // Mips
			{
				CurrentMipSizeX >>= 1;
				CurrentMipSizeY >>= 1;
				InitOrResizeRT(Landscape->HeightmapRTList[i], CurrentMipSizeX, CurrentMipSizeY, bCreateFromScratch);
			}

			// Only generate required mips RT
			if (CurrentMipSizeX == ComponentCounts.X && CurrentMipSizeY == ComponentCounts.Y)
			{
				break;
			}
		}

		// Weightmap mip size
		CurrentMipSizeX = ((SubsectionSizeQuads + 1) * NumSubsections) * ComponentCounts.X;
		CurrentMipSizeY = ((SubsectionSizeQuads + 1) * NumSubsections) * ComponentCounts.Y;
		bCreateFromScratch = (Landscape->WeightmapRTList.Num() == 0);

		if (bCreateFromScratch)
		{
			Landscape->WeightmapRTList.Init(nullptr, (int32)EWeightmapRTType::WeightmapRT_Count);
		}

		for (int32 i = 0; i < (int32)EWeightmapRTType::WeightmapRT_Count; ++i)
		{
			if (bCreateFromScratch)
			{
				FText DisplayName = StaticEnum<EHeightmapRTType>()->GetDisplayValueAsText((EWeightmapRTType)i);
				FName RTName = MakeUniqueObjectName(GetTransientPackage(), UTextureRenderTarget2D::StaticClass(), FName(*DisplayName.ToString()));
				Landscape->WeightmapRTList[i] = NewObject<UTextureRenderTarget2D>(GetTransientPackage(), RTName, RF_Transient);

				check(Landscape->WeightmapRTList[i]);
				Landscape->WeightmapRTList[i]->AddressX = TextureAddress::TA_Clamp;
				Landscape->WeightmapRTList[i]->AddressY = TextureAddress::TA_Clamp;
				Landscape->WeightmapRTList[i]->ClearColor = FLinearColor(0.0f, 0.0f, 0.0f, 0.0f);
				Landscape->WeightmapRTList[i]->RenderTargetFormat = RTF_RGBA8;

				// scratch 1/2/3 RTs are R8 format
				if ((i >= (int32)EWeightmapRTType::WeightmapRT_Scratch1) &&
					(i < (int32)EWeightmapRTType::WeightmapRT_Mip0))
				{
					Landscape->WeightmapRTList[i]->RenderTargetFormat = RTF_R8;
				}
			}

			if (i < (int32)EWeightmapRTType::WeightmapRT_Mip0)
			{
				InitOrResizeRT(Landscape->WeightmapRTList[i], CurrentMipSizeX, CurrentMipSizeY, bCreateFromScratch);
			}
			else // Mips
			{
				InitOrResizeRT(Landscape->WeightmapRTList[i], CurrentMipSizeX, CurrentMipSizeY, bCreateFromScratch);

				CurrentMipSizeX >>= 1;
				CurrentMipSizeY >>= 1;
			}

			// Only generate required mips RT
			if (CurrentMipSizeX < ComponentCounts.X && CurrentMipSizeY < ComponentCounts.Y)
			{
				break;
			}
		}

		InitializeLayersWeightmapResources();
	}
	return true;
}

void ALandscape::ToggleCanHaveLayersContent()
{
	bCanHaveLayersContent = !bCanHaveLayersContent;

	if (!bCanHaveLayersContent)
	{
		ReleaseLayersRenderingResource();
		DeleteLayers();
	}
	else
	{
		check(GetLayerCount() == 0);
		CreateDefaultLayer();
		CopyOldDataToDefaultLayer();
	}

	if (LandscapeEdMode)
	{
		LandscapeEdMode->OnCanHaveLayersContentChanged();
	}
}

void ALandscape::ReleaseLayersRenderingResource()
{
	ULandscapeInfo* Info = GetLandscapeInfo();
	if (Info == nullptr)
	{
		return;
	}

	Info->ForEachLandscapeProxy([&](ALandscapeProxy* Proxy)
	{
		for (auto& ItPair : Proxy->HeightmapsCPUReadback)
		{
			FLandscapeEditLayerReadback* HeightmapCPUReadback = ItPair.Value;
			delete HeightmapCPUReadback;
		}
		Proxy->HeightmapsCPUReadback.Empty();

		for (auto& ItPair : Proxy->WeightmapsCPUReadback)
		{
			FLandscapeEditLayerReadback* WeightmapCPUReadback = ItPair.Value;
			delete WeightmapCPUReadback;
		}
		Proxy->WeightmapsCPUReadback.Empty();
		return true;
	});

	if (CombinedLayersWeightmapAllMaterialLayersResource != nullptr)
	{
		BeginReleaseResource(CombinedLayersWeightmapAllMaterialLayersResource);
	}

	if (CurrentLayersWeightmapAllMaterialLayersResource != nullptr)
	{
		BeginReleaseResource(CurrentLayersWeightmapAllMaterialLayersResource);
	}

	if (WeightmapScratchExtractLayerTextureResource != nullptr)
	{
		BeginReleaseResource(WeightmapScratchExtractLayerTextureResource);
	}

	if (WeightmapScratchPackLayerTextureResource != nullptr)
	{
		BeginReleaseResource(WeightmapScratchPackLayerTextureResource);
	}

	{
		TRACE_CPUPROFILER_EVENT_SCOPE(LandscapeLayers_Flush_ResourceRelease);
		FlushRenderingCommands();
	}

	delete CombinedLayersWeightmapAllMaterialLayersResource;
	delete CurrentLayersWeightmapAllMaterialLayersResource;
	delete WeightmapScratchExtractLayerTextureResource;
	delete WeightmapScratchPackLayerTextureResource;

	CombinedLayersWeightmapAllMaterialLayersResource = nullptr;
	CurrentLayersWeightmapAllMaterialLayersResource = nullptr;
	WeightmapScratchExtractLayerTextureResource = nullptr;
	WeightmapScratchPackLayerTextureResource = nullptr;
}

FIntPoint ALandscape::ComputeComponentCounts() const
{
	ULandscapeInfo* Info = GetLandscapeInfo();

	if (Info == nullptr)
	{
		return FIntPoint(INDEX_NONE, INDEX_NONE);
	}

	FIntPoint NumComponents(0, 0);
	FIntPoint MaxSectionBase(TNumericLimits<int32>::Min(), TNumericLimits<int32>::Min());
	FIntPoint MinSectionBase(TNumericLimits<int32>::Max(), TNumericLimits<int32>::Max());

	Info->ForEachLandscapeProxy([&MaxSectionBase, &MinSectionBase](ALandscapeProxy* Proxy)
	{
		for (ULandscapeComponent* Component : Proxy->LandscapeComponents)
		{
			MaxSectionBase.X = FMath::Max(MaxSectionBase.X, Component->SectionBaseX);
			MaxSectionBase.Y = FMath::Max(MaxSectionBase.Y, Component->SectionBaseY);

			MinSectionBase.X = FMath::Min(MinSectionBase.X, Component->SectionBaseX);
			MinSectionBase.Y = FMath::Min(MinSectionBase.Y, Component->SectionBaseY);
		}
		return true;
	});

	if ((MaxSectionBase.X >= MinSectionBase.X) && (MaxSectionBase.Y >= MinSectionBase.Y))
	{
		NumComponents.X = ((MaxSectionBase.X - MinSectionBase.X) / ComponentSizeQuads) + 1;
		NumComponents.Y = ((MaxSectionBase.Y - MinSectionBase.Y) / ComponentSizeQuads) + 1;
	}

	return NumComponents;
}

void ALandscape::CopyOldDataToDefaultLayer()
{
	ULandscapeInfo* Info = GetLandscapeInfo();

	if (Info == nullptr)
	{
		return;
	}

	Info->ForEachLandscapeProxy([&](ALandscapeProxy* Proxy)
	{
		CopyOldDataToDefaultLayer(Proxy);
		return true;
	});
}

void ALandscape::CopyOldDataToDefaultLayer(ALandscapeProxy* InProxy)
{
	ULandscapeInfo* Info = GetLandscapeInfo();

	if (Info == nullptr)
	{
		return;
	}

	if (InProxy->LandscapeComponents.IsEmpty())
	{
		// No data to migrate, we can early-out to avoid modifying the proxy:
		return;
	}

	InProxy->Modify();

	const FLandscapeLayer* DefaultLayer = GetLayerConst(0);
	check(DefaultLayer != nullptr);

	struct FWeightmapTextureData
	{
		UTexture2D* Texture;
		ULandscapeWeightmapUsage* Usage;
	};

	TMap<UTexture2D*, FWeightmapTextureData> ProcessedWeightmaps;
	TArray<UTexture2D*> ProcessedHeightmaps;
	TArray<ULandscapeComponent*> WeightmapsComponentsToCleanup;

	for (ULandscapeComponent* Component : InProxy->LandscapeComponents)
	{
		FLandscapeLayerComponentData* LayerData = Component->GetLayerData(DefaultLayer->Guid);

		if (ensure(LayerData != nullptr && LayerData->IsInitialized()))
		{
			// Heightmap
			UTexture2D* ComponentHeightmap = Component->GetHeightmap();

			if (!ProcessedHeightmaps.Contains(ComponentHeightmap))
			{
				ProcessedHeightmaps.Add(ComponentHeightmap);

				UTexture* DefaultLayerHeightmap = LayerData->HeightmapData.Texture;
				check(DefaultLayerHeightmap != nullptr);

				// Only copy Mip0 as other mips will get regenerated
				TArray64<uint8> ExistingMip0Data;
				ComponentHeightmap->Source.GetMipData(ExistingMip0Data, 0);

				// Calling modify here makes sure that async texture compilation finishes so we can Lock the mip
				DefaultLayerHeightmap->Modify();
				FColor* Mip0Data = (FColor*)DefaultLayerHeightmap->Source.LockMip(0);
				FMemory::Memcpy(Mip0Data, ExistingMip0Data.GetData(), ExistingMip0Data.Num());
				DefaultLayerHeightmap->Source.UnlockMip(0);

				DefaultLayerHeightmap->UpdateResource();
			}

			// Weightmaps
			WeightmapsComponentsToCleanup.Add(Component);

			const TArray<UTexture2D*>& ComponentWeightmapTextures = Component->GetWeightmapTextures();
			const TArray<FWeightmapLayerAllocationInfo>& ComponentLayerAllocations = Component->GetWeightmapLayerAllocations();
			const TArray<ULandscapeWeightmapUsage*>& ComponentWeightmapTexturesUsage = Component->GetWeightmapTexturesUsage();

			LayerData->WeightmapData.Textures.AddDefaulted(ComponentWeightmapTextures.Num());
			LayerData->WeightmapData.TextureUsages.AddDefaulted(ComponentWeightmapTexturesUsage.Num());

			for (int32 TextureIndex = 0; TextureIndex < ComponentWeightmapTextures.Num(); ++TextureIndex)
			{
				UTexture2D* ComponentWeightmap = ComponentWeightmapTextures[TextureIndex];
				const FWeightmapTextureData* WeightmapTextureData = ProcessedWeightmaps.Find(ComponentWeightmap);

				if (WeightmapTextureData != nullptr)
				{
					LayerData->WeightmapData.Textures[TextureIndex] = WeightmapTextureData->Texture;
					LayerData->WeightmapData.TextureUsages[TextureIndex] = WeightmapTextureData->Usage;
					check(WeightmapTextureData->Usage->LayerGuid == DefaultLayer->Guid);

					for (int32 ChannelIndex = 0; ChannelIndex < ULandscapeWeightmapUsage::NumChannels; ++ChannelIndex)
					{
						const ULandscapeComponent* ChannelLandscapeComponent = LayerData->WeightmapData.TextureUsages[TextureIndex]->ChannelUsage[ChannelIndex];

						if (ChannelLandscapeComponent != nullptr && ChannelLandscapeComponent == Component)
						{
							for (const FWeightmapLayerAllocationInfo& Allocation : ComponentLayerAllocations)
							{
								if (Allocation.WeightmapTextureIndex == TextureIndex)
								{
									LayerData->WeightmapData.LayerAllocations.Add(Allocation);
								}
							}

							break;
						}
					}
				}
				else
				{
					// No need for mip chain on edit layers : 
					UTexture2D* NewLayerWeightmapTexture = InProxy->CreateLandscapeTexture(ComponentWeightmap->Source.GetSizeX(), ComponentWeightmap->Source.GetSizeY(), TEXTUREGROUP_Terrain_Weightmap, 
						ComponentWeightmap->Source.GetFormat(), /*OptionalOverrideOuter = */ nullptr, /*bCompress = */false, /*bMipChain = */false);

					// Only copy Mip0 as other mips will get regenerated
					TArray64<uint8> ExistingMip0Data;
					ComponentWeightmap->Source.GetMipData(ExistingMip0Data, 0);

					FColor* Mip0Data = (FColor*)NewLayerWeightmapTexture->Source.LockMip(0);
					FMemory::Memcpy(Mip0Data, ExistingMip0Data.GetData(), ExistingMip0Data.Num());
					NewLayerWeightmapTexture->Source.UnlockMip(0);

					LayerData->WeightmapData.Textures[TextureIndex] = NewLayerWeightmapTexture;
					LayerData->WeightmapData.TextureUsages[TextureIndex] = InProxy->WeightmapUsageMap.Add(NewLayerWeightmapTexture, InProxy->CreateWeightmapUsage());

					for (int32 ChannelIndex = 0; ChannelIndex < ULandscapeWeightmapUsage::NumChannels; ++ChannelIndex)
					{
						LayerData->WeightmapData.TextureUsages[TextureIndex]->ChannelUsage[ChannelIndex] = ComponentWeightmapTexturesUsage[TextureIndex]->ChannelUsage[ChannelIndex];
					}

					LayerData->WeightmapData.TextureUsages[TextureIndex]->LayerGuid = DefaultLayer->Guid;

					// Create new Usage for the "final" layer as the other one will now be used by the Default layer
					for (const FWeightmapLayerAllocationInfo& Allocation : ComponentLayerAllocations)
					{
						if (Allocation.WeightmapTextureIndex == TextureIndex)
						{
							LayerData->WeightmapData.LayerAllocations.Add(Allocation);
						}
					}

					FWeightmapTextureData NewTextureData;
					NewTextureData.Texture = NewLayerWeightmapTexture;
					NewTextureData.Usage = LayerData->WeightmapData.TextureUsages[TextureIndex];

					ProcessedWeightmaps.Add(ComponentWeightmap, NewTextureData);

					NewLayerWeightmapTexture->UpdateResource();
				}
			}
		}
	}

	for (ULandscapeComponent* Component : WeightmapsComponentsToCleanup)
	{
		TArray<FWeightmapLayerAllocationInfo>& ComponentLayerAllocations = Component->GetWeightmapLayerAllocations();

		for (FWeightmapLayerAllocationInfo& Allocation : ComponentLayerAllocations)
		{
			Allocation.Free();
		}
	}
}

void ALandscape::UpdateProxyLayersWeightmapUsage()
{
	ULandscapeInfo* Info = GetLandscapeInfo();

	if (Info == nullptr)
	{
		return;
	}

	Info->ForEachLandscapeProxy([&](ALandscapeProxy* Proxy)
	{
		Proxy->UpdateProxyLayersWeightmapUsage();
		return true;
	});
}

void ALandscapeProxy::UpdateProxyLayersWeightmapUsage()
{
	if (bNeedsWeightmapUsagesUpdate)
	{
		InitializeProxyLayersWeightmapUsage();
	}
	check(!bNeedsWeightmapUsagesUpdate);
}

void ALandscapeProxy::PostEditUndo()
{
	check(ULandscapeComponent::UndoRedoModifiedComponentCount == 0);

	Super::PostEditUndo();
}

void ALandscape::InitializeLandscapeLayersWeightmapUsage()
{
	ULandscapeInfo* Info = GetLandscapeInfo();

	if (Info == nullptr)
	{
		return;
	}

	Info->ForEachLandscapeProxy([&](ALandscapeProxy* Proxy)
	{
		Proxy->InitializeProxyLayersWeightmapUsage();
		return true;
	});
}

void ALandscapeProxy::InitializeProxyLayersWeightmapUsage()
{
	if (ALandscape* Landscape = GetLandscapeActor())
	{
		// Reset the entire proxy's usage map and then request all components to repopulate it :
		WeightmapUsageMap.Reset();
		for (ULandscapeComponent* Component : LandscapeComponents)
		{
			// Reinitialize the weightmap usages for the base (final) paint layers allocations :
			Component->InitializeLayersWeightmapUsage(FGuid());

			for (const FLandscapeLayer& Layer : Landscape->GetLayers())
			{
				// Reinitialize each edit layer's weightmap usages list :
				Component->InitializeLayersWeightmapUsage(Layer.Guid);
			}
		}
	}

	bNeedsWeightmapUsagesUpdate = false;
	ValidateProxyLayersWeightmapUsage();
}
                                         
void ULandscapeComponent::InitializeLayersWeightmapUsage(const FGuid& InLayerGuid)
{
	ALandscapeProxy* Proxy = GetLandscapeProxy();
	check(Proxy);
	ALandscape* Landscape = GetLandscapeActor();
	check(Landscape);
	const FLandscapeLayer* SplinesLayer = Landscape->FindLayerOfType(ULandscapeEditLayerSplines::StaticClass());
	FGuid SplinesEditLayerGuid = SplinesLayer ? SplinesLayer->Guid : FGuid();

	// Don't consider invalid edit layers : 
	if (InLayerGuid.IsValid())
	{
		FLandscapeLayerComponentData* LayerData = GetLayerData(InLayerGuid);
		if (LayerData == nullptr || !LayerData->IsInitialized())
		{
			return;
		}
	}

	const TArray<FWeightmapLayerAllocationInfo>& ComponentWeightmapLayerAllocations = GetWeightmapLayerAllocations(InLayerGuid);
	const TArray<UTexture2D*>& ComponentWeightmapTextures = GetWeightmapTextures(InLayerGuid);
	TArray<TObjectPtr<ULandscapeWeightmapUsage>>& ComponentWeightmapTexturesUsage = GetWeightmapTexturesUsage(InLayerGuid);

	ComponentWeightmapTexturesUsage.Reset();
	ComponentWeightmapTexturesUsage.AddDefaulted(ComponentWeightmapTextures.Num());

	for (int32 LayerIdx = 0; LayerIdx < ComponentWeightmapLayerAllocations.Num(); LayerIdx++)
	{
		const FWeightmapLayerAllocationInfo& Allocation = ComponentWeightmapLayerAllocations[LayerIdx];
		if (Allocation.IsAllocated())
		{
			check(ComponentWeightmapTextures.IsValidIndex(Allocation.WeightmapTextureIndex));
			UTexture2D* WeightmapTexture = ComponentWeightmapTextures[Allocation.WeightmapTextureIndex];
			TObjectPtr<ULandscapeWeightmapUsage>* TempUsage = Proxy->WeightmapUsageMap.Find(WeightmapTexture);
			if (TempUsage == nullptr)
			{
				TempUsage = &Proxy->WeightmapUsageMap.Add(WeightmapTexture, Proxy->CreateWeightmapUsage());
				(*TempUsage)->LayerGuid = InLayerGuid;
			}

			ULandscapeWeightmapUsage* Usage = *TempUsage;
			ComponentWeightmapTexturesUsage[Allocation.WeightmapTextureIndex] = Usage; // Keep a ref to it for faster access

			// Validate that there are no conflicting allocations (two allocations claiming the same texture channel)
			check(Usage->ChannelUsage[Allocation.WeightmapTextureChannel] == nullptr || Usage->ChannelUsage[Allocation.WeightmapTextureChannel] == this);

			// Validate that there are no duplicated allocation (except on the splines layer, since it's updated outside of a transaction and the transactor can later restore a duplicated 
			//  allocation in 2 different components, which will assert here but will be corrected in the next UpdateLandscapeSplines, which is called right after)
			check((SplinesEditLayerGuid.IsValid() && (InLayerGuid == SplinesEditLayerGuid))
				|| (Usage->ChannelUsage[Allocation.WeightmapTextureChannel] == nullptr)
				|| (Usage->ChannelUsage[Allocation.WeightmapTextureChannel] == this));

			Usage->ChannelUsage[Allocation.WeightmapTextureChannel] = this;
		}
	}

	// If there were some invalid allocations there, we will end up with null entries in ComponentWeightmapTexturesUsage, which is not desirable since we want 
	//  ComponentWeightmapTexturesUsage and ComponentWeightmapTextures to be in sync. Fix the situation by creating the missing usages here : 
	for (int32 Index = 0; Index < ComponentWeightmapTexturesUsage.Num(); ++Index)
	{
		if (UTexture2D* WeightmapTexture = ComponentWeightmapTextures[Index])
		{
			if (ComponentWeightmapTexturesUsage[Index] == nullptr)
			{
				TObjectPtr<ULandscapeWeightmapUsage>* TempUsage = Proxy->WeightmapUsageMap.Find(WeightmapTexture);
				if (TempUsage == nullptr)
				{
					TempUsage = &Proxy->WeightmapUsageMap.Add(WeightmapTexture, Proxy->CreateWeightmapUsage());
					(*TempUsage)->LayerGuid = InLayerGuid;
				}

				ULandscapeWeightmapUsage* Usage = *TempUsage;
				ComponentWeightmapTexturesUsage[Index] = Usage; // Keep a ref to it for faster access
			}
		}
	}
}

void ALandscape::ValidateProxyLayersWeightmapUsage() const
{
	ULandscapeInfo* Info = GetLandscapeInfo();
	if (Info == nullptr)
	{
		return;
	}

	Info->ForEachLandscapeProxy([=](const ALandscapeProxy* Proxy)
	{
		Proxy->ValidateProxyLayersWeightmapUsage();
		return true;
	});
}

void ALandscapeProxy::ValidateProxyLayersWeightmapUsage() const
{
	if ((CVarLandscapeValidateProxyWeightmapUsages.GetValueOnGameThread() == 0) || bTemporarilyDisableWeightmapUsagesValidation)
	{
		return;
	}

	// Fixup and usages should have been updated any time we run validation
	check(WeightmapFixupVersion == CurrentVersion);
	check(!bNeedsWeightmapUsagesUpdate);

	TRACE_CPUPROFILER_EVENT_SCOPE(Landscape_ValidateProxyLayersWeightmapUsage);
	TMap<UTexture2D*, TArray<FWeightmapLayerAllocationInfo>> PerTextureAllocations;
	if (const ALandscape* Landscape = GetLandscapeActor())
	{
		for (ULandscapeComponent* Component : LandscapeComponents)
		{
			auto ValidateWeightmapAllocationAndUsage = [&](UTexture2D* InWeightmapTexture, const FWeightmapLayerAllocationInfo& InAllocation, ULandscapeWeightmapUsage* InUsage, const FGuid &InLayerGuid)
			{
				if (InUsage)
				{
					// Each usage should also be stored in the proxy's map
					const TObjectPtr<ULandscapeWeightmapUsage>* ProxyMapUsage = WeightmapUsageMap.Find(InWeightmapTexture);
					check(ProxyMapUsage);
					check(InUsage == *ProxyMapUsage);

					// Our component should own the channel, and the LayerGuid should match
					check(InUsage->ChannelUsage[InAllocation.WeightmapTextureChannel] == Component);
					check(InUsage->LayerGuid == InLayerGuid);
				}

				// There should not be any other allocations pointing to this channel on this texture
				TArray<FWeightmapLayerAllocationInfo>& AllAllocationsForThisTexture = PerTextureAllocations.FindOrAdd(InWeightmapTexture);
				for (FWeightmapLayerAllocationInfo &Alloc : AllAllocationsForThisTexture)
				{
					check(Alloc.WeightmapTextureChannel != InAllocation.WeightmapTextureChannel);
				}
			};

			const TArray<UTexture2D*>& WeightmapTextures = Component->GetWeightmapTextures(/*InReturnEditingWeightmap = */false);
			const TArray<ULandscapeWeightmapUsage*>& WeightmapTextureUsages = Component->GetWeightmapTexturesUsage(/*InReturnEditingWeightmap = */false);

			// Validate weightmap allocations
			FGuid BaseGuid;
			for (const FWeightmapLayerAllocationInfo& Allocation : Component->GetWeightmapLayerAllocations(/*InReturnEditingWeightmap = */false))
			{
				if (Allocation.IsAllocated())
				{
					// The allocation texture index should point to a valid texture
					UTexture2D* WeightmapTexture = WeightmapTextures[Allocation.WeightmapTextureIndex];
					check(WeightmapTexture != nullptr);

					// Either it's out of bounds  i.e. not initialized yet,  or it is initialized and we validate that it is correct...
					ULandscapeWeightmapUsage* Usage = WeightmapTextureUsages.IsValidIndex(Allocation.WeightmapTextureIndex) ? WeightmapTextureUsages[Allocation.WeightmapTextureIndex] : nullptr;
					ValidateWeightmapAllocationAndUsage(WeightmapTexture, Allocation, Usage, BaseGuid);
				}
			}

			// Validate edit layers weightmap allocations : 
			{
				const FLandscapeLayer* SplinesLayer = Landscape->FindLayerOfType(ULandscapeEditLayerSplines::StaticClass());
				for (const FLandscapeLayer& Layer : Landscape->GetLayers())
				{
					FLandscapeLayerComponentData* LayerData = Component->GetLayerData(Layer.Guid);

					// Skip validation on SplinesLayer since it can momentarily contain duplicated layer allocations after undo (since it's updated outside of a transaction) :
					if (LayerData != nullptr && LayerData->IsInitialized() && (&Layer != SplinesLayer))
					{
						for (int32 LayerIdx = 0; LayerIdx < LayerData->WeightmapData.LayerAllocations.Num(); LayerIdx++)
						{
							const FWeightmapLayerAllocationInfo& Allocation = LayerData->WeightmapData.LayerAllocations[LayerIdx];
							if (Allocation.IsAllocated())
							{
								UTexture2D* WeightmapTexture = LayerData->WeightmapData.Textures[Allocation.WeightmapTextureIndex];
								if (ULandscapeWeightmapUsage* Usage = LayerData->WeightmapData.TextureUsages.IsValidIndex(Allocation.WeightmapTextureIndex) ? LayerData->WeightmapData.TextureUsages[Allocation.WeightmapTextureIndex].Get() : nullptr)
								{
									ValidateWeightmapAllocationAndUsage(WeightmapTexture, Allocation, Usage, Layer.Guid);
								}
							}
						}
					}
				}
			}
		}
	}
}

void ALandscapeProxy::RequestProxyLayersWeightmapUsageUpdate()
{
	bNeedsWeightmapUsagesUpdate = true;
}

void ExecuteCopyLayersTexture(TArray<FLandscapeLayersCopyTextureParams>&& InCopyTextureParams)
{
	ENQUEUE_RENDER_COMMAND(LandscapeLayers_Cmd_CopyTexture)(
		[CopyTextureParams = MoveTemp(InCopyTextureParams)](FRHICommandListImmediate& RHICmdList) mutable
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(LandscapeLayers_RT_CopyTextures);
		SCOPED_DRAW_EVENTF(RHICmdList, LandscapeLayers, TEXT("LandscapeLayers : Copy %d texture regions"), CopyTextureParams.Num());

		for (const FLandscapeLayersCopyTextureParams& Params : CopyTextureParams)
		{
			if ((Params.SourceResource != nullptr) && (Params.DestResource != nullptr))
			{
				FLandscapeLayersCopyTexture_RenderThread CopyTexture(Params);
				CopyTexture.Copy(RHICmdList);
			}
		}
	});
}

/** Per component information from read back results. */
struct FLandscapeEditLayerComponentReadbackResult
{
	ULandscapeComponent* LandscapeComponent = nullptr;
	/** ELandscapeLayerUpdateMode flags set on ULandscapeComponent at time when read back task was submitted. */
	uint32 UpdateModes = 0;
	/** Were the associated heightmap/weightmaps modified. */
	bool bModified = false;
	/** Indicates which of the component's weightmaps is not needed anymore. */
	TArray<ULandscapeLayerInfoObject*> AllZeroLayers;

	FLandscapeEditLayerComponentReadbackResult(ULandscapeComponent* InLandscapeComponent, uint32 InUpdateModes)
		: LandscapeComponent(InLandscapeComponent)
		, UpdateModes(InUpdateModes)
	{}
};

/** Description for a single read back operation. */
struct FLandscapeLayersCopyReadbackTextureParams
{
	FLandscapeLayersCopyReadbackTextureParams(UTexture2D* InSource, FLandscapeEditLayerReadback* InDest)
		: Source(InSource)
		, Dest(InDest)
	{}

	UTexture2D* Source;
	FLandscapeEditLayerReadback* Dest;
	FLandscapeEditLayerReadback::FReadbackContext Context;
};

void ExecuteCopyToReadbackTexture(TArray<FLandscapeLayersCopyReadbackTextureParams>& InParams)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(ExecuteCopyToReadbackTexture);
	RHI_BREADCRUMB_EVENT_GAMETHREAD("Copy to readback textures (%d copies)", InParams.Num());
	if (!FApp::CanEverRender())
	{
		return;
	}
	for (FLandscapeLayersCopyReadbackTextureParams& Params : InParams)
	{
		Params.Dest->Enqueue(Params.Source, MoveTemp(Params.Context));
	}
}

TArray<FLandscapeLayersCopyReadbackTextureParams> PrepareLandscapeLayersCopyReadbackTextureParams(const FTextureToComponentHelper& InMapHelper, TArray<UTexture2D*> InTextures, bool bWeightmaps)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(LandscapeLayers_PrepareLandscapeLayersCopyReadbackTextureParams);
	TArray<FLandscapeLayersCopyReadbackTextureParams> Result;
	Result.Reserve(InTextures.Num());

	for (UTexture2D* Texture : InTextures)
	{
		const TMap<UTexture2D*, TArray<ULandscapeComponent*>>& TexturesToComponents = bWeightmaps ? InMapHelper.WeightmapToComponents : InMapHelper.HeightmapToComponents;
		const TArray<ULandscapeComponent*>* Components = TexturesToComponents.Find(Texture);
		check(Components && !Components->IsEmpty());
		ALandscapeProxy* Proxy = (*Components)[0]->GetLandscapeProxy();
		FLandscapeEditLayerReadback** CPUReadback = bWeightmaps ? Proxy->WeightmapsCPUReadback.Find(Texture) : Proxy->HeightmapsCPUReadback.Find(Texture);
		check(CPUReadback && *CPUReadback);

		FLandscapeLayersCopyReadbackTextureParams& CopyReadbackTextureParams = Result.Add_GetRef(FLandscapeLayersCopyReadbackTextureParams(Texture, *CPUReadback));
		// Init the CPU read back contexts for all components dependent on this texture. This includes a context containing the current component states : 
		for (ULandscapeComponent* ComponentToResolve : *Components)
		{
			const FIntPoint ComponentToResolveKey = ComponentToResolve->GetComponentKey();
			const int32 ComponentToResolveFlags = ComponentToResolve->GetLayerUpdateFlagPerMode();
			FLandscapeEditLayerReadback::FPerChannelLayerNames PerChannelLayerNames;

			// Weightmaps could be reallocated randomly before we actually perform the readback, so we need to keep a picture of which channel was affected to which paint layer at readback time:
			if (bWeightmaps)
			{
				const TArray<UTexture2D*>& WeightmapTextures = ComponentToResolve->GetWeightmapTextures(/*InReturnEditingWeightmap = */false);
				for (FWeightmapLayerAllocationInfo const& AllocInfo : ComponentToResolve->GetWeightmapLayerAllocations(/*InReturnEditingWeightmap = */false))
				{
					if (AllocInfo.IsAllocated())
					{
						UTexture2D* PaintLayerTexture = WeightmapTextures[AllocInfo.WeightmapTextureIndex];
						if (PaintLayerTexture == Texture)
						{
							PerChannelLayerNames[AllocInfo.WeightmapTextureChannel] = AllocInfo.LayerInfo->LayerName;
						}
					}
				}
			}
			CopyReadbackTextureParams.Context.Add(FLandscapeEditLayerReadback::FComponentReadbackContext { ComponentToResolveKey, ComponentToResolveFlags, PerChannelLayerNames });
		}
	}

	return Result;
}

void ALandscape::CopyTexturePS(const FString& InSourceDebugName, FTextureResource* InSourceResource, const FString& InDestDebugName, FTextureResource* InDestResource) const
{
	check(InSourceResource != nullptr);
	check(InDestResource != nullptr);

	ENQUEUE_RENDER_COMMAND(LandscapeLayers_Cmd_CopyTexturePS)(
		[InSourceDebugName, InSourceResource, InDestDebugName, InDestResource](FRHICommandListImmediate& RHICmdList)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(LandscapeLayers_RT_CopyTexturePS);
		RHI_BREADCRUMB_EVENT_STAT(RHICmdList, LandscapeLayers_CopyTexturePS, "LandscapeLayers_CopyTexturePS %s -> %s", InSourceDebugName, InDestDebugName);
		SCOPED_GPU_STAT(RHICmdList, LandscapeLayers_CopyTexturePS);

		check(InSourceResource->GetSizeX() == InDestResource->GetSizeX());
		check(InSourceResource->GetSizeY() == InDestResource->GetSizeY());
		FRHIRenderPassInfo RPInfo(InDestResource->TextureRHI, ERenderTargetActions::DontLoad_Store);
		RHICmdList.BeginRenderPass(RPInfo, TEXT("CopyTexture"));

		FGlobalShaderMap* GlobalShaderMap = GetGlobalShaderMap(GMaxRHIFeatureLevel);
		TShaderMapRef< FLandscapeCopyTextureVS > VertexShader(GlobalShaderMap);
		TShaderMapRef< FLandscapeCopyTexturePS > PixelShader(GlobalShaderMap);

		FGraphicsPipelineStateInitializer GraphicsPSOInit;
		RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);
		GraphicsPSOInit.BlendState = TStaticBlendState<>::GetRHI();
		GraphicsPSOInit.RasterizerState = TStaticRasterizerState<>::GetRHI();
		GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();
		GraphicsPSOInit.PrimitiveType = PT_TriangleList;
		GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GetVertexDeclarationFVector4();
		GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
		GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();
		SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit, 0);

		SetShaderParametersLegacyPS(RHICmdList, PixelShader, InSourceResource->TextureRHI);

		RHICmdList.SetViewport(0.0f, 0.0f, 0.0f, (float)InDestResource->GetSizeX(), (float)InDestResource->GetSizeY(), 1.0f);
		RHICmdList.DrawIndexedPrimitive(GTwoTrianglesIndexBuffer.IndexBufferRHI, 0, 0, 4, 0, 2, 1);

		RHICmdList.EndRenderPass();
	});
}

void ALandscape::DrawWeightmapComponentsToRenderTarget(const FString& InDebugName, const TArray<FIntPoint>& InSectionBaseList, const FVector2f& InScaleBias, TArray<FVector2f>* InScaleBiasPerSection, UTexture* InWeightmapRTRead, UTextureRenderTarget2D* InOptionalWeightmapRTRead2, UTextureRenderTarget2D* InWeightmapRTWrite,
	ERTDrawingType InDrawType, bool InClearRTWrite, FLandscapeLayersWeightmapShaderParameters& InShaderParams, uint8 InMipRender) const
{
	check(InWeightmapRTRead != nullptr);
	check(InWeightmapRTWrite != nullptr);
	check(InScaleBiasPerSection == nullptr || InScaleBiasPerSection->Num() == InSectionBaseList.Num());

	FIntPoint WeightmapWriteTextureSize(InWeightmapRTWrite->SizeX, InWeightmapRTWrite->SizeY);
	FIntPoint WeightmapReadTextureSize(InWeightmapRTRead->Source.GetSizeX(), InWeightmapRTRead->Source.GetSizeY());
	UTextureRenderTarget2D* WeightmapRTRead = Cast<UTextureRenderTarget2D>(InWeightmapRTRead);

	if (WeightmapRTRead != nullptr)
	{
		WeightmapReadTextureSize.X = WeightmapRTRead->SizeX;
		WeightmapReadTextureSize.Y = WeightmapRTRead->SizeY;
	}

	// Quad Setup
	TArray<FLandscapeLayersTriangle> TriangleList;
	TriangleList.Reserve(InSectionBaseList.Num() * 2 * NumSubsections);

	for (int i = 0; i < InSectionBaseList.Num(); ++i)
	{
		const FVector2f& WeightmapScaleBias = InScaleBiasPerSection != nullptr ? (*InScaleBiasPerSection)[i] : InScaleBias;
		switch (InDrawType)
		{
		case ERTDrawingType::RTAtlas:
		{
			GenerateLayersRenderQuadsAtlas(InSectionBaseList[i], FVector2D(WeightmapScaleBias), static_cast<float>(SubsectionSizeQuads), WeightmapReadTextureSize, WeightmapWriteTextureSize, TriangleList);
		} break;

		case ERTDrawingType::RTAtlasToNonAtlas:
		{
			GenerateLayersRenderQuadsAtlasToNonAtlas(InSectionBaseList[i], FVector2D(WeightmapScaleBias), static_cast<float>(SubsectionSizeQuads), WeightmapReadTextureSize, WeightmapWriteTextureSize, TriangleList);
		} break;

		case ERTDrawingType::RTNonAtlas:
		{
			GenerateLayersRenderQuadsNonAtlas(InSectionBaseList[i], FVector2D(WeightmapScaleBias), static_cast<float>(SubsectionSizeQuads), WeightmapReadTextureSize, WeightmapWriteTextureSize, TriangleList);
		} break;

		case ERTDrawingType::RTNonAtlasToAtlas:
		{
			GenerateLayersRenderQuadsNonAtlasToAtlas(InSectionBaseList[i], FVector2D(WeightmapScaleBias), static_cast<float>(SubsectionSizeQuads), WeightmapReadTextureSize, WeightmapWriteTextureSize, TriangleList);
		} break;

		case ERTDrawingType::RTMips:
		{
			GenerateLayersRenderQuadsMip(InSectionBaseList[i], FVector2D(WeightmapScaleBias), static_cast<float>(SubsectionSizeQuads), WeightmapReadTextureSize, WeightmapWriteTextureSize, InMipRender, TriangleList);
		} break;

		default:
		{
			check(false);
			return;
		}
		}
	}

	InShaderParams.ReadWeightmap1 = InWeightmapRTRead;
	InShaderParams.ReadWeightmap2 = InOptionalWeightmapRTRead2;
	InShaderParams.CurrentMipComponentVertexCount = ((SubsectionSizeQuads + 1) >> InMipRender);

	if (InMipRender > 0)
	{
		InShaderParams.CurrentMipSize = WeightmapWriteTextureSize;
		InShaderParams.ParentMipSize = WeightmapReadTextureSize;
	}

	FMatrix ProjectionMatrix = AdjustProjectionMatrixForRHI(FTranslationMatrix(FVector(0, 0, 0)) *
		FMatrix(FPlane(1.0f / (FMath::Max<uint32>(WeightmapWriteTextureSize.X, 1.f) / 2.0f), 0.0, 0.0f, 0.0f), FPlane(0.0f, -1.0f / (FMath::Max<uint32>(WeightmapWriteTextureSize.Y, 1.f) / 2.0f), 0.0f, 0.0f), FPlane(0.0f, 0.0f, 1.0f, 0.0f), FPlane(-1.0f, 1.0f, 0.0f, 1.0f)));

	FLandscapeLayersWeightmapRender_RenderThread LayersRender(InDebugName, InWeightmapRTWrite, WeightmapWriteTextureSize, WeightmapReadTextureSize, ProjectionMatrix, InShaderParams, InMipRender, TriangleList);

	ENQUEUE_RENDER_COMMAND(LandscapeLayers_Cmd_RenderWeightmap)(
		[LayersRender, InDebugName, InDrawType, InClearRTWrite](FRHICommandListImmediate& RHICmdList) mutable
	{
		SCOPED_DRAW_EVENTF(RHICmdList, LandscapeLayers, TEXT("DrawWeightmapComponentsToRenderTarget %s (%s)"), InDebugName, StaticEnum<EWeightmapRTType>()->GetDisplayValueAsText(InDrawType).ToString());
		TRACE_CPUPROFILER_EVENT_SCOPE(LandscapeLayers_RT_RenderWeightmap);
		LayersRender.Render(RHICmdList, InClearRTWrite);
	});

	PrintLayersDebugRT(InDebugName, InWeightmapRTWrite, InMipRender, false);
}

void ALandscape::DrawWeightmapComponentsToRenderTarget(const FString& InDebugName, const TArray<ULandscapeComponent*>& InComponentsToDraw, const FIntPoint& InLandscapeBase, UTexture* InWeightmapRTRead, UTextureRenderTarget2D* InOptionalWeightmapRTRead2, UTextureRenderTarget2D* InWeightmapRTWrite,
	ERTDrawingType InDrawType, bool InClearRTWrite, FLandscapeLayersWeightmapShaderParameters& InShaderParams, uint8 InMipRender) const
{

	TArray<FIntPoint> SectionBaseList;
	SectionBaseList.Reserve(InComponentsToDraw.Num());
	TArray<FVector2f> WeightmapScaleBiasList;
	WeightmapScaleBiasList.Reserve(InComponentsToDraw.Num());

	for (ULandscapeComponent* Component : InComponentsToDraw)
	{
		FVector2f WeightmapScaleBias(static_cast<float>(Component->WeightmapScaleBias.Z), static_cast<float>(Component->WeightmapScaleBias.W));
		WeightmapScaleBiasList.Add(WeightmapScaleBias);

		FIntPoint ComponentSectionBase = Component->GetSectionBase() - InLandscapeBase;
		SectionBaseList.Add(ComponentSectionBase);
	}

	DrawWeightmapComponentsToRenderTarget(InDebugName, SectionBaseList, FVector2f::ZeroVector, &WeightmapScaleBiasList, InWeightmapRTRead, InOptionalWeightmapRTRead2, InWeightmapRTWrite, InDrawType, InClearRTWrite, InShaderParams, InMipRender);

	PrintLayersDebugRT(InDebugName, InWeightmapRTWrite, InMipRender, false);
}

void ALandscape::DrawWeightmapComponentToRenderTargetMips(const TArray<FVector2f>& InTexturePositionsToDraw, UTexture* InReadWeightmap, bool InClearRTWrite, struct FLandscapeLayersWeightmapShaderParameters& InShaderParams) const
{
	int32 CurrentMip = 1;
	UTexture* ReadMipRT = InReadWeightmap;

	// Convert from Texture position to SectionBase
	int32 LocalComponentSizeQuad = SubsectionSizeQuads * NumSubsections;
	int32 LocalComponentSizeVerts = (SubsectionSizeQuads + 1) * NumSubsections;

	TArray<FIntPoint> SectionBaseToDraw;
	SectionBaseToDraw.Reserve(InTexturePositionsToDraw.Num());

	for (const FVector2f& TexturePosition : InTexturePositionsToDraw)
	{
		FVector2f PositionOffset(static_cast<float>(FMath::RoundToInt(TexturePosition.X / LocalComponentSizeVerts)), static_cast<float>(FMath::RoundToInt(TexturePosition.Y / LocalComponentSizeVerts)));
		SectionBaseToDraw.Add(FIntPoint(static_cast<int32>(PositionOffset.X * LocalComponentSizeQuad), static_cast<int32>(PositionOffset.Y * LocalComponentSizeQuad)));
	}

	FVector2f WeightmapScaleBias(0.0f, 0.0f); // we dont need a scale bias for mip drawing

	for (int32 MipRTIndex = (int32)EWeightmapRTType::WeightmapRT_Mip1; MipRTIndex < (int32)EWeightmapRTType::WeightmapRT_Count; ++MipRTIndex)
	{
		UTextureRenderTarget2D* WriteMipRT = WeightmapRTList[MipRTIndex];

		if (WriteMipRT != nullptr)
		{
			DrawWeightmapComponentsToRenderTarget(FString::Printf(TEXT("LS Weight: %s = -> %s Mips %d"), *ReadMipRT->GetName(), *WriteMipRT->GetName(), CurrentMip),
				SectionBaseToDraw, WeightmapScaleBias, nullptr, ReadMipRT, nullptr, WriteMipRT, ERTDrawingType::RTMips, InClearRTWrite, InShaderParams, static_cast<uint8>(CurrentMip));
			++CurrentMip;
		}

		ReadMipRT = WeightmapRTList[MipRTIndex];
	}
}

void ALandscape::ClearLayersWeightmapTextureResource(const FString& InDebugName, FTextureRenderTargetResource* InTextureResourceToClear) const
{
	LandscapeLayersWeightmapClear_RenderThread LayersClear(InDebugName, InTextureResourceToClear);

	ENQUEUE_RENDER_COMMAND(LandscapeLayers_Cmd_Clear)(
		[LayersClear](FRHICommandListImmediate& RHICmdList) mutable
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(LandscapeLayers_RT_Clear);
		LayersClear.Clear(RHICmdList);
	});
}

void ALandscape::DrawHeightmapComponentsToRenderTargetMips(const TArray<ULandscapeComponent*>& InComponentsToDraw, const FIntPoint& InLandscapeBase, UTexture* InReadHeightmap, bool InClearRTWrite, struct FLandscapeLayersHeightmapShaderParameters& InShaderParams) const
{
	int32 CurrentMip = 1;
	UTexture* ReadMipRT = InReadHeightmap;

	for (int32 MipRTIndex = (int32)EHeightmapRTType::HeightmapRT_Mip1; MipRTIndex < (int32)EHeightmapRTType::HeightmapRT_Count; ++MipRTIndex)
	{
		UTextureRenderTarget2D* WriteMipRT = HeightmapRTList[MipRTIndex];

		if (WriteMipRT != nullptr)
		{
			DrawHeightmapComponentsToRenderTarget(FString::Printf(TEXT("LS Height: %s = -> %s CombinedAtlasWithMips %d"), *ReadMipRT->GetName(), *WriteMipRT->GetName(), CurrentMip),
				InComponentsToDraw, InLandscapeBase, ReadMipRT, nullptr, WriteMipRT, ERTDrawingType::RTMips, InClearRTWrite, InShaderParams, static_cast<uint8>(CurrentMip));
			++CurrentMip;
		}

		ReadMipRT = HeightmapRTList[MipRTIndex];
	}
}

void ALandscape::DrawHeightmapComponentsToRenderTarget(const FString& InDebugName, const TArray<ULandscapeComponent*>& InComponentsToDraw, const FIntPoint& InLandscapeBase, UTexture* InHeightmapRTRead, UTextureRenderTarget2D* InOptionalHeightmapRTRead2, UTextureRenderTarget2D* InHeightmapRTWrite,
	ERTDrawingType InDrawType, bool InClearRTWrite, FLandscapeLayersHeightmapShaderParameters& InShaderParams, uint8 InMipRender) const
{
	check(InHeightmapRTRead != nullptr);
	check(InHeightmapRTWrite != nullptr);
	if (!FApp::CanEverRender())
	{
		return;
	}

	FIntPoint HeightmapWriteTextureSize(InHeightmapRTWrite->SizeX, InHeightmapRTWrite->SizeY);
	FIntPoint HeightmapReadTextureSize(InHeightmapRTRead->Source.GetSizeX(), InHeightmapRTRead->Source.GetSizeY());
	UTextureRenderTarget2D* HeightmapRTRead = Cast<UTextureRenderTarget2D>(InHeightmapRTRead);

	if (HeightmapRTRead != nullptr)
	{
		HeightmapReadTextureSize.X = HeightmapRTRead->SizeX;
		HeightmapReadTextureSize.Y = HeightmapRTRead->SizeY;
	}

	// Quad Setup
	TArray<FLandscapeLayersTriangle> TriangleList;
	TriangleList.Reserve(InComponentsToDraw.Num() * 2 * NumSubsections);

	for (ULandscapeComponent* Component : InComponentsToDraw)
	{
		FVector2f HeightmapScaleBias(static_cast<float>(Component->HeightmapScaleBias.Z), static_cast<float>(Component->HeightmapScaleBias.W));
		FIntPoint ComponentSectionBase = Component->GetSectionBase() - InLandscapeBase;

		switch (InDrawType)
		{
			case ERTDrawingType::RTAtlas:
			{
				GenerateLayersRenderQuadsAtlas(ComponentSectionBase, FVector2D(HeightmapScaleBias), static_cast<float>(SubsectionSizeQuads), HeightmapReadTextureSize, HeightmapWriteTextureSize, TriangleList);
			} break;

			case ERTDrawingType::RTAtlasToNonAtlas:
			{
				GenerateLayersRenderQuadsAtlasToNonAtlas(ComponentSectionBase, FVector2D(HeightmapScaleBias), static_cast<float>(SubsectionSizeQuads), HeightmapReadTextureSize, HeightmapWriteTextureSize, TriangleList);
			} break;

			case ERTDrawingType::RTNonAtlas:
			{
				GenerateLayersRenderQuadsNonAtlas(ComponentSectionBase, FVector2D(HeightmapScaleBias), static_cast<float>(SubsectionSizeQuads), HeightmapReadTextureSize, HeightmapWriteTextureSize, TriangleList);
			} break;

			case ERTDrawingType::RTNonAtlasToAtlas:
			{
				GenerateLayersRenderQuadsNonAtlasToAtlas(ComponentSectionBase, FVector2D(HeightmapScaleBias), static_cast<float>(SubsectionSizeQuads), HeightmapReadTextureSize, HeightmapWriteTextureSize, TriangleList);
			} break;

			case ERTDrawingType::RTMips:
			{
				GenerateLayersRenderQuadsMip(ComponentSectionBase, FVector2D(HeightmapScaleBias), static_cast<float>(SubsectionSizeQuads), HeightmapReadTextureSize, HeightmapWriteTextureSize, InMipRender, TriangleList);
			} break;

		default:
		{
			check(false);
			return;
		}
		}
	}

	InShaderParams.ReadHeightmap1 = InHeightmapRTRead;
	InShaderParams.ReadHeightmap2 = InOptionalHeightmapRTRead2;
	InShaderParams.HeightmapSize = HeightmapReadTextureSize;
	InShaderParams.CurrentMipComponentVertexCount = ((SubsectionSizeQuads + 1) >> InMipRender);

	if (InMipRender > 0)
	{
		InShaderParams.CurrentMipSize = HeightmapWriteTextureSize;
		InShaderParams.ParentMipSize = HeightmapReadTextureSize;
	}

	FMatrix ProjectionMatrix = AdjustProjectionMatrixForRHI(FTranslationMatrix(FVector(0, 0, 0)) *
		FMatrix(FPlane(1.0f / (FMath::Max<uint32>(HeightmapWriteTextureSize.X, 1.f) / 2.0f), 0.0, 0.0f, 0.0f), FPlane(0.0f, -1.0f / (FMath::Max<uint32>(HeightmapWriteTextureSize.Y, 1.f) / 2.0f), 0.0f, 0.0f), FPlane(0.0f, 0.0f, 1.0f, 0.0f), FPlane(-1.0f, 1.0f, 0.0f, 1.0f)));

	FLandscapeLayersHeightmapRender_RenderThread LayersRender(InDebugName, InHeightmapRTWrite, HeightmapWriteTextureSize, HeightmapReadTextureSize, ProjectionMatrix, InShaderParams, InMipRender, TriangleList);

	ENQUEUE_RENDER_COMMAND(LandscapeLayers_Cmd_RenderHeightmap)(
		[LayersRender, InDebugName, InDrawType, InClearRTWrite](FRHICommandListImmediate& RHICmdList) mutable
	{
		SCOPED_DRAW_EVENTF(RHICmdList, LandscapeLayers, TEXT("DrawHeightmapComponentsToRenderTarget %s (%s)"), InDebugName, StaticEnum<EHeightmapRTType>()->GetDisplayValueAsText(InDrawType).ToString());
		TRACE_CPUPROFILER_EVENT_SCOPE(LandscapeLayers_RT_RenderHeightmap);
		LayersRender.Render(RHICmdList, InClearRTWrite);
	});

	PrintLayersDebugRT(InDebugName, InHeightmapRTWrite, InMipRender, true, InShaderParams.GenerateNormals);
}

void ALandscape::GenerateLayersRenderQuad(const FIntPoint& InVertexPosition, float InVertexSize, const FVector2D& InUVStart, const FVector2D& InUVSize, TArray<FLandscapeLayersTriangle>& OutTriangles) const
{
	// Set min/max values for rectangle in XY and UV.
	const float X[] = { static_cast<float>(InVertexPosition.X), static_cast<float>(InVertexPosition.X) + InVertexSize };
	const float Y[] = { static_cast<float>(InVertexPosition.Y), static_cast<float>(InVertexPosition.Y) + InVertexSize };
	const float U[] = { static_cast<float>(InUVStart.X), static_cast<float>(InUVStart.X + InUVSize.X) };
	const float V[] = { static_cast<float>(InUVStart.Y), static_cast<float>(InUVStart.Y + InUVSize.Y) };

	// Helper function for creating a vertex from given min/max indices.
	auto SetVertex = [&X, &Y, &U, &V](FLandscapeLayersVertex& Vertex, const int32 Index1, const int32 Index2)
	{
		Vertex.Position.X = X[Index1];
		Vertex.Position.Y = Y[Index2];
		Vertex.UV.X = U[Index1];
		Vertex.UV.Y = V[Index2];
	};

	FLandscapeLayersTriangle Tri;

	// Create first triangle.
	SetVertex(Tri.V0, 0, 0);
	SetVertex(Tri.V1, 1, 0);
	SetVertex(Tri.V2, 1, 1);
	OutTriangles.Add(Tri);

	// Create second triangle; V0 is identical to the previous triangle.
	SetVertex(Tri.V1, 1, 1);
	SetVertex(Tri.V2, 0, 1);
	OutTriangles.Add(Tri);
}

void ALandscape::GenerateLayersRenderQuadsAtlas(const FIntPoint& InSectionBase, const FVector2D& InScaleBias, float InSubSectionSizeQuad, const FIntPoint& InReadSize, const FIntPoint& InWriteSize, TArray<FLandscapeLayersTriangle>& OutTriangles) const
{
	FIntPoint ComponentSectionBase = InSectionBase;
	FIntPoint UVComponentSectionBase = InSectionBase;

	const int32 SubsectionSizeVerts = static_cast<int32>(InSubSectionSizeQuad) + 1;
	const int32 LocalComponentSizeQuad = static_cast<int32>(InSubSectionSizeQuad * NumSubsections);
	const int32 LocalComponentSizeVerts = SubsectionSizeVerts * NumSubsections;

	FVector2D PositionOffset(FMath::RoundToInt((float)ComponentSectionBase.X / (float)LocalComponentSizeQuad), FMath::RoundToInt((float)ComponentSectionBase.Y / (float)LocalComponentSizeQuad));
	FVector2D ComponentsPerTexture(FMath::RoundToInt((float)InWriteSize.X / (float)LocalComponentSizeQuad), FMath::RoundToInt((float)InWriteSize.Y / (float)LocalComponentSizeQuad));

	if (InReadSize.X >= InWriteSize.X)
	{
		if (InReadSize.X == InWriteSize.X)
		{
			if (ComponentsPerTexture.X > 1.0f)
			{
				UVComponentSectionBase.X = static_cast<int32>(PositionOffset.X * LocalComponentSizeVerts);
			}
			else
			{
				UVComponentSectionBase.X -= (UVComponentSectionBase.X + LocalComponentSizeQuad > InWriteSize.X)
					                            ? static_cast<int32>(FMath::FloorToInt(PositionOffset.X / ComponentsPerTexture.X) * ComponentsPerTexture.X * LocalComponentSizeQuad)
					                            : 0;
			}
		}

		ComponentSectionBase.X -= (ComponentSectionBase.X + LocalComponentSizeQuad > InWriteSize.X)
			                          ? static_cast<int32>(FMath::FloorToInt(PositionOffset.X / ComponentsPerTexture.X) * ComponentsPerTexture.X * LocalComponentSizeQuad)
			                          : 0;
		PositionOffset.X = ComponentSectionBase.X / LocalComponentSizeQuad;
	}

	if (InReadSize.Y >= InWriteSize.Y)
	{
		if (InReadSize.Y == InWriteSize.Y)
		{
			if (ComponentsPerTexture.Y > 1.0f)
			{
				UVComponentSectionBase.Y = static_cast<int32>(PositionOffset.Y * LocalComponentSizeVerts);
			}
			else
			{
				UVComponentSectionBase.Y -= (UVComponentSectionBase.Y + LocalComponentSizeQuad > InWriteSize.Y)
					                            ? static_cast<int32>(FMath::FloorToInt((PositionOffset.Y / ComponentsPerTexture.Y)) * ComponentsPerTexture.Y * LocalComponentSizeQuad)
					                            : 0;
			}
		}

		ComponentSectionBase.Y -= (ComponentSectionBase.Y + LocalComponentSizeQuad > InWriteSize.Y)
			                          ? static_cast<int32>(FMath::FloorToInt((PositionOffset.Y / ComponentsPerTexture.Y)) * ComponentsPerTexture.Y * LocalComponentSizeQuad)
			                          : 0;
		PositionOffset.Y = ComponentSectionBase.Y / LocalComponentSizeQuad;
	}

	ComponentSectionBase.X = static_cast<int32>(PositionOffset.X * LocalComponentSizeVerts);
	ComponentSectionBase.Y = static_cast<int32>(PositionOffset.Y * LocalComponentSizeVerts);

	FVector2D UVSize((float)SubsectionSizeVerts / (float)InReadSize.X, (float)SubsectionSizeVerts / (float)InReadSize.Y);
	FIntPoint SubSectionSectionBase;

	for (int8 SubY = 0; SubY < NumSubsections; ++SubY)
	{
		for (int8 SubX = 0; SubX < NumSubsections; ++SubX)
		{
			SubSectionSectionBase.X = ComponentSectionBase.X + SubsectionSizeVerts * SubX;
			SubSectionSectionBase.Y = ComponentSectionBase.Y + SubsectionSizeVerts * SubY;

			// Offset for this component's data in texture
			FVector2D UVStart;

			if (InReadSize.X >= InWriteSize.X)
			{
				UVStart.X = ((float)UVComponentSectionBase.X / (float)InReadSize.X) + UVSize.X * (float)SubX;
			}
			else
			{
				UVStart.X = InScaleBias.X + UVSize.X * (float)SubX;
			}

			if (InReadSize.Y >= InWriteSize.Y)
			{
				UVStart.Y = ((float)UVComponentSectionBase.Y / (float)InReadSize.Y) + UVSize.Y * (float)SubY;
			}
			else
			{
				UVStart.Y = InScaleBias.Y + UVSize.Y * (float)SubY;
			}

			GenerateLayersRenderQuad(SubSectionSectionBase, static_cast<float>(SubsectionSizeVerts), UVStart, UVSize, OutTriangles);
		}
	}
}

void ALandscape::GenerateLayersRenderQuadsMip(const FIntPoint& InSectionBase, const FVector2D& InScaleBias, float InSubSectionSizeQuad, const FIntPoint& InReadSize, const FIntPoint& InWriteSize, uint8 InCurrentMip, TArray<FLandscapeLayersTriangle>& OutTriangles) const
{
	int32 SubsectionSizeVerts = static_cast<int32>(InSubSectionSizeQuad) + 1;
	int32 LocalComponentSizeQuad = static_cast<int32>(InSubSectionSizeQuad) * NumSubsections;
	int32 LocalComponentSizeVerts = SubsectionSizeVerts * NumSubsections;
	int32 MipSubsectionSizeVerts = SubsectionSizeVerts >> InCurrentMip;
	int32 MipLocalComponentSizeVerts = MipSubsectionSizeVerts * NumSubsections;

	FVector2D PositionOffset(FMath::RoundToInt((float)InSectionBase.X / (float)LocalComponentSizeQuad), FMath::RoundToInt((float)InSectionBase.Y / (float)LocalComponentSizeQuad));
	FVector2D ComponentsPerTexture(FMath::RoundToInt((float)InWriteSize.X / (float)LocalComponentSizeQuad), FMath::RoundToInt((float)InWriteSize.Y / (float)LocalComponentSizeQuad));

	FIntPoint ComponentSectionBase(static_cast<int32>(PositionOffset.X * MipLocalComponentSizeVerts), static_cast<int32>(PositionOffset.Y * MipLocalComponentSizeVerts));
	FIntPoint UVComponentSectionBase(static_cast<int32>(PositionOffset.X * LocalComponentSizeVerts), static_cast<int32>(PositionOffset.Y * LocalComponentSizeVerts));
	FVector2D UVSize((float)(SubsectionSizeVerts >> (InCurrentMip - 1)) / (float)InReadSize.X, (float)(SubsectionSizeVerts >> (InCurrentMip - 1)) / (float)InReadSize.Y);
	FIntPoint SubSectionSectionBase;

	for (int8 SubY = 0; SubY < NumSubsections; ++SubY)
	{
		for (int8 SubX = 0; SubX < NumSubsections; ++SubX)
		{
			SubSectionSectionBase.X = ComponentSectionBase.X + MipSubsectionSizeVerts * SubX;
			SubSectionSectionBase.Y = ComponentSectionBase.Y + MipSubsectionSizeVerts * SubY;

			// Offset for this component's data in texture
			FVector2D UVStart(((float)(UVComponentSectionBase.X >> (InCurrentMip - 1)) / (float)InReadSize.X) + UVSize.X * (float)SubX, ((float)(UVComponentSectionBase.Y >> (InCurrentMip - 1)) / (float)InReadSize.Y) + UVSize.Y * (float)SubY);

			GenerateLayersRenderQuad(SubSectionSectionBase, static_cast<float>(MipSubsectionSizeVerts), UVStart, UVSize, OutTriangles);
		}
	}
}

void ALandscape::GenerateLayersRenderQuadsAtlasToNonAtlas(const FIntPoint& InSectionBase, const FVector2D& InScaleBias, float InSubSectionSizeQuad, const FIntPoint& InReadSize, const FIntPoint& InWriteSize, TArray<struct FLandscapeLayersTriangle>& OutTriangles) const
{
	int32 SubsectionSizeVerts = static_cast<int32>(InSubSectionSizeQuad) + 1;
	FVector2D UVSize((float)SubsectionSizeVerts / (float)InReadSize.X, (float)SubsectionSizeVerts / (float)InReadSize.Y);

	for (int8 SubY = 0; SubY < NumSubsections; ++SubY)
	{
		for (int8 SubX = 0; SubX < NumSubsections; ++SubX)
		{
			FIntPoint SubSectionSectionBase(static_cast<int32>(InSectionBase.X + InSubSectionSizeQuad * SubX), static_cast<int32>(InSectionBase.Y + InSubSectionSizeQuad * SubY));
			FVector2D PositionOffset(FMath::RoundToInt(SubSectionSectionBase.X / InSubSectionSizeQuad), FMath::RoundToInt(SubSectionSectionBase.Y / InSubSectionSizeQuad));
			FIntPoint UVComponentSectionBase(static_cast<int32>(PositionOffset.X * SubsectionSizeVerts), static_cast<int32>(PositionOffset.Y * SubsectionSizeVerts));

			// Offset for this component's data in texture
			FVector2D UVStart;

			if (InReadSize.X >= InWriteSize.X)
			{
				UVStart.X = ((float)UVComponentSectionBase.X / (float)InReadSize.X);
			}
			else
			{
				UVStart.X = InScaleBias.X + UVSize.X * (float)SubX;
			}

			if (InReadSize.Y >= InWriteSize.Y)
			{
				UVStart.Y = ((float)UVComponentSectionBase.Y / (float)InReadSize.Y);
			}
			else
			{
				UVStart.Y = InScaleBias.Y + UVSize.Y * (float)SubY;
			}

			GenerateLayersRenderQuad(SubSectionSectionBase, static_cast<float>(SubsectionSizeVerts), UVStart, UVSize, OutTriangles);
		}
	}
}

void ALandscape::GenerateLayersRenderQuadsNonAtlas(const FIntPoint& InSectionBase, const FVector2D& InScaleBias, float InSubSectionSizeQuad, const FIntPoint& InReadSize, const FIntPoint& InWriteSize, TArray<struct FLandscapeLayersTriangle>& OutTriangles) const
{
	// We currently only support drawing in non atlas mode with the same texture size
	check(InReadSize.X == InWriteSize.X && InReadSize.Y == InWriteSize.Y);

	int32 SubsectionSizeVerts = static_cast<int32>(InSubSectionSizeQuad) + 1;

	FVector2D UVSize((float)SubsectionSizeVerts / (float)InReadSize.X, (float)SubsectionSizeVerts / (float)InReadSize.Y);

	for (int8 SubY = 0; SubY < NumSubsections; ++SubY)
	{
		for (int8 SubX = 0; SubX < NumSubsections; ++SubX)
		{
			FIntPoint SubSectionSectionBase(InSectionBase.X + SubsectionSizeQuads * SubX, InSectionBase.Y + SubsectionSizeQuads * SubY);
			FVector2D PositionOffset(FMath::RoundToInt(SubSectionSectionBase.X / InSubSectionSizeQuad), FMath::RoundToInt(SubSectionSectionBase.Y / InSubSectionSizeQuad));
			FIntPoint UVComponentSectionBase(static_cast<int32>(PositionOffset.X * InSubSectionSizeQuad), static_cast<int32>(PositionOffset.Y * InSubSectionSizeQuad));

			// Offset for this component's data in texture
			FVector2D UVStart(((float)UVComponentSectionBase.X / (float)InReadSize.X), ((float)UVComponentSectionBase.Y / (float)InReadSize.Y));
			GenerateLayersRenderQuad(SubSectionSectionBase, static_cast<float>(SubsectionSizeVerts), UVStart, UVSize, OutTriangles);
		}
	}
}

void ALandscape::GenerateLayersRenderQuadsNonAtlasToAtlas(const FIntPoint& InSectionBase, const FVector2D& InScaleBias, float InSubSectionSizeQuad, const FIntPoint& InReadSize, const FIntPoint& InWriteSize, TArray<struct FLandscapeLayersTriangle>& OutTriangles) const
{
	int32 SubsectionSizeVerts = static_cast<int32>(InSubSectionSizeQuad) + 1;
	int32 LocalComponentSizeQuad = static_cast<int32>(InSubSectionSizeQuad) * NumSubsections;
	int32 LocalComponentSizeVerts = SubsectionSizeVerts * NumSubsections;

	FVector2D PositionOffset(FMath::RoundToInt((float)InSectionBase.X / (float)LocalComponentSizeQuad), FMath::RoundToInt((float)InSectionBase.Y / (float)LocalComponentSizeQuad));
	FIntPoint ComponentSectionBase(static_cast<int32>(PositionOffset.X * LocalComponentSizeVerts), static_cast<int32>(PositionOffset.Y * LocalComponentSizeVerts));
	FVector2D UVSize((float)SubsectionSizeVerts / (float)InReadSize.X, (float)SubsectionSizeVerts / (float)InReadSize.Y);

	FIntPoint SubSectionSectionBase;

	for (int8 SubY = 0; SubY < NumSubsections; ++SubY)
	{
		for (int8 SubX = 0; SubX < NumSubsections; ++SubX)
		{
			SubSectionSectionBase.X = ComponentSectionBase.X + SubsectionSizeVerts * SubX;
			SubSectionSectionBase.Y = ComponentSectionBase.Y + SubsectionSizeVerts * SubY;

			// Offset for this component's data in texture
			float ScaleBiasZ = (float)InSectionBase.X / (float)InReadSize.X;
			float ScaleBiasW = (float)InSectionBase.Y / (float)InReadSize.Y;
			FVector2D UVStart(ScaleBiasZ + ((float)InSubSectionSizeQuad / (float)InReadSize.X) * (float)SubX, ScaleBiasW + ((float)InSubSectionSizeQuad / (float)InReadSize.Y) * (float)SubY);

			GenerateLayersRenderQuad(SubSectionSectionBase, static_cast<float>(SubsectionSizeVerts), UVStart, UVSize, OutTriangles);
		}
	}
}

void ALandscape::PrintLayersDebugHeightData(const FString& InContext, const TArray<FColor>& InHeightmapData, const FIntPoint& InDataSize, uint8 InMipRender, bool InOutputNormals) const
{
	bool DisplayDebugPrint = CVarOutputLayersRTContent.GetValueOnAnyThread() == 1 ? true : false;
	bool DisplayHeightAsDelta = false;

	if (!DisplayDebugPrint)
	{
		return;
	}

	TArray<uint16> HeightData;
	TArray<FVector> NormalData;
	HeightData.Reserve(InHeightmapData.Num());
	NormalData.Reserve(InHeightmapData.Num());

	for (FColor Color : InHeightmapData)
	{
		uint16 Height = ((Color.R << 8) | Color.G);
		HeightData.Add(Height);

		if (InOutputNormals)
		{
			FVector Normal;
			Normal.X = Color.B > 0.0f ? ((float)Color.B / 127.5f - 1.0f) : 0.0f;
			Normal.Y = Color.A > 0.0f ? ((float)Color.A / 127.5f - 1.0f) : 0.0f;
			Normal.Z = 0.0f;

			NormalData.Add(Normal);
		}
	}

	UE_LOG(LogLandscape, Display, TEXT("Context: %s"), *InContext);

	int32 MipSize = ((SubsectionSizeQuads + 1) >> InMipRender);

	for (int32 Y = 0; Y < InDataSize.Y; ++Y)
	{
		FString HeightmapHeightOutput;

		for (int32 X = 0; X < InDataSize.X; ++X)
		{
			int32 HeightDelta = HeightData[X + Y * InDataSize.X];

			if (DisplayHeightAsDelta)
			{
				HeightDelta = static_cast<int32>(HeightDelta >= LandscapeDataAccess::MidValue ? HeightDelta - LandscapeDataAccess::MidValue : HeightDelta);
			}

			if (X > 0 && MipSize > 0 && X % MipSize == 0)
			{
				HeightmapHeightOutput += FString::Printf(TEXT("  "));
			}

			FString HeightStr = FString::Printf(TEXT("%d"), HeightDelta);

			int32 PadCount = 5 - HeightStr.Len();
			if (PadCount > 0)
			{
				HeightStr = FString::ChrN(PadCount, '0') + HeightStr;
			}

			HeightmapHeightOutput += HeightStr + TEXT(" ");
		}

		if (Y > 0 && MipSize > 0 && Y % MipSize == 0)
		{
			UE_LOG(LogLandscape, Display, TEXT(""));
		}

		UE_LOG(LogLandscape, Display, TEXT("%s"), *HeightmapHeightOutput);
	}

	if (InOutputNormals)
	{
		UE_LOG(LogLandscape, Display, TEXT(""));

		for (int32 Y = 0; Y < InDataSize.Y; ++Y)
		{
			FString HeightmapNormaltOutput;

			for (int32 X = 0; X < InDataSize.X; ++X)
			{
				FVector Normal = NormalData[X + Y * InDataSize.X];

				if (X > 0 && MipSize > 0 && X % MipSize == 0)
				{
					HeightmapNormaltOutput += FString::Printf(TEXT("  "));
				}

				HeightmapNormaltOutput += FString::Printf(TEXT(" %s"), *Normal.ToString());
			}

			if (Y > 0 && MipSize > 0 && Y % MipSize == 0)
			{
				UE_LOG(LogLandscape, Display, TEXT(""));
			}

			UE_LOG(LogLandscape, Display, TEXT("%s"), *HeightmapNormaltOutput);
		}
	}
}

void ALandscape::PrintLayersDebugWeightData(const FString& InContext, const TArray<FColor>& InWeightmapData, const FIntPoint& InDataSize, uint8 InMipRender) const
{
	bool DisplayDebugPrint = (CVarOutputLayersRTContent.GetValueOnAnyThread() == 1 || CVarOutputLayersWeightmapsRTContent.GetValueOnAnyThread() == 1) ? true : false;

	if (!DisplayDebugPrint)
	{
		return;
	}

	UE_LOG(LogLandscape, Display, TEXT("Context: %s"), *InContext);

	int32 MipSize = ((SubsectionSizeQuads + 1) >> InMipRender);

	for (int32 Y = 0; Y < InDataSize.Y; ++Y)
	{
		FString WeightmapOutput;

		for (int32 X = 0; X < InDataSize.X; ++X)
		{
			const FColor& Weight = InWeightmapData[X + Y * InDataSize.X];

			if (X > 0 && MipSize > 0 && X % MipSize == 0)
			{
				WeightmapOutput += FString::Printf(TEXT("  "));
			}

			WeightmapOutput += FString::Printf(TEXT("%s "), *Weight.ToString());
		}

		if (Y > 0 && MipSize > 0 && Y % MipSize == 0)
		{
			UE_LOG(LogLandscape, Display, TEXT(""));
		}

		UE_LOG(LogLandscape, Display, TEXT("%s"), *WeightmapOutput);
	}
}

void ALandscape::PrintLayersDebugRT(const FString& InContext, UTextureRenderTarget2D* InDebugRT, uint8 InMipRender, bool InOutputHeight, bool InOutputNormals) const
{
	bool DisplayDebugPrint = (CVarOutputLayersRTContent.GetValueOnAnyThread() == 1 || CVarOutputLayersWeightmapsRTContent.GetValueOnAnyThread() == 1) ? true : false;

	if (!DisplayDebugPrint)
	{
		return;
	}

	FTextureRenderTargetResource* RenderTargetResource = InDebugRT->GameThread_GetRenderTargetResource();
	ENQUEUE_RENDER_COMMAND(LandscapeLayers_Cmd_DebugResolve)(
		[RenderTargetResource](FRHICommandListImmediate& RHICmdList) mutable
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(LandscapeLayers_RT_DebugResolve);
		TransitionAndCopyTexture(RHICmdList, RenderTargetResource->GetRenderTargetTexture(), RenderTargetResource->TextureRHI, {});
	});

	{
		TRACE_CPUPROFILER_EVENT_SCOPE(LandscapeLayers_Flush_DebugResolve);
		FlushRenderingCommands();
	}

	int32 MinX, MinY, MaxX, MaxY;
	const ULandscapeInfo* LandscapeInfo = GetLandscapeInfo();
	LandscapeInfo->GetLandscapeExtent(MinX, MinY, MaxX, MaxY);
	FIntRect SampleRect = FIntRect(0, 0, InDebugRT->SizeX, InDebugRT->SizeY);

	FReadSurfaceDataFlags Flags(RCM_UNorm, CubeFace_MAX);

	TArray<FColor> OutputRT;
	OutputRT.Reserve(SampleRect.Width() * SampleRect.Height());

	InDebugRT->GameThread_GetRenderTargetResource()->ReadPixels(OutputRT, Flags, SampleRect);

	if (InOutputHeight)
	{
		PrintLayersDebugHeightData(InContext, OutputRT, FIntPoint(SampleRect.Width(), SampleRect.Height()), InMipRender, InOutputNormals);
	}
	else
	{
		PrintLayersDebugWeightData(InContext, OutputRT, FIntPoint(SampleRect.Width(), SampleRect.Height()), InMipRender);
	}
}

void ALandscape::PrintLayersDebugTextureResource(const FString& InContext, FTextureResource* InTextureResource, uint8 InMipRender, bool InOutputHeight, bool InOutputNormals) const
{
	bool DisplayDebugPrint = (CVarOutputLayersRTContent.GetValueOnAnyThread() == 1 || CVarOutputLayersWeightmapsRTContent.GetValueOnAnyThread() == 1) ? true : false;

	if (!DisplayDebugPrint)
	{
		return;
	}

	int32 MinX, MinY, MaxX, MaxY;
	const ULandscapeInfo* LandscapeInfo = GetLandscapeInfo();
	LandscapeInfo->GetLandscapeExtent(MinX, MinY, MaxX, MaxY);
	FIntRect SampleRect = FIntRect(0, 0, InTextureResource->GetSizeX(), InTextureResource->GetSizeY());

	TArray<FColor> OutputTexels;
	OutputTexels.Reserve(SampleRect.Width() * SampleRect.Height());

	FReadSurfaceDataFlags Flags(RCM_UNorm, CubeFace_MAX);
	Flags.SetMip(InMipRender);

	ENQUEUE_RENDER_COMMAND(LandscapeLayers_Cmd_Readback)(
		[InTextureResource, SampleRect = SampleRect, OutData = &OutputTexels, ReadFlags = Flags](FRHICommandListImmediate& RHICmdList) mutable
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(LandscapeLayers_RT_Readback);
		RHICmdList.ReadSurfaceData(InTextureResource->TextureRHI, SampleRect, *OutData, ReadFlags);
	});

	{
		TRACE_CPUPROFILER_EVENT_SCOPE(LandscapeLayers_Flush_Readback);
		FlushRenderingCommands();
	}

	if (InOutputHeight)
	{
		PrintLayersDebugHeightData(InContext, OutputTexels, FIntPoint(SampleRect.Width(), SampleRect.Height()), InMipRender, InOutputNormals);
	}
	else
	{
		PrintLayersDebugWeightData(InContext, OutputTexels, FIntPoint(SampleRect.Width(), SampleRect.Height()), InMipRender);
	}
}

bool ALandscape::PrepareTextureResources(bool bInWaitForStreaming)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(LandscapeLayers_PrepareTextureResources);

	ULandscapeInfo* Info = GetLandscapeInfo();
	if (Info == nullptr || !FApp::CanEverRender())
	{
		return false;
	}

	// Only keep the textures that are still valid:
	TSet<UTexture2D*> StreamingInTexturesBefore;
	StreamingInTexturesBefore.Reserve(TrackedStreamingInTextures.Num());	
	Algo::TransformIf(TrackedStreamingInTextures, StreamingInTexturesBefore,
		[](const TWeakObjectPtr<UTexture2D>& Texture) { return Texture.IsValid(); },
		[](const TWeakObjectPtr<UTexture2D>& Texture) { return Texture.Get(); });
	TrackedStreamingInTextures.Empty();

	// Textures that are still streaming in (filled out below)
	TSet<UTexture2D*> StreamingInTexturesAfter;
	StreamingInTexturesAfter.Reserve(TrackedStreamingInTextures.Num());

	// Textures that have just completed streaming in (filled out below)
	TSet<UTexture2D*> StreamedInTextures;

	// All components containing heightmaps that have just completed streaming in (filled out below)
	TSet<ULandscapeComponent*> StreamedInHeightmapComponents;

	FLandscapeTextureStreamingManager* TextureStreamingManager = GetWorld()->GetSubsystem<ULandscapeSubsystem>()->GetTextureStreamingManager();

	bool bIsReady = true;
	Info->ForEachLandscapeProxy([&](ALandscapeProxy* Proxy)
	{
		for (ULandscapeComponent* Component : Proxy->LandscapeComponents)
		{
			UTexture2D* ComponentHeightmap = Component->GetHeightmap();

			{
				bool bIsTextureReady = TextureStreamingManager->RequestTextureFullyStreamedInForever(ComponentHeightmap, bInWaitForStreaming);
				if (!bIsTextureReady)
				{
					StreamingInTexturesAfter.Add(ComponentHeightmap);
				}
				else
				{
					// If it was previously streaming in, then it has just completed.
					if (StreamingInTexturesBefore.Contains(ComponentHeightmap))
					{
						StreamedInTextures.Add(ComponentHeightmap);
						StreamedInHeightmapComponents.Add(Component);
					}
				}
				bIsReady &= bIsTextureReady;
			}

			for (UTexture2D* ComponentWeightmap : Component->GetWeightmapTextures())
			{
				bool bIsTextureReady = TextureStreamingManager->RequestTextureFullyStreamedInForever(ComponentWeightmap, bInWaitForStreaming);
				// If the texture is not ready, start tracking its state changes to be notified when it's fully streamed in : 
				if (!bIsTextureReady)
				{
					StreamingInTexturesAfter.Add(ComponentWeightmap);
				}
				else
				{
					// If it was previously streaming in, then it has just completed.
					if (StreamingInTexturesBefore.Contains(ComponentWeightmap))
					{
						StreamedInTextures.Add(ComponentWeightmap);
					}
				}
				bIsReady &= bIsTextureReady;
			}
		}
		return true;
	});

	// The assets that were streaming in before and are not anymore can be considered streamed in: 
	InvalidateRVTForTextures(StreamedInTextures);

	// If we streamed in any heightmaps, notify interested parties (i.e. water)
	if (StreamedInHeightmapComponents.Num() > 0)
	{
		// Calculate update region.
		FBox2D HeightmapUpdateRegion(ForceInit);
		for (ULandscapeComponent* Component : StreamedInHeightmapComponents)
		{
			if (ALandscapeProxy* Proxy = Component->GetLandscapeProxy())
			{
				const FBox ProxyBox = Proxy->GetComponentsBoundingBox();
				HeightmapUpdateRegion += FBox2D(FVector2D(ProxyBox.Min), FVector2D(ProxyBox.Max));
			}
		}

		// Notify that heightmaps have been streamed.
		if (ULandscapeSubsystem* LandscapeSubsystem = GetWorld()->GetSubsystem<ULandscapeSubsystem>())
		{
			FOnHeightmapStreamedContext Context(HeightmapUpdateRegion, StreamedInHeightmapComponents);
			LandscapeSubsystem->GetOnHeightmapStreamedDelegate().Broadcast(Context);
		}
	}

	// Store as a list of TWeakObjectPtr<UTexture2D> so as not to keep references on the tracked textures :
	Algo::Transform(StreamingInTexturesAfter, TrackedStreamingInTextures, [](UTexture2D* Texture) { return TWeakObjectPtr<UTexture2D>(Texture); });

	return bIsReady;
}

void ALandscape::DeleteUnusedLayers()
{
	ULandscapeInfo* LandscapeInfo = GetLandscapeInfo();

	if (LandscapeInfo == nullptr)
	{
		return;
	}

	for (const TWeakObjectPtr<ALandscapeStreamingProxy>& Proxy : LandscapeInfo->StreamingProxies)
	{
		if (!Proxy.IsValid())
		{
			continue;
		}

		Proxy->DeleteUnusedLayers();
	}

	ALandscapeProxy::DeleteUnusedLayers();
}

// Note: this approach is generic, because FObjectCacheContextScope is a fast texture->material interface->primitive component lookup. 
// If FObjectCacheContextScope was available at runtime, it could become an efficient way to automatically invalidate RVT areas corresponding to primitive components that use textures that are being streamed in:
void ALandscape::InvalidateRVTForTextures(const TSet<UTexture2D*>& InTextures)
{
#if WITH_EDITOR
	TRACE_CPUPROFILER_EVENT_SCOPE(ALandscape_InvalidateRVTForTextures);

	if (!InTextures.IsEmpty())
	{
		// Retrieve all primitive components that use this texture through a RVT-writing material, using FObjectCacheContextScope, which is a fast texture->material interface->primitive component lookup
		FObjectCacheContextScope ObjectCacheScope;
		TSet<UPrimitiveComponent*> PrimitiveComponentsToInvalidate;

		for (UTexture2D* Texture : InTextures)
		{
			if (Texture != nullptr)
			{
				// First, find all the materials referencing this texture that are writing to the RVT in order to invalidate the primitive components referencing them when the texture 
				//  gets fully streamed in so that we're not left with low-res mips being rendered in the RVT tiles : 
				for (UMaterialInterface* MaterialInterface : ObjectCacheScope.GetContext().GetMaterialsAffectedByTexture(Texture))
				{
					if (MaterialInterface->WritesToRuntimeVirtualTexture())
					{
						for (IPrimitiveComponent* PrimitiveComponentInterface : ObjectCacheScope.GetContext().GetPrimitivesAffectedByMaterial(MaterialInterface))
						{
							// Landscape only supports UPrimitiveComponent for the moment
							if (UPrimitiveComponent* PrimitiveComponent = PrimitiveComponentInterface->GetUObject<UPrimitiveComponent>())
						{
							PrimitiveComponentsToInvalidate.Add(PrimitiveComponent);
						}
					}
				}
			}
		}
		}

		if (!PrimitiveComponentsToInvalidate.IsEmpty())
		{
			// Now invalidate the RVT regions that correspond to these components :
			for (TObjectIterator<URuntimeVirtualTextureComponent> It(/*AdditionalExclusionFlags = */RF_ClassDefaultObject, /*bIncludeDerivedClasses = */true, /*InInternalExclusionFlags = */EInternalObjectFlags::Garbage); It; ++It)
			{
				for (UPrimitiveComponent* PrimitiveComponent : PrimitiveComponentsToInvalidate)
				{
					if (PrimitiveComponent->GetRuntimeVirtualTextures().Contains(It->GetVirtualTexture()))
					{
						It->Invalidate(FBoxSphereBounds(PrimitiveComponent->Bounds));
					}
				}
			}
		}
	}
#endif // WITH_EDITOR
}

bool ALandscape::PrepareLayersTextureResources(bool bInWaitForStreaming)
{
	return PrepareLayersTextureResources(LandscapeEditLayers, bInWaitForStreaming);
}

bool ALandscape::PrepareLayersTextureResources(const TArray<FLandscapeLayer>& InLayers, bool bInWaitForStreaming)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(ALandscape::PrepareLayersTextureResources);

	ULandscapeInfo* Info = GetLandscapeInfo();
	if (Info == nullptr)
	{
		return false;
	}

	FLandscapeTextureStreamingManager* TextureStreamingManager = GetWorld()->GetSubsystem<ULandscapeSubsystem>()->GetTextureStreamingManager();

	bool bIsReady = true;
	Info->ForEachLandscapeProxy([&, TextureStreamingManager](ALandscapeProxy* Proxy)
	{
		for (const FLandscapeLayer& Layer : InLayers)
		{
			for (ULandscapeComponent* Component : Proxy->LandscapeComponents)
			{
				if (FLandscapeLayerComponentData* ComponentLayerData = Component->GetLayerData(Layer.Guid))
				{
					bIsReady &= TextureStreamingManager->RequestTextureFullyStreamedInForever(ComponentLayerData->HeightmapData.Texture, bInWaitForStreaming);

					for (UTexture2D* LayerWeightmap : ComponentLayerData->WeightmapData.Textures)
					{
						bIsReady &= TextureStreamingManager->RequestTextureFullyStreamedInForever(LayerWeightmap, bInWaitForStreaming);
					}
				}
			}
		}
		return true;
	});

	return bIsReady;
}

bool ALandscape::PrepareLayersResources(ERHIFeatureLevel::Type InFeatureLevel, bool bInWaitForStreaming)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(ALandscape::PrepareLayersResources);
	TSet<UObject*> Dependencies;
	for (const FLandscapeLayer& Layer : LandscapeEditLayers)
	{
		ULandscapeEditLayerBase* EditLayer = Layer.EditLayer;
		check(EditLayer != nullptr);

		if (EditLayer->SupportsTargetType(ELandscapeToolTargetType::Heightmap) || EditLayer->SupportsTargetType(ELandscapeToolTargetType::Weightmap) || EditLayer->SupportsTargetType(ELandscapeToolTargetType::Visibility))
		{
			EditLayer->GetRenderDependencies(Dependencies);
		}

		for (const FLandscapeLayerBrush& Brush : Layer.Brushes)
		{
			if (ALandscapeBlueprintBrushBase* LandscapeBrush = Brush.GetBrush())
			{
				if (LandscapeBrush->AffectsWeightmap() || LandscapeBrush->AffectsHeightmap() || LandscapeBrush->AffectsVisibilityLayer())
				{
					LandscapeBrush->GetRenderDependencies(Dependencies);
				}
			}
		}
	}

	FLandscapeTextureStreamingManager* TextureStreamingManager = GetWorld()->GetSubsystem<ULandscapeSubsystem>()->GetTextureStreamingManager();

	bool bIsReady = true;
	for (UObject* Dependency : Dependencies)
	{
		// Streamable textures need to be fully streamed in : 
		if (UTexture* Texture = Cast<UTexture>(Dependency))
		{
			bIsReady &= TextureStreamingManager->RequestTextureFullyStreamedInForever(Texture, bInWaitForStreaming);
		}

		// Material shaders need to be fully compiled : 
		if (UMaterialInterface* MaterialInterface = Cast<UMaterialInterface>(Dependency))
		{
			if (FMaterialResource* MaterialResource = MaterialInterface->GetMaterialResource(InFeatureLevel))
			{
				// Don't early-out because checking for the material resource actually requests the shaders to be loaded so we want to make sure to request them all at once instead of one by one :
				bIsReady &= IsMaterialResourceCompiled(MaterialResource, bInWaitForStreaming);
			}
		}
	}

	return bIsReady;
}

// Must match FEditLayerHeightmapMergeInfo in LandscapeLayersHeightmapsPS.usf
struct FLandscapeEditLayerHeightmapMergeInfo
{
	// COMMENT [jonathan.bard] : not used at the moment because we copy to a texture 2D array but if we didn't and had instead N statically bound textures, we could save that copy and sample the textures directly :
	FIntRect TextureSubregion; // Subregion of the source (edit layer) texture to use

	ELandscapeEditLayerHeightmapBlendMode BlendMode = ELandscapeEditLayerHeightmapBlendMode::Num; // How this layer blends with the previous ones in the layers stack
	float Alpha = 1.0f; // Alpha value to be used in the blend
	uint32 Padding0; // Align to next float4 
	uint32 Padding1;
};


// Must match FEditLayerWeightmapMergeInfo in LandscapeLayersWeightmapsPS.usf
struct FLandscapeEditLayerWeightmapMergeInfo
{
	uint32 SourceWeightmapTextureIndex = (uint32)INDEX_NONE; // The index in InPackedWeightmaps of the texture to read from for this layer
	uint32 SourceWeightmapTextureChannel = (uint32)INDEX_NONE; // The channel of the texture to read from for this layer
	UE::Landscape::EditLayers::EWeightmapBlendMode BlendMode = UE::Landscape::EditLayers::EWeightmapBlendMode::Num; // How this layer blends with the previous ones in the layers stack
	float Alpha = 1.0f; // Alpha value to be used in the blend
};

// Must match FPerEditLayerWeightmapPaintLayerInfo in LandscapeEditLayersWeightmaps.usf
// Additional info about this paint layer on this edit layer
struct FLandscapePerEditLayerWeightmapPaintLayerInfo
{
	UE::Landscape::EditLayers::EWeightmapBlendMode Flags = UE::Landscape::EditLayers::EWeightmapBlendMode::Num; // How this layer blends with the previous ones in the layers stack
};

// Struct that contains all the information relevant for the edit layers update operation (list of dirty components, heightmaps, weightmaps, etc.
//  Because this information can change during the course of the update (e.g. new weightmaps are added) it can be (partially or not) refreshed if necessary :
struct FUpdateLayersContentContext
{
	// Partial refresh flags : allows to recompute only a subset of the context information :
	enum class ERefreshFlags
	{
		None = 0,
		RefreshComponentInfos = (1 << 0),
		RefreshHeightmapInfos = (1 << 1),
		RefreshWeightmapInfos = (1 << 2),
		RefreshMapHelper = (1 << 3),
		RefreshAll = ~0,
	};
	FRIEND_ENUM_CLASS_FLAGS(ERefreshFlags);

	FUpdateLayersContentContext(const FTextureToComponentHelper& InMapHelper, bool bInPartialUpdate)
		: bPartialUpdate(bInPartialUpdate)
		, MapHelper(InMapHelper)
	{
		// No need to update the map helper, it's assumed to be already ready in the constructor
		Refresh(ERefreshFlags::RefreshComponentInfos | ERefreshFlags::RefreshHeightmapInfos | ERefreshFlags::RefreshWeightmapInfos);
	}

	FTextureToComponentHelper::ERefreshFlags RefreshFlagsToMapHelperRefreshFlags(ERefreshFlags InRefreshFlags)
	{
		FTextureToComponentHelper::ERefreshFlags MapHelperRefreshFlags = FTextureToComponentHelper::ERefreshFlags::None;
		if (EnumHasAnyFlags(InRefreshFlags, ERefreshFlags::RefreshComponentInfos))
		{
			MapHelperRefreshFlags |= FTextureToComponentHelper::ERefreshFlags::RefreshComponents;
		}
		if (EnumHasAnyFlags(InRefreshFlags, ERefreshFlags::RefreshHeightmapInfos))
		{
			MapHelperRefreshFlags |= FTextureToComponentHelper::ERefreshFlags::RefreshHeightmaps;
		}
		if (EnumHasAnyFlags(InRefreshFlags, ERefreshFlags::RefreshWeightmapInfos))
		{
			MapHelperRefreshFlags |= FTextureToComponentHelper::ERefreshFlags::RefreshWeightmaps;
		}
		return MapHelperRefreshFlags;
	}

	void Refresh(ERefreshFlags InRefreshFlags)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(UpdateLayersContentContext_Refresh);
		// Start by updating the map helper if necessary (keep track of components/heightmaps/weightmaps relationship) :
		if (EnumHasAnyFlags(InRefreshFlags, ERefreshFlags::RefreshMapHelper))
		{
			MapHelper.Refresh(RefreshFlagsToMapHelperRefreshFlags(InRefreshFlags));
		}

		// Then triage the dirty/non-dirty components  :
		if (EnumHasAnyFlags(InRefreshFlags, ERefreshFlags::RefreshComponentInfos))
		{
			// When components are refreshed, all other info has to be :
			check(EnumHasAllFlags(InRefreshFlags, ERefreshFlags::RefreshHeightmapInfos | ERefreshFlags::RefreshWeightmapInfos));

			DirtyLandscapeComponents.Reset();
			NonDirtyLandscapeComponents.Reset();
			for (ULandscapeComponent* Component : MapHelper.LandscapeComponents)
			{
				if (!bPartialUpdate || Component->GetLayerUpdateFlagPerMode() != 0)
				{
					DirtyLandscapeComponents.Add(Component);
				}
				else
				{
					NonDirtyLandscapeComponents.Add(Component);
				}
			}
		}

		if (EnumHasAnyFlags(InRefreshFlags, ERefreshFlags::RefreshHeightmapInfos | ERefreshFlags::RefreshWeightmapInfos))
		{
			TSet<UTexture2D*> HeightmapsToRender;
			TSet<ULandscapeComponent*> NeighborsComponents;
			TSet<ULandscapeComponent*> WeightmapsComponents;

			// Cleanup our heightmap/weightmap info:
			if (EnumHasAnyFlags(InRefreshFlags, ERefreshFlags::RefreshHeightmapInfos))
			{
				HeightmapsToResolve.Reset();
				LandscapeComponentsHeightmapsToRender.Reset();
				LandscapeComponentsHeightmapsToResolve.Reset();
			}
			if (EnumHasAnyFlags(InRefreshFlags, ERefreshFlags::RefreshWeightmapInfos))
			{
				WeightmapsToResolve.Reset();
				LandscapeComponentsWeightmapsToRender.Reset();
				LandscapeComponentsWeightmapsToResolve.Reset();
			}
			// Note that the AllLandscapeComponentsToResolve and AllLandscapeComponentReadbackResults are *not* reset here: they can only grow (we're assuming refresh only adds new components): 

			// Iterate on all dirty components and retrieve the components that need to be resolved or rendered for their heightmap or weightmaps :
			TArray<ULandscapeComponent*> AllLandscapeComponents;
			for (ULandscapeComponent* Component : DirtyLandscapeComponents)
			{
				AllLandscapeComponents.Add(Component);

				// If all components are dirty, we can take some shortcuts since all components will need to be rendered and resolved : 
				if (bPartialUpdate)
				{
					if (EnumHasAnyFlags(InRefreshFlags, ERefreshFlags::RefreshHeightmapInfos))
					{
						// Gather Neighbors (Neighbors need to be Rendered but not resolved so that the resolved Components have valid normals on edges)
						Component->GetLandscapeComponentNeighborsToRender(NeighborsComponents);
						Component->ForEachLayer([&](const FGuid&, FLandscapeLayerComponentData& LayerData)
						{
							HeightmapsToRender.Add(LayerData.HeightmapData.Texture);
						});
					}

					if (EnumHasAnyFlags(InRefreshFlags, ERefreshFlags::RefreshWeightmapInfos))
					{
						// Gather WeightmapUsages (Components sharing weightmap usages with the resolved Components need to be rendered so that the resolving is valid)
						Component->GetLandscapeComponentWeightmapsToRender(WeightmapsComponents);
					}
				}

				if (EnumHasAnyFlags(InRefreshFlags, ERefreshFlags::RefreshHeightmapInfos))
				{
					// Gather Heightmaps (All Components sharing Heightmap textures need to be rendered and resolved)
					HeightmapsToResolve.Add(Component->GetHeightmap(/*InReturnEditingHeightmap = */false));
				}

				if (EnumHasAnyFlags(InRefreshFlags, ERefreshFlags::RefreshWeightmapInfos))
				{
					// Gather Weightmaps
					const TArray<UTexture2D*>& WeightmapTextures = Component->GetWeightmapTextures(/*InReturnEditingWeightmap = */false);
					for (FWeightmapLayerAllocationInfo const& AllocInfo : Component->GetWeightmapLayerAllocations(/*InReturnEditingWeightmap = */false))
					{
						if (AllocInfo.IsAllocated() && AllocInfo.WeightmapTextureIndex < WeightmapTextures.Num())
						{
							WeightmapsToResolve.Add(WeightmapTextures[AllocInfo.WeightmapTextureIndex]);
						}
					}
				}
			}

			if (EnumHasAnyFlags(InRefreshFlags, ERefreshFlags::RefreshHeightmapInfos))
			{
				// Because of Heightmap Sharing anytime we render a heightmap we need to render all the components that use it
				for (ULandscapeComponent* NeighborsComponent : NeighborsComponents)
				{
					NeighborsComponent->ForEachLayer([&](const FGuid&, FLandscapeLayerComponentData& LayerData)
					{
						HeightmapsToRender.Add(LayerData.HeightmapData.Texture);
					});
				}

				// Copy first list into others
				LandscapeComponentsHeightmapsToResolve.Append(AllLandscapeComponents);
				LandscapeComponentsHeightmapsToRender.Append(AllLandscapeComponents);
			}

			if (EnumHasAnyFlags(InRefreshFlags, ERefreshFlags::RefreshWeightmapInfos))
			{
				LandscapeComponentsWeightmapsToResolve.Append(AllLandscapeComponents);
				LandscapeComponentsWeightmapsToRender.Append(AllLandscapeComponents);
			}

			if (bPartialUpdate)
			{
				for (ULandscapeComponent* Component : NonDirtyLandscapeComponents)
				{
					if (EnumHasAnyFlags(InRefreshFlags, ERefreshFlags::RefreshHeightmapInfos))
					{
						if (HeightmapsToResolve.Contains(Component->GetHeightmap(false)))
						{
							AllLandscapeComponents.Add(Component);
							LandscapeComponentsHeightmapsToRender.Add(Component);
							LandscapeComponentsHeightmapsToResolve.Add(Component);
						}
						else if (NeighborsComponents.Contains(Component))
						{
							LandscapeComponentsHeightmapsToRender.Add(Component);
						}
						else
						{
							bool bAdd = false;
							Component->ForEachLayer([&](const FGuid&, FLandscapeLayerComponentData& LayerData)
							{
								if (HeightmapsToRender.Contains(LayerData.HeightmapData.Texture))
								{
									bAdd = true;
								}
							});
							if (bAdd)
							{
								LandscapeComponentsHeightmapsToRender.Add(Component);
							}
						}
					}

					if (EnumHasAnyFlags(InRefreshFlags, ERefreshFlags::RefreshWeightmapInfos))
					{
						if (WeightmapsComponents.Contains(Component))
						{
							LandscapeComponentsWeightmapsToRender.Add(Component);
						}
					}
				}
			}

			// All selected components will have to be resolved : 
			AllLandscapeComponentsToResolve.Append(AllLandscapeComponents);

			// Add components with deferred flag to update list
			for (ULandscapeComponent* Component : AllLandscapeComponents)
			{
				if (Component->GetLayerUpdateFlagPerMode() & ELandscapeLayerUpdateMode::Update_Client_Deferred)
				{
					FLandscapeEditLayerComponentReadbackResult* ComponentReadbackResult = AllLandscapeComponentReadbackResults.FindByPredicate([Component](const FLandscapeEditLayerComponentReadbackResult& Element) { return Element.LandscapeComponent == Component; });
					if (ComponentReadbackResult == nullptr)
					{
						AllLandscapeComponentReadbackResults.Add(FLandscapeEditLayerComponentReadbackResult(Component, ELandscapeLayerUpdateMode::Update_Client_Deferred));
					}
				}
			}
		}
	}

	// Indicates whether all components of the landscape are marked dirty :
	const bool bPartialUpdate = false;
	// Helper to gather mappings between heightmaps/weightmaps and components :
	FTextureToComponentHelper MapHelper;
	// List of landscape components that have been made dirty and need to be updated : 
	TArray<ULandscapeComponent*> DirtyLandscapeComponents;
	// List of landscape components that have not been made dirty : 
	TArray<ULandscapeComponent*> NonDirtyLandscapeComponents;
	// List of heightmap textures that might be affected by the update : 
	TSet<UTexture2D*> HeightmapsToResolve;
	// List of weightmap textures that might be affected by the update : 
	TSet<UTexture2D*> WeightmapsToResolve;
	// List of components that need to be rendered because they are either dirty or are neighbor to a component that is dirty or share a heightmap with a component that is dirty:
	TArray<ULandscapeComponent*> LandscapeComponentsHeightmapsToRender;
	// List of components whose heightmap needs to be resolved because they are either dirty or are neighbor to a component that is dirty:
	TArray<ULandscapeComponent*> LandscapeComponentsHeightmapsToResolve;
	// List of components that need to be rendered because they are either dirty or are neighbor to a component that is dirty or share a weightmap with a component that is dirty:
	TArray<ULandscapeComponent*> LandscapeComponentsWeightmapsToRender;
	// List of components whose weightmap needs to be resolved because they are either dirty or are neighbor to a component that is dirty:
	TArray<ULandscapeComponent*> LandscapeComponentsWeightmapsToResolve;
	// List of components whose heightmap or weightmaps needs to be resolved because they are either dirty or are neighbor to a component that is dirty:
	TSet<ULandscapeComponent*> AllLandscapeComponentsToResolve;
	// List of GPU readback results for heightmaps/weightmaps that need to be resolved, associated with their owning landscape component :
	TArray<FLandscapeEditLayerComponentReadbackResult> AllLandscapeComponentReadbackResults;
};
ENUM_CLASS_FLAGS(FUpdateLayersContentContext::ERefreshFlags);

// Little struct that holds information common to PerformLayersHeightmapsLocalMerge, PerformLayersHeightmapsBatchedMerge and PerformLayersHeightmapsGlobalMerge
struct FEditLayersHeightmapMergeParams
{
	int32 HeightmapUpdateModes;
	bool bForceRender;
	bool bSkipBrush;
};

// Render-thread version of the data / functions we need for the local merge of edit layers : 
namespace EditLayersHeightmapLocalMerge_RenderThread
{
	struct FComponentRenderInfo
	{
		// Name of the component for debug purposes :
		FString Name;

		// The component's visible layer's heightmaps (Num must be == Num of VisibleEditLayerInfos)
		TArray<FTexture2DResourceSubregion> VisibleLayerHeightmapTextures;

		// List of 9 component render info indices corresponding to the 9 neighbors of this component (index into ComponentToRenderInfos) :
		//  Some may not be set and that's fine : the only goal is to know which are the valid neighbors when it comes to stitching adjacent rendered components
		//  If a neighbor is missing, we can't stitch the border adjacent to it, but it also means it won't contribute to what we really want to compute : the 
		//  component to resolve, which is guaranteed to have its neighbors present (if those do exist) :
		TStaticArray<int32, 9> NeighborComponentToRenderInfoIndices;
	};

	struct FComponentResolveInfo
	{
		FComponentResolveInfo(int32 InNumComponentsToRender)
		{
			NeighborComponentToRenderInfoBitIndices.Init(false, InNumComponentsToRender);
		}

		void SetNeighborRenderInfo(int32 InNeighborIndex, int32 InNeighborComponentRenderInfoIndex)
		{
			NeighborComponentToRenderInfoIndices[InNeighborIndex] = InNeighborComponentRenderInfoIndex;
			if (InNeighborComponentRenderInfoIndex != INDEX_NONE)
			{
				check(!NeighborComponentToRenderInfoBitIndices[InNeighborComponentRenderInfoIndex]);
				NeighborComponentToRenderInfoBitIndices[InNeighborComponentRenderInfoIndex] = true;
			}
		}

	public:
		// Index of this components in ComponentToResolveInfos :
		int32 ComponentToResolveInfoIndex = INDEX_NONE;
		// Name of the component for debug purposes :
		FString Name;
		// Subregion of the heightmap that we want to compute normals for and resolve, that corresponds to this component :
		FTexture2DResourceSubregion Heightmap;

		// List of 9 component render info indices corresponding to the 9 neighbors of this component (index into ComponentToRenderInfos) :
		//  Some may not be set if the component is on the border of the landscape, for example :
		TStaticArray<int32, 9> NeighborComponentToRenderInfoIndices;
		// Same as NeighborComponentToRenderInfoIndices but as a bit array (1 bit per component to render info) to vastly optimize the division of component resolve infos into batches, which is a O(N^2) operation : 
		TBitArray<> NeighborComponentToRenderInfoBitIndices;
	};

	struct FTextureResolveInfo
	{
		// Size of the entire texture that needs resolving :
		FIntPoint TextureSize = FIntPoint(ForceInitToZero);
		// Number of mips corresponding to that size : 
		int32 NumMips = 0;
		// Texture that was updated and needs resolving : 
		FTexture2DResource* Texture = nullptr;
		// CPU readback utility to bring back the result on the CPU : 
		FLandscapeEditLayerReadback* CPUReadback;
	};

	// Because of heightmaps being shared between one component and another, we have to group the components to render into batches
	//  where we'll render all of the heightmaps into slices of a single scratch texture array, which we'll then be able to re-assemble into the final, packed, heightmaps (subregions) :
	struct FComponentResolveBatchInfo
	{
		FComponentResolveBatchInfo(int32 InNumComponentsToRender, int32 InBatchIndex)
			: BatchIndex(InBatchIndex)
		{
			ComponentToRenderInfoBitIndices.Init(false, InNumComponentsToRender);
		}

		void AddComponent(const FComponentResolveInfo& InComponentResolveInfo)
		{
			check(!ComponentToResolveInfoIndices.Contains(InComponentResolveInfo.ComponentToResolveInfoIndex));
			ComponentToResolveInfoIndices.Add(InComponentResolveInfo.ComponentToResolveInfoIndex);

			// Remember all the unique components that this texture needs for resolving:
			ComponentToRenderInfoBitIndices.CombineWithBitwiseOR(InComponentResolveInfo.NeighborComponentToRenderInfoBitIndices, EBitwiseOperatorFlags::MinSize);
		}

	public:
		int32 BatchIndex = INDEX_NONE;

		// Indices (in ComponentToRenderInfos) of the components whose heightmaps we need to render within this batch in order to produce (and then resolve) the textures in TextureToResolveInfos:
		//  It's a bit array (1 bit per component to render info) to vastly optimize the division of texture resolve infos into batches, which is a O(N^2) operation : 
		TBitArray<> ComponentToRenderInfoBitIndices;
		// Indices (in ComponentToResolveInfos) of components whose heightmap subregion needs to be resolved / read back on the CPU :
		TArray<int32> ComponentToResolveInfoIndices;
	};

	struct FEditLayerInfo
	{
		ELandscapeEditLayerHeightmapBlendMode BlendMode = ELandscapeEditLayerHeightmapBlendMode::Num;
		float Alpha = 1.0f;
	};

	// Description of the entire merge pass :
	struct FMergeInfo
	{
		bool NeedsMerge() const
		{
			// If no edit layer or if no paint layer present on any edit layer, we've got nothing to do :
			bool bNeedsMerge = (MaxNumEditLayersTexturesToMerge > 0) && (MaxNumComponentsToRenderPerResolveComponentBatch > 0);
			check(!bNeedsMerge || !TextureToResolveInfos.IsEmpty()); // If we need merging, we must have at one texture to resolve
			return bNeedsMerge;
		}

	public:
		// Number of vertices per component
		FIntPoint ComponentSizeVerts = FIntPoint(ForceInitToZero);

		// Number of sub sections for this landscape 
		uint32 NumSubsections = 1;

		// Maximum size of all heigthmaps (one heightmap can contain multiple components due to heightmap sharing)
		FIntPoint MaxHeightmapSize = FIntPoint(ForceInitToZero);

		// Maximum number of mips of all heightmaps (which can be of different sizes) :
		int32 MaxHeightmapNumMips = 0;

		// Heightmap pixel to world scale factor
		FVector LandscapeGridScale = FVector::ZeroVector;
		
		// Maximum number of visible edit layers that have to be merged for a single FComponentRenderInfo : 
		int32 MaxNumEditLayersTexturesToMerge = 0;

		// Maximum number of components to render in any given FComponentResolveBatchInfo. This is the number of slices needed for the scratch texture arrays that we reuse from one batch to another :
		int32 MaxNumComponentsToRenderPerResolveComponentBatch = 0;

		// Describes how to access each visible edit layer's heightmap and how to blend it in the final heightmap for this paint layer :
		TArray<FEditLayerInfo> VisibleEditLayerInfos;

		// List of infos for each component that needs its edit layers' heightmaps to be rendered (merged) and ultimately participate to the final heightmap of the component we're trying to resolve :
		TArray<FComponentRenderInfo> ComponentToRenderInfos;

		// List of infos for each component that needs to be resolved :
		TArray<FComponentResolveInfo> ComponentToResolveInfos;

		// List of batches of FComponentResolveInfo that needs to be resolved in the same pass. This allows massive saves on transient resources on large landscapes because those can be re-cycled from one pass to another :
		TArray<FComponentResolveBatchInfo> ComponentResolveBatchInfos;

		// List of infos for each texture that needs to be resolved :
		TArray<FTextureResolveInfo> TextureToResolveInfos;

		// Not truly render-thread data because it references UTextures but it's just because FLandscapeEditLayerReadback were historically game-thread initiated so for as long as we'll use those for readback, we need to store this here : 
		TArray<FLandscapeLayersCopyReadbackTextureParams> DeferredCopyReadbackTextures;
	};

	struct FRDGResources
	{
		// Texture array in which all possible edit layers heightmaps can fit : we copy the edit layers heightmaps there in order to dynamically access it in the merge shader : 
		FRDGTextureRef EditLayersHeightmapsTextureArray;
		FRDGTextureSRVRef EditLayersHeightmapsTextureArraySRV;

		// Temporary scratch texture array that stores the output (packed height only) of all (edit layer-merged) landscape components to render within a batch (one per landscape component)  
		//  Can be reused from one batch to another :
		FRDGTextureRef ScratchMergedHeightmapTextureArray;
		FRDGTextureSRVRef ScratchMergedHeightmapTextureArraySRV;

		// Temporary scratch texture array that stores the output (packed height only) of all (edit layer-merged) landscape components to render within a batch (one per landscape component), after stitching step is done:
		//  Can be reused from one batch to another :
		FRDGTextureRef ScratchStitchedHeightmapTextureArray;
		FRDGTextureSRVRef ScratchStitchedHeightmapTextureArraySRV;

		// Single structured buffer that will contain layer merge infos (FLandscapeEditLayerHeightmapMergeInfo): doesn't changed from one component to another :
		FRDGBufferRef EditLayersMergeInfosBuffer;
		FRDGBufferSRVRef EditLayersMergeInfosBufferSRV;
	};

	void PrepareLayersHeightmapsLocalMergeRDGResources(const FMergeInfo& InLocalMergeInfo, FRDGBuilder& GraphBuilder, FRDGResources& OutResources)
	{
		{
			int32 SizeZ = InLocalMergeInfo.MaxNumEditLayersTexturesToMerge;
			check(SizeZ > 0);
			// TODO [jonathan.bard] : change to PF_R8G8 once edit layers heightmaps are stored as such :
			FRDGTextureDesc Desc = FRDGTextureDesc::Create2DArray(InLocalMergeInfo.ComponentSizeVerts, PF_B8G8R8A8, FClearValueBinding::None, TexCreate_RenderTargetable | TexCreate_ShaderResource, 
				static_cast<uint16>(SizeZ), /*InNumMips = */1, /*InNumSamples = */1);
			OutResources.EditLayersHeightmapsTextureArray = GraphBuilder.CreateTexture(Desc, TEXT("LandscapeEditLayersHeightmapsTextureArray"));
			OutResources.EditLayersHeightmapsTextureArraySRV = GraphBuilder.CreateSRV(FRDGTextureSRVDesc::Create(OutResources.EditLayersHeightmapsTextureArray));
		}

		{
			int32 SizeZ = InLocalMergeInfo.MaxNumComponentsToRenderPerResolveComponentBatch;
			// We only need 2 channels since this only stores the (packed) height :
			FRDGTextureDesc Desc = FRDGTextureDesc::Create2DArray(InLocalMergeInfo.ComponentSizeVerts, PF_R8G8, FClearValueBinding::None, TexCreate_RenderTargetable | TexCreate_TargetArraySlicesIndependently | TexCreate_ShaderResource, 
				static_cast<uint16>(SizeZ), /*InNumMips = */1, /*InNumSamples = */1);
			// Create 2 texture arrays "Merged" and "Stitched" (ScratchMergedHeightmapTextureArray will be copied/merged into ScratchStitchedHeightmapTextureArray, slice by slice)
			OutResources.ScratchMergedHeightmapTextureArray = GraphBuilder.CreateTexture(Desc, TEXT("LandscapeEditLayersMergedHeightmapTextureArray"));
			OutResources.ScratchStitchedHeightmapTextureArray = GraphBuilder.CreateTexture(Desc, TEXT("LandscapeEditLayersStitchedHeightmapTextureArray"));
			OutResources.ScratchMergedHeightmapTextureArraySRV = GraphBuilder.CreateSRV(FRDGTextureSRVDesc::Create(OutResources.ScratchMergedHeightmapTextureArray));
			OutResources.ScratchStitchedHeightmapTextureArraySRV = GraphBuilder.CreateSRV(FRDGTextureSRVDesc::Create(OutResources.ScratchStitchedHeightmapTextureArray));
		}

		{
			// Upload layer merge info buffer once and for all since it's unchanged from one component to another : 
			TArray<FLandscapeEditLayerHeightmapMergeInfo, TInlineAllocator<16>> EditLayerMergeInfos;
			for (const FEditLayerInfo& EditLayerInfo : InLocalMergeInfo.VisibleEditLayerInfos)
			{
				FLandscapeEditLayerHeightmapMergeInfo& EditLayerMergeInfo = EditLayerMergeInfos.Emplace_GetRef();
				EditLayerMergeInfo.BlendMode = EditLayerInfo.BlendMode;
				EditLayerMergeInfo.Alpha = EditLayerInfo.Alpha;
			}
			OutResources.EditLayersMergeInfosBuffer = CreateStructuredBuffer(GraphBuilder, TEXT("LandscapeEditLayersMergeInfosBuffer"), EditLayerMergeInfos);
			OutResources.EditLayersMergeInfosBufferSRV = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(OutResources.EditLayersMergeInfosBuffer));
		}
	}

	// Gather all textures we will want to write into or read from in the render graph and output them in OutTrackedTextures:
	void GatherLayersHeightmapsLocalMergeRDGTextures(const FMergeInfo& InLocalMergeInfo, TMap<FTexture2DResource*, FLandscapeRDGTrackedTexture>& OutTrackedTextures)
	{
		// First pass, gather all textures we'll need for merging layers, i.e. the component edit layers' heightmaps : 
		for (const FComponentRenderInfo& ComponentRenderInfo : InLocalMergeInfo.ComponentToRenderInfos)
		{
			for (const FTexture2DResourceSubregion& LayerHeightmap : ComponentRenderInfo.VisibleLayerHeightmapTextures)
			{
				check(LayerHeightmap.Texture != nullptr);
				FLandscapeRDGTrackedTexture* LayerTrackedTexture = OutTrackedTextures.Find(LayerHeightmap.Texture);
				if (LayerTrackedTexture == nullptr)
				{
					LayerTrackedTexture = &(OutTrackedTextures.Add(LayerHeightmap.Texture, FLandscapeRDGTrackedTexture(LayerHeightmap.Texture)));
				}
			}
		}

		// Second pass, gather all textures we'll need to regenerate : 
		for (const FComponentResolveInfo& ComponentResolveInfo : InLocalMergeInfo.ComponentToResolveInfos)
		{
			check(ComponentResolveInfo.Heightmap.Texture != nullptr);
			FLandscapeRDGTrackedTexture* TrackedTexture = OutTrackedTextures.Find(ComponentResolveInfo.Heightmap.Texture);
			if (TrackedTexture == nullptr)
			{
				TrackedTexture = &(OutTrackedTextures.Add(ComponentResolveInfo.Heightmap.Texture, FLandscapeRDGTrackedTexture(ComponentResolveInfo.Heightmap.Texture)));
			}
			TrackedTexture->bNeedsScratch = true;
		}
	}

	void MergeEditLayersHeightmapsForBatch(const FComponentResolveBatchInfo& InComponentResolveBatchInfo, const FMergeInfo& InLocalMergeInfo, const TMap<FTexture2DResource*, FLandscapeRDGTrackedTexture>& InTrackedTextures, FRDGBuilder& GraphBuilder, FRDGResources& RDGResources)
	{
		RDG_EVENT_SCOPE(GraphBuilder, "Merge %d edit layers on %d components", InLocalMergeInfo.VisibleEditLayerInfos.Num(), InComponentResolveBatchInfo.ComponentToRenderInfoBitIndices.CountSetBits());

		// For each component to render, perform the edit layers merge and write the resulting heightmap :
		int32 IndexInBatch = 0;
		for (TConstSetBitIterator<> BitIt(InComponentResolveBatchInfo.ComponentToRenderInfoBitIndices); BitIt; ++BitIt, ++IndexInBatch)
		{
			int32 ComponentRenderInfoIndex = BitIt.GetIndex();
			const FComponentRenderInfo& ComponentRenderInfo = InLocalMergeInfo.ComponentToRenderInfos[ComponentRenderInfoIndex];
		
			RDG_EVENT_SCOPE(GraphBuilder, "Component %s", *ComponentRenderInfo.Name);

			// Prepare the texture array of layer heightmaps for this component : 
			int32 NumLayers = InLocalMergeInfo.VisibleEditLayerInfos.Num();
			for (int32 i = 0; i < NumLayers; ++i)
			{
				const FTexture2DResourceSubregion& LayerHeighmapSubregion = ComponentRenderInfo.VisibleLayerHeightmapTextures[i];

				const FLandscapeRDGTrackedTexture* TrackedTexture = InTrackedTextures.Find(LayerHeighmapSubregion.Texture);
				check(TrackedTexture != nullptr);

				// We need to copy the (portion of the) layer's texture to the texture array : 
				FRHICopyTextureInfo CopyTextureInfo;
				CopyTextureInfo.Size = FIntVector(LayerHeighmapSubregion.Subregion.Size().X, LayerHeighmapSubregion.Subregion.Size().Y, 0);
				CopyTextureInfo.DestSliceIndex = i;
				CopyTextureInfo.SourcePosition = FIntVector(LayerHeighmapSubregion.Subregion.Min.X, LayerHeighmapSubregion.Subregion.Min.Y, 0);

				AddCopyTexturePass(GraphBuilder, TrackedTexture->ExternalTextureRef, RDGResources.EditLayersHeightmapsTextureArray, CopyTextureInfo);
			}

			// Then, merge all heightmaps using the MergeEditLayers PS and write into a single slice in ScratchMergedHeightmapTextureArray: 
			{
				check(IndexInBatch < RDGResources.ScratchMergedHeightmapTextureArray->Desc.ArraySize);

				FLandscapeLayersHeightmapsMergeEditLayersPS::FParameters* MergeEditLayersPSParams = GraphBuilder.AllocParameters<FLandscapeLayersHeightmapsMergeEditLayersPS::FParameters>();
				MergeEditLayersPSParams->RenderTargets[0] = FRenderTargetBinding(RDGResources.ScratchMergedHeightmapTextureArray, ERenderTargetLoadAction::ENoAction, /*InMipIndex = */0, /*InArraySlice = */static_cast<int16>(IndexInBatch));
				MergeEditLayersPSParams->InNumEditLayers = NumLayers;
				MergeEditLayersPSParams->InEditLayersTextures = RDGResources.EditLayersHeightmapsTextureArraySRV;
				MergeEditLayersPSParams->InEditLayersMergeInfos = RDGResources.EditLayersMergeInfosBufferSRV;

				FLandscapeLayersHeightmapsMergeEditLayersPS::MergeEditLayers(GraphBuilder, MergeEditLayersPSParams, /*InTextureSize =*/InLocalMergeInfo.ComponentSizeVerts);
			}
		}
	}

	void StitchHeightmapsForBatch(const FComponentResolveBatchInfo& InComponentResolveBatchInfo, const FMergeInfo& InLocalMergeInfo, FRDGBuilder& GraphBuilder, FRDGResources& RDGResources)
	{
		RDG_EVENT_SCOPE(GraphBuilder, "Stitch %d components for batch %d", InComponentResolveBatchInfo.ComponentToRenderInfoBitIndices.CountSetBits(), InComponentResolveBatchInfo.BatchIndex);

		// For each component to render (i.e. including the neighbors of dirty components, that are also need to properly recompute normals), correct the heightmap 
		//  subregion (by stitching adjacent component borders) :
		int32 IndexInBatch = 0;
		for (TConstSetBitIterator<> BitIt(InComponentResolveBatchInfo.ComponentToRenderInfoBitIndices); BitIt; ++BitIt, ++IndexInBatch)
		{
			int32 ComponentRenderInfoIndex = BitIt.GetIndex();
			const FComponentRenderInfo& ComponentRenderInfo = InLocalMergeInfo.ComponentToRenderInfos[ComponentRenderInfoIndex];

			RDG_EVENT_SCOPE(GraphBuilder, "Component %s", *ComponentRenderInfo.Name);

			// Now, stitch the heightmap using the StitchHeightmapPS and output the result to a single slice in ScratchStitchedHeightmapTextureArray: 
			{
				FLandscapeLayersHeightmapsStitchHeightmapPS::FParameters* StitchHeightmapPSParams = GraphBuilder.AllocParameters<FLandscapeLayersHeightmapsStitchHeightmapPS::FParameters>();
				StitchHeightmapPSParams->RenderTargets[0] = FRenderTargetBinding(RDGResources.ScratchStitchedHeightmapTextureArray, ERenderTargetLoadAction::ENoAction, /*InMipIndex = */0, /*InArraySlice = */static_cast<int16>(IndexInBatch));
				StitchHeightmapPSParams->InSourceTextureSize = FUintVector2(InLocalMergeInfo.ComponentSizeVerts.X, InLocalMergeInfo.ComponentSizeVerts.Y);
				StitchHeightmapPSParams->InNumSubsections = InLocalMergeInfo.NumSubsections;
				StitchHeightmapPSParams->InSourceHeightmaps = RDGResources.ScratchMergedHeightmapTextureArraySRV;

				for (int32 NeighborIndex = 0; NeighborIndex < 9; ++NeighborIndex)
				{
					int32 NeighborComponentToRenderInfoIndex = ComponentRenderInfo.NeighborComponentToRenderInfoIndices[NeighborIndex];
					// Index of the neighbor component in this batch : allows to retrieve the proper slice in the source heightmap array :
					int32 NeighborIndexInBatch = INDEX_NONE;
					// The neighbor could be totally absent (index == INDEX_NONE) or it can be absent from the batch. That means that it's not actually relevant for this step 
					//  since the vertices that will be "invalid" won't be taken into account by the components we're actually trying to resolve :
					if ((NeighborComponentToRenderInfoIndex != INDEX_NONE) && InComponentResolveBatchInfo.ComponentToRenderInfoBitIndices[NeighborComponentToRenderInfoIndex])
					{
						// Components are rendered in order within the batch so the index of this component to render in the batch is == to how many components are there before it:
						NeighborIndexInBatch = InComponentResolveBatchInfo.ComponentToRenderInfoBitIndices.CountSetBits(0, NeighborComponentToRenderInfoIndex);
						check(NeighborIndexInBatch < RDGResources.ScratchMergedHeightmapTextureArray->Desc.ArraySize);
					}

					check((NeighborIndex != 4) || (NeighborIndexInBatch != INDEX_NONE)); // The central component (the one we finalize) should always be valid
					GET_SCALAR_ARRAY_ELEMENT(StitchHeightmapPSParams->InNeighborHeightmapIndices, NeighborIndex) = NeighborIndexInBatch;
				}

				FLandscapeLayersHeightmapsStitchHeightmapPS::StitchHeightmap(GraphBuilder, StitchHeightmapPSParams);
			}
		}
	}

	void FinalizeComponentsForBatch(const FComponentResolveBatchInfo& InComponentResolveBatchInfo, const FMergeInfo& InLocalMergeInfo, const TMap<FTexture2DResource*, FLandscapeRDGTrackedTexture>& InTrackedTextures, FRDGBuilder& GraphBuilder, FRDGResources& RDGResources)
	{
		RDG_EVENT_SCOPE(GraphBuilder, "Finalize %d components", InLocalMergeInfo.ComponentToResolveInfos.Num());

		// For each component to resolve (i.e. including the neighbors of dirty components, that also need to have their normals recomputed), finalize the heightmap subregion (i.e. compute the normals) :
		for (int32 ComponentResolveInfoIndex : InComponentResolveBatchInfo.ComponentToResolveInfoIndices)
		{
			const FComponentResolveInfo& ComponentResolveInfo = InLocalMergeInfo.ComponentToResolveInfos[ComponentResolveInfoIndex];

			RDG_EVENT_SCOPE(GraphBuilder, "Component %s", *ComponentResolveInfo.Name);

			const FLandscapeRDGTrackedTexture* TrackedTexture = InTrackedTextures.Find(ComponentResolveInfo.Heightmap.Texture);
			check((TrackedTexture != nullptr) && (TrackedTexture->ScratchTextureRef != nullptr));

			// Now, finalize the heightmap using the FinalizeHeighmap PS : 
			{
				FLandscapeLayersHeightmapsFinalizeHeightmapPS::FParameters* FinalizeHeightmapPSParams = GraphBuilder.AllocParameters<FLandscapeLayersHeightmapsFinalizeHeightmapPS::FParameters>();
				FinalizeHeightmapPSParams->RenderTargets[0] = FRenderTargetBinding(TrackedTexture->ScratchTextureRef, ERenderTargetLoadAction::ENoAction);
				FinalizeHeightmapPSParams->InSourceTextureSize = FUintVector2(InLocalMergeInfo.ComponentSizeVerts.X, InLocalMergeInfo.ComponentSizeVerts.Y);
				FinalizeHeightmapPSParams->InNumSubsections = InLocalMergeInfo.NumSubsections;
				FinalizeHeightmapPSParams->InSourceHeightmaps = RDGResources.ScratchStitchedHeightmapTextureArraySRV;
				FinalizeHeightmapPSParams->InDestinationTextureSubregion = FUintVector4(ComponentResolveInfo.Heightmap.Subregion.Min.X, ComponentResolveInfo.Heightmap.Subregion.Min.Y, ComponentResolveInfo.Heightmap.Subregion.Max.X, ComponentResolveInfo.Heightmap.Subregion.Max.Y);
				FinalizeHeightmapPSParams->InLandscapeGridScale = (FVector3f)InLocalMergeInfo.LandscapeGridScale;

				for (int32 NeighborIndex = 0; NeighborIndex < 9; ++NeighborIndex)
				{
					int32 NeighborComponentToRenderInfoIndex = ComponentResolveInfo.NeighborComponentToRenderInfoIndices[NeighborIndex];
					// Index of the neighbor component in this batch : allows to retrieve the proper slice in the source heightmap array :
					int32 NeighborIndexInBatch = INDEX_NONE;
					// The neighbor could be absent : 
					if (NeighborComponentToRenderInfoIndex != INDEX_NONE)
					{
						check(InComponentResolveBatchInfo.ComponentToRenderInfoBitIndices[NeighborComponentToRenderInfoIndex]);
						// Components are rendered in order within the batch so the index of this component to render in the batch is == to how many components are there before it:
						NeighborIndexInBatch = InComponentResolveBatchInfo.ComponentToRenderInfoBitIndices.CountSetBits(0, NeighborComponentToRenderInfoIndex);
						check(NeighborIndexInBatch < RDGResources.ScratchMergedHeightmapTextureArray->Desc.ArraySize);
					}

					check((NeighborIndex != 4) || (NeighborIndexInBatch != INDEX_NONE)); // The central component (the one we finalize) should always be valid
					GET_SCALAR_ARRAY_ELEMENT(FinalizeHeightmapPSParams->InNeighborHeightmapIndices, NeighborIndex) = NeighborIndexInBatch;
				}

				FLandscapeLayersHeightmapsFinalizeHeightmapPS::FinalizeHeightmap(GraphBuilder, FinalizeHeightmapPSParams);
			}
		}
	}

	void GenerateHeightmapMips(const FMergeInfo& InLocalMergeInfo, const TMap<FTexture2DResource*, FLandscapeRDGTrackedTexture>& InTrackedTextures, FRDGBuilder& GraphBuilder)
	{
		RDG_EVENT_SCOPE(GraphBuilder, "Generate mips for %d heightmaps", InLocalMergeInfo.TextureToResolveInfos.Num());

		// For each texture to resolve, simply regenerate the mips on the entire texture :
		for (const FTextureResolveInfo& TextureResolveInfo : InLocalMergeInfo.TextureToResolveInfos)
		{
			RDG_EVENT_SCOPE(GraphBuilder, "Texture %s", *TextureResolveInfo.Texture->GetTextureName().ToString());

			const FLandscapeRDGTrackedTexture* TrackedTexture = InTrackedTextures.Find(TextureResolveInfo.Texture);
			check((TrackedTexture != nullptr) && (TrackedTexture->ScratchTextureRef != nullptr));

			//check(TextureResolveInfo.NumMips == TrackedTexture->ScratchTextureRef->Desc.NumMips);
			//check(TrackedTexture->ScratchTextureMipsSRVRefs.Num() == TextureResolveInfo.NumMips);

			FIntPoint CurrentMipSubregionSize = InLocalMergeInfo.ComponentSizeVerts;
			for (int32 MipLevel = 1; MipLevel < TextureResolveInfo.NumMips; ++MipLevel)
			{
				CurrentMipSubregionSize.X >>= 1;
				CurrentMipSubregionSize.Y >>= 1;

				FLandscapeLayersHeightmapsGenerateMipsPS::FParameters* GenerateMipsPSParams = GraphBuilder.AllocParameters<FLandscapeLayersHeightmapsGenerateMipsPS::FParameters>();
				GenerateMipsPSParams->RenderTargets[0] = FRenderTargetBinding(TrackedTexture->ScratchTextureRef, ERenderTargetLoadAction::ENoAction, static_cast<uint8>(MipLevel));
				GenerateMipsPSParams->InCurrentMipSubregionSize = FUintVector2(CurrentMipSubregionSize.X, CurrentMipSubregionSize.Y);
				GenerateMipsPSParams->InNumSubsections = InLocalMergeInfo.NumSubsections;
				GenerateMipsPSParams->InSourceHeightmap = TrackedTexture->ScratchTextureMipsSRVRefs[MipLevel - 1];

				FLandscapeLayersHeightmapsGenerateMipsPS::GenerateSingleMip(GraphBuilder, GenerateMipsPSParams);
			}
		}
	}

	void CopyScratchToSourceHeightmaps(const FMergeInfo& InLocalMergeInfo, const TMap<FTexture2DResource*, FLandscapeRDGTrackedTexture>& InTrackedTextures, FRDGBuilder& GraphBuilder)
	{
		RDG_EVENT_SCOPE(GraphBuilder, "Copy %d scratch to source heightmaps", InLocalMergeInfo.ComponentToResolveInfos.Num());

		// For each texture to resolve, copy from scratch to final texture :
		for (const FTextureResolveInfo& TextureResolveInfo : InLocalMergeInfo.TextureToResolveInfos)
		{
			const FLandscapeRDGTrackedTexture* TrackedTexture = InTrackedTextures.Find(TextureResolveInfo.Texture);
			check((TrackedTexture != nullptr) && (TrackedTexture->ScratchTextureRef != nullptr) && (TrackedTexture->ExternalTextureRef != nullptr));

			FRHICopyTextureInfo CopyTextureInfo;
			// We want to copy all mips : 
			CopyTextureInfo.NumMips = TextureResolveInfo.NumMips;

			AddCopyTexturePass(GraphBuilder, TrackedTexture->ScratchTextureRef, TrackedTexture->ExternalTextureRef, CopyTextureInfo);
		}
	}

	FRDGTextureRef CreateAndClearEmptyTexture(const FMergeInfo& InLocalMergeInfo, FRDGBuilder& GraphBuilder)
	{
		// Convert the height value 0.0f to how it's stored in the texture : 
		const uint16 HeightValue = LandscapeDataAccess::GetTexHeight(0.0f);
		FLinearColor ClearHeightColor((float)((HeightValue - (HeightValue & 255)) >> 8) / 255.0f, (float)(HeightValue & 255) / 255.0f, 0.0f, 0.0f);

		// Even if we have heightmaps of different sizes to handle, we only need one empty heightmap to copy from (whose size is MaxHeightmapSize) :
		// TODO [jonathan.bard] : change to PF_R8G8 once edit layers heightmaps are stored as such :
		FRDGTextureDesc Desc = FRDGTextureDesc::Create2D(InLocalMergeInfo.MaxHeightmapSize, PF_B8G8R8A8, FClearValueBinding(ClearHeightColor), TexCreate_ShaderResource | TexCreate_RenderTargetable, static_cast<uint8>(InLocalMergeInfo.MaxHeightmapNumMips), /*InNumSamples = */1);
		FRDGTextureRef EmptyTexture = GraphBuilder.CreateTexture(Desc, TEXT("LandscapeEditLayersEmptyHeightmap"));

		FRDGTextureClearInfo ClearInfo;
		ClearInfo.NumMips = InLocalMergeInfo.MaxHeightmapNumMips;
		AddClearRenderTargetPass(GraphBuilder, EmptyTexture, ClearInfo);

		return EmptyTexture;
	}

	void ClearSourceHeightmaps(const FMergeInfo& InLocalMergeInfo, FRDGBuilder& GraphBuilder)
	{
		RDG_EVENT_SCOPE(GraphBuilder, "Clear %d source heightmaps", InLocalMergeInfo.ComponentToResolveInfos.Num());

		// We cannot clear heightmaps directly, since they are external, non-render targetable, textures so we need to copy from an empty source texture :
		FRDGTextureRef EmptyTexture = CreateAndClearEmptyTexture(InLocalMergeInfo, GraphBuilder);

		// For each texture to resolve, copy from empty to final texture :
		for (const FTextureResolveInfo& TextureResolveInfo : InLocalMergeInfo.TextureToResolveInfos)
		{
			// Register the output texture to the GraphBuilder so that we can copy to it: 
			FString* DebugName = GraphBuilder.AllocObject<FString>(TextureResolveInfo.Texture->GetTextureName().ToString());
			TRefCountPtr<IPooledRenderTarget> RenderTarget = CreateRenderTarget(TextureResolveInfo.Texture->TextureRHI, **DebugName);

			// Force tracking on the external texture, so that it can be copied to via CopyTexture within the graph : 
			FRDGTextureRef DestinationTexture = GraphBuilder.RegisterExternalTexture(RenderTarget);

			FRHICopyTextureInfo CopyTextureInfo;
			// We want to copy all mips : 
			CopyTextureInfo.NumMips = TextureResolveInfo.NumMips;
			// We need specify the size since the empty texture might be of higher size :
			check(EmptyTexture->Desc.GetSize().X >= TextureResolveInfo.TextureSize.X);
			CopyTextureInfo.Size = FIntVector(TextureResolveInfo.TextureSize.X, TextureResolveInfo.TextureSize.X, 0);

			AddCopyTexturePass(GraphBuilder, EmptyTexture, DestinationTexture, CopyTextureInfo);
		}
	}
}; // End namespace EditLayersHeightmapLocalMerge_RenderThread

void ALandscape::PrepareLayersHeightmapsLocalMergeRenderThreadData(const FUpdateLayersContentContext& InUpdateLayersContentContext, const FEditLayersHeightmapMergeParams& InMergeParams, EditLayersHeightmapLocalMerge_RenderThread::FMergeInfo& OutRenderThreadData)
{
	using namespace EditLayersHeightmapLocalMerge_RenderThread;
	using namespace UE::Landscape::Private;

	TRACE_CPUPROFILER_EVENT_SCOPE(LandscapeLayers_PrepareLayersHeightmapsLocalMergeRenderThreadData);

	ULandscapeInfo* Info = GetLandscapeInfo();
	check(Info != nullptr);

	// Number of vertices for each landscape component :
	int32 ComponentSizeVerts = (SubsectionSizeQuads + 1) * NumSubsections;
	OutRenderThreadData.ComponentSizeVerts = FIntPoint(ComponentSizeVerts, ComponentSizeVerts);
	OutRenderThreadData.LandscapeGridScale = GetRootComponent()->GetRelativeScale3D();
	OutRenderThreadData.NumSubsections = NumSubsections;

	// Prepare landscape edit layers data common to all landscape components: 
	OutRenderThreadData.VisibleEditLayerInfos.Reserve(LandscapeEditLayers.Num());
	for (const FLandscapeLayer& Layer : LandscapeEditLayers)
	{
		if (Layer.bVisible && !InMergeParams.bSkipBrush)
		{
			FEditLayerInfo& EditLayerInfo = OutRenderThreadData.VisibleEditLayerInfos.Emplace_GetRef();
			EditLayerInfo.BlendMode = LandscapeBlendModeToEditLayerBlendMode(Layer.BlendMode);
			EditLayerInfo.Alpha = Layer.HeightmapAlpha;
		}
	}

	const int32 NumComponentsToRender = InUpdateLayersContentContext.LandscapeComponentsHeightmapsToRender.Num();

	// Lookup table to retrieve, for a given rendered component, the index of its FComponentRenderInfo in ComponentToRenderInfos :
	TMap<ULandscapeComponent*, int32> ComponentToComponentRenderInfoIndex;

	// Prepare all per-landscape component render data : 
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(LandscapeLayers_PrepareHeightmapComponentRenderInfos);

		OutRenderThreadData.ComponentToRenderInfos.Reserve(InUpdateLayersContentContext.LandscapeComponentsHeightmapsToRender.Num());
		for (ULandscapeComponent* Component : InUpdateLayersContentContext.LandscapeComponentsHeightmapsToRender)
		{
			// Add a new component render info and set it up :
			FComponentRenderInfo& NewComponentRenderInfo = OutRenderThreadData.ComponentToRenderInfos.Emplace_GetRef();
			NewComponentRenderInfo.Name = Component->GetName();

			// Associate the component with its index in ComponentToRenderInfos :
			check(!ComponentToComponentRenderInfoIndex.Contains(Component));
			ComponentToComponentRenderInfoIndex.Add(Component, OutRenderThreadData.ComponentToRenderInfos.Num() - 1);

			UTexture2D* ComponentHeightmap = Component->GetHeightmap();
			FIntPoint TextureSize(ComponentHeightmap->Source.GetSizeX(), ComponentHeightmap->Source.GetSizeY());
			OutRenderThreadData.MaxHeightmapSize = TextureSize.ComponentMax(OutRenderThreadData.MaxHeightmapSize);
			OutRenderThreadData.MaxHeightmapNumMips = FMath::Max((int32)FMath::CeilLogTwo(TextureSize.GetMin()) + 1, OutRenderThreadData.MaxHeightmapNumMips);

			FIntPoint HeightmapOffset(static_cast<int32>(Component->HeightmapScaleBias.Z * TextureSize.X), static_cast<int32>(Component->HeightmapScaleBias.W * TextureSize.Y));
			// Effective area of the texture affecting this component (because of texture sharing) :
			const FIntRect ComponentTextureSubregion(HeightmapOffset, HeightmapOffset + OutRenderThreadData.ComponentSizeVerts);

			NewComponentRenderInfo.VisibleLayerHeightmapTextures.Reserve(OutRenderThreadData.VisibleEditLayerInfos.Num());
			for (const FLandscapeLayer& Layer : LandscapeEditLayers)
			{
				if (Layer.bVisible && !InMergeParams.bSkipBrush)
				{
					if (FLandscapeLayerComponentData* ComponentLayerData = Component->GetLayerData(Layer.Guid))
					{
						if (UTexture2D* LayerHeightmap = ComponentLayerData->HeightmapData.Texture.Get())
						{
							NewComponentRenderInfo.VisibleLayerHeightmapTextures.Add(FTexture2DResourceSubregion(LayerHeightmap->GetResource()->GetTexture2DResource(), ComponentTextureSubregion));
						}
					}
				}
			}
		}

		// Now that all landscape components have been registered, identify the (valid) neighbors for each : 
		for (int32 ComponentToRenderIndex = 0; ComponentToRenderIndex < NumComponentsToRender; ++ComponentToRenderIndex)
		{
			ULandscapeComponent* Component = InUpdateLayersContentContext.LandscapeComponentsHeightmapsToRender[ComponentToRenderIndex];
			FComponentRenderInfo& ComponentRenderInfo = OutRenderThreadData.ComponentToRenderInfos[ComponentToRenderIndex];

			// Gather neighboring component infos : 
			TStaticArray<ULandscapeComponent*, 9> NeighborComponents;
			Component->GetLandscapeComponentNeighbors3x3(NeighborComponents);
			for (int32 NeighborIndex = 0; NeighborIndex < 9; ++NeighborIndex)
			{
				int32 NeighborComponentRenderInfoIndex = INDEX_NONE;
				if (ULandscapeComponent* NeighborComponent = NeighborComponents[NeighborIndex])
				{
					if (int32* NeighborComponentRenderInfoIndexPtr = ComponentToComponentRenderInfoIndex.Find(NeighborComponent))
					{
						NeighborComponentRenderInfoIndex = *NeighborComponentRenderInfoIndexPtr;
					}
				}
				ComponentRenderInfo.NeighborComponentToRenderInfoIndices[NeighborIndex] = NeighborComponentRenderInfoIndex;
			}
		}
	}

	// List of UTexture2D that we need to kick off readbacks for :
	TArray<UTexture2D*> TexturesNeedingReadback;

	// Prepare per-landscape component resolve data : 
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(LandscapeLayers_PrepareHeightmapComponentResolveInfos);
		OutRenderThreadData.ComponentToResolveInfos.Reserve(InUpdateLayersContentContext.LandscapeComponentsHeightmapsToResolve.Num());
		for (ULandscapeComponent* Component : InUpdateLayersContentContext.LandscapeComponentsHeightmapsToResolve)
		{
			UTexture2D* ComponentHeightmap = Component->GetHeightmap();
			const int32 HeightmapOffsetX = static_cast<int32>(Component->HeightmapScaleBias.Z * ComponentHeightmap->Source.GetSizeX());
			const int32 HeightmapOffsetY = static_cast<int32>(Component->HeightmapScaleBias.W * ComponentHeightmap->Source.GetSizeY());
			// Effective area of the texture affecting this component (because of texture sharing) :
			const FIntRect ComponentTextureSubregion(FIntPoint(HeightmapOffsetX, HeightmapOffsetY), FIntPoint(HeightmapOffsetX, HeightmapOffsetY) + OutRenderThreadData.ComponentSizeVerts);

			FComponentResolveInfo& NewComponentResolveInfo = OutRenderThreadData.ComponentToResolveInfos.Add_GetRef(FComponentResolveInfo(NumComponentsToRender));
			NewComponentResolveInfo.ComponentToResolveInfoIndex = OutRenderThreadData.ComponentToResolveInfos.Num() - 1;
			NewComponentResolveInfo.Name = Component->GetName();
			NewComponentResolveInfo.Heightmap = FTexture2DResourceSubregion(ComponentHeightmap->GetResource()->GetTexture2DResource(), ComponentTextureSubregion);

			// Gather neighboring component infos : 
			TStaticArray<ULandscapeComponent*, 9> NeighborComponents;
			Component->GetLandscapeComponentNeighbors3x3(NeighborComponents);
			for (int32 NeighborIndex = 0; NeighborIndex < 9; ++NeighborIndex)
			{
				int32 NeighborComponentRenderInfoIndex = INDEX_NONE;
				if (ULandscapeComponent* NeighborComponent = NeighborComponents[NeighborIndex])
				{
					if (int32* NeighborComponentRenderInfoIndexPtr = ComponentToComponentRenderInfoIndex.Find(NeighborComponent))
					{
						NeighborComponentRenderInfoIndex = *NeighborComponentRenderInfoIndexPtr;
					}
				}
				NewComponentResolveInfo.SetNeighborRenderInfo(NeighborIndex, NeighborComponentRenderInfoIndex);
			}

			FTextureResolveInfo* TextureResolveInfo = OutRenderThreadData.TextureToResolveInfos.FindByPredicate(
				[TextureToResolve = NewComponentResolveInfo.Heightmap.Texture](const FTextureResolveInfo& TextureResolveInfo) { return TextureResolveInfo.Texture == TextureToResolve; });
			if (TextureResolveInfo == nullptr)
			{
				ALandscapeProxy* Proxy = Component->GetLandscapeProxy();
				FLandscapeEditLayerReadback** CPUReadback = Proxy->HeightmapsCPUReadback.Find(ComponentHeightmap);
				check((CPUReadback != nullptr) && (*CPUReadback != nullptr));

				FTextureResolveInfo NewTextureResolveInfo;
				NewTextureResolveInfo.TextureSize = FIntPoint(ComponentHeightmap->Source.GetSizeX(), ComponentHeightmap->Source.GetSizeY());
				NewTextureResolveInfo.NumMips = (int32)FMath::CeilLogTwo(NewTextureResolveInfo.TextureSize.GetMin()) + 1;
				NewTextureResolveInfo.Texture = NewComponentResolveInfo.Heightmap.Texture;
				NewTextureResolveInfo.CPUReadback = *CPUReadback;
				OutRenderThreadData.TextureToResolveInfos.Add(NewTextureResolveInfo);

				check(!TexturesNeedingReadback.Contains(ComponentHeightmap));
				TexturesNeedingReadback.Add(ComponentHeightmap);
			}
		}
	}

	// Prepare the texture resolve batches: 
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(LandscapeLayers_PrepareHeightmapComponentResolveInfoBatches);

		int32 MaxComponentsPerResolveBatch = CVarLandscapeEditLayersMaxComponentsPerHeightmapResolveBatch.GetValueOnGameThread();

		// Copy the component infos because TextureToResolveInfos indices need to remain stable at this point :
		TArray<FComponentResolveInfo> RemainingComponentToResolveInfos = OutRenderThreadData.ComponentToResolveInfos;
		if (!RemainingComponentToResolveInfos.IsEmpty())
		{
			TBitArray<TInlineAllocator<1>> TempBitArray;
			TempBitArray.Reserve(OutRenderThreadData.ComponentToResolveInfos.Num());

			while (!RemainingComponentToResolveInfos.IsEmpty())
			{
				const FComponentResolveInfo& ComponentResolveInfo = RemainingComponentToResolveInfos.Pop(EAllowShrinking::No);

				int32 BestBatchIndex = INDEX_NONE;
				int32 MinNumComponents = MAX_int32;

				// Iterate through all all batches and try to find which would be able to accept it and amongst those, which it would share the most components to render with:
				int32 NumBatches = OutRenderThreadData.ComponentResolveBatchInfos.Num();
				for (int32 BatchIndex = 0; BatchIndex < NumBatches; ++BatchIndex)
				{
					const FComponentResolveBatchInfo& Batch = OutRenderThreadData.ComponentResolveBatchInfos[BatchIndex];
					TempBitArray = TBitArray<>::BitwiseOR(Batch.ComponentToRenderInfoBitIndices, ComponentResolveInfo.NeighborComponentToRenderInfoBitIndices, EBitwiseOperatorFlags::MinSize);

					// If after adding its components, the batch still has less than MaxComponentsPerResolveBatch, it can accept it
					int32 NumComponentsAfter = TempBitArray.CountSetBits();
					if (NumComponentsAfter <= MaxComponentsPerResolveBatch)
					{
						// Is this the best candidate so far?
						if (NumComponentsAfter < MinNumComponents)
						{
							BestBatchIndex = BatchIndex;
							MinNumComponents = NumComponentsAfter;
						}

						// If the number of components after addition of this texture is unchanged, it's a perfect match, we won't ever find a better batch so just stop there for this texture:
						if (NumComponentsAfter == Batch.ComponentToRenderInfoBitIndices.CountSetBits())
						{
							break;
						}
					}
				}

				// If we have found a batch, just add the texture to it, otherwise, add a new batch:
				FComponentResolveBatchInfo& SelectedBatch = (BestBatchIndex != INDEX_NONE) ? OutRenderThreadData.ComponentResolveBatchInfos[BestBatchIndex]
					: OutRenderThreadData.ComponentResolveBatchInfos.Add_GetRef(FComponentResolveBatchInfo(OutRenderThreadData.ComponentToRenderInfos.Num(), /*InBatchIndex = */OutRenderThreadData.ComponentResolveBatchInfos.Num()));

				SelectedBatch.AddComponent(ComponentResolveInfo);
				check(SelectedBatch.ComponentToRenderInfoBitIndices.CountSetBits() <= MaxComponentsPerResolveBatch);

				// Keep track of the maximum number of slices in the scratch texture arrays we'll need for any given batch :
				OutRenderThreadData.MaxNumComponentsToRenderPerResolveComponentBatch = FMath::Max(SelectedBatch.ComponentToRenderInfoBitIndices.CountSetBits(), OutRenderThreadData.MaxNumComponentsToRenderPerResolveComponentBatch);
			}
		}
	}

	// Finalize :
	{
		// Prepare the UTexture2D readbacks we'll need to perform :
		OutRenderThreadData.DeferredCopyReadbackTextures = PrepareLandscapeLayersCopyReadbackTextureParams(InUpdateLayersContentContext.MapHelper, TexturesNeedingReadback, /*bWeightmaps = */false);

		// We'll only ever need this amount of edit layers textures for any MergeEditLayers operation :
		OutRenderThreadData.MaxNumEditLayersTexturesToMerge = OutRenderThreadData.VisibleEditLayerInfos.Num();
	}
}

int32 ALandscape::PerformLayersHeightmapsLocalMerge(const FUpdateLayersContentContext& InUpdateLayersContentContext, const FEditLayersHeightmapMergeParams& InMergeParams)
{
	using namespace EditLayersHeightmapLocalMerge_RenderThread;

	TRACE_CPUPROFILER_EVENT_SCOPE(LandscapeLayers_PerformLayersWeightmapsLocalMerge);

	FMergeInfo RenderThreadData;
	PrepareLayersHeightmapsLocalMergeRenderThreadData(InUpdateLayersContentContext, InMergeParams, RenderThreadData);
	
	ENQUEUE_RENDER_COMMAND(PerformLayersHeightmapsLocalMerge)([RenderThreadData](FRHICommandListImmediate& RHICmdList)
	{
		FRDGBuilder GraphBuilder(RHICmdList, RDG_EVENT_NAME("PerformLayersHeightmapsLocalMerge"));

		if (RenderThreadData.NeedsMerge())
		{
			// Prepare the GPU resources we will use during the local merge : 
			FRDGResources RDGResources;
			PrepareLayersHeightmapsLocalMergeRDGResources(RenderThreadData, GraphBuilder, RDGResources);

			// Get a list of all external textures (heightmaps) we will manipulate during the local merge : 
			TMap<FTexture2DResource*, FLandscapeRDGTrackedTexture> TrackedTextures;
			GatherLayersHeightmapsLocalMergeRDGTextures(RenderThreadData, TrackedTextures);

			// Start tracking those in the render graph :
			TrackLandscapeRDGTextures(GraphBuilder, TrackedTextures);

			// Process the components batch by batch in order to avoid over-allocating temporary textures :
			for (const FComponentResolveBatchInfo& ComponentResolveBatchInfo : RenderThreadData.ComponentResolveBatchInfos)
			{
				RDG_EVENT_SCOPE(GraphBuilder, "Process batch %d", ComponentResolveBatchInfo.BatchIndex);

				// Perform all edit layers merges, for all components to render in that batch :
				MergeEditLayersHeightmapsForBatch(ComponentResolveBatchInfo, RenderThreadData, TrackedTextures, GraphBuilder, RDGResources);

				// Correct borders of all rendered components so that they're all stitched together :
				StitchHeightmapsForBatch(ComponentResolveBatchInfo, RenderThreadData, GraphBuilder, RDGResources);

				// Finalize (compute normals) of each component to resolve :
				FinalizeComponentsForBatch(ComponentResolveBatchInfo, RenderThreadData, TrackedTextures, GraphBuilder, RDGResources);
			}

			// Generate the mips on the entire heightmaps : 
			GenerateHeightmapMips(RenderThreadData, TrackedTextures, GraphBuilder);

			// Finally, we can put those scratch textures to good usage and update our actual heightmaps : 
			CopyScratchToSourceHeightmaps(RenderThreadData, TrackedTextures, GraphBuilder);
		}
		else
		{
			// When there's nothing to do, we still have the obligation to output empty heightmaps :
			ClearSourceHeightmaps(RenderThreadData, GraphBuilder);
		}

		GraphBuilder.Execute();

	});

	ExecuteCopyToReadbackTexture(RenderThreadData.DeferredCopyReadbackTextures);

	return InMergeParams.HeightmapUpdateModes;
}

TArray<UE::Landscape::EditLayers::FEditLayerRendererState> ALandscape::GetEditLayerRendererStates(const ULandscapeInfo* InLandscapeInfo, bool bInSkipBrush)
{
	using namespace UE::Landscape::EditLayers;

	TArray<FEditLayerRendererState> RendererStates;
	for (FLandscapeLayer& Layer : LandscapeEditLayers)
	{
		RendererStates.Append(Layer.GetEditLayerRendererStates(InLandscapeInfo, bInSkipBrush));
	}
	return RendererStates;
}

TArray<UE::Landscape::EditLayers::FEditLayerRendererState> FLandscapeLayer::GetEditLayerRendererStates(const ULandscapeInfo* InLandscapeInfo, bool bInSkipBrush)
{
	using namespace UE::Landscape::EditLayers;

	// Gather all elements that can render some edit layers data, ordered: 
	TArray<FEditLayerRendererState> RendererStates;
	RendererStates.Reserve(1 + Brushes.Num());

	check(EditLayer != nullptr);
	// The edit layer itself might be a renderer:
	if (ILandscapeEditLayerRenderer* AsRenderer = Cast<ILandscapeEditLayerRenderer>(EditLayer))
	{
		RendererStates.Emplace(AsRenderer, InLandscapeInfo);
	}

	// The layer can also be a renderer provider
	RendererStates.Append(EditLayer->GetEditLayerRendererStates(InLandscapeInfo, bInSkipBrush));

	for (FLandscapeLayerBrush& Brush : Brushes)
	{
		RendererStates.Append(Brush.GetEditLayerRendererStates(InLandscapeInfo, bInSkipBrush));
	}
	return RendererStates;
}

TArray<UE::Landscape::EditLayers::FEditLayerRendererState> FLandscapeLayerBrush::GetEditLayerRendererStates(const ULandscapeInfo* InLandscapeInfo, bool bInSkipBrush)
{
	using namespace UE::Landscape::EditLayers;

	// Gather all elements that can render some edit layers data, ordered: 
	if (BlueprintBrush == nullptr)
	{
		return {};
	}

	return BlueprintBrush->GetEditLayerRendererStates(InLandscapeInfo, bInSkipBrush);
}

void ULandscapeEditLayerPersistent::GetRendererStateInfo(const ULandscapeInfo* InLandscapeInfo, 
	UE::Landscape::EditLayers::FEditLayerTargetTypeState& OutSupportedTargetTypeState, UE::Landscape::EditLayers::FEditLayerTargetTypeState& OutEnabledTargetTypeState, TArray<TSet<FName>>& OutRenderGroups) const
{
	using namespace UE::Landscape::EditLayers;

	const FLandscapeLayer* LayerStruct = GetOwningLayer();
	check(LayerStruct != nullptr);

	// A layer can support all target types :
	OutSupportedTargetTypeState.SetTargetTypeMask(ELandscapeToolTargetTypeFlags::All);
	// Add an entry for each weightmap and consider them supported because there's nothing that prevents a given edit layer to write on a given weightmap layer :
	Algo::ForEach(InLandscapeInfo->Layers, [&OutSupportedTargetTypeState] (const FLandscapeInfoLayerSettings& InInfoLayerSettings) { OutSupportedTargetTypeState.AddWeightmap(InInfoLayerSettings.GetLayerName()); });

	// Compute the default state of each target type : 
	ELandscapeToolTargetTypeFlags EnabledTargetTypeMask = ELandscapeToolTargetTypeFlags::None;
	if (LayerStruct->bVisible)
	{
		if (LayerStruct->HeightmapAlpha > 0.0f)
		{
			EnabledTargetTypeMask |= ELandscapeToolTargetTypeFlags::Heightmap;
		}
		if (LayerStruct->WeightmapAlpha > 0.0f)
		{
			EnabledTargetTypeMask |= ELandscapeToolTargetTypeFlags::Weightmap | ELandscapeToolTargetTypeFlags::Visibility;
		}
	}
	OutEnabledTargetTypeState = FEditLayerTargetTypeState(EnabledTargetTypeMask, MakeArrayView(OutSupportedTargetTypeState.GetActiveWeightmaps()));
}

TArray<UE::Landscape::EditLayers::FEditLayerRenderItem> ULandscapeEditLayerPersistent::GetRenderItems(const ULandscapeInfo* InLandscapeInfo) const
{
	using namespace UE::Landscape::EditLayers;

	const FLandscapeLayer* LayerStruct = GetOwningLayer();
	check(LayerStruct != nullptr);

	TArray<FEditLayerRenderItem> RenderItems;
	RenderItems.Reserve(InLandscapeInfo->XYtoComponentMap.Num() + 1);

	// Heightmaps : We only need a single render item for heightmaps, because heightmaps are always present :
	{
		// The layer doesn't need more than the component itself to render properly:
		FInputWorldArea InputWorldArea(FInputWorldArea::CreateLocalComponent());
		// The layer only writes into the component itself (i.e. it renders to the area that it's currently being asked to render to):
		FOutputWorldArea OutputWorldArea(FOutputWorldArea::CreateLocalComponent());
		RenderItems.Emplace(FEditLayerTargetTypeState(ELandscapeToolTargetTypeFlags::Heightmap), InputWorldArea, OutputWorldArea, /*bModifyExistingWeightmapsOnly = */false);
	}

	// Weightmaps : add one render item per component in order to be able to indicate exactly which weightmap is needed for each of them. This avoids pre-allocating weightmaps on the merged result where we know we won't write a weightmap : 
	{
		TArray<ULandscapeComponent*> AllLandscapeComponents;
		InLandscapeInfo->XYtoComponentMap.GenerateValueArray(AllLandscapeComponents);

		for (ULandscapeComponent* Component : AllLandscapeComponents)
		{
			const FIntPoint ComponentKey = Component->GetComponentKey();

			if (FLandscapeLayerComponentData* LayerData = Component->GetLayerData(LayerStruct->Guid))
			{
				TArray<FName, TInlineAllocator<16>> ComponentWeightmaps;
				FEditLayerTargetTypeState OutputTargetTypeState;

				// Iterate through all allocated weightmaps in order to find which one we will really write to : 
				for (const FWeightmapLayerAllocationInfo& LayerAllocationInfo : LayerData->WeightmapData.LayerAllocations)
				{
					if (LayerAllocationInfo.IsAllocated())
					{
						if (LayerAllocationInfo.LayerInfo == ALandscapeProxy::VisibilityLayer)
						{
							OutputTargetTypeState.AddTargetTypeMask(ELandscapeToolTargetTypeFlags::Visibility);
						}
						else
						{
							OutputTargetTypeState.AddTargetTypeMask(ELandscapeToolTargetTypeFlags::Weightmap);
						}
						OutputTargetTypeState.AddWeightmap(LayerAllocationInfo.GetLayerName());
					}
				}

				if (OutputTargetTypeState.GetTargetTypeMask() != ELandscapeToolTargetTypeFlags::None)
				{
					// The layer doesn't need more than the component itself to render properly:
					FInputWorldArea InputWorldArea(FInputWorldArea::CreateSpecificComponent(ComponentKey));
					// The layer only writes into the component itself (i.e. it renders to the area that it's currently being asked to render to):
					FOutputWorldArea OutputWorldArea(FOutputWorldArea::CreateSpecificComponent(ComponentKey));
					RenderItems.Emplace(OutputTargetTypeState, InputWorldArea, OutputWorldArea, /*bModifyExistingWeightmapsOnly = */false);
				}
			}
		}
	}
	return RenderItems;
}

void ULandscapeEditLayerPersistent::RenderLayer(ILandscapeEditLayerRenderer::FRenderParams& InRenderParams)
{
	using namespace UE::Landscape;
	using namespace UE::Landscape::Private;
	using namespace UE::Landscape::EditLayers;

	check(!InRenderParams.SortedComponentMergeRenderInfos.IsEmpty());

	ULandscapeInfo* Info = InRenderParams.MergeRenderContext->GetLandscape()->GetLandscapeInfo();
	check(Info != nullptr);
	ALandscape* Landscape = InRenderParams.MergeRenderContext->GetLandscape();
	FSceneInterface* SceneInterface = Landscape->GetWorld()->Scene;
	ULandscapeScratchRenderTarget* WriteRT = nullptr;

	const FLandscapeLayer* LayerStruct = GetOwningLayer();
	check(LayerStruct != nullptr);

	// Don't do anything with the blend render targets just yet : if this layer has nothing to render at all (e.g. no weightmap on the rendered area), that will save some processing :
	auto LazyInitializeBlendRenderTargets = [MergeRenderContext = InRenderParams.MergeRenderContext, &WriteRT]
	{
		if (WriteRT == nullptr)
		{
			MergeRenderContext->CycleBlendRenderTargets();
			WriteRT = MergeRenderContext->GetBlendRenderTargetWrite();

			// We will write to the RT using a (bunch of) PS : 
			WriteRT->Clear();
			WriteRT->TransitionTo(ERHIAccess::RTV);
		}
	};

	const int32 NumTargetLayers = InRenderParams.RenderGroupTargetLayerNames.Num();

	// The first step is to copy all of the necessary components' textures to WriteRT and remove the duplicate borders when doing so (subsection by subsection). This is done with a 
	//  "copy from multiple sources" shader instead of several texture copies, in order to reduce the amount of copy texture commands, which can have a big impact on render-thread performance
	//  for large landscapes. 

	// For each subsection, we'll add a quad to render, using the following struct :
	bool bUseSwizzleMask = false;
	struct FCopyQuadParams
	{
		// Texture to read from :
		FTextureResource* SourceTextureResource = nullptr;
		// Texture region to read from :
		FIntRect SourceRect;
		// Texture region to write to :
		FIntRect DestinationRect;
		// In case the destination is a texture array (weightmaps), this is the slice index to write to :
		int32 DestinationArrayIndex = 0;
		// Weightmaps are packed in the source texture. This specifies which channel to read from in the source texture for this quad :
		uint8 ChannelSwizzleMask = RGBAToRGBASwizzleMask;
	};
	TArray<FCopyQuadParams> CopyQuadsParams;
	CopyQuadsParams.Reserve(InRenderParams.SortedComponentMergeRenderInfos.Num() * NumTargetLayers * Landscape->NumSubsections * Landscape->NumSubsections);

	// This is kinda hacky, but we since we reuse AddRasterizeToRectsPass and it passes the source texture's sample coordinates from VS to PS via the UVs, dividing the source rect by a single TextureSize, 
	//  we correct each quad's source rect by a common scale factor so that it samples at the right location in each source texture: 
	const FVector2D CommonSourceTextureSize(InRenderParams.MergeRenderContext->GetMaxNeededResolution());
	const int32 SubsectionSizeVerts = (Landscape->SubsectionSizeQuads + 1);

	// Build the full list of quads to copy :
	for (const FComponentMergeRenderInfo& ComponentMergeRenderInfo : InRenderParams.SortedComponentMergeRenderInfos)
	{
		for (int32 TargetLayerIndex = 0; TargetLayerIndex < NumTargetLayers; ++TargetLayerIndex)
		{
			const FName TargetLayerName = InRenderParams.RenderGroupTargetLayerNames[TargetLayerIndex];
			UTexture2D* SourceTexture = nullptr;
			FVector2D SourceTextureBias(ForceInit);
			// Copy all channels by default :
			uint8 CopyTextureSwizzleMask = RGBAToRGBASwizzleMask;

			if (InRenderParams.MergeRenderContext->IsHeightmapMerge())
			{
				SourceTexture = ComponentMergeRenderInfo.Component->GetHeightmap(LayerStruct->Guid);
				SourceTextureBias = FVector2D(ComponentMergeRenderInfo.Component->HeightmapScaleBias.Z, ComponentMergeRenderInfo.Component->HeightmapScaleBias.W);
			}
			else 
			{
				const TArray<UTexture2D*>& WeightmapTextures = ComponentMergeRenderInfo.Component->GetWeightmapTextures(LayerStruct->Guid);
				const TArray<FWeightmapLayerAllocationInfo>& AllocInfos = ComponentMergeRenderInfo.Component->GetWeightmapLayerAllocations(LayerStruct->Guid);
				const FWeightmapLayerAllocationInfo* AllocInfo = AllocInfos.FindByPredicate([TargetLayerName](const FWeightmapLayerAllocationInfo& InAllocInfo) { return InAllocInfo.IsAllocated() && (InAllocInfo.GetLayerName() == TargetLayerName);});
				if (AllocInfo != nullptr)
				{
					SourceTexture = WeightmapTextures[AllocInfo->WeightmapTextureIndex];
					check(SourceTexture != nullptr);
					// Note : don't use WeightmapScaleBias here, it has a different meaning than HeightmapScaleBias (very conveniently!) : this is compensated by the FloorToInt32 later on, 
					//  but still, let's set this to zero here and use the fact that there's no texture sharing on weightmaps : 
					SourceTextureBias = FVector2D::ZeroVector;
					// Swizzle the appropriate source channel into the R component (as the target texture array's format is R8) : 
					CopyTextureSwizzleMask = BuildChannelSwizzleMask(static_cast<ERGBAChannel>(AllocInfo->WeightmapTextureChannel));
					bUseSwizzleMask = true;
				}
			}

			if (SourceTexture != nullptr)
			{
				// We know we'll render something now so it's time we initialized our render target : 
				LazyInitializeBlendRenderTargets();
				check(WriteRT != nullptr);

				FTextureResource* SourceTextureResource = SourceTexture->GetResource();
				checkf(!SourceTexture->IsCompiling(), TEXT("All mips must have been loaded prior to using this function (%s)"), *SourceTexture->GetName());
				// We get the overall texture size via the resource instead of direct GetSizeX/Y calls because the latter is unreliable while the texture is being built.
				const FVector2D SourceTextureSize(SourceTextureResource->GetSizeX(), SourceTextureResource->GetSizeY());
				const FIntPoint SourceTextureOffset(FMath::FloorToInt32(SourceTextureBias.X * SourceTextureSize.X), FMath::FloorToInt32(SourceTextureBias.Y * SourceTextureSize.Y));
				
				auto SourceTexturePixelCoordinatesToQuadCoords = [&SourceTextureSize, &CommonSourceTextureSize](const FIntPoint& InSourceCoords)
				{ 
					FVector2D Result = FVector2D(InSourceCoords) / SourceTextureSize * CommonSourceTextureSize;
					return FIntPoint(FMath::FloorToInt32(Result.X), FMath::FloorToInt32(Result.Y));
				};

				// Fill that render target subsection by subsection, in order to bypass the redundant columns/lines on the subsection edges:
				for (int32 SubsectionY = 0; SubsectionY < Landscape->NumSubsections; ++SubsectionY)
				{
					for (int32 SubsectionX = 0; SubsectionX < Landscape->NumSubsections; ++SubsectionX)
					{
						const FIntPoint SubsectionKey(SubsectionX, SubsectionY);
						const FIntPoint SourcePosition = SourceTextureOffset + SubsectionKey * SubsectionSizeVerts;
						const FIntPoint DestinationPosition = ComponentMergeRenderInfo.ComponentRegionInRenderArea.Min + SubsectionKey * ComponentMergeRenderInfo.Component->SubsectionSizeQuads;

						FCopyQuadParams& QuadParams = CopyQuadsParams.Emplace_GetRef();
						QuadParams.SourceTextureResource = SourceTextureResource;
						QuadParams.SourceRect = FIntRect(SourceTexturePixelCoordinatesToQuadCoords(SourcePosition), SourceTexturePixelCoordinatesToQuadCoords(SourcePosition + FIntPoint(SubsectionSizeVerts, SubsectionSizeVerts)));
						QuadParams.DestinationRect = FIntRect(DestinationPosition, DestinationPosition + FIntPoint(SubsectionSizeVerts, SubsectionSizeVerts));
						QuadParams.DestinationArrayIndex = TargetLayerIndex;
						QuadParams.ChannelSwizzleMask = CopyTextureSwizzleMask;
					}
				}
			}
		}
	}

	// Sort the list of quads by output texture first, then by source texture, because we'll coalesce several texture copies into the same pass (one output, multiple inputs)
	CopyQuadsParams.Sort([](const FCopyQuadParams& InLHS, const FCopyQuadParams& InRHS)
	{
		if (InLHS.DestinationArrayIndex == InRHS.DestinationArrayIndex)
		{
			if (InLHS.SourceTextureResource == InRHS.SourceTextureResource)
			{
				return (InLHS.SourceRect.Min.Y == InRHS.SourceRect.Min.Y) 
					? (InLHS.SourceRect.Min.X < InRHS.SourceRect.Min.X)
					: (InLHS.SourceRect.Min.Y < InRHS.SourceRect.Min.Y);
			}
			return (InLHS.SourceTextureResource < InRHS.SourceTextureResource);
		}

		return (InLHS.DestinationArrayIndex < InRHS.DestinationArrayIndex);
	});

	// Now process this list of quads and prepare as many passes as necessary for performing all the copies :
	if (!CopyQuadsParams.IsEmpty())
	{
		ENQUEUE_RENDER_COMMAND(LandscapeLayers_Cmd_CopySourcesToTargetLayer)(
			[ CopyQuadsParams = MoveTemp(CopyQuadsParams)
			, OutputResource = WriteRT->GetRenderTarget()->GetResource()
			, OutputResourceName = WriteRT->GetDebugName()
			, SceneInterface
			, CommonSourceTextureSize
			, bUseSwizzleMask] (FRHICommandListImmediate& InRHICmdList)
		{
			SCOPED_DRAW_EVENTF(InRHICmdList, LandscapeLayers, TEXT("CopyEditLayer -> %s"), OutputResourceName);
			FRDGBuilder GraphBuilder(InRHICmdList, RDG_EVENT_NAME("CopySourcesToTargetLayer"));

			FRDGTextureSRVRef BlackDummySRVRef = GraphBuilder.CreateSRV(FRDGTextureSRVDesc::Create(GSystemTextures.GetBlackDummy(GraphBuilder)));

			FCopyQuadsMultiSourcePS::FPermutationDomain PermutationVector;
			PermutationVector.Set< FCopyQuadsMultiSourcePS::FUseSwizleMask >(bUseSwizzleMask);
			FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(GMaxRHIFeatureLevel);
			TShaderRef<FCopyQuadsMultiSourcePS> PixelShader = ShaderMap->GetShader<FCopyQuadsMultiSourcePS>(PermutationVector);

			// TODO [jonathan.bard] this is just to avoid a RHI validation error for unoptimized shaders... once validation is made to not issue those errors, we can remove this
			// Create a SceneView to please the shader bindings, but it's unused in practice 
			FSceneViewFamilyContext ViewFamily(FSceneViewFamily::ConstructionValues(nullptr, SceneInterface, FEngineShowFlags(ESFIM_Game)).SetTime(FGameTime::GetTimeSinceAppStart()));
			FSceneViewInitOptions ViewInitOptions;
			ViewInitOptions.ViewFamily = &ViewFamily;
			GetRendererModule().CreateAndInitSingleView(InRHICmdList, &ViewFamily, &ViewInitOptions);
			const FSceneView* View = ViewFamily.Views[0];

			// The following variables allow to accumulate quads for rendering in a single pass. Then, we flush them all out when the pass is full and we start again : 
			int32 CurrentDestinationArrayIndex = INDEX_NONE;
			TArray<FTextureResource*> CurrentSourceTextureResources;
			TArray<FUintVector2> CurrentQuadInfos;
			TArray<FUintVector4> CurrentSourceRects, CurrentDestinationRects;

			// When we've reached the limit of what we can render in one pass, flush all and add a render pass : 
			auto FlushCopyQuads = [&GraphBuilder, View, OutputResource, &CurrentDestinationArrayIndex, &CurrentSourceTextureResources, &CurrentSourceRects, &CurrentDestinationRects,
				&CurrentQuadInfos, &CommonSourceTextureSize, BlackDummySRVRef, ShaderMap, PixelShader]()
			{
				if (CurrentSourceTextureResources.IsEmpty())
				{
					// Nothing to flush :
					check(CurrentSourceRects.IsEmpty() && CurrentDestinationRects.IsEmpty() && CurrentQuadInfos.IsEmpty());
					return;
				}

				check(!CurrentSourceRects.IsEmpty() && (CurrentSourceRects.Num() == CurrentDestinationRects.Num()) && (CurrentSourceRects.Num() == CurrentQuadInfos.Num()));

				FRDGBufferRef RectBuffer = CreateUploadBuffer(GraphBuilder, TEXT("DestinationRects"), TConstArrayView<FUintVector4>(MakeArrayView(CurrentDestinationRects)));
				FRDGBufferSRVRef RectBufferSRV = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(RectBuffer, PF_R32G32B32A32_UINT));

				FRDGBufferRef RectUVBuffer = CreateUploadBuffer(GraphBuilder, TEXT("RectUVs"), TConstArrayView<FUintVector4>(MakeArrayView(CurrentSourceRects)));
				FRDGBufferSRVRef RectUVBufferSRV = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(RectUVBuffer, PF_R32G32B32A32_UINT));

				FRDGBufferRef QuadInfosBuffer = CreateUploadBuffer(GraphBuilder, TEXT("QuadInfos"), TConstArrayView<FUintVector2>(MakeArrayView(CurrentQuadInfos)));
				FRDGBufferSRVRef QuadInfosBufferSRV = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(QuadInfosBuffer, PF_R32G32_UINT));

				FRDGTextureRef OutputTexture = GraphBuilder.RegisterExternalTexture(CreateRenderTarget(OutputResource->GetTextureRHI(), TEXT("OutputTexture")), ERDGTextureFlags::SkipTracking);
				
				// We may target an array slice directly : 
				int32 ArrayIndex = -1;
				if (OutputTexture->Desc.IsTextureArray())
				{
					check(CurrentDestinationArrayIndex < OutputTexture->Desc.ArraySize);
					ArrayIndex = CurrentDestinationArrayIndex;
				}

				FCopyQuadsMultiSourcePSParameters* PassParameters = GraphBuilder.AllocParameters<FCopyQuadsMultiSourcePSParameters>();
				PassParameters->RenderTargets[0] = FRenderTargetBinding(OutputTexture, ERenderTargetLoadAction::ELoad, /*InMipIndex = */0, ArrayIndex);
				PassParameters->PS.View = View->ViewUniformBuffer;
				PassParameters->PS.InQuadInfos = QuadInfosBufferSRV;
				check(CurrentSourceTextureResources.Num() <= FCopyQuadsMultiSourcePS::NumMultiSources);
				for (int32 TextureIndex = 0; TextureIndex < FCopyQuadsMultiSourcePS::NumMultiSources; ++TextureIndex)
				{
					FRDGTextureRef SourceTexture = BlackDummySRVRef->GetParent();
					if (CurrentSourceTextureResources.IsValidIndex(TextureIndex))
					{
						SourceTexture = GraphBuilder.RegisterExternalTexture(CreateRenderTarget(CurrentSourceTextureResources[TextureIndex]->GetTexture2DRHI(), TEXT("SourceTexture")));
					}
					PassParameters->PS.InSourceTexture[TextureIndex] = SourceTexture;
				}

				FPixelShaderUtils::AddRasterizeToRectsPass<FCopyQuadsMultiSourcePS>(GraphBuilder,
					ShaderMap,
					RDG_EVENT_NAME("CopyQuadsMultiSourcePS"),
					PixelShader,
					PassParameters,
					/*ViewportSize = */OutputTexture->Desc.Extent,
					RectBufferSRV,
					CurrentDestinationRects.Num(),
					/*BlendState = */nullptr,
					/*RasterizerState = */nullptr,
					/*DepthStencilState = */nullptr,
					/*StencilRef = */0,
					/*TextureSize = */FIntPoint(FMath::FloorToInt32(CommonSourceTextureSize.X), FMath::FloorToInt32(CommonSourceTextureSize.Y)),
					RectUVBufferSRV,
					/*DownsampleFactor = */1,
					ERDGPassFlags::NeverCull); // Use NeverCull because it renders a texture for which tracking is disabled

				// Reset all for the next pass that comes along : 
				CurrentDestinationArrayIndex = INDEX_NONE;
				CurrentSourceTextureResources.Empty();
				CurrentSourceRects.Empty();
				CurrentDestinationRects.Empty();
				CurrentQuadInfos.Empty();
			};

			for (const FCopyQuadParams& InSingleQuadParams : CopyQuadsParams)
			{
				check(InSingleQuadParams.DestinationArrayIndex != INDEX_NONE);
				// If the output texture/slice has changed since the last iteration, it's time to flush and start a new pass :
				if (CurrentDestinationArrayIndex != InSingleQuadParams.DestinationArrayIndex)
				{
					if (CurrentDestinationArrayIndex != INDEX_NONE)
					{
						FlushCopyQuads();
					}
					CurrentDestinationArrayIndex = InSingleQuadParams.DestinationArrayIndex;
				}

				if (CurrentSourceTextureResources.IsEmpty())
				{
					CurrentSourceTextureResources.Add(InSingleQuadParams.SourceTextureResource);
				}
				else if (InSingleQuadParams.SourceTextureResource != CurrentSourceTextureResources.Last())
				{
					// If we've reached the amount of textures we can render in a single pass, we flush the pass and initiate a new one : 
					if (CurrentSourceTextureResources.Num() == FCopyQuadsMultiSourcePS::NumMultiSources)
					{
						FlushCopyQuads();
						check(CurrentSourceTextureResources.IsEmpty());
					}
					CurrentSourceTextureResources.Add(InSingleQuadParams.SourceTextureResource);
				}

				// If we are using the same texture as the last one, we can render it in the same pass, just append our quad : 
				if (InSingleQuadParams.SourceTextureResource == CurrentSourceTextureResources.Last())
				{
					const int32 SourceTextureIndex = CurrentSourceTextureResources.Num() - 1;
					CurrentQuadInfos.Add(FUintVector2(static_cast<uint32>(SourceTextureIndex), InSingleQuadParams.ChannelSwizzleMask));
					CurrentSourceRects.Add(FUintVector4(InSingleQuadParams.SourceRect.Min.X, InSingleQuadParams.SourceRect.Min.Y, InSingleQuadParams.SourceRect.Max.X, InSingleQuadParams.SourceRect.Max.Y));
					CurrentDestinationRects.Add(FUintVector4(InSingleQuadParams.DestinationRect.Min.X, InSingleQuadParams.DestinationRect.Min.Y, InSingleQuadParams.DestinationRect.Max.X, InSingleQuadParams.DestinationRect.Max.Y));
				}
			}

			// Flush the remaining quads if any : 
			FlushCopyQuads();

			GraphBuilder.Execute();
		});
	}

	// No need to do anything if there's nothing to be rendered, this layer will just be ineffective on this batch :
	if (WriteRT != nullptr)
	{
		// The second step is to perform the blend : merge the edit layer with the previous layers in the stack
		InRenderParams.MergeRenderContext->CycleBlendRenderTargets(/*InDesiredWriteAccess = */ERHIAccess::RTV);
		WriteRT = InRenderParams.MergeRenderContext->GetBlendRenderTargetWrite();
		ULandscapeScratchRenderTarget* CurrentLayerReadRT = InRenderParams.MergeRenderContext->GetBlendRenderTargetRead();
		ULandscapeScratchRenderTarget* PreviousLayersReadRT = InRenderParams.MergeRenderContext->GetBlendRenderTargetReadPrevious();
	 
		if (InRenderParams.MergeRenderContext->IsHeightmapMerge())
		{
			ENQUEUE_RENDER_COMMAND(LandscapeLayers_Cmd_HeightmapsMergeEditLayer)(
				[ TargetLayerName = InRenderParams.RenderGroupTargetLayerNames[0]
				, OutputResource = WriteRT->GetRenderTarget2D()->GetResource()
				, OutputResourceName = WriteRT->GetDebugName()
				, CurrentEditLayerResource = CurrentLayerReadRT->GetRenderTarget2D()->GetResource()
				, PreviousEditLayersResource = PreviousLayersReadRT->GetRenderTarget2D()->GetResource()
				, EffectiveTextureSize = InRenderParams.MergeRenderContext->GetCurrentRenderBatch()->GetRenderTargetResolution(/*bInWithDuplicateBorders = */false)
				, BlendMode = LayerStruct->BlendMode, HeightmapAlpha = LayerStruct->HeightmapAlpha ]
				(FRHICommandListImmediate& InRHICmdList)
			{
				SCOPED_DRAW_EVENTF(InRHICmdList, LandscapeLayers, TEXT("MergeEditLayer -> %s"), OutputResourceName);
				FRDGBuilder GraphBuilder(InRHICmdList, RDG_EVENT_NAME("HeightmapsMergeEditLayer"));

				FRDGTextureRef OutputTextureRef = GraphBuilder.RegisterExternalTexture(CreateRenderTarget(OutputResource->TextureRHI, TEXT("OutputTexture")), ERDGTextureFlags::SkipTracking);
				FRDGTextureRef CurrentEditLayerTextureRef = GraphBuilder.RegisterExternalTexture(CreateRenderTarget(CurrentEditLayerResource->TextureRHI, TEXT("CurrentEditLayerTexture")));
				FRDGTextureRef PreviousEditLayersTextureRef = GraphBuilder.RegisterExternalTexture(CreateRenderTarget(PreviousEditLayersResource->TextureRHI, TEXT("PreviousEditLayersTexture")));

				FLandscapeEditLayersHeightmapsMergeEditLayerPS::FParameters* PSParams = GraphBuilder.AllocParameters<FLandscapeEditLayersHeightmapsMergeEditLayerPS::FParameters>();
				PSParams->RenderTargets[0] = FRenderTargetBinding(OutputTextureRef, ERenderTargetLoadAction::ENoAction);
				PSParams->InEditLayerBlendMode = static_cast<uint32>(LandscapeBlendModeToEditLayerBlendMode(BlendMode));
				PSParams->InEditLayerAlpha = HeightmapAlpha;
				PSParams->InCurrentEditLayerHeightmap = GraphBuilder.CreateSRV(FRDGTextureSRVDesc::Create(CurrentEditLayerTextureRef));
				PSParams->InPreviousEditLayersHeightmap = GraphBuilder.CreateSRV(FRDGTextureSRVDesc::Create(PreviousEditLayersTextureRef));

				FLandscapeEditLayersHeightmapsMergeEditLayerPS::MergeEditLayerPS(GraphBuilder, PSParams, TargetLayerName, EffectiveTextureSize);
				GraphBuilder.Execute();
			});
		}
		else
		{
			struct FMergePaintLayerParams
			{
				FMergePaintLayerParams(const FName& InPaintLayerName)
					: PaintLayerName(InPaintLayerName)
				{}

				FName PaintLayerName;
				EWeightmapBlendMode EditLayerPaintLayerBlendMode = EWeightmapBlendMode::Additive;
				float EditLayerAlpha = 1.0f;
			};

			// Prepare a list of all paint layers to merge and do it all in 1 command :
			TArray<FMergePaintLayerParams> MergePaintLayersParams;
			MergePaintLayersParams.Reserve(NumTargetLayers);
			for (const FName& TargetLayerName : InRenderParams.RenderGroupTargetLayerNames)
			{
				FMergePaintLayerParams& MergePaintLayerParams = MergePaintLayersParams.Emplace_GetRef(TargetLayerName);

				const FLandscapeInfoLayerSettings* LayerSettings = Info->Layers.FindByPredicate([TargetLayerName](const FLandscapeInfoLayerSettings& InLayerSettings) { return (InLayerSettings.LayerName == TargetLayerName); });
				check(LayerSettings != nullptr);
				ULandscapeLayerInfoObject* LayerInfoObj = LayerSettings->LayerInfoObj;
				check(LayerInfoObj != nullptr);
				bool bIsSubtractiveBlend = false;
				if (const bool* bSubstractiveInLayer = LayerStruct->WeightmapLayerAllocationBlend.Find(LayerInfoObj); (bSubstractiveInLayer != nullptr) && *bSubstractiveInLayer)
				{
					MergePaintLayerParams.EditLayerPaintLayerBlendMode = EWeightmapBlendMode::Subtractive;
				}

				if (TargetLayerName != UMaterialExpressionLandscapeVisibilityMask::ParameterName)
				{
					MergePaintLayerParams.EditLayerAlpha = LayerStruct->WeightmapAlpha;
				}
			}

			ENQUEUE_RENDER_COMMAND(LandscapeLayers_Cmd_WeightmapsMergeEditLayer)(
				[ MergePaintLayersParams
				, OutputResource = WriteRT->GetRenderTarget2DArray()->GetResource()
				, OutputResourceName = WriteRT->GetDebugName()
				, CurrentEditLayerResource = CurrentLayerReadRT->GetRenderTarget2DArray()->GetResource()
				, PreviousEditLayersResource = PreviousLayersReadRT->GetRenderTarget2DArray()->GetResource()
				, EffectiveTextureSize = InRenderParams.MergeRenderContext->GetCurrentRenderBatch()->GetRenderTargetResolution(/*bInWithDuplicateBorders = */false) ]
				(FRHICommandListImmediate& InRHICmdList)
			{
				SCOPED_DRAW_EVENTF(InRHICmdList, LandscapeLayers, TEXT("MergeEditLayer -> %s"), OutputResourceName);
				FRDGBuilder GraphBuilder(InRHICmdList, RDG_EVENT_NAME("WeightmapsMergeEditLayer"));

				FRDGTextureRef OutputTextureRef = GraphBuilder.RegisterExternalTexture(CreateRenderTarget(OutputResource->TextureRHI, TEXT("OutputTexture")), ERDGTextureFlags::SkipTracking);
				FRDGTextureRef CurrentEditLayerTextureRef = GraphBuilder.RegisterExternalTexture(CreateRenderTarget(CurrentEditLayerResource->TextureRHI, TEXT("CurrentEditLayerTexture")));
				FRDGTextureSRVRef CurrentEditLayerTextureSRVRef = GraphBuilder.CreateSRV(FRDGTextureSRVDesc::Create(CurrentEditLayerTextureRef));
				FRDGTextureRef PreviousEditLayersTextureRef = GraphBuilder.RegisterExternalTexture(CreateRenderTarget(PreviousEditLayersResource->TextureRHI, TEXT("PreviousEditLayersTexture")));
				FRDGTextureSRVRef PreviousEditLayersTextureSRVRef = GraphBuilder.CreateSRV(FRDGTextureSRVDesc::Create(PreviousEditLayersTextureRef));

				const int32 NumTargetLayers = MergePaintLayersParams.Num();
				for (int32 TargetLayerIndex = 0; TargetLayerIndex < NumTargetLayers; ++TargetLayerIndex)
				{
					FLandscapeEditLayersWeightmapsMergeEditLayerPS::FParameters* PSParams = GraphBuilder.AllocParameters<FLandscapeEditLayersWeightmapsMergeEditLayerPS::FParameters>();
					PSParams->RenderTargets[0] = FRenderTargetBinding(OutputTextureRef, ERenderTargetLoadAction::ENoAction, /*InMipIndex = */0, /*InArraySlice = */TargetLayerIndex);
					PSParams->InPaintLayerIndex = TargetLayerIndex;
					PSParams->InEditLayerPaintLayerBlendMode = static_cast<uint32>(MergePaintLayersParams[TargetLayerIndex].EditLayerPaintLayerBlendMode);
					PSParams->InEditLayerAlpha = MergePaintLayersParams[TargetLayerIndex].EditLayerAlpha;
					PSParams->InCurrentEditLayerWeightmaps = CurrentEditLayerTextureSRVRef;
					PSParams->InPreviousEditLayersWeightmaps = PreviousEditLayersTextureSRVRef;

					FLandscapeEditLayersWeightmapsMergeEditLayerPS::MergeEditLayerPS(GraphBuilder, PSParams, MergePaintLayersParams[TargetLayerIndex].PaintLayerName, EffectiveTextureSize);
				}
				GraphBuilder.Execute();
			});
		}
	}
}

FString ULandscapeEditLayerPersistent::GetEditLayerRendererDebugName() const
{
	const FLandscapeLayer* LayerStruct = GetOwningLayer();
	check(LayerStruct != nullptr);
	return LayerStruct->Name.ToString();
}

namespace UE::Landscape::EditLayers::Private
{
	using namespace UE::Landscape::EditLayers;

	/** Struct that holds all the per-component information needed when preparing the batched merge context */
	struct FComponentToRenderInfo
	{
		FComponentToRenderInfo() = default;

		FComponentToRenderInfo(ULandscapeComponent* InComponent, int32 InComponentIndex, const int32 InNumAllComponents, const int32 NumTargetLayersToRender)
			: Component(InComponent)
			, ComponentIndex(InComponentIndex)
			, DependentComponentBitIndices(false, InNumAllComponents)
			, CombinedSectionRect(InComponent->GetSectionBase(), InComponent->GetSectionBase() + FIntPoint(InComponent->ComponentSizeQuads + 1, InComponent->ComponentSizeQuads + 1))
			, ComponentKey(InComponent->GetComponentKey())
			, MinDependentComponentKey(ComponentKey)
			, MaxDependentComponentKey(ComponentKey)
			, LocalBounds(InComponent->CachedLocalBox)
			, WorldBounds(InComponent->CachedLocalBox.TransformBy(InComponent->GetComponentTransform()))
			, TargetLayerBitIndices(false, NumTargetLayersToRender)
		{}

		void Finalize(const FIntRect& InDependentComponentInclusiveBounds, const FIntPoint& InComponentSizeQuads)
		{
			MinDependentComponentKey = InDependentComponentInclusiveBounds.Min;
			MaxDependentComponentKey = InDependentComponentInclusiveBounds.Max;
			CombinedSectionRect.Min = InDependentComponentInclusiveBounds.Min * InComponentSizeQuads;
			CombinedSectionRect.Max = (InDependentComponentInclusiveBounds.Max + 1) * InComponentSizeQuads + 1;
		}

		/** Component to render */
		ULandscapeComponent* Component = nullptr;

		/** Index of the component to render in AllComponentsToRenderInfos */
		int32 ComponentIndex = INDEX_NONE;

		/** List of components this component depends on to render appropriately. Each dependency is represented by a bit which corresponds to the index of the dependent component in AllComponentsToRenderInfos */
		TBitArray<> DependentComponentBitIndices;

		/** Section rect of all the components that this component depends on to render */
		FIntRect CombinedSectionRect;

		/** Coordinate of the component to render */
		FIntPoint ComponentKey = FIntPoint(ForceInit);

		/** Minimum coordinate of the components that this component depends on to render */
		FIntPoint MinDependentComponentKey = FIntPoint(MAX_int32, MAX_int32);

		/** Maximum coordinate of the components that this component depends on to render */
		FIntPoint MaxDependentComponentKey = FIntPoint(MIN_int32, MIN_int32);

		/** Bounding volume of this component in local space */
		FBox LocalBounds = FBox(ForceInit);

		/** Bounding volume of this component in world space */
		FBox WorldBounds = FBox(ForceInit);

		/** List of target layers being written by this component. Each target layer is represented by a bit which corresponds to the index of the target layer names in AllTargetLayerNames */
		TBitArray<> TargetLayerBitIndices;
	};

	/** Struct that holds all the per-render batch information needed when preparing the batched merge context */
	struct FRenderBatchInfo
	{
		FRenderBatchInfo(int32 InNumComponentsToRender, int32 InBatchIndex)
			: BatchIndex(InBatchIndex)
			, ComponentToRenderInfoBitIndices(false, InNumComponentsToRender)
		{
		}

		FIntRect GetProjectedSectionRect(const FComponentToRenderInfo& InComponentToRenderInfo) const
		{
			checkf(MinComponentKey.X != MAX_int32, TEXT("Shouldn't be called when the batch is empty"));
			FIntRect NewCombinedSectionRect(CombinedSectionRect);
			NewCombinedSectionRect.Union(InComponentToRenderInfo.CombinedSectionRect);
			return NewCombinedSectionRect;
		}

		void AddToBatch(const FComponentToRenderInfo& InComponentToRenderInfo)
		{
			ComponentToRenderInfoBitIndices.CombineWithBitwiseOR(InComponentToRenderInfo.DependentComponentBitIndices, EBitwiseOperatorFlags::MinSize);
			// Special case when it's the first addition to the batch : 
			if (MinComponentKey.X == MAX_int32)
			{
				CombinedSectionRect = InComponentToRenderInfo.CombinedSectionRect;
			}
			else
			{
				CombinedSectionRect.Union(InComponentToRenderInfo.CombinedSectionRect);
			}
			MinComponentKey = MinComponentKey.ComponentMin(InComponentToRenderInfo.MinDependentComponentKey);
			MaxComponentKey = MaxComponentKey.ComponentMax(InComponentToRenderInfo.MaxDependentComponentKey);
		}


	public:
		/** Index of the batch within the merge operation */
		int32 BatchIndex = INDEX_NONE;

		/** Indices (in AllComponentsToRenderInfos) of the components which we need to render within this batch
		  It's a bit array (1 bit per component to render info) to vastly optimize the batching operation, which is a O(N^2):  */
		TBitArray<> ComponentToRenderInfoBitIndices;

		/** Section rect of all the components that this batch will render*/
		FIntRect CombinedSectionRect;

		/** Minimum coordinate of the components that this batch will render */
		FIntPoint MinComponentKey = FIntPoint(MAX_int32, MAX_int32);

		/** Maximum coordinate of the components that this batch will render */
		FIntPoint MaxComponentKey = FIntPoint(MIN_int32, MIN_int32);
	};

	/** Struct that holds all the per-renderer information needed when preparing the batched merge context */
	struct FEditLayerRendererRenderInfo
	{
		FEditLayerRendererRenderInfo() = default;
		FEditLayerRendererRenderInfo(const FEditLayerRendererState& InRendererState, int32 InRendererIndex, const FTransform& InLandscapeTransform, double InMaxLocalHeight, int32 InNumComponentsToRender)
			: RendererState(InRendererState)
			, RendererIndex(InRendererIndex)
			, ComponentToRenderInfoBitIndices(false, InNumComponentsToRender)
		{

#if ENABLE_VISUAL_LOG
			// Pick a random color for each renderer : 
			uint32 Hash = GetTypeHashHelper(InRendererIndex);
			uint8* HashElement = reinterpret_cast<uint8*>(&Hash);
			VisualLogColor = FColor(HashElement[0], HashElement[1], HashElement[2], FMergeRenderContext::GetVisualLogAlpha());

			VisualLogOffsetLocalSpace = InRendererIndex * InLandscapeTransform.InverseTransformVector(FVector(0.0, 0.0, CVarLandscapeBatchedMergeVisualLogOffsetIncrement.GetValueOnGameThread())).Z + InMaxLocalHeight;
#endif // ENABLE_VISUAL_LOG
		}
		
		FEditLayerRendererState RendererState;
		int32 RendererIndex = INDEX_NONE;

		// Indices (in AllComponentsToRenderInfos) of the components which we need to render for this renderer
		TBitArray<> ComponentToRenderInfoBitIndices;

#if ENABLE_VISUAL_LOG
		FColor VisualLogColor = FColor(ForceInit);
		double VisualLogOffsetLocalSpace = 0.0;
#endif // ENABLE_VISUAL_LOG
	};

	/** Given the list of components that actually need to be rendered, divide the work into batches, such that:
	 *   - When a component is being rendered by a given batch, all components needed for rendering this given component are present in the batch
	 *   - All components end up being rendered in at least one of the batches
	 */
	TArray<FRenderBatchInfo> DivideIntoBatches(const TBitArray<>& InFinalComponentsToRenderInfoBitIndices, const TArray<FComponentToRenderInfo>& InAllComponentsToRenderInfos)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(DivideIntoBatches);

		const int32 NumFinalComponentsToRender = InFinalComponentsToRenderInfoBitIndices.CountSetBits();

		// Compute the final batch resolution we will use for this work : 
		const int32 DesiredMaxBatchResolution = CVarLandscapeEditLayersMaxResolutionPerRenderBatch.GetValueOnGameThread();

		// Compute the minimal batch resolution :
		FIntPoint MinBatchResolution(MIN_int32, MIN_int32);
		// Sort the components to render by MinDependentComponentKey in order to minimize the number of batches needed. 
		TArray<TPair<int32, FIntPoint>> RemainingComponentsToRenderInfoIndices;
		RemainingComponentsToRenderInfoIndices.Reserve(NumFinalComponentsToRender);
		for (TConstSetBitIterator It(InFinalComponentsToRenderInfoBitIndices); It; ++It)
		{
			const int32 ComponentIndex = It.GetIndex();
			const FComponentToRenderInfo& ComponentToRenderInfo = InAllComponentsToRenderInfos[ComponentIndex];
			RemainingComponentsToRenderInfoIndices.Add(MakeTuple(ComponentIndex, ComponentToRenderInfo.MinDependentComponentKey));
			// The component that has the largest CombinedSectionRect defines the minimum batch size, because it requires all components in that area to render appropriately : 
			MinBatchResolution = MinBatchResolution.ComponentMax(ComponentToRenderInfo.CombinedSectionRect.Size());
		}

		FIntPoint MaxBatchResolution = MinBatchResolution.ComponentMax(FIntPoint(DesiredMaxBatchResolution));
		if ((MinBatchResolution.X > DesiredMaxBatchResolution) || (MinBatchResolution.Y > DesiredMaxBatchResolution))
		{
			UE_LOG(LogLandscape, Warning, TEXT("Landscape edit layers merge requires a minimum batch size of resolution %ix%i, which is higher than the current desired maximum batch resolution %ix%i. Consider adjusting the maximum batch resolution (landscape.EditLayersLocalMerge.MaxResolutionPerRenderBatch) or make sure the landscape edit layers renderers in use require smaller work area."),
				MinBatchResolution.X, MinBatchResolution.Y, DesiredMaxBatchResolution, DesiredMaxBatchResolution);
			MaxBatchResolution = MinBatchResolution;
		}

		// Sort the components to render by MinDependentComponentKey in order to minimize the number of batches needed. 
		//  We actually use the inverse order because we want to process the elements from RemainingComponentsToRenderInfoIndices in inverse order 
		RemainingComponentsToRenderInfoIndices.Sort([](const TPair<int32, FIntPoint>& InLHS, const TPair<int32, FIntPoint>& InRHS)
		{
			const FIntPoint& InLHSMinDependentComponentKey = InLHS.Value;
			const FIntPoint& InRHSMinDependentComponentKey = InRHS.Value;
			if (InLHSMinDependentComponentKey.Y > InRHSMinDependentComponentKey.Y)
			{
				return true;
			}
			else if (InLHSMinDependentComponentKey.Y == InRHSMinDependentComponentKey.Y)
			{
				return (InLHSMinDependentComponentKey.X > InRHSMinDependentComponentKey.X);
			}
			return false;
		});

		// Iterate on all the work items and organize them into batches as large as possible (within the maximum allowed resolution)
		TArray<FRenderBatchInfo> AllBatchInfos;
		if (!RemainingComponentsToRenderInfoIndices.IsEmpty())
		{
			TBitArray<TInlineAllocator<1>> TempBitArray(false, InAllComponentsToRenderInfos.Num());
			while (!RemainingComponentsToRenderInfoIndices.IsEmpty())
			{
				const int32 ComponentIndex = RemainingComponentsToRenderInfoIndices.Pop(EAllowShrinking::No).Key;
				const FComponentToRenderInfo& ComponentToRenderInfo = InAllComponentsToRenderInfos[ComponentIndex];
				const FIntPoint NeededResolution = ComponentToRenderInfo.CombinedSectionRect.Size();
				check((NeededResolution.X <= MaxBatchResolution.X) && (NeededResolution.Y <= MaxBatchResolution.Y));

				int32 BestBatchIndex = INDEX_NONE;
				int32 MinBatchRenderArea = MAX_int32;
				int32 MaxNumComponentsInCommonWithBatch = -1;

				// Iterate through all batches and try to find which would be able to accept it and amongst those, which would have the minimal overall resolution: 
				const int32 NumBatches = AllBatchInfos.Num();
				for (int32 BatchIndex = 0; BatchIndex < NumBatches; ++BatchIndex)
				{
					const FRenderBatchInfo& BatchInfo = AllBatchInfos[BatchIndex];
					FIntRect ProjectedBatchSectionRect = BatchInfo.GetProjectedSectionRect(ComponentToRenderInfo);

					// If after adding this component and its dependent components, the batch still fits within the max allowed resolution, it can accept it : 
					if ((ProjectedBatchSectionRect.Width() <= MaxBatchResolution.X) && (ProjectedBatchSectionRect.Height() <= MaxBatchResolution.Y))
					{
						// Favor the batch that has the most components in common with what we're trying to render : 
						TempBitArray = TBitArray<>::BitwiseAND(BatchInfo.ComponentToRenderInfoBitIndices, ComponentToRenderInfo.DependentComponentBitIndices, EBitwiseOperatorFlags::MinSize);
						const int32 NumComponentsInCommonWithBatch = TempBitArray.CountSetBits();
						// If the batch already has all the components we need, it's a perfect match, we won't ever find a better batch so just stop the search there :
						if (NumComponentsInCommonWithBatch == ComponentToRenderInfo.DependentComponentBitIndices.CountSetBits())
						{
							BestBatchIndex = BatchIndex;
							break;
						}

						const int32 ProjectedBatchRenderArea = ProjectedBatchSectionRect.Size().X * ProjectedBatchSectionRect.Size().Y;
						if (NumComponentsInCommonWithBatch > MaxNumComponentsInCommonWithBatch)
						{
							MaxNumComponentsInCommonWithBatch = NumComponentsInCommonWithBatch;
							MinBatchRenderArea = ProjectedBatchRenderArea;
							BestBatchIndex = BatchIndex;
						}
						else if (NumComponentsInCommonWithBatch == MaxNumComponentsInCommonWithBatch)
						{
							// Favor the batch with the lesser area 
							if (ProjectedBatchRenderArea < MinBatchRenderArea)
							{
								MinBatchRenderArea = ProjectedBatchRenderArea;
								BestBatchIndex = BatchIndex;
							}
						}
					}
				}

				// If we have found a batch, just add the FRenderBatchInfo to it, otherwise, add a new batch:
				FRenderBatchInfo& SelectedBatchInfo = (BestBatchIndex != INDEX_NONE) ? AllBatchInfos[BestBatchIndex]
					: AllBatchInfos.Add_GetRef(FRenderBatchInfo(InAllComponentsToRenderInfos.Num(), /*InBatchIndex = */AllBatchInfos.Num()));

				SelectedBatchInfo.AddToBatch(ComponentToRenderInfo);
			}
		}
		return AllBatchInfos;
	}

#if ENABLE_VISUAL_LOG
	struct FComponentDependenciesVisLogHelper
	{
		enum class EShowNodeInfo : uint8
		{
			None = 0, 
			Minimal,
			Detailed
		};

		FComponentDependenciesVisLogHelper(const ALandscape* InLandscape, bool bInIsHeightmapMerge, EShowNodeInfo InShowNodeInfo, const FMergeRenderContext& InRenderContext)
			: Landscape(InLandscape)
			, LandscapeTransform(InLandscape->GetTransform())
			, bIsHeightmapMerge(bInIsHeightmapMerge)
			, ShowNodeInfo(InShowNodeInfo)
			, LandscapeComponentLocalSize(InLandscape->ComponentSizeQuads)
			, RenderContext(&InRenderContext)
		{}
		
		const FVector& AddNode(const FComponentToRenderInfo& InComponentRenderInfo, const FEditLayerRendererRenderInfo& InRendererRenderInfo)
		{
			const FIntPoint ComponentKey = InComponentRenderInfo.Component->GetComponentKey();

			TPair<int32, int32> Key(InComponentRenderInfo.ComponentIndex, InRendererRenderInfo.RendererIndex);
			const FVector* Center = KeyToCenter.Find(Key);
			if (Center == nullptr)
			{
				FTransform BaseTransform = FTransform(FVector(0.0, 0.0, InRendererRenderInfo.VisualLogOffsetLocalSpace)) * LandscapeTransform;
				FBox VisualBounds(
					FVector(ComponentKey.X * LandscapeComponentLocalSize, ComponentKey.Y * LandscapeComponentLocalSize, 0.0),
					FVector((ComponentKey.X + 1) * LandscapeComponentLocalSize, (ComponentKey.Y + 1) * LandscapeComponentLocalSize, 0.0));

				FString Message;
				if (ShowNodeInfo == EShowNodeInfo::Minimal)
				{
					FIntRect DependentComponentArea(InComponentRenderInfo.MinDependentComponentKey, InComponentRenderInfo.MaxDependentComponentKey + 1);
					Message = FString::Printf(TEXT("%s"), *ComponentKey.ToString());
				}
				else if (ShowNodeInfo == EShowNodeInfo::Detailed)
				{
					Message = FString::Printf(TEXT("%s\n(%s)"), *ComponentKey.ToString(), *InRendererRenderInfo.RendererState.GetRenderer()->GetEditLayerRendererDebugName());
				}
				// On the first renderer, show additional info: 
				if (InRendererRenderInfo.RendererIndex == 0)
				{
					FIntRect DependentComponentAreaRelative(InComponentRenderInfo.MinDependentComponentKey - ComponentKey, InComponentRenderInfo.MaxDependentComponentKey - ComponentKey);
					Message += FString::Printf(TEXT("\n{%s}"), *DependentComponentAreaRelative.ToString());
					if (!bIsHeightmapMerge)
					{
						TArray<FName> TargetLayerNames = RenderContext->ConvertTargetLayerBitIndicesToNames(InComponentRenderInfo.TargetLayerBitIndices);
						Message += FString::Printf(TEXT("\n%s"), *ConvertTargetLayerNamesToString(TargetLayerNames));
					}
				}

				FMatrix Transform = BaseTransform.ToMatrixWithScale();
				UE_VLOG_OBOX(Landscape, LogLandscape, Log, VisualBounds, Transform, InRendererRenderInfo.VisualLogColor, TEXT("%s"), *Message);
				Center = &KeyToCenter.Add(Key, Transform.TransformPosition(VisualBounds.GetCenter()));
			}
			return *Center;
		}

		void AddDependency(const FComponentToRenderInfo& InSourceComponentRenderInfo, const FEditLayerRendererRenderInfo& InSourceRendererRenderInfo,
			const FComponentToRenderInfo& InDestinationComponentRenderInfo, const FEditLayerRendererRenderInfo& InDestinationRendererRenderInfo)
		{
			const FVector SourceCenter = AddNode(InSourceComponentRenderInfo, InSourceRendererRenderInfo);
			const FVector DestinationCenter = AddNode(InDestinationComponentRenderInfo, InDestinationRendererRenderInfo);
			// TODO [jonathan.bard] : UE_VLOG_ARROW_MAG(Landscape, LogLandscape, Log, SourceCenter, DestinationCenter, InSourceRendererRenderInfo.VisualLogColor, TEXT(""), 80.0f); // TODO [jonathan.bard] : use proper mag here
			UE_VLOG_ARROW(Landscape, LogLandscape, Log, SourceCenter, DestinationCenter, InSourceRendererRenderInfo.VisualLogColor, TEXT(""));
		}

	private:
		const ALandscape* Landscape = nullptr;
		FTransform LandscapeTransform;
		bool bIsHeightmapMerge = false;
		EShowNodeInfo ShowNodeInfo = EShowNodeInfo::None;
		double LandscapeComponentLocalSize = 0.0;
		const FMergeRenderContext* RenderContext = nullptr;

		TMap<TPair<int32, int32>, FVector> KeyToCenter;
	};

	// Log the shapes of the render item output input render items if requested : 
	void VisLogRenderItemInput(const ALandscape* InLandscape, const FInputWorldArea& InInputWorldArea, const FEditLayerRendererRenderInfo& InRendererRenderInfo,
		const FTransform& InLandscapeTransform, const FBox& InLandscapeLoadedBounds, const TArrayView<ULandscapeComponent*>& InComponentsToRender)
	{
		// Display the shapes 2 unreal unit (in world space) under the requested offset (so that they're located under the output items) :
		static double OutputLocalOffsetLocalSpace = -2.0 / InLandscapeTransform.GetScale3D().Z;
		const double LandscapeComponentLocalSize = InLandscape->ComponentSizeQuads;

		FTransform BaseTransform = FTransform(FVector(0.0, 0.0, InRendererRenderInfo.VisualLogOffsetLocalSpace + OutputLocalOffsetLocalSpace)) * InLandscapeTransform;
		switch (InInputWorldArea.GetType())
		{
		case FInputWorldArea::EType::Infinite:
		{
			// Infinite input area means all loaded components: 
			FBox VisualBounds = InLandscapeLoadedBounds;
			VisualBounds.Min.Z = 0.0;
			VisualBounds.Max.Z = 0.0;
			UE_VLOG_WIREOBOX(InLandscape, LogLandscape, Log, VisualBounds, BaseTransform.ToMatrixWithScale(), InRendererRenderInfo.VisualLogColor, TEXT(""));
			break;
		}
		case FInputWorldArea::EType::LocalComponent:
		{
			// Local input area means each of the landscape components : 
			for (ULandscapeComponent* Component : InComponentsToRender)
			{
				FIntRect ComponentKeys = InInputWorldArea.GetLocalComponentKeys(Component->GetComponentKey());
				// Transform from inclusive to exclusive bounds : 
				ComponentKeys.Max += FIntPoint(1, 1);
				FBox VisualBounds(
					FVector(ComponentKeys.Min.X * LandscapeComponentLocalSize, ComponentKeys.Min.Y * LandscapeComponentLocalSize, 0.0),
					FVector(ComponentKeys.Max.X * LandscapeComponentLocalSize, ComponentKeys.Max.Y * LandscapeComponentLocalSize, 0.0));
				UE_VLOG_WIREOBOX(InLandscape, LogLandscape, Log, VisualBounds, BaseTransform.ToMatrixWithScale(), InRendererRenderInfo.VisualLogColor, TEXT(""));
			}
			break;
		}
		case FInputWorldArea::EType::SpecificComponent:
		{
			FIntRect ComponentKeys = InInputWorldArea.GetSpecificComponentKeys();
			// Transform from inclusive to exclusive bounds : 
			ComponentKeys.Max += FIntPoint(1, 1);
			FBox VisualBounds(
				FVector(ComponentKeys.Min.X * LandscapeComponentLocalSize, ComponentKeys.Min.Y * LandscapeComponentLocalSize, 0.0),
				FVector(ComponentKeys.Max.X * LandscapeComponentLocalSize, ComponentKeys.Max.Y * LandscapeComponentLocalSize, 0.0));
			UE_VLOG_WIREOBOX(InLandscape, LogLandscape, Log, VisualBounds, BaseTransform.ToMatrixWithScale(), InRendererRenderInfo.VisualLogColor, TEXT(""));
			break;
		}
		case FInputWorldArea::EType::OOBox:
		{
			FOOBox2D OOBox = InInputWorldArea.GetOOBox();
			FBox VisualBounds(-FVector(OOBox.Extents, 0.0), FVector(OOBox.Extents, 0.0));
			UE_VLOG_WIREOBOX(InLandscape, LogLandscape, Log, VisualBounds, OOBox.Transform.ToMatrixWithScale(), InRendererRenderInfo.VisualLogColor, TEXT(""));
			break;
		}
		default:
			check(false);
		}
	}

	// Log the shapes of the render item output input render items if requested : 
	void VisLogRenderItemOutput(const ALandscape* InLandscape, bool bInIsHeightmapMerge, bool bInAffectsOutputLayerBitIndices, const TArrayView<const FName>& InRenderItemTargetLayerNames,
		const FOutputWorldArea& InOutputWorldArea, const FEditLayerRendererRenderInfo& InRendererRenderInfo, const FTransform& InLandscapeTransform, const FBox& InLandsapeLoadedBounds, const TArrayView<ULandscapeComponent*>& InComponentsToRender)
	{
		// Display the shapes 1 unreal unit (in world space) under the requested offset :
		static double OutputLocalOffsetLocalSpace = -1.0 / InLandscapeTransform.GetScale3D().Z;
		const double LandscapeComponentLocalSize = InLandscape->ComponentSizeQuads;
		FString LogMessage = InRendererRenderInfo.RendererState.GetRenderer()->GetEditLayerRendererDebugName();
		if (!bInIsHeightmapMerge)
		{
			LogMessage += FString::Printf(TEXT("\n%s%s%s"), *ConvertTargetLayerNamesToString(InRenderItemTargetLayerNames), !bInAffectsOutputLayerBitIndices ? TEXT("(") : TEXT(""), !bInAffectsOutputLayerBitIndices ? TEXT(")") : TEXT(""));
		}

		FTransform BaseTransform = FTransform(FVector(0.0, 0.0, InRendererRenderInfo.VisualLogOffsetLocalSpace + OutputLocalOffsetLocalSpace)) * InLandscapeTransform;
		switch (InOutputWorldArea.GetType())
		{
		case FOutputWorldArea::EType::LocalComponent:
		{
			// Local input area means each of the landscape components : 
			for (ULandscapeComponent* Component : InComponentsToRender)
			{
				FIntPoint ComponentKey = Component->GetComponentKey();
				FBox VisualBounds(
					FVector(ComponentKey.X * LandscapeComponentLocalSize, ComponentKey.Y * LandscapeComponentLocalSize, 0.0),
					FVector((ComponentKey.X + 1) * LandscapeComponentLocalSize, (ComponentKey.Y + 1) * LandscapeComponentLocalSize, 0.0));
				UE_VLOG_OBOX(InLandscape, LogLandscape, Log, VisualBounds, BaseTransform.ToMatrixWithScale(), InRendererRenderInfo.VisualLogColor, TEXT("%s"), *LogMessage);
			}
			break;
		}
		case FOutputWorldArea::EType::SpecificComponent:
		{
			FIntPoint ComponentKey = InOutputWorldArea.GetSpecificComponentKey();
			FBox VisualBounds(
				FVector(ComponentKey.X * LandscapeComponentLocalSize, ComponentKey.Y * LandscapeComponentLocalSize, 0.0),
				FVector((ComponentKey.X + 1) * LandscapeComponentLocalSize, (ComponentKey.Y + 1) * LandscapeComponentLocalSize, 0.0));
			UE_VLOG_OBOX(InLandscape, LogLandscape, Log, VisualBounds, BaseTransform.ToMatrixWithScale(), InRendererRenderInfo.VisualLogColor, TEXT("%s"), *LogMessage);
			break;
		}
		case FOutputWorldArea::EType::OOBox:
		{
			FOOBox2D OOBox = InOutputWorldArea.GetOOBox();
			FBox VisualBounds(-FVector(OOBox.Extents, 0.0), FVector(OOBox.Extents, 0.0));
			UE_VLOG_OBOX(InLandscape, LogLandscape, Log, VisualBounds, OOBox.Transform.ToMatrixWithScale(), InRendererRenderInfo.VisualLogColor, TEXT("%s"), *LogMessage);
			break;
		}
		default:
			check(false);
		}
	}
#endif // ENABLE_VISUAL_LOG
} // UE::Landscape::EditLayers::Private

void ULandscapeHeightmapNormalsEditLayerRenderer::RenderLayer(FRenderParams& InRenderParams)
{
	using namespace UE::Landscape::EditLayers;
	using namespace UE::Landscape::EditLayers::Private;

	FMergeRenderContext* RenderContext = InRenderParams.MergeRenderContext;
	const FMergeRenderBatch* RenderBatch = RenderContext->GetCurrentRenderBatch();
	ALandscape* Landscape = RenderContext->GetLandscape();

	checkf(InRenderParams.RenderGroupTargetLayerNames.Num() == 1, TEXT("Normals should only be generated on heightmap merge, which should have 1 and only target layer"));

	RenderContext->CycleBlendRenderTargets(/*InDesiredWriteAccess = */ERHIAccess::RTV);
	ULandscapeScratchRenderTarget* WriteRT = RenderContext->GetBlendRenderTargetWrite();
	ULandscapeScratchRenderTarget* ReadRT = RenderContext->GetBlendRenderTargetRead();
	ULandscapeScratchRenderTarget* ValidityRT = RenderContext->GetValidityRenderTarget(InRenderParams.RenderGroupTargetLayerNames[0]);
	check((WriteRT != nullptr) && (ReadRT != nullptr) && (ValidityRT != nullptr));

	ValidityRT->TransitionTo(ERHIAccess::SRVMask);

	FIntPoint NumComponentsInRenderArea = RenderBatch->MaxComponentKey - RenderBatch->MinComponentKey + 1;
	check((NumComponentsInRenderArea.X > 0) && (NumComponentsInRenderArea.Y > 0));

	ENQUEUE_RENDER_COMMAND(LandscapeLayers_Cmd_HeightmapsGenerateNormals)(
		[OutputResource = WriteRT->GetRenderTarget2D()->GetResource()
		, OutputResourceName = WriteRT->GetDebugName()
		, SourceResource = ReadRT->GetRenderTarget2D()->GetResource()
		, ValidityResource = ValidityRT->GetRenderTarget2D()->GetResource()
		, EffectiveTextureSize = RenderBatch->GetRenderTargetResolution(/*bInWithDuplicateBorders = */false)
		, LandscapeGridScale = Landscape->GetRootComponent()->GetRelativeScale3D()
		, ComponentSizeQuads = Landscape->ComponentSizeQuads
		, NumComponentsInRenderArea]
		(FRHICommandListImmediate& InRHICmdList)
	{
		SCOPED_DRAW_EVENTF(InRHICmdList, LandscapeLayers, TEXT("GenerateNormals -> %s"), OutputResourceName);
		FRDGBuilder GraphBuilder(InRHICmdList, RDG_EVENT_NAME("HeightmapsGenerateNormals"));

		FRDGTextureRef OutputTextureRef = GraphBuilder.RegisterExternalTexture(CreateRenderTarget(OutputResource->TextureRHI, TEXT("OutputTexture")), ERDGTextureFlags::SkipTracking);
		FRDGTextureRef SourceTextureRef = GraphBuilder.RegisterExternalTexture(CreateRenderTarget(SourceResource->TextureRHI, TEXT("SourceTexture")));
		FRDGTextureRef ValidityTextureRef = GraphBuilder.RegisterExternalTexture(CreateRenderTarget(ValidityResource->TextureRHI, TEXT("ValidityTexture")));

		FLandscapeEditLayersHeightmapsGenerateNormalsPS::FParameters* PSParams = GraphBuilder.AllocParameters<FLandscapeEditLayersHeightmapsGenerateNormalsPS::FParameters>();
		PSParams->RenderTargets[0] = FRenderTargetBinding(OutputTextureRef, ERenderTargetLoadAction::ENoAction);
		PSParams->InTextureSize = FUintVector4(EffectiveTextureSize.X, EffectiveTextureSize.Y, SourceResource->GetSizeX(), SourceResource->GetSizeY());
		PSParams->InLandscapeGridScale = static_cast<FVector3f>(LandscapeGridScale);
		PSParams->InComponentSizeQuads = ComponentSizeQuads;
		PSParams->InNumComponents = FUintVector2(NumComponentsInRenderArea.X, NumComponentsInRenderArea.Y);
		PSParams->InSourceHeightmapSampler = TStaticSamplerState<SF_Point, AM_Clamp, AM_Clamp>::GetRHI();
		PSParams->InSourceHeightmap = GraphBuilder.CreateSRV(FRDGTextureSRVDesc::Create(SourceTextureRef));
		PSParams->InValidityTexture = GraphBuilder.CreateSRV(FRDGTextureSRVDesc::Create(ValidityTextureRef));
		FLandscapeEditLayersHeightmapsGenerateNormalsPS::GenerateNormalsPS(GraphBuilder, PSParams, EffectiveTextureSize);
		GraphBuilder.Execute();
	});
}

void ULandscapeWeightmapWeightBlendedLayersRenderer::RenderLayer(FRenderParams& InRenderParams)
{
	using namespace UE::Landscape::EditLayers;
	using namespace UE::Landscape::EditLayers::Private;

	check(!InRenderParams.MergeRenderContext->IsHeightmapMerge());

	InRenderParams.MergeRenderContext->CycleBlendRenderTargets(ERHIAccess::RTV);
	ULandscapeScratchRenderTarget* WriteRT = InRenderParams.MergeRenderContext->GetBlendRenderTargetWrite();
	ULandscapeScratchRenderTarget* ReadRT = InRenderParams.MergeRenderContext->GetBlendRenderTargetRead();

	TArray<FWeightmapPaintLayerInfo> WeightmapPaintLayerInfos;
	WeightmapPaintLayerInfos.Reserve(InRenderParams.RenderGroupTargetLayerInfos.Num());
	Algo::Transform(InRenderParams.RenderGroupTargetLayerInfos, WeightmapPaintLayerInfos, [](ULandscapeLayerInfoObject* InLayerInfo)
		{
			check(InLayerInfo != nullptr); // There should only be valid layer infos at this point
			FWeightmapPaintLayerInfo WeightmapPaintLayerInfo;
			if (!InLayerInfo->bNoWeightBlend)
			{
				WeightmapPaintLayerInfo.Flags = EWeightmapPaintLayerFlags::IsWeightBlended;
			}
			if (InLayerInfo == ALandscapeProxy::VisibilityLayer)
			{
				WeightmapPaintLayerInfo.Flags = EWeightmapPaintLayerFlags::IsVisibilityLayer;
			}
			return WeightmapPaintLayerInfo;
		});

	ENQUEUE_RENDER_COMMAND(LandscapeLayers_Cmd_WeightmapsPerformLegacyWeightBlending)(
		[TargetLayerNames = InRenderParams.RenderGroupTargetLayerNames
		, WeightmapPaintLayerInfos
		, OutputResource = WriteRT->GetRenderTarget2DArray()->GetResource()
		, OutputResourceName = WriteRT->GetDebugName()
		, CurrentEditLayerResource = ReadRT->GetRenderTarget2DArray()->GetResource()
		, EffectiveTextureSize = InRenderParams.MergeRenderContext->GetCurrentRenderBatch()->GetRenderTargetResolution(/*bInWithDuplicateBorders = */false)]
		(FRHICommandListImmediate& InRHICmdList)
	{
		SCOPED_DRAW_EVENTF(InRHICmdList, LandscapeLayers, TEXT("LegacyWeightBlending -> %s"), OutputResourceName);
		FRDGBuilder GraphBuilder(InRHICmdList, RDG_EVENT_NAME("WeightmapsPerformLegacyWeightBlending"));

		FRDGTextureRef OutputTextureRef = GraphBuilder.RegisterExternalTexture(CreateRenderTarget(OutputResource->TextureRHI, TEXT("OutputTexture")), ERDGTextureFlags::SkipTracking);
		FRDGTextureRef CurrentEditLayerTextureRef = GraphBuilder.RegisterExternalTexture(CreateRenderTarget(CurrentEditLayerResource->TextureRHI, TEXT("CurrentEditLayerTexture")));
		FRDGTextureSRVRef CurrentEditLayerTextureSRVRef = GraphBuilder.CreateSRV(FRDGTextureSRVDesc::Create(CurrentEditLayerTextureRef));

		FRDGBufferRef PaintLayerInfosBuffer = CreateStructuredBuffer(GraphBuilder, TEXT("LandscapePaintLayerInfosBuffer"), WeightmapPaintLayerInfos);
		FRDGBufferSRVRef PaintLayerInfosBufferSRV = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(PaintLayerInfosBuffer));

		const int32 NumTargetLayers = TargetLayerNames.Num();
		for (int32 TargetLayerIndex = 0; TargetLayerIndex < NumTargetLayers; ++TargetLayerIndex)
		{
			FLandscapeEditLayersWeightmapsPerformLegacyWeightBlendingPS::FParameters* PSParams = GraphBuilder.AllocParameters<FLandscapeEditLayersWeightmapsPerformLegacyWeightBlendingPS::FParameters>();
			PSParams->RenderTargets[0] = FRenderTargetBinding(OutputTextureRef, ERenderTargetLoadAction::ENoAction, /*InMipIndex = */0, /*InArraySlice = */TargetLayerIndex);
			PSParams->InPaintLayerIndex = TargetLayerIndex;
			PSParams->InNumPaintLayers = NumTargetLayers;
			PSParams->InPaintLayerInfos = PaintLayerInfosBufferSRV;
			PSParams->InCurrentEditLayerWeightmaps = CurrentEditLayerTextureSRVRef;

			FLandscapeEditLayersWeightmapsPerformLegacyWeightBlendingPS::PerformLegacyWeightBlendingPS(GraphBuilder, PSParams, TargetLayerNames[TargetLayerIndex], EffectiveTextureSize);
		}
		GraphBuilder.Execute();
	});
}

UE::Landscape::EditLayers::FMergeRenderContext ALandscape::PrepareEditLayersMergeRenderContext(const UE::Landscape::EditLayers::FMergeRenderParams& InMergeRenderParams)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(ALandscape::PrepareEditLayersMergeRenderContext);

	using namespace UE::Landscape::EditLayers;
	using namespace UE::Landscape::EditLayers::Private;
	using namespace UE::Landscape::Private;

	const int32 VisualLogShowRenderItems = CVarLandscapeBatchedMergeVisualLogShowRenderItemsType.GetValueOnGameThread();
	const bool bVisualLogShowRenderItemsInput = (VisualLogShowRenderItems == 1) || (VisualLogShowRenderItems == 3);
	const bool bVisualLogShowRenderItemsOutput = (VisualLogShowRenderItems == 2) || (VisualLogShowRenderItems == 3);
	const bool bVisualLogShowAllRenderItems = CVarLandscapeBatchedMergeVisualLogShowAllRenderItems.GetValueOnGameThread();
	const FString VisualLogShowRenderItemsEditLayerRendererFilter = CVarLandscapeBatchedMergeVisualLogShowRenderItemsEditLayerRendererFilter.GetValueOnGameThread();
	const int32 VisualLogShowComponentDependencies = CVarLandscapeBatchedMergeVisualLogShowComponentDependencies.GetValueOnGameThread();
	const FString VisualLogShowComponentDependenciesFilter = CVarLandscapeBatchedMergeVisualLogShowComponentDependenciesFilter.GetValueOnGameThread();

	ULandscapeInfo* Info = GetLandscapeInfo();
	check(Info != nullptr);
	check(!LandscapeEditLayers.IsEmpty()); 

	const FTransform& LandscapeTransform = GetTransform();
	const FBox LandscapeLoadedBounds = Info->GetLoadedBounds();

	FMergeRenderContext MergeRenderContext(InMergeRenderParams.Landscape, InMergeRenderParams.bIsHeightmapMerge);

	// Start by gathering all possible target layer names on this landscape: 
	//  This list of all unique target layer names will help accelerate the gathering of output layers on each component (using bit arrays) as well as the target layers intersection tests:
	if (InMergeRenderParams.bIsHeightmapMerge)
	{
		// Only one target layer in the case of heightmap: 
		const FName TargetLayerName = FName("Height");
		MergeRenderContext.AllTargetLayerNames = { TargetLayerName };
		// And it's valid and final :
		MergeRenderContext.ValidTargetLayerBitIndices = TBitArray<>(true, 1);
		MergeRenderContext.FinalTargetLayerBitIndices = TBitArray<>(true, 1);
	}
	else
	{
		// Gather all target layer names and mark those that are valid layers :
		for (const FLandscapeInfoLayerSettings& LayerSettings : Info->Layers)
		{ 
			check(!LayerSettings.LayerName.IsNone());
			ULandscapeLayerInfoObject*& LayerInfo = MergeRenderContext.WeightmapLayerInfos.Add_GetRef(LayerSettings.LayerInfoObj);

			MergeRenderContext.AllTargetLayerNames.Add(LayerSettings.LayerName);
			MergeRenderContext.ValidTargetLayerBitIndices.Add(LayerInfo != nullptr);
			MergeRenderContext.FinalTargetLayerBitIndices.Add(false); // will be computed later...
		}
	}
	const int32 NumAllTargetLayerNames = MergeRenderContext.AllTargetLayerNames.Num();
	check(MergeRenderContext.ValidTargetLayerBitIndices.Num() == NumAllTargetLayerNames);
	check(MergeRenderContext.FinalTargetLayerBitIndices.Num() == NumAllTargetLayerNames);

	// InMergeRenderParams.EditLayerRendererStates contains a list of renderers that is not quite final : ULandscapeDefaultEditLayerRenderer is always inserted at the beginning to make sure we always render at 
	//  least the default value and ULandscapeWeightmapWeightBlendedLayersRenderer can optionally be inserted at the end too :
	TArray<FEditLayerRendererState> CandidateEditLayerRendererStates;
	{
		CandidateEditLayerRendererStates.Reserve(InMergeRenderParams.EditLayerRendererStates.Num() + 2);

		// We always have at least 1 renderer at the start : the default one, whose job is to both provide the default value and act as the final "gatherer" of the component dependencies 
		//  from all the renderers above (see class comment for more details) :
		ULandscapeDefaultEditLayerRenderer* InitialEditLayerRenderer = ULandscapeDefaultEditLayerRenderer::StaticClass()->GetDefaultObject<ULandscapeDefaultEditLayerRenderer>();
		CandidateEditLayerRendererStates.Emplace(InitialEditLayerRenderer, Info);

		// Then append all the provided renderer states
		CandidateEditLayerRendererStates.Append(InMergeRenderParams.EditLayerRendererStates);

		// Legacy weight-blending requires an additional renderer at the end of the stack, to weight-blend the weightmaps needing it :
		if (!InMergeRenderParams.bIsHeightmapMerge)
		{
			ULandscapeWeightmapWeightBlendedLayersRenderer* WeightmapWeightBlendedLayersRenderer = ULandscapeWeightmapWeightBlendedLayersRenderer::StaticClass()->GetDefaultObject<ULandscapeWeightmapWeightBlendedLayersRenderer>();
			if (!WeightmapWeightBlendedLayersRenderer->GatherWeightBlendedWeightmapLayerNames(Info).IsEmpty())
			{
				CandidateEditLayerRendererStates.Emplace(WeightmapWeightBlendedLayersRenderer, Info);
			}
		}
	}

	// FinalEditLayerRendererStates will contain the renderer states that are actually relevant to this merge :
	TArray<FEditLayerRendererState> FinalEditLayerRendererStates;
	FinalEditLayerRendererStates.Reserve(CandidateEditLayerRendererStates.Num());

	// Within each render batch, elements can be processed group by group. For heightmap/visibility, there's only one such group. For weightmaps, there's one group per list of weightmaps
	//  that need to be processed together for weight-blending. Each group is composed of a list of (weightmap layer) names (it's only a debug name in the case of heightmaps)

	// First, let's work out the weightmaps inter-dependencies (i.e. horizontal dependencies) : weight-blending requires some weightmaps to be processed together: 
	TArray<TBitArray<>> FinalRenderGroups;
	if (!InMergeRenderParams.bIsHeightmapMerge)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PrepareRenderGroups);

		TArray<TArray<TBitArray<>>> RelevantRenderGroupsPerRenderer;
		TArray<TBitArray<>> RelevantTargetLayerBitIndicesPerRenderer;
		RelevantRenderGroupsPerRenderer.Reserve(CandidateEditLayerRendererStates.Num());
		RelevantTargetLayerBitIndicesPerRenderer.Reserve(CandidateEditLayerRendererStates.Num());
		// Iterate through all renderers to find out what render group they render (only keep those that are relevant for the current state and request) : 
		for (const FEditLayerRendererState& EditLayerRendererState : CandidateEditLayerRendererStates)
		{
			TBitArray<> RendererStateEnabledTargetLayerBitIndices(MergeRenderContext.ConvertTargetLayerNamesToBitIndices(EditLayerRendererState.GetEnabledTargetWeightmaps()));
			// Retain only the target layer names that are enabled and valid :
			RendererStateEnabledTargetLayerBitIndices.CombineWithBitwiseAND(MergeRenderContext.ValidTargetLayerBitIndices, EBitwiseOperatorFlags::MinSize);

			// List of all supported render groups for this renderer :
			TArray<TBitArray<>> RelevantRenderGroupsForThisRenderer;
			for (const TSet<FName>& RenderGroup : EditLayerRendererState.GetWeightmapRenderGroups())
			{
				// Only retain the target layers that have passed the previous test (valid and enabled):
				TBitArray<> RenderGroupBitIndices = MergeRenderContext.ConvertTargetLayerNamesToBitIndices(RenderGroup.Array());
				RenderGroupBitIndices.CombineWithBitwiseAND(RendererStateEnabledTargetLayerBitIndices, EBitwiseOperatorFlags::MinSize);
				if (RenderGroupBitIndices.Find(true) != INDEX_NONE)
				{
					checkf(Algo::NoneOf(RelevantRenderGroupsForThisRenderer, [&RenderGroupBitIndices](const TBitArray<>& InOtherRenderGroupBitIndices) 
						{ 
							return (TBitArray<>::BitwiseAND(InOtherRenderGroupBitIndices, RenderGroupBitIndices, EBitwiseOperatorFlags::MinSize).Find(true) != INDEX_NONE);
						}), 
						TEXT("All of the target layers returned by the renderer must belong to 1 render group of this renderer and 1 only"));
					RelevantRenderGroupsForThisRenderer.Add(RenderGroupBitIndices);
				}
			}

			RelevantRenderGroupsPerRenderer.Add(RelevantRenderGroupsForThisRenderer);
			RelevantTargetLayerBitIndicesPerRenderer.Add(RendererStateEnabledTargetLayerBitIndices);
		}
		check(CandidateEditLayerRendererStates.Num() == RelevantRenderGroupsPerRenderer.Num());
		check(CandidateEditLayerRendererStates.Num() == RelevantTargetLayerBitIndicesPerRenderer.Num());

		const int32 CandidateNumRenderers = CandidateEditLayerRendererStates.Num();
		TBitArray<> RequestedTargetLayerBitIndices = MergeRenderContext.ConvertTargetLayerNamesToBitIndices(InMergeRenderParams.WeightmapLayerNames.Array());
		// No need to retain the invalid target layers : 
		RequestedTargetLayerBitIndices.CombineWithBitwiseAND(MergeRenderContext.ValidTargetLayerBitIndices, EBitwiseOperatorFlags::MinSize);
		// Early-out when there's nothing to do :
		if (RequestedTargetLayerBitIndices.Find(true) == INDEX_NONE)
		{
			return MergeRenderContext;
		}

		FinalRenderGroups.Reserve(MergeRenderContext.ValidTargetLayerBitIndices.CountSetBits());
		// Start with minimal render groups : one per requested target layer : 
		for (TConstSetBitIterator It(RequestedTargetLayerBitIndices); It; ++It)
		{
			TBitArray<> RenderGroup(false, NumAllTargetLayerNames);
			RenderGroup[It.GetIndex()] = true;
			FinalRenderGroups.Add(MoveTemp(RenderGroup));
		}

		// Then iterate in reverse order on renderers to trace the dependency of each of their render groups towards one another and move target layers from one render group to another as we discover new dependencies:
		MergeRenderContext.FinalTargetLayerBitIndices = RequestedTargetLayerBitIndices;
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(AnalyzeRenderersForRenderGroups);

			for (int32 CurrentRendererIndex = CandidateNumRenderers - 1; CurrentRendererIndex >= 0; --CurrentRendererIndex)
			{
				const TArray<TBitArray<>>& RendererRenderGroups = RelevantRenderGroupsPerRenderer[CurrentRendererIndex];
				const int32 NumRenderGroups = RendererRenderGroups.Num();
				for (int32 RenderGroupIndex = 0; RenderGroupIndex < NumRenderGroups; ++RenderGroupIndex)
				{
					const TBitArray<>& RenderGroup = RendererRenderGroups[RenderGroupIndex];

					TBitArray<> NewMergedRenderGroup(false, NumAllTargetLayerNames);
					// In all of the final render groups, find the ones that have a layer in common with this render group and merge them all into a single new one
					FinalRenderGroups.RemoveAllSwap([&NewMergedRenderGroup, &RenderGroup](const TBitArray<>& InFinalRenderGroup)
						{
							const bool bShouldMerge = (InFinalRenderGroup != RenderGroup)
								&& (TBitArray<>::BitwiseAND(InFinalRenderGroup, RenderGroup, EBitwiseOperatorFlags::MinSize).Find(true) != INDEX_NONE);
							if (bShouldMerge)
							{
								NewMergedRenderGroup.CombineWithBitwiseOR(InFinalRenderGroup, EBitwiseOperatorFlags::MinSize);
							}
							return bShouldMerge;
						});
					// Now add it back to the list of final render groups if it's valid, so that it can be merged by the next renderer if required :
					if (NewMergedRenderGroup.CountSetBits() > 0)
					{
						FinalRenderGroups.Add(NewMergedRenderGroup);
						MergeRenderContext.FinalTargetLayerBitIndices.CombineWithBitwiseOR(NewMergedRenderGroup, EBitwiseOperatorFlags::MinSize);
					}
				}
			}
		}

		{
			TRACE_CPUPROFILER_EVENT_SCOPE(FinalizeRenderGroups);

			// Now that we have the list of all target layers required for rendering the requested ones, we trim the list of edit layer renderers that just don't do anything with those:
			for (int32 RendererIndex = 0; RendererIndex < CandidateNumRenderers; ++RendererIndex)
			{
				const TBitArray<> RelevantTargetLayerBitIndices = RelevantTargetLayerBitIndicesPerRenderer[RendererIndex];
				if (TBitArray<>::BitwiseAND(RelevantTargetLayerBitIndices, MergeRenderContext.FinalTargetLayerBitIndices, EBitwiseOperatorFlags::MinSize).Find(true) != INDEX_NONE)
				{
					FinalEditLayerRendererStates.Add(CandidateEditLayerRendererStates[RendererIndex]);
				}
			}

			// TODO [jonathan.bard] : revisit : this shouldn't be true now that we support additively resolving channels with blending : 
#if !SUPPORTS_LANDSCAPE_EDITORONLY_UBER_MATERIAL
			// When 4 weightmaps are packed in 1 RGBA channel, we cannot guarantee that weightmaps will be fully resolvable when a group is done, since there's no guarantee that a given component's 4 
			//  allocations will be contained in that group. So we create a single group instead, that contains all weightmap layers. This consumes more memory since we need 3 texture arrays for a batch and 
			//  the number of weightmaps in a group is the number of slices of the array : 
			FinalRenderGroups = { MergeRenderContext.FinalTargetLayerBitIndices };
#endif // !SUPPORTS_LANDSCAPE_EDITORONLY_UBER_MATERIAL
		}
	}
	else
	{
		// For heightmap, there's no other dependency so simply trim all renderers that are not enabled : 
		FinalEditLayerRendererStates = CandidateEditLayerRendererStates.FilterByPredicate([&InMergeRenderParams](const FEditLayerRendererState& InRendererState)
		{
			return InRendererState.IsTargetEnabled(ELandscapeToolTargetType::Heightmap);
		});

		// Only one group in the case of heightmap: 
		const FName TargetLayerName = FName("Height");
		FinalRenderGroups = { TBitArray<>(true, 1) };
	}

	const int32 FinalNumRenderers = FinalEditLayerRendererStates.Num();
	
	// Early-out when there's nothing to do :
	if (FinalNumRenderers == 0)
	{
		return MergeRenderContext;
	}

	const ELandscapeToolTargetTypeFlags MergeTypeMask = InMergeRenderParams.bIsHeightmapMerge ? ELandscapeToolTargetTypeFlags::Heightmap : (ELandscapeToolTargetTypeFlags::Weightmap | ELandscapeToolTargetTypeFlags::Visibility);

	FLandscapeComponent2DIndexer Component2DIndexer = CreateLandscapeComponent2DIndexer(Info);
	const TArray<ULandscapeComponent*> AllComponents = Component2DIndexer.GetAllValues();
	const TBitArray<> AllValidComponentBitIndices = Component2DIndexer.GetValidValueBitIndices();
	const int32 NumAllComponents = AllComponents.Num();

	// The list of all components that will end up being rendered across all renderers (one bit per component) :
	TBitArray<> FinalComponentsToRenderInfoBitIndices(false, NumAllComponents);

	// Pre-allocate a working list of all landscape components render info. Some of which we might not end up rendering, but at least, that allows to associate a component with an index, 
	//  which allows to turn intersection/union of components (which we do a lot in this function) into simple bit array bitwise AND/OR operations : 
	TArray<FComponentToRenderInfo> AllComponentsToRenderInfos;
	TArray<FEditLayerRendererRenderInfo> OrderedEditLayerRendererRenderInfos;

#if ENABLE_VISUAL_LOG
	int32 VisualLogShowComponentDependenciesIndex = INDEX_NONE;
	if (!VisualLogShowComponentDependenciesFilter.IsEmpty())
	{
		FIntPoint ComponentKey;
		if (ComponentKey.InitFromString(VisualLogShowComponentDependenciesFilter))
		{
			VisualLogShowComponentDependenciesIndex = Component2DIndexer.GetValueIndexForKeySafe(ComponentKey);
			if (VisualLogShowComponentDependenciesIndex == INDEX_NONE)
			{
				UE_LOG(LogLandscape, Warning, TEXT("Component key \"%s\" specified for dependencies filter does not correspond to a valid component. Ignoring show component dependencies filter"), *VisualLogShowComponentDependenciesFilter);
			}
		}
		else
		{
			UE_LOG(LogLandscape, Warning, TEXT("Cannot parse string \"%s\". Ignoring show component dependencies filter"), *VisualLogShowComponentDependenciesFilter);
		}
	}

	// Helper for debugging component dependencies : only if the CVar requires it
	TOptional<FComponentDependenciesVisLogHelper> VisualLogDependencyHelper;
	UE_IFVLOG(
			if ((MergeRenderContext.IsVisualLogEnabled()) && ((VisualLogShowComponentDependencies > 0) || (VisualLogShowComponentDependenciesIndex != INDEX_NONE)))
			{
				// Force the display of all info when we show the dependencies of one component in particular : 
				const FComponentDependenciesVisLogHelper::EShowNodeInfo ShowNodeInfo = (VisualLogShowComponentDependenciesIndex != INDEX_NONE)
					? FComponentDependenciesVisLogHelper::EShowNodeInfo::Detailed
					: static_cast<FComponentDependenciesVisLogHelper::EShowNodeInfo>(VisualLogShowComponentDependencies);
				VisualLogDependencyHelper.Emplace(this, InMergeRenderParams.bIsHeightmapMerge, ShowNodeInfo, MergeRenderContext);
			}
		);
	auto VisLogDependency = [VisualLogShowComponentDependenciesIndex, &VisualLogDependencyHelper, &AllComponentsToRenderInfos, &OrderedEditLayerRendererRenderInfos]
		(int32 InSourceComponentIndex, int32 InSourceRendererIndex, int32 InDestinationComponentIndex, int32 InDestinationRendererIndex)
		{
			if (VisualLogDependencyHelper.IsSet() 
				&& (InSourceRendererIndex >= 0) 
				&& ((VisualLogShowComponentDependenciesIndex == INDEX_NONE) || (InSourceComponentIndex == VisualLogShowComponentDependenciesIndex)))
			{
				VisualLogDependencyHelper->AddDependency(AllComponentsToRenderInfos[InSourceComponentIndex], OrderedEditLayerRendererRenderInfos[InSourceRendererIndex],
					AllComponentsToRenderInfos[InDestinationComponentIndex], OrderedEditLayerRendererRenderInfos[InDestinationRendererIndex]);
			}
		};
#endif // ENABLE_VISUAL_LOG

	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PrepareRenderersAnalysis);

		AllComponentsToRenderInfos.AddDefaulted(NumAllComponents);
		for (TConstSetBitIterator It(AllValidComponentBitIndices); It; ++It)
		{
			const int32 ComponentIndex = It.GetIndex();
			AllComponentsToRenderInfos[ComponentIndex] = FComponentToRenderInfo(AllComponents[ComponentIndex], ComponentIndex, NumAllComponents, NumAllTargetLayerNames);
			MergeRenderContext.MaxLocalHeight = FMath::Max(MergeRenderContext.MaxLocalHeight, AllComponentsToRenderInfos[ComponentIndex].LocalBounds.Max.Z);
		}

		// Initiate the process by flipping a bit for each component to merge, for the last renderer in the stack, then we'll register the dependencies 
		//  between components on this renderer and those on the previous renderer by iterating on renderers in reverse stack order : 
		for (ULandscapeComponent* Component : InMergeRenderParams.ComponentsToMerge)
		{
			const int32 ComponentIndex = Component2DIndexer.GetValueIndexChecked(Component);
			FinalComponentsToRenderInfoBitIndices[ComponentIndex] = true;
		}

		// Prepare the render infos of all these renderers : 
		OrderedEditLayerRendererRenderInfos.Reserve(FinalNumRenderers);
		for (int32 RendererIndex = 0; RendererIndex < FinalNumRenderers; ++RendererIndex)
		{
			OrderedEditLayerRendererRenderInfos.Emplace(FinalEditLayerRendererStates[RendererIndex], RendererIndex, LandscapeTransform, MergeRenderContext.MaxLocalHeight, NumAllComponents);
		}
	}

	// Iterate over all renderers in inverse order to compute which landscape component needs to be included in the render. This way, the renderers on top are able to 
	//  request potentially more components from renderers underneath (e.g. if the renderer performs a blur, it will require an area around the component's area, thus additional components, potentially, 
	//  which will then request potentially more components on the renderer underneath, etc.)
	// For each renderer we'll only iterate on nodes that correspond to it. Since they're added in reverse order, this is just a matter of starting the iteration from the first of the renderer's nodes :
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(AnalyzeRenderers);
		for (int32 CurrentRendererIndex = FinalNumRenderers - 1; CurrentRendererIndex >= 0; --CurrentRendererIndex)
		{
			FEditLayerRendererRenderInfo& EditLayerRendererRenderInfo = OrderedEditLayerRendererRenderInfos[CurrentRendererIndex];

			const FString EditLayerRendererDebugName = EditLayerRendererRenderInfo.RendererState.GetRenderer()->GetEditLayerRendererDebugName();
			const bool bPassesVisualLogRenderItemFilter = VisualLogShowRenderItemsEditLayerRendererFilter.IsEmpty() || EditLayerRendererDebugName.Contains(VisualLogShowRenderItemsEditLayerRendererFilter);

			// This renderer will eventually append new components to render along the way so we add them to a new list and swap at the end : 
			TBitArray<> ComponentsToRenderInfoBitIndicesAfterRenderer = FinalComponentsToRenderInfoBitIndices;

			// Intersect this renderer's render items' outputs with the components to render to find out which ones should participate to the merge :
			for (const FEditLayerRenderItem& RenderItem : EditLayerRendererRenderInfo.RendererState.GetRenderer()->GetRenderItems(Info))
			{
				const FEditLayerTargetTypeState& RenderItemTargetTypeState = RenderItem.GetTargetTypeState();
				checkf(RenderItemTargetTypeState.Intersect(EditLayerRendererRenderInfo.RendererState.GetSupportedTargetTypeState()) == RenderItemTargetTypeState,
					TEXT("One of edit layer renderer %s's render items target type state is not supported by the renderer's supported target type state. \
					This is invalid : the renderer's supported target type state should be a superset of its render item's target type state. \n\
					- Render item state: %s \n\
					- Renderer supported state: %s \n"), *EditLayerRendererDebugName, *RenderItemTargetTypeState.ToString(), *EditLayerRendererRenderInfo.RendererState.GetSupportedTargetTypeState().ToString());

				bool bOutputRenderItem = false;
				const FOutputWorldArea& OutputWorldArea = RenderItem.GetOutputWorldArea();
				// Only consider render items which affect the type of merge being performed: 
				const bool bRenderItemAffectsMergeType = EnumHasAnyFlags(RenderItemTargetTypeState.GetTargetTypeMask(), MergeTypeMask);
				if (bRenderItemAffectsMergeType)
				{
					switch (OutputWorldArea.GetType())
					{
					case FOutputWorldArea::EType::LocalComponent:
					{
						// This render item writes to every component : 
						bOutputRenderItem = true;
						break;
					}
					case FOutputWorldArea::EType::SpecificComponent:
					{
						// This render item writes to only 1 component :
						if (int32 ComponentIndex = Component2DIndexer.GetValueIndexForKeySafe(OutputWorldArea.GetSpecificComponentKey()); ComponentIndex != INDEX_NONE)
						{
							// Retain the render item if the component it renders to overlaps with one the components to render :
							bOutputRenderItem = FinalComponentsToRenderInfoBitIndices[ComponentIndex];
						}
						break;
					}
					case FOutputWorldArea::EType::OOBox:
					{
						FIntRect ComponentIndicesBoundingRect;
						TMap<FIntPoint, ULandscapeComponent*> DependentComponents;
						// TODO [jonathan.bard] : Change this to OOBB to OOBB test to cull more components : look at FOrientedBoxHelpers
						const FOOBox2D& OOBox = OutputWorldArea.GetOOBox();
						Info->GetOverlappedComponents(OOBox.Transform, FBox2D(-OOBox.Extents / 2.0, OOBox.Extents / 2.0), DependentComponents, ComponentIndicesBoundingRect);
						for (auto It = DependentComponents.CreateConstIterator(); It; ++It && !bOutputRenderItem)
						{
							int32 ComponentIndex = Component2DIndexer.GetValueIndexForKeyChecked(It.Key());
							// Retain the render item if one of the components it renders to overlaps with one the components to render :
							bOutputRenderItem = FinalComponentsToRenderInfoBitIndices[ComponentIndex];
						}
						break;
					}
					default:
						check(false);
					}
				}

				// Mark which output layers of the component this render item will only need to affect :
				// For a weightmap merge, it's possible the render item will only end up modifying the existing ones (as opposed to "generating" new ones).
				//  In that case, restrain the render item from modifying the target layer mask for this component :
				const bool bAffectsOutputLayerBitIndices = MergeRenderContext.IsHeightmapMerge() || !RenderItem.GetModifyExistingWeightmapsOnly();

				UE_IFVLOG(
					{
						if (MergeRenderContext.IsVisualLogEnabled() && bRenderItemAffectsMergeType && bPassesVisualLogRenderItemFilter)
						{
							if (bVisualLogShowRenderItemsInput && (bOutputRenderItem || bVisualLogShowAllRenderItems))
							{
								TArray<ULandscapeComponent*> AllComponentsToVisLog = bVisualLogShowAllRenderItems ? Component2DIndexer.GetValidValues() : Component2DIndexer.GetValidValuesForBitIndices(FinalComponentsToRenderInfoBitIndices);
								VisLogRenderItemInput(this, RenderItem.GetInputWorldArea(), EditLayerRendererRenderInfo, LandscapeTransform, LandscapeLoadedBounds, MakeArrayView(AllComponentsToVisLog));
							}

							if (bVisualLogShowRenderItemsOutput && (bOutputRenderItem || bVisualLogShowAllRenderItems))
							{
								TArray<ULandscapeComponent*> AllComponentsToVisLog = bVisualLogShowAllRenderItems ? Component2DIndexer.GetValidValues() : Component2DIndexer.GetValidValuesForBitIndices(FinalComponentsToRenderInfoBitIndices);
								VisLogRenderItemOutput(this, MergeRenderContext.IsHeightmapMerge(), bAffectsOutputLayerBitIndices, MakeArrayView(RenderItemTargetTypeState.GetActiveWeightmaps()),
									OutputWorldArea, EditLayerRendererRenderInfo, LandscapeTransform, LandscapeLoadedBounds, MakeArrayView(AllComponentsToVisLog));
							}
						}
					});

				if (bOutputRenderItem)
				{
					TBitArray<> InputComponentBitIndices(false, NumAllComponents);
					const TBitArray<>* ComponentsToIterateBitIndices = nullptr;

					const FInputWorldArea& InputWorldArea = RenderItem.GetInputWorldArea();
					switch (InputWorldArea.GetType())
					{
					case FInputWorldArea::EType::Infinite:
					{
						InputComponentBitIndices = Component2DIndexer.GetValidValueBitIndices();
						ComponentsToIterateBitIndices = &InputComponentBitIndices;
						break;
					}
					case FInputWorldArea::EType::LocalComponent:
					{
						// This render item requires the component itself and potentially its neighbors, so we need to iterate on all the components currently being processed : 
						ComponentsToIterateBitIndices = &FinalComponentsToRenderInfoBitIndices;
						break;
					}
					case FInputWorldArea::EType::SpecificComponent:
					{
						InputComponentBitIndices = Component2DIndexer.GetValidValueBitIndicesInBounds(InputWorldArea.GetSpecificComponentKeys(), /* bInInclusiveBounds = */true);
						ComponentsToIterateBitIndices = &InputComponentBitIndices;
						break;
					}
					case FInputWorldArea::EType::OOBox:
					{
						FIntRect ComponentIndicesBoundingRect;
						TMap<FIntPoint, ULandscapeComponent*> DependentComponents;
						// TODO [jonathan.bard] : Change this to OOBB to OOBB test to cull more components
						const FOOBox2D& OOBox = InputWorldArea.GetOOBox();
						Info->GetOverlappedComponents(OOBox.Transform, FBox2D(-OOBox.Extents / 2.0, OOBox.Extents / 2.0), DependentComponents, ComponentIndicesBoundingRect);
						for (auto It : DependentComponents)
						{
							int32 ComponentIndex = Component2DIndexer.GetValueIndexForKeyChecked(It.Key);
							InputComponentBitIndices[ComponentIndex] = true;
						}
						ComponentsToIterateBitIndices = &InputComponentBitIndices;
						break;
					}
					default:
						check(false);
					}
					check(ComponentsToIterateBitIndices != nullptr);

					// List all target layers written by this render item :
					TBitArray<> OutputLayerBitIndices;
					if (bAffectsOutputLayerBitIndices)
					{
						if (MergeRenderContext.IsHeightmapMerge())
						{
							// Only one target layer in the case of a heightmap merge
							check(MergeRenderContext.AllTargetLayerNames.Num() == 1);
							OutputLayerBitIndices.SetNum(1, true);
						}
						else
						{
							OutputLayerBitIndices = MergeRenderContext.ConvertTargetLayerNamesToBitIndices(RenderItemTargetTypeState.GetActiveWeightmaps());
						}
					}

					// Iterate on all the required components :
					for (TConstSetBitIterator It(*ComponentsToIterateBitIndices); It; ++It)
					{
						const int32 ComponentToRenderIndex = It.GetIndex();
						FComponentToRenderInfo& ComponentToRenderInfo = AllComponentsToRenderInfos[ComponentToRenderIndex];
						check(ComponentToRenderInfo.Component != nullptr);

						// Add the render item's target layers to the component's own : 
						if (bAffectsOutputLayerBitIndices)
						{
							ComponentToRenderInfo.TargetLayerBitIndices.CombineWithBitwiseOR(OutputLayerBitIndices, EBitwiseOperatorFlags::MinSize);
						}

						// Special case for FInputWorldArea::EType::LocalComponent, where the input components are specific to the component being iterated : 
						if (InputWorldArea.GetType() == FInputWorldArea::EType::LocalComponent)
						{
							InputComponentBitIndices = Component2DIndexer.GetValidValueBitIndicesInBounds(InputWorldArea.GetLocalComponentKeys(ComponentToRenderInfo.Component->GetComponentKey()), /* bInInclusiveBounds = */true);
						}

						// There should always be a dependency between this renderer and the previous in the stack for the component itself : 
						InputComponentBitIndices[ComponentToRenderIndex] = true;

						// If these components are not yet in the final list of components to render, add them: 
						ComponentsToRenderInfoBitIndicesAfterRenderer.CombineWithBitwiseOR(InputComponentBitIndices, EBitwiseOperatorFlags::MinSize);

						// Add these components to the list that this renderer needs to render: 
						EditLayerRendererRenderInfo.ComponentToRenderInfoBitIndices.CombineWithBitwiseOR(InputComponentBitIndices, EBitwiseOperatorFlags::MinSize);

						// Finally add these components as dependencies to the component we're trying to render : 
						ComponentToRenderInfo.DependentComponentBitIndices.CombineWithBitwiseOR(InputComponentBitIndices, EBitwiseOperatorFlags::MinSize);

#if ENABLE_VISUAL_LOG
						if (VisualLogDependencyHelper.IsSet())
						{
							for (TConstSetBitIterator ItInputComponent(InputComponentBitIndices); ItInputComponent; ++ItInputComponent)
							{
								// Register a dependency from the component we want to render towards all of the components its input area overlaps with on the previous renderer : 
								const int32 DependentComponentIndex = ItInputComponent.GetIndex();
								VisLogDependency(DependentComponentIndex, CurrentRendererIndex - 1, ComponentToRenderIndex, CurrentRendererIndex);
							}
						}
#endif // ENABLE_VISUAL_LOG
					}
				}
				else // !bOutputRenderItem
				{
#if ENABLE_VISUAL_LOG
					// Declare a passthrough dependency between each component on this renderer to the next, to display the full chain of dependencies : 
					if (VisualLogDependencyHelper.IsSet())
					{
						for (TConstSetBitIterator It(ComponentsToRenderInfoBitIndicesAfterRenderer); It; ++It)
						{
							const int32 ComponentToRenderIndex = It.GetIndex();
							VisLogDependency(ComponentToRenderIndex, CurrentRendererIndex - 1, ComponentToRenderIndex, CurrentRendererIndex);
						}
					}
#endif // ENABLE_VISUAL_LOG
				}
			}

			// The renderer has been fully processed, now we can update the list of components to render for the next renderer in line : 
			Swap(ComponentsToRenderInfoBitIndicesAfterRenderer, FinalComponentsToRenderInfoBitIndices);
		}
	}

	for (TConstSetBitIterator It(FinalComponentsToRenderInfoBitIndices); It; ++It)
	{
		const int32 ComponentIndex = It.GetIndex();
		FComponentToRenderInfo& ComponentToRenderInfo = AllComponentsToRenderInfos[ComponentIndex];
		// Now compute the bounds to finalize this component render info (it's faster to do via Component2DIndexer than iterating through components) : 
		FIntRect DependentComponentsInclusiveBounds = Component2DIndexer.GetValidValuesBoundsForBitIndices(ComponentToRenderInfo.DependentComponentBitIndices, /*bInInclusiveBounds = */true);
		ComponentToRenderInfo.Finalize(DependentComponentsInclusiveBounds, ComponentSizeQuads);
	}

#if ENABLE_VISUAL_LOG
	if (VisualLogDependencyHelper.IsSet() && (VisualLogShowComponentDependencies > 0))
	{
		// Display a node for every component that will be rendered in the end: 
		for (TConstSetBitIterator It(FinalComponentsToRenderInfoBitIndices); It; ++It)
		{
			VisualLogDependencyHelper->AddNode(AllComponentsToRenderInfos[It.GetIndex()], OrderedEditLayerRendererRenderInfos[0]);
		}
	}
#endif // ENABLE_VISUAL_LOG

	// Now divide the work into batches as large as possible (but fitting in the desired max batch resolution, if possible): 
	TArray<FRenderBatchInfo> AllBatchInfos = DivideIntoBatches(FinalComponentsToRenderInfoBitIndices, AllComponentsToRenderInfos);
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PrepareBatches);
		MergeRenderContext.RenderBatches.Reserve(AllBatchInfos.Num());
		MergeRenderContext.TargetLayersToComponents.AddDefaulted(MergeRenderContext.AllTargetLayerNames.Num());
		for (const FRenderBatchInfo& BatchInfo : AllBatchInfos)
		{
			int32 MergeBatchIndex = MergeRenderContext.RenderBatches.Num();
			FMergeRenderBatch& MergeRenderBatch = MergeRenderContext.RenderBatches.Emplace_GetRef();
			MergeRenderBatch.Landscape = this;
			MergeRenderBatch.SectionRect = BatchInfo.CombinedSectionRect;
			MergeRenderBatch.MinComponentKey = MergeRenderBatch.MinComponentKey.ComponentMin(BatchInfo.MinComponentKey);
			MergeRenderBatch.MaxComponentKey = MergeRenderBatch.MaxComponentKey.ComponentMax(BatchInfo.MaxComponentKey);
			// We'll reuse the same merge render targets in order to generate the mips, which include the duplicate borders, so we need to expand the render target's size to accommodate for this : 
			MergeRenderBatch.Resolution = (MergeRenderBatch.MaxComponentKey - MergeRenderBatch.MinComponentKey + 1) * NumSubsections * (SubsectionSizeQuads + 1);
			MergeRenderBatch.TargetLayersToComponents.AddDefaulted(MergeRenderContext.AllTargetLayerNames.Num());
			MergeRenderBatch.TargetLayerNameBitIndices.SetNum(MergeRenderContext.AllTargetLayerNames.Num(), false);

			MergeRenderContext.MaxNeededResolution = MergeRenderContext.MaxNeededResolution.ComponentMax(MergeRenderBatch.Resolution);
		
			// For each renderer, find the list of components actually needed and store that in a separate array, to avoid recomputing it for each target group : 
			int32 LocalNumRenderers = OrderedEditLayerRendererRenderInfos.Num();
			TArray<TArray<ULandscapeComponent*>> ComponentsForRenderers;
			ComponentsForRenderers.SetNum(LocalNumRenderers);
			for (int32 EditLayerRendererIndex = 0; EditLayerRendererIndex < LocalNumRenderers; ++EditLayerRendererIndex)
			{
				const FEditLayerRendererRenderInfo& EditLayerRendererRenderInfo = OrderedEditLayerRendererRenderInfos[EditLayerRendererIndex];
				// Find out all components that are in common between the renderer's list and the batch's list : only these will need to be rendered in that render step : 
				TBitArray<> ComponentToRenderForRendererBitIndices = TBitArray<>::BitwiseAND(EditLayerRendererRenderInfo.ComponentToRenderInfoBitIndices, BatchInfo.ComponentToRenderInfoBitIndices, EBitwiseOperatorFlags::MinSize);
				TArray<ULandscapeComponent*>& ComponentsToRender = ComponentsForRenderers[EditLayerRendererIndex];
				ComponentsToRender.Reserve(ComponentToRenderForRendererBitIndices.CountSetBits());
				// Transform the bit indices back into a proper component list : 
				for (TConstSetBitIterator ItComponent(ComponentToRenderForRendererBitIndices); ItComponent; ++ItComponent)
				{
					FComponentToRenderInfo& ComponentToRenderInfo = AllComponentsToRenderInfos[ItComponent.GetIndex()];
					ComponentsToRender.Add(ComponentToRenderInfo.Component);

					// Inform the render batch and context of the target layer names associated with each component :

					// Declare these target layers as being in use for the batch :
					MergeRenderBatch.TargetLayerNameBitIndices.CombineWithBitwiseOR(ComponentToRenderInfo.TargetLayerBitIndices, EBitwiseOperatorFlags::MinSize);
					MergeRenderBatch.ComponentToTargetLayerBitIndices.FindOrAdd(ComponentToRenderInfo.Component).CombineWithBitwiseOR(ComponentToRenderInfo.TargetLayerBitIndices, EBitwiseOperatorFlags::MaxSize); // Use EBitwiseOperatorFlags::MaxSize here in order to allocate NumTargetLayerNames entries to the resulting bit array in case FindOrAdd is an add
					MergeRenderContext.ComponentToTargetLayerBitIndices.FindOrAdd(ComponentToRenderInfo.Component).CombineWithBitwiseOR(ComponentToRenderInfo.TargetLayerBitIndices, EBitwiseOperatorFlags::MaxSize); // Use EBitwiseOperatorFlags::MaxSize here in order to allocate NumTargetLayerNames entries to the resulting bit array in case FindOrAdd is an add
					for (TConstSetBitIterator ItTargetLayer(ComponentToRenderInfo.TargetLayerBitIndices); ItTargetLayer; ++ItTargetLayer)
					{
						const int32 TargetLayerIndex = ItTargetLayer.GetIndex();
						MergeRenderBatch.TargetLayersToComponents[TargetLayerIndex].Add(ComponentToRenderInfo.Component);
						MergeRenderContext.TargetLayersToComponents[TargetLayerIndex].Add(ComponentToRenderInfo.Component);
					}
				}

				// Declare these components as being in use for the batch : 
				MergeRenderBatch.ComponentsToRender.Append(ComponentsToRender);
			}

			// Now, we have all the info to build our list of successive render steps : process group by group :
			for (const TBitArray<>& RenderGroup : FinalRenderGroups)
			{
				// Execute the required operations for the whole stack of renderers for this group :
				for (int32 RendererIndex = 0; RendererIndex < LocalNumRenderers; ++RendererIndex)
				{
					const FEditLayerRendererRenderInfo& EditLayerRendererRenderInfo = OrderedEditLayerRendererRenderInfos[RendererIndex];
					TArrayView<ULandscapeComponent*> ComponentsToRender = MakeArrayView(ComponentsForRenderers[RendererIndex]);

					if (!ComponentsToRender.IsEmpty() && EditLayerRendererRenderInfo.RendererState.GetRenderer()->CanRender())
					{
						// Render the content of this render layer : 
						MergeRenderBatch.RenderSteps.Emplace(FMergeRenderStep::EType::RenderLayer, EditLayerRendererRenderInfo.RendererState, RenderGroup, ComponentsToRender);
					}
				}

				// Finally, signal the group as done for this batch
				MergeRenderBatch.RenderSteps.Emplace(FMergeRenderStep::EType::SignalBatchMergeGroupDone, FEditLayerRendererState(), RenderGroup, MergeRenderBatch.ComponentsToRender.Array());

				MergeRenderContext.MaxNeededNumSlices = FMath::Max(MergeRenderContext.MaxNeededNumSlices, RenderGroup.CountSetBits());
			}
		}

		// Sort the batches for easier debugging : 
		MergeRenderContext.RenderBatches.Sort();
	}

	return MergeRenderContext;
}

int32 ALandscape::PerformLayersHeightmapsGlobalMerge(const FUpdateLayersContentContext& InUpdateLayersContentContext, const FEditLayersHeightmapMergeParams& InMergeParams)
{
	ULandscapeInfo* Info = GetLandscapeInfo();
	check(Info != nullptr);
	check(HeightmapRTList.Num() > 0);

	FIntRect LandscapeExtent;
	if (!Info->GetLandscapeExtent(LandscapeExtent.Min.X, LandscapeExtent.Min.Y, LandscapeExtent.Max.X, LandscapeExtent.Max.Y))
	{
		return 0;
	}

	// Use to compute top-left vertex position per Heightmap and the actual size to copy : 
	struct FHeightmapCopyInfo
	{
		FHeightmapCopyInfo(UTexture2D* InTexture, const FIntPoint& InComponentVertexPosition, int32 InComponentSizeVerts, FLandscapeEditLayerReadback* InCPUReadback = nullptr)
			: Texture(InTexture)
			, ComponentSizeVerts(InComponentSizeVerts)
			, SectionRect(InComponentVertexPosition, InComponentVertexPosition + FIntPoint(InComponentSizeVerts, InComponentSizeVerts))
			, CPUReadback(InCPUReadback)
		{}

		FHeightmapCopyInfo(FHeightmapCopyInfo&&) = default;
		FHeightmapCopyInfo& operator=(FHeightmapCopyInfo&&) = default;

		void Union(const FIntPoint& InComponentVertexPosition)
		{
			SectionRect.Union(FIntRect(InComponentVertexPosition, InComponentVertexPosition + FIntPoint(ComponentSizeVerts, ComponentSizeVerts)));
		}

		UTexture2D* Texture;
		int32 ComponentSizeVerts;
		FIntRect SectionRect;
		FLandscapeEditLayerReadback* CPUReadback;
	};

	// Calculate Top Left Lambda
	auto GetUniqueHeightmaps = [&](const TArray<ULandscapeComponent*>& InLandscapeComponents, TArray<FHeightmapCopyInfo>& OutHeightmaps, const FIntPoint& LandscapeBaseQuads, const FGuid& InLayerGuid = FGuid())
	{
		const int32 ComponentSizeQuad = SubsectionSizeQuads * NumSubsections;
		const int32 ComponentSizeVerts = (SubsectionSizeQuads + 1) * NumSubsections;
		for (ULandscapeComponent* Component : InLandscapeComponents)
		{
			UTexture2D* ComponentHeightmap = Component->GetHeightmap(InLayerGuid);

			int32 Index = OutHeightmaps.IndexOfByPredicate([=](const FHeightmapCopyInfo& LayerHeightmap) { return LayerHeightmap.Texture == ComponentHeightmap; });

			FIntPoint ComponentSectionBase = Component->GetSectionBase() - LandscapeBaseQuads;
			FVector2D SourcePositionOffset(FMath::RoundToInt((float)ComponentSectionBase.X / ComponentSizeQuad), FMath::RoundToInt((float)ComponentSectionBase.Y / ComponentSizeQuad));
			FIntPoint ComponentVertexPosition = FIntPoint(static_cast<int32>(SourcePositionOffset.X * ComponentSizeVerts), static_cast<int32>(SourcePositionOffset.Y * ComponentSizeVerts));
			ALandscapeProxy* Proxy = Component->GetLandscapeProxy();

			if (Index == INDEX_NONE)
			{
				FLandscapeEditLayerReadback** CPUReadback = Proxy->HeightmapsCPUReadback.Find(ComponentHeightmap);
				OutHeightmaps.Add(FHeightmapCopyInfo(ComponentHeightmap, ComponentVertexPosition, ComponentSizeVerts, CPUReadback != nullptr ? *CPUReadback : nullptr));
			}
			else
			{
				OutHeightmaps[Index].Union(ComponentVertexPosition);
			}
		}
	};

	FLandscapeLayersHeightmapShaderParameters ShaderParams;

	bool FirstLayer = true;
	bool bHasBoundaryNormalCopy = false;	// HACK [chris.tchou] remove once we have a better boundary normal solution
	UTextureRenderTarget2D* CombinedHeightmapAtlasRT = HeightmapRTList[(int32)EHeightmapRTType::HeightmapRT_CombinedAtlas];
	UTextureRenderTarget2D* CombinedHeightmapNonAtlasRT = HeightmapRTList[(int32)EHeightmapRTType::HeightmapRT_CombinedNonAtlas];
	UTextureRenderTarget2D* LandscapeScratchRT1 = HeightmapRTList[(int32)EHeightmapRTType::HeightmapRT_Scratch1];
	UTextureRenderTarget2D* LandscapeScratchRT2 = HeightmapRTList[(int32)EHeightmapRTType::HeightmapRT_Scratch2];
	UTextureRenderTarget2D* LandscapeScratchRT3 = HeightmapRTList[(int32)EHeightmapRTType::HeightmapRT_Scratch3];

	for (FLandscapeLayer& Layer : LandscapeEditLayers)
	{
		// Draw each Layer's heightmaps to a Combined RT Atlas in LandscapeScratchRT1
		ShaderParams.ApplyLayerModifiers = false;
		ShaderParams.SetAlphaOne = false;
		ShaderParams.LayerVisible = Layer.bVisible;
		ShaderParams.GenerateNormals = false;
		ShaderParams.LayerBlendMode = Layer.BlendMode;

		if (Layer.BlendMode == LSBM_AlphaBlend)
		{
			// For now, only Layer reserved for Landscape Splines will use the AlphaBlendMode
			check((Layer.EditLayer != nullptr) && Layer.EditLayer->IsA<ULandscapeEditLayerSplines>());
			ShaderParams.LayerAlpha = 1.0f;
		}
		else
		{
			check(Layer.BlendMode == LSBM_AdditiveBlend);
			ShaderParams.LayerAlpha = Layer.HeightmapAlpha;
		}

		{
			TArray<FLandscapeLayersCopyTextureParams> DeferredCopyTextures;
			TArray<FHeightmapCopyInfo> LayerHeightmaps;
			GetUniqueHeightmaps(InUpdateLayersContentContext.LandscapeComponentsHeightmapsToRender, LayerHeightmaps, LandscapeExtent.Min, Layer.Guid);
			for (const FHeightmapCopyInfo& LayerHeightmap : LayerHeightmaps)
			{
				FLandscapeLayersCopyTextureParams& CopyTextureParams = DeferredCopyTextures.Add_GetRef(FLandscapeLayersCopyTextureParams(LayerHeightmap.Texture, LandscapeScratchRT1));
				// Only copy the size that's actually needed : 
				CopyTextureParams.CopySize = LayerHeightmap.SectionRect.Size();
				// Copy from the heightmap's top-left corner to the composited texture's position :
				CopyTextureParams.DestPosition = LayerHeightmap.SectionRect.Min;
			}
			ExecuteCopyLayersTexture(MoveTemp(DeferredCopyTextures));
		}

		// Convert Atlas LandscapeScratchRT1 to the world-projected NonAtlas in LandscapeScratchRT2
		// TODO: just use this format from the beginning above...
		DrawHeightmapComponentsToRenderTarget(FString::Printf(TEXT("LS Height: %s %s += -> NonAtlas %s"), *Layer.Name.ToString(), *LandscapeScratchRT1->GetName(), *LandscapeScratchRT2->GetName()),
			InUpdateLayersContentContext.LandscapeComponentsHeightmapsToRender, LandscapeExtent.Min, LandscapeScratchRT1, nullptr, LandscapeScratchRT2, ERTDrawingType::RTAtlasToNonAtlas, true, ShaderParams);

		ShaderParams.ApplyLayerModifiers = true;

		// Combine Current layer NonAtlas LandscapeScratchRT2 with current result in LandscapeScratchRT3, writing final result to CombinedHeightmapNonAtlasRT
		DrawHeightmapComponentsToRenderTarget(FString::Printf(TEXT("LS Height: %s %s += -> CombinedNonAtlas %s"), *Layer.Name.ToString(), *LandscapeScratchRT2->GetName(), *CombinedHeightmapNonAtlasRT->GetName()),
			InUpdateLayersContentContext.LandscapeComponentsHeightmapsToRender, LandscapeExtent.Min, LandscapeScratchRT2, FirstLayer ? nullptr : LandscapeScratchRT3, CombinedHeightmapNonAtlasRT, ERTDrawingType::RTNonAtlas, FirstLayer, ShaderParams);

		ShaderParams.ApplyLayerModifiers = false;

		if (Layer.bVisible && !InMergeParams.bSkipBrush)
		{
			// Draw each brushes
			for (int32 i = 0; i < Layer.Brushes.Num(); ++i)
			{
				// TODO: handle conversion from float to RG8 by using material params to write correct values

				FLandscapeLayerBrush& Brush = Layer.Brushes[i];
				FLandscapeBrushParameters BrushParameters = BuildLandscapeBrushParameters(/*bIsHeightmapMerge = */true, LandscapeExtent, CombinedHeightmapNonAtlasRT);

				UTextureRenderTarget2D* BrushOutputNonAtlasRT = Brush.RenderLayer(BrushParameters);
				if (BrushOutputNonAtlasRT == nullptr) 
				{
					continue;
				}

				// TODO: handle conversion/handling of RT not same size as internal size
				check((BrushOutputNonAtlasRT->SizeX == CombinedHeightmapNonAtlasRT->SizeX) && (BrushOutputNonAtlasRT->SizeY == CombinedHeightmapNonAtlasRT->SizeY));

				ALandscapeBlueprintBrushBase* LandscapeBrush = Brush.GetBrush();
				check(LandscapeBrush != nullptr); // If we managed to render, the brush should be valid

				INC_DWORD_STAT(STAT_LandscapeLayersRegenerateDrawCalls); // Brush Render

				PrintLayersDebugRT(FString::Printf(TEXT("LS Height: %s %s -> BrushNonAtlas %s"), *Layer.Name.ToString(), *LandscapeBrush->GetName(), *BrushOutputNonAtlasRT->GetName()), BrushOutputNonAtlasRT);

				// HACK [chris.tchou] remove once we have a better boundary normal solution
				if (LandscapeBrush->GetCaptureBoundaryNormals())
				{
					if (bHasBoundaryNormalCopy)
					{
						UE_LOG(LogLandscape, Warning, TEXT("Multiple Landscape Blueprint Brush Layers are invoking Boundary Normal Capture!  Only the most recent one will be used."));
					}

					// GPU copy to another RT (we copy the entire thing... easier than picking out just the boundaries)
					UTextureRenderTarget2D* BoundaryNormalRT = HeightmapRTList[(int32)EHeightmapRTType::HeightmapRT_BoundaryNormal];
					ExecuteCopyLayersTexture({ FLandscapeLayersCopyTextureParams(BrushOutputNonAtlasRT, BoundaryNormalRT) });
					bHasBoundaryNormalCopy = true;
				}

				// Resolve back to Combined heightmap (it's unlikely, but possible that the brush returns the same RT as input and output, if it did various operations on it, in which case the copy is useless) :
				if (BrushOutputNonAtlasRT != CombinedHeightmapNonAtlasRT)
				{
					ExecuteCopyLayersTexture({ FLandscapeLayersCopyTextureParams(BrushOutputNonAtlasRT, CombinedHeightmapNonAtlasRT) });
					PrintLayersDebugRT(FString::Printf(TEXT("LS Height: %s Component %s += -> CombinedNonAtlas %s"), *Layer.Name.ToString(), *BrushOutputNonAtlasRT->GetName(), *CombinedHeightmapNonAtlasRT->GetName()), CombinedHeightmapNonAtlasRT);
				}
			}

			// Legacy global merge support for non-blueprint-brush renderers: allow edit layer to act as blueprint brush.
			ILandscapeBrushRenderCallAdapter_GlobalMergeLegacySupport* RenderCallAdapter = Cast<ILandscapeBrushRenderCallAdapter_GlobalMergeLegacySupport>(Layer.EditLayer);
			if (RenderCallAdapter)
			{
				FLandscapeBrushParameters BrushParameters = BuildLandscapeBrushParameters(
					/*bIsHeightmapMerge = */true, LandscapeExtent, CombinedHeightmapNonAtlasRT);

				UTextureRenderTarget2D* BrushOutputNonAtlasRT = RenderCallAdapter->RenderAsBlueprintBrush(BrushParameters, GetTransform());

				// Do the same conditional copy that we do for blueprint brushes
				if (BrushOutputNonAtlasRT != CombinedHeightmapNonAtlasRT
					&& BrushOutputNonAtlasRT
					&& BrushOutputNonAtlasRT->SizeX == CombinedHeightmapNonAtlasRT->SizeX
					&& BrushOutputNonAtlasRT->SizeY == CombinedHeightmapNonAtlasRT->SizeY)
				{
					ExecuteCopyLayersTexture({ FLandscapeLayersCopyTextureParams(BrushOutputNonAtlasRT, CombinedHeightmapNonAtlasRT) });
					PrintLayersDebugRT(FString::Printf(TEXT("LS Height: %s Component %s += -> CombinedNonAtlas %s"), *Layer.Name.ToString(), 
						*BrushOutputNonAtlasRT->GetName(), *CombinedHeightmapNonAtlasRT->GetName()), CombinedHeightmapNonAtlasRT);
				}
			}
		}

		// copy CombinedHeightmapNonAtlasRT to LandscapeScratchRT3 (as a source for later layers... this is wasted on the last layer I think...)
		// TODO: you can get the same effect for much cheaper by swapping these two pointers before the render above...
		ExecuteCopyLayersTexture({ FLandscapeLayersCopyTextureParams(CombinedHeightmapNonAtlasRT, LandscapeScratchRT3) });
		PrintLayersDebugRT(FString::Printf(TEXT("LS Height: %s Component %s += -> CombinedNonAtlas %s"), *Layer.Name.ToString(), *CombinedHeightmapNonAtlasRT->GetName(), *LandscapeScratchRT3->GetName()), LandscapeScratchRT3);

		FirstLayer = false;
	}

	// Set Alpha channel of valid areas to 1 (via shader copy to LandscapeScratchRT2)
	ShaderParams.SetAlphaOne = true;
	DrawHeightmapComponentsToRenderTarget(FString::Printf(TEXT("Mark Valid Area Alpha 1: %s -> %s"), *CombinedHeightmapNonAtlasRT->GetName(), *LandscapeScratchRT2->GetName()),
		InUpdateLayersContentContext.LandscapeComponentsHeightmapsToRender, LandscapeExtent.Min, CombinedHeightmapNonAtlasRT, nullptr, LandscapeScratchRT2, ERTDrawingType::RTNonAtlas, true, ShaderParams);
	ShaderParams.SetAlphaOne = false;

	// Broadcast Event of the Full Render
	if ((InMergeParams.HeightmapUpdateModes & Update_Heightmap_All) == Update_Heightmap_All)
	{
		OnEditLayersMergedDelegate.Broadcast(FOnLandscapeEditLayersMergedParams(LandscapeScratchRT3, /*InRenderAreaResolution = */LandscapeExtent.Max - LandscapeExtent.Min, /*bInIsHeightmapMerge = */true));
	}

	// compute Normals into LandscapeScratchRT1
	ShaderParams.GenerateNormals = true;
	ShaderParams.OverrideBoundaryNormals = bHasBoundaryNormalCopy;	// HACK [chris.tchou] remove once we have a better boundary normal solution
	ShaderParams.GridSize = GetRootComponent()->GetRelativeScale3D();
	UTextureRenderTarget2D* BoundaryNormalRT = HeightmapRTList[(int32)EHeightmapRTType::HeightmapRT_BoundaryNormal];	// HACK [chris.tchou] remove once we have a better boundary normal solution
	DrawHeightmapComponentsToRenderTarget(FString::Printf(TEXT("LS Height: %s = -> CombinedNonAtlasNormals : %s"), *LandscapeScratchRT2->GetName(), *LandscapeScratchRT1->GetName()),
		InUpdateLayersContentContext.LandscapeComponentsHeightmapsToRender, LandscapeExtent.Min, LandscapeScratchRT2,
		bHasBoundaryNormalCopy ? BoundaryNormalRT : nullptr,	// HACK [chris.tchou] remove once we have a better boundary normal solution
		LandscapeScratchRT1, ERTDrawingType::RTNonAtlas, true, ShaderParams);

	ShaderParams.GenerateNormals = false;
	ShaderParams.OverrideBoundaryNormals = false;	// HACK [chris.tchou] remove once we have a better boundary normal solution

	// convert back to atlas (TODO: we could do this on the first mip downsample instead...)
	DrawHeightmapComponentsToRenderTarget(FString::Printf(TEXT("LS Height: %s = -> CombinedAtlasFinal : %s"), *LandscapeScratchRT1->GetName(), *CombinedHeightmapAtlasRT->GetName()),
		InUpdateLayersContentContext.LandscapeComponentsHeightmapsToRender, LandscapeExtent.Min, LandscapeScratchRT1, nullptr, CombinedHeightmapAtlasRT, ERTDrawingType::RTNonAtlasToAtlas, true, ShaderParams);

	// Downsample to generate mips...
	DrawHeightmapComponentsToRenderTargetMips(InUpdateLayersContentContext.LandscapeComponentsHeightmapsToRender, LandscapeExtent.Min, CombinedHeightmapAtlasRT, true, ShaderParams);

	// List of UTexture2D that we need to kick off readbacks for :
	TArray<UTexture2D*> TexturesNeedingReadback;

	// Copy back all Mips to original heightmap data
	{
		TArray<FLandscapeLayersCopyTextureParams> DeferredCopyTextures;
		TArray<FHeightmapCopyInfo> Heightmaps;
		GetUniqueHeightmaps(InUpdateLayersContentContext.LandscapeComponentsHeightmapsToResolve, Heightmaps, LandscapeExtent.Min);
		for (const FHeightmapCopyInfo& Heightmap : Heightmaps)
		{
			check(Heightmap.CPUReadback);

			const FIntPoint Mip0CopySize = Heightmap.SectionRect.Size();
			const FIntPoint Mip0SourcePosition = Heightmap.SectionRect.Min;

			// Mip 0
			{
				FLandscapeLayersCopyTextureParams& CopyTextureParams = DeferredCopyTextures.Add_GetRef(FLandscapeLayersCopyTextureParams(CombinedHeightmapAtlasRT, Heightmap.Texture));
				// Only copy the size that's actually needed : 
				CopyTextureParams.CopySize = Mip0CopySize;
				// Copy from the composited texture's position to the top-left corner of the heightmap
				CopyTextureParams.SourcePosition = Mip0SourcePosition;
			}

			// Other Mips
			uint8 MipIndex = 1;
			for (int32 MipRTIndex = (int32)EHeightmapRTType::HeightmapRT_Mip1; MipRTIndex < (int32)EHeightmapRTType::HeightmapRT_Count; ++MipRTIndex)
			{
				UTextureRenderTarget2D* RenderTargetMip = HeightmapRTList[MipRTIndex];
				if (RenderTargetMip != nullptr)
				{
					FLandscapeLayersCopyTextureParams& CopyTextureParams = DeferredCopyTextures.Add_GetRef(FLandscapeLayersCopyTextureParams(RenderTargetMip, Heightmap.Texture));
					CopyTextureParams.CopySize.X = Mip0CopySize.X >> MipIndex;
					CopyTextureParams.CopySize.Y = Mip0CopySize.Y >> MipIndex;
					CopyTextureParams.SourcePosition.X = Mip0SourcePosition.X >> MipIndex;
					CopyTextureParams.SourcePosition.Y = Mip0SourcePosition.Y >> MipIndex;
					CopyTextureParams.DestMip = MipIndex;

					++MipIndex;
				}
			}

			check(!TexturesNeedingReadback.Contains(Heightmap.Texture));
			TexturesNeedingReadback.Add(Heightmap.Texture);
		}
		ExecuteCopyLayersTexture(MoveTemp(DeferredCopyTextures));
	}

	// Prepare the UTexture2D readbacks we'll need to perform :
	TArray<FLandscapeLayersCopyReadbackTextureParams> DeferredCopyReadbackTextures = PrepareLandscapeLayersCopyReadbackTextureParams(InUpdateLayersContentContext.MapHelper, TexturesNeedingReadback, /*bWeightmaps = */false);
	ExecuteCopyToReadbackTexture(DeferredCopyReadbackTextures);

	return InMergeParams.HeightmapUpdateModes;
}

int32 ALandscape::PerformLayersHeightmapsBatchedMerge(const FUpdateLayersContentContext& InUpdateLayersContentContext, const FEditLayersHeightmapMergeParams& InMergeParams)
{
	using namespace UE::Landscape::EditLayers;
	using namespace UE::Landscape::EditLayers::Private;

	TRACE_CPUPROFILER_EVENT_SCOPE(ALandscape::PerformLayersHeightmapsBatchedMerge);
	RHI_BREADCRUMB_EVENT_GAMETHREAD("PerformLayersHeightmapsBatchedMerge");

	ULandscapeInfo* Info = GetLandscapeInfo();
	check(Info != nullptr);

	// Prepare the heightmap merge operations for all components that need to be updated :
	TArray<FEditLayerRendererState> RendererStates = GetEditLayerRendererStates(Info, InMergeParams.bSkipBrush);
	// Add an edit layer renderer at the top of the stack in order to add dependencies between each landscape component and its immediate neighbors in order to ensure they end up
	//  in the same render batch. The renderer is responsible for computing the normals at the end of the batch : 
	ULandscapeHeightmapNormalsEditLayerRenderer* HeightmapNormalsRenderer = ULandscapeHeightmapNormalsEditLayerRenderer::StaticClass()->GetDefaultObject<ULandscapeHeightmapNormalsEditLayerRenderer>();
	RendererStates.Emplace(HeightmapNormalsRenderer, Info);

	FMergeRenderParams MergeRenderParams(/*bInIsHeightmapMerge = */true, /*InLandscape = */this, InUpdateLayersContentContext.LandscapeComponentsHeightmapsToRender, RendererStates);
	FMergeRenderContext MergeRenderContext = PrepareEditLayersMergeRenderContext(MergeRenderParams);
	if (!MergeRenderContext.IsValid())
	{
		// Nothing to do : 
		return InMergeParams.HeightmapUpdateModes;
	}

	// For each batch, render and resolve the raw heightmaps into the individual textures : 
	TSet<ULandscapeComponent*> ResolvedComponents;
	ResolvedComponents.Reserve(InUpdateLayersContentContext.LandscapeComponentsHeightmapsToRender.Num());

	// Callback executed each time a render batch is done computing the requested into, just before releasing the render resources : 
	auto OnRenderBatchGroupDone = [&ResolvedComponents, &InUpdateLayersContentContext, OnEditLayersMergedDelegate = &OnEditLayersMergedDelegate](const FMergeRenderContext::FOnRenderBatchTargetGroupDoneParams& InParams)
	{
		ALandscape* Landscape = InParams.MergeRenderContext->GetLandscape();

		//  Note: thanks to HeightmapNormalsRenderer, we have the guarantee that the (up to) 8 neighbors of each of the components requested 
		//  for are present in the batch, which means we have all the data to generate the normals already 
		TSet<ULandscapeComponent*> ComponentsToResolveThisBatch;
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(PrepareResolve);
			ComponentsToResolveThisBatch.Reserve(InParams.RenderBatch->ComponentsToRender.Num());
			for (ULandscapeComponent* RenderedComponent : InParams.RenderBatch->ComponentsToRender)
			{
				checkf(InParams.SortedComponentMergeRenderInfos.FindByPredicate([RenderedComponent](const FComponentMergeRenderInfo& InComponentMergeInfo) { return InComponentMergeInfo.Component == RenderedComponent; }) != nullptr,
					TEXT("All components in the batch must be present in SortedComponentMergeRenderInfos"));

				ULandscapeComponent** ResolvedComponent = ResolvedComponents.Find(RenderedComponent);
				if (ResolvedComponent == nullptr)
				{
					TStaticArray<ULandscapeComponent*, 9> NeighborComponents;
					RenderedComponent->GetLandscapeComponentNeighbors3x3(NeighborComponents);
					TSet<ULandscapeComponent*> ValidNeighborComponents;
					for (ULandscapeComponent* NeighborComponent : NeighborComponents)
					{
						if ((NeighborComponent != nullptr) && (NeighborComponent != RenderedComponent))
						{
							ValidNeighborComponents.Add(NeighborComponent);
						}
					}

					// We need all neighbors to be present in this batch in order to be able to finalize that component :
					if ((ValidNeighborComponents.Intersect(InParams.RenderBatch->ComponentsToRender).Num() == ValidNeighborComponents.Num())
						&& InUpdateLayersContentContext.LandscapeComponentsHeightmapsToResolve.Contains(RenderedComponent))
					{
						ResolvedComponents.Add(RenderedComponent);
						ComponentsToResolveThisBatch.Add(RenderedComponent);
					}
				}
			}
		}

		// Copy to mip0 of the final textures and expand the vertices on borders so that we can generate the mips from it:
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(CopyMip0AndExpand);
			RHI_BREADCRUMB_EVENT_GAMETHREAD("CopyMip0AndExpand");

			// Recompose mip0 of the final heightmaps, subsection by subsection, to duplicate borders : 
			InParams.MergeRenderContext->CycleBlendRenderTargets(/*InDesiredWriteAccess = */ERHIAccess::SRVMask); // TODO [jonathan.bard] : This should be CopyDst but ExecuteCopyLayersTexture doesn't allow for it ATM
			ULandscapeScratchRenderTarget* ReadRT = InParams.MergeRenderContext->GetBlendRenderTargetRead();

			OnEditLayersMergedDelegate->Broadcast(FOnLandscapeEditLayersMergedParams(ReadRT->GetRenderTarget(),
				/*InRenderAreaResolution = */InParams.RenderBatch->GetRenderTargetResolution(/*bInWithDuplicateBorders = */false), /*bInIsHeightmapMerge = */true));

			ULandscapeScratchRenderTarget* WriteRT = InParams.MergeRenderContext->GetBlendRenderTargetWrite();

			struct FComponentCopyInfo
			{
				UTexture2D* Texture = nullptr;
				ULandscapeComponent* Component = nullptr;
				FIntPoint TextureOffset = FIntPoint::ZeroValue;
				TArray<FIntRect, TInlineAllocator<4>> SourceSubsectionRects;
				TArray<FIntRect, TInlineAllocator<4>> DestinationSubsectionRects;
			};
			TArray<FComponentCopyInfo> ComponentCopyInfos;
			const int32 TotalNumSubsections = Landscape->NumSubsections * Landscape->NumSubsections;
			const int32 ComponentSubsectionVerts = Landscape->SubsectionSizeQuads + 1;

			for (ULandscapeComponent* Component : InParams.RenderBatch->ComponentsToRender)
			{
				FComponentCopyInfo& ComponentCopyInfo = ComponentCopyInfos.Emplace_GetRef();
				ComponentCopyInfo.Component = Component;
				ComponentCopyInfo.Texture = Component->GetHeightmap(false);
				// Effective area of the texture affecting this component (because of texture sharing) :
				ComponentCopyInfo.TextureOffset = FIntPoint(static_cast<int32>(Component->HeightmapScaleBias.Z * ComponentCopyInfo.Texture->Source.GetSizeX()), static_cast<int32>(Component->HeightmapScaleBias.W * ComponentCopyInfo.Texture->Source.GetSizeY()));
		
				InParams.RenderBatch->ComputeSubsectionRects(Component, /*OutSubsectionRects = */ComponentCopyInfo.SourceSubsectionRects, /*OutSubsectionRectsWithDuplicateBorders = */ComponentCopyInfo.DestinationSubsectionRects);
				check(ComponentCopyInfo.SourceSubsectionRects.Num() == TotalNumSubsections);
				check(ComponentCopyInfo.DestinationSubsectionRects.Num() == TotalNumSubsections);
			}

			// TODO [jonathan.bard] : move this after expand (and rename "Expand" to "Generate mip 0")
			{
				RHI_BREADCRUMB_EVENT_GAMETHREAD("CopyToMip0");
				// Copy sub-section by sub-section in order to duplicate borders :
				TArray<FLandscapeLayersCopyTextureParams> DeferredCopyTextures;
				for (const FComponentCopyInfo& ComponentCopyInfo : ComponentCopyInfos)
				{
					for (int32 SubSectionIndex = 0; SubSectionIndex < TotalNumSubsections; ++SubSectionIndex)
					{
						FIntPoint SubSection(SubSectionIndex % Landscape->NumSubsections, SubSectionIndex / Landscape->NumSubsections);
						const FIntRect& SourceSubSectionRect = ComponentCopyInfo.SourceSubsectionRects[SubSectionIndex];
		
						// Copy to mip0 of the final texture if requested : 
						if (ComponentsToResolveThisBatch.Contains(ComponentCopyInfo.Component))
						{
							FLandscapeLayersCopyTextureParams& CopyTextureParams = DeferredCopyTextures.Add_GetRef(FLandscapeLayersCopyTextureParams(ReadRT->GetRenderTarget(), ComponentCopyInfo.Texture));
							CopyTextureParams.SourcePosition = SourceSubSectionRect.Min;
							CopyTextureParams.CopySize = SourceSubSectionRect.Size();
							CopyTextureParams.DestPosition = ComponentCopyInfo.TextureOffset + FIntPoint(SubSection.X * ComponentSubsectionVerts, SubSection.Y * ComponentSubsectionVerts);
						}
					}
				}
				ExecuteCopyLayersTexture(MoveTemp(DeferredCopyTextures));
			}

			// "Expand" the scratch render target : 
			// Copy sub-section by sub-section in order to duplicate borders :
			InParams.MergeRenderContext->RenderExpandedRenderTarget(*InParams.RenderBatch);
		}

		// Generate the mips from the expanded RT and copy to the final texture mips
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(GenerateMips);
			const int32 NumMips = (int32)FMath::CeilLogTwo(Landscape->SubsectionSizeQuads) + 1;
			RHI_BREADCRUMB_EVENT_GAMETHREAD("Generate %i remaining mips", NumMips - 1);

			FIntPoint CurrentMipResolution = InParams.RenderBatch->GetRenderTargetResolution(/*bInWithDuplicateBorders = */true); // Mips are generated after the borders have been duplicated
			FIntPoint CurrentMipSubsectionSize = Landscape->SubsectionSizeQuads + 1;
			for (int32 MipIndex = 1; MipIndex < NumMips; ++MipIndex)
			{
				InParams.MergeRenderContext->CycleBlendRenderTargets(/*InDesiredWriteAccess = */ERHIAccess::RTV);
				ULandscapeScratchRenderTarget* WriteRT = InParams.MergeRenderContext->GetBlendRenderTargetWrite();
				ULandscapeScratchRenderTarget* ReadRT = InParams.MergeRenderContext->GetBlendRenderTargetRead();
				CurrentMipResolution /= 2;
				check(CurrentMipResolution.X > 0 && CurrentMipResolution.Y > 0);
				CurrentMipSubsectionSize /= 2;
				check(CurrentMipSubsectionSize.X > 0 && CurrentMipSubsectionSize.Y > 0);

				{
					RHI_BREADCRUMB_EVENT_GAMETHREAD("Generate mip %i", MipIndex);

					ENQUEUE_RENDER_COMMAND(LandscapeLayers_Cmd_HeightmapsGenerateMips)(
						[ OutputResource = WriteRT->GetRenderTarget2D()->GetResource()
						, SourceResource = ReadRT->GetRenderTarget2D()->GetResource()
						, CurrentMipResolution
						, CurrentMipSubsectionSize
						, MipIndex ]
						(FRHICommandListImmediate& InRHICmdList)
					{
						FRDGBuilder GraphBuilder(InRHICmdList, RDG_EVENT_NAME("HeightmapsGenerateMips"));

						FRDGTextureRef OutputTextureRef = GraphBuilder.RegisterExternalTexture(CreateRenderTarget(OutputResource->TextureRHI, TEXT("OutputTexture")), ERDGTextureFlags::SkipTracking);
						FRDGTextureRef SourceTextureRef = GraphBuilder.RegisterExternalTexture(CreateRenderTarget(SourceResource->TextureRHI, TEXT("SourceTexture")));

						FLandscapeEditLayersHeightmapsGenerateMipsPS::FParameters* PSParams = GraphBuilder.AllocParameters<FLandscapeEditLayersHeightmapsGenerateMipsPS::FParameters>();
						PSParams->RenderTargets[0] = FRenderTargetBinding(OutputTextureRef, ERenderTargetLoadAction::ENoAction);
						PSParams->InCurrentMipSubsectionSize = FUintVector2(CurrentMipSubsectionSize.X, CurrentMipSubsectionSize.Y);
						PSParams->InSourceHeightmap = GraphBuilder.CreateSRV(FRDGTextureSRVDesc::Create(SourceTextureRef));
						FLandscapeEditLayersHeightmapsGenerateMipsPS::GenerateMipsPS(GraphBuilder, PSParams, CurrentMipResolution);
						GraphBuilder.Execute();
					});
				}

				// Then copy the appropriate regions to the destination texture mips : 
				// TODO [jonathan.bard] : add this when we don't auto-transition to SRV in the copy texture thing : WriteRT->TransitionTo(ERHIAccess::CopySrc);
				{
					RHI_BREADCRUMB_EVENT_GAMETHREAD("Copy mip %i", MipIndex);

					WriteRT->TransitionTo(ERHIAccess::SRVMask);
					TArray<FLandscapeLayersCopyTextureParams> DeferredCopyTextures;
					for (ULandscapeComponent* Component : ComponentsToResolveThisBatch)
					{
						UTexture2D* ComponentHeightmap = Component->GetHeightmap(false);

						FIntRect SourceSectionRect = InParams.RenderBatch->ComputeSectionRect(Component, /*bInWithDuplicateBorders = */true);
						SourceSectionRect.Min.X >>= MipIndex;
						SourceSectionRect.Min.Y >>= MipIndex;
						SourceSectionRect.Max.X >>= MipIndex;
						SourceSectionRect.Max.Y >>= MipIndex;

						// Effective area of the texture affecting this component (because of texture sharing) :
						FIntPoint TextureOffset = FIntPoint(static_cast<int32>(Component->HeightmapScaleBias.Z * ComponentHeightmap->Source.GetSizeX()), static_cast<int32>(Component->HeightmapScaleBias.W * ComponentHeightmap->Source.GetSizeY()));
						TextureOffset.X >>= MipIndex;
						TextureOffset.Y >>= MipIndex;

						FLandscapeLayersCopyTextureParams& CopyTextureParams = DeferredCopyTextures.Add_GetRef(FLandscapeLayersCopyTextureParams(WriteRT->GetRenderTarget(), ComponentHeightmap));
						CopyTextureParams.SourcePosition = SourceSectionRect.Min;
						CopyTextureParams.CopySize = SourceSectionRect.Size();
						CopyTextureParams.DestPosition = TextureOffset;
						CopyTextureParams.DestMip = MipIndex;
					}
					ExecuteCopyLayersTexture(MoveTemp(DeferredCopyTextures));
				}
			}
		}
	};

	// Render everything now. Every time a group from a batch is done (there's only one group per batch for heightmaps), the OnRenderBatchGroupDone callback is called : 
	MergeRenderContext.Render(OnRenderBatchGroupDone);

	// All requested components must have been resolved by now :
	check(ResolvedComponents.Num() == InUpdateLayersContentContext.LandscapeComponentsHeightmapsToResolve.Num());

	// Prepare the UTexture2D readbacks we'll need to perform :
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(CopyToReadback);
		RHI_BREADCRUMB_EVENT_GAMETHREAD("CopyToReadback");

		TArray<FLandscapeLayersCopyReadbackTextureParams> DeferredCopyReadbackTextures = PrepareLandscapeLayersCopyReadbackTextureParams(InUpdateLayersContentContext.MapHelper, InUpdateLayersContentContext.HeightmapsToResolve.Array(), /*bWeightmaps = */false);
		ExecuteCopyToReadbackTexture(DeferredCopyReadbackTextures);
	}

	return InMergeParams.HeightmapUpdateModes;
}

FLandscapeBrushParameters ALandscape::BuildLandscapeBrushParameters(bool bInIsHeightmapMerge, const FIntRect& InRenderAreaExtents, UTextureRenderTarget2D* InRenderTarget, FName InWeightmapLayerName)
{
	FTransform RenderAreaWorldTransform = GetTransform();
	FVector OffsetVector(InRenderAreaExtents.Min.X, InRenderAreaExtents.Min.Y, 0.f);
	FVector Translation = RenderAreaWorldTransform.TransformFVector4(OffsetVector);
	RenderAreaWorldTransform.SetTranslation(Translation);
	FIntPoint RenderAreaSize = InRenderAreaExtents.Max - InRenderAreaExtents.Min;
	return FLandscapeBrushParameters(bInIsHeightmapMerge, RenderAreaWorldTransform, RenderAreaSize, InRenderTarget, InWeightmapLayerName);
}

int32 ALandscape::RegenerateLayersHeightmaps(const FUpdateLayersContentContext& InUpdateLayersContentContext)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(LandscapeLayers_RegenerateLayersHeightmaps);
	ULandscapeInfo* Info = GetLandscapeInfo();

	const int32 HeightmapUpdateModes = LayerContentUpdateModes & ELandscapeLayerUpdateMode::Update_Heightmap_Types;
	const bool bForceRender = CVarForceLayersUpdate.GetValueOnAnyThread() != 0;
	const bool bSkipBrush = CVarLandscapeLayerBrushOptim.GetValueOnAnyThread() == 1 && (HeightmapUpdateModes == ELandscapeLayerUpdateMode::Update_Heightmap_Editing);

	if ((HeightmapUpdateModes == 0 && !bForceRender) || Info == nullptr)
	{
		return 0;
	}

	// Nothing to do (return that we did the processing)
	if (InUpdateLayersContentContext.LandscapeComponentsHeightmapsToRender.Num() == 0)
	{
		return HeightmapUpdateModes;
	}

	// Lazily create CPU read back objects as required
	if (HeightmapUpdateModes)
	{
		for (ULandscapeComponent* Component : InUpdateLayersContentContext.LandscapeComponentsHeightmapsToRender)
		{
			UTexture2D* ComponentHeightmap = Component->GetHeightmap(false);
			ALandscapeProxy* Proxy = Component->GetLandscapeProxy();
			FLandscapeEditLayerReadback** CPUReadback = Proxy->HeightmapsCPUReadback.Find(ComponentHeightmap);
			if (CPUReadback == nullptr)
			{
				FLandscapeEditLayerReadback* NewCPUReadback = new FLandscapeEditLayerReadback();
				const uint8* LockedMip = ComponentHeightmap->Source.LockMip(0);
				const uint64 Hash = FLandscapeEditLayerReadback::CalculateHash(LockedMip, ComponentHeightmap->Source.GetSizeX() * ComponentHeightmap->Source.GetSizeY() * sizeof(FColor));
				ComponentHeightmap->Source.UnlockMip(0);
				NewCPUReadback->SetHash(Hash);
				Proxy->HeightmapsCPUReadback.Add(ComponentHeightmap, NewCPUReadback);
			}
		}
	}

	if (HeightmapUpdateModes || bForceRender)
	{
		RenderCaptureInterface::FScopedCapture RenderCapture((RenderCaptureLayersNextHeightmapDraws != 0), TEXT("LandscapeLayersHeightmapCapture"));
		RenderCaptureLayersNextHeightmapDraws = FMath::Max(0, RenderCaptureLayersNextHeightmapDraws - 1);

		FEditLayersHeightmapMergeParams MergeParams;
		MergeParams.HeightmapUpdateModes = HeightmapUpdateModes;
		MergeParams.bForceRender = bForceRender;
		MergeParams.bSkipBrush = bSkipBrush;

		switch (CurrentEditLayersMergeMode)
		{
		case ELandscapeEditLayersMergeMode::GlobalMerge:
		{
			return PerformLayersHeightmapsGlobalMerge(InUpdateLayersContentContext, MergeParams);
		}
		case ELandscapeEditLayersMergeMode::LocalMerge:
		{
			return PerformLayersHeightmapsLocalMerge(InUpdateLayersContentContext, MergeParams);
		}
		case ELandscapeEditLayersMergeMode::BatchedMerge:
		{
			return PerformLayersHeightmapsBatchedMerge(InUpdateLayersContentContext, MergeParams);
		}
		default:
			check(false);
		}
	}

	return 0;
}

void ALandscape::UpdateForChangedHeightmaps(const TArrayView<FLandscapeEditLayerComponentReadbackResult>& InComponentReadbackResults)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(LandscapeLayers_UpdateForChangedHeightmaps);

	for (const FLandscapeEditLayerComponentReadbackResult& ComponentReadbackResult : InComponentReadbackResults)
	{
		// If the source data has changed, mark the component as needing a collision data update:
		//  - If ELandscapeComponentUpdateFlag::Component_Update_Heightmap_Collision is passed, it will be done immediately
		//  - If not, at least the component's collision data will still get updated eventually, when the flag is finally passed :
		if (ComponentReadbackResult.bModified)
		{
			ComponentReadbackResult.LandscapeComponent->SetPendingCollisionDataUpdate(true);
		}

		const uint32 HeightUpdateMode = ComponentReadbackResult.UpdateModes & (ELandscapeLayerUpdateMode::Update_Heightmap_All | ELandscapeLayerUpdateMode::Update_Heightmap_Editing | ELandscapeLayerUpdateMode::Update_Heightmap_Editing_NoCollision);

		// Only update collision if there was an actual change performed on the source data : 
		if (ComponentReadbackResult.LandscapeComponent->GetPendingCollisionDataUpdate())
		{
			if (IsUpdateFlagEnabledForModes(ELandscapeComponentUpdateFlag::Component_Update_Heightmap_Collision, HeightUpdateMode))
			{
				ComponentReadbackResult.LandscapeComponent->UpdateCachedBounds();
				ComponentReadbackResult.LandscapeComponent->UpdateComponentToWorld();

				// Avoid updating height field if we are going to recreate collision in this update
				bool bUpdateHeightfieldRegion = !IsUpdateFlagEnabledForModes(ELandscapeComponentUpdateFlag::Component_Update_Recreate_Collision, HeightUpdateMode);
				ComponentReadbackResult.LandscapeComponent->UpdateCollisionData(bUpdateHeightfieldRegion);
				ComponentReadbackResult.LandscapeComponent->SetPendingCollisionDataUpdate(false);
			}
			else if (IsUpdateFlagEnabledForModes(ELandscapeComponentUpdateFlag::Component_Update_Approximated_Bounds, HeightUpdateMode))
			{
				// Update bounds with an approximated value (real computation will be done anyways when computing collision)
				const bool bInApproximateBounds = true;
				ComponentReadbackResult.LandscapeComponent->UpdateCachedBounds(bInApproximateBounds);
				ComponentReadbackResult.LandscapeComponent->UpdateComponentToWorld();
			}
		}
	}
}

void ALandscape::ResolveLayersHeightmapTexture(
	FTextureToComponentHelper const& MapHelper,
	TSet<UTexture2D*> const& HeightmapsToResolve,
	bool bIntermediateRender,
	TArray<FLandscapeEditLayerComponentReadbackResult>& InOutComponentReadbackResults)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(LandscapeLayers_ResolveLayersHeightmapTexture);

	ULandscapeInfo* Info = GetLandscapeInfo();
	if (Info == nullptr)
	{
		return;
	}

	TArray<ULandscapeComponent*> ChangedComponents;
	for (UTexture2D* Heightmap : HeightmapsToResolve)
	{
		ALandscapeProxy* LandscapeProxy = Heightmap->GetTypedOuter<ALandscapeProxy>();
		check(LandscapeProxy);
		if (FLandscapeEditLayerReadback** CPUReadback = LandscapeProxy->HeightmapsCPUReadback.Find(Heightmap))
		{
			const bool bChanged = ResolveLayersTexture(MapHelper, *CPUReadback, Heightmap, bIntermediateRender, InOutComponentReadbackResults, /*bIsWeightmap = */false);
			if (bChanged)
			{
				ChangedComponents.Append(MapHelper.HeightmapToComponents[Heightmap]);
			}
		}
	}

	const bool bInvalidateLightingCache = true;
	InvalidateGeneratedComponentData(ChangedComponents, bInvalidateLightingCache);
}

void ALandscape::ClearDirtyData(ULandscapeComponent* InLandscapeComponent)
{
	if (InLandscapeComponent->EditToolRenderData.DirtyTexture == nullptr)
	{
		return;
	}

	if (!CVarLandscapeTrackDirty.GetValueOnAnyThread())
	{
		return;
	}

	FLandscapeEditDataInterface LandscapeEdit(GetLandscapeInfo());
	const int32 X1 = InLandscapeComponent->GetSectionBase().X;
	const int32 X2 = X1 + ComponentSizeQuads;
	const int32 Y1 = InLandscapeComponent->GetSectionBase().Y;
	const int32 Y2 = Y1 + ComponentSizeQuads;
	const int32 ComponentWidth = ComponentSizeQuads + 1;
	const int32 DirtyDataSize = ComponentWidth * ComponentWidth;
	TUniquePtr<uint8[]> DirtyData = MakeUnique<uint8[]>(DirtyDataSize);
	FMemory::Memzero(DirtyData.Get(), DirtyDataSize);
	LandscapeEdit.SetDirtyData(X1, Y1, X2, Y2, DirtyData.Get(), 0);
}

void ALandscape::UpdateWeightDirtyData(ULandscapeComponent* InLandscapeComponent, UTexture2D const* InWeightmap, FColor const* InOldData, FColor const* InNewData, uint8 InChannel)
{
	check(InOldData && InNewData);

	FLandscapeEditDataInterface LandscapeEdit(GetLandscapeInfo());
	const int32 X1 = InLandscapeComponent->GetSectionBase().X;
	const int32 X2 = X1 + ComponentSizeQuads;
	const int32 Y1 = InLandscapeComponent->GetSectionBase().Y;
	const int32 Y2 = Y1 + ComponentSizeQuads;
	const int32 ComponentWidth = ComponentSizeQuads + 1;
	const int32 DirtyDataSize = ComponentWidth * ComponentWidth;
	const int32 SizeU = InWeightmap->Source.GetSizeX();
	const int32 SizeV = InWeightmap->Source.GetSizeY();
	const uint8 DirtyWeight = 1 << 1;

	TUniquePtr<uint8[]> DirtyData = MakeUnique<uint8[]>(DirtyDataSize);
	LandscapeEdit.GetDirtyData(X1, Y1, X2, Y2, DirtyData.Get(), 0);

	// COMMENT [jonathan.bard] : this isn't quite working, because of weightmap re-assignment during painting, which can lead to InOldData to be totally different than the previous frame, which 
	//  will mark pretty much everything as dirty. This will be this way until we stop using weightmap sharing in the tool
	FLandscapeComponentDataInterface CDI(InLandscapeComponent);
	for (int32 X = 0; X < ComponentWidth; ++X)
	{
		for (int32 Y = 0; Y < ComponentWidth; ++Y)
		{
			int32 TexX, TexY;
			CDI.VertexXYToTexelXY(X, Y, TexX, TexY);
			int32 TexIndex = TexX + TexY * SizeU;
			check(TexIndex < SizeU * SizeV);
			if (InOldData[TexIndex] != InNewData[TexIndex])
			{
				DirtyData[X + Y * ComponentWidth] |= DirtyWeight;
			}
		}
	}

	LandscapeEdit.SetDirtyData(X1, Y1, X2, Y2, DirtyData.Get(), 0);
}

void ALandscape::OnDirtyWeightmap(FTextureToComponentHelper const& MapHelper, UTexture2D const* InWeightmap, FColor const* InOldData, FColor const* InNewData, int32 InMipLevel, uint8 ChangedChannelsMask)
{
	using namespace UE::Landscape::Private;

	int32 DumpWeightmapDiff = CVarLandscapeDumpWeightmapDiff.GetValueOnGameThread();
	const bool bDumpDiff = (DumpWeightmapDiff > 0);
	const bool bDumpDiffAllMips = (DumpWeightmapDiff > 1);
	const bool bDumpDiffDetails = CVarLandscapeDumpDiffDetails.GetValueOnGameThread();
	const bool bTrackDirty = CVarLandscapeTrackDirty.GetValueOnGameThread() != 0;
	ULandscapeSubsystem* LandscapeSubsystem = GetWorld()->GetSubsystem<ULandscapeSubsystem>();
	check(LandscapeSubsystem != nullptr);
	const FDateTime CurrentTime = LandscapeSubsystem->GetAppCurrentDateTime();

	if ((!bDumpDiff && !bTrackDirty)
		|| (bDumpDiff && !bDumpDiffAllMips && (InMipLevel > 0))
		|| (bTrackDirty && (InMipLevel > 0)))
	{
		return;
	}

	check(ChangedChannelsMask != 0);

	TArray<ULandscapeComponent*> const* Components = MapHelper.WeightmapToComponents.Find(InWeightmap);
	if (Components != nullptr)
	{
		for (ULandscapeComponent* Component : *Components)
		{
			const TArray<UTexture2D*>& WeightmapTextures = Component->GetWeightmapTextures();
			const TArray<FWeightmapLayerAllocationInfo>& AllocInfos = Component->GetWeightmapLayerAllocations();

			for (FWeightmapLayerAllocationInfo const& AllocInfo : AllocInfos)
			{
				check(AllocInfo.IsAllocated() && AllocInfo.WeightmapTextureIndex < WeightmapTextures.Num());
				if (InWeightmap == WeightmapTextures[AllocInfo.WeightmapTextureIndex] 
					// Only dump if that particular weightmap channel has changed
					&& ((1 << AllocInfo.WeightmapTextureChannel) & ChangedChannelsMask) != 0)
				{
					if (bTrackDirty)
					{
						UpdateWeightDirtyData(Component, InWeightmap, InOldData, InNewData, AllocInfo.WeightmapTextureChannel);
					}

					if (bDumpDiff)
					{
						const int32 SizeU = InWeightmap->Source.GetSizeX() >> InMipLevel;
						const int32 SizeV = InWeightmap->Source.GetSizeY() >> InMipLevel;

						FString WorldName = GetWorld()->GetName();
						FString ParentLandscapeActorName = GetActorLabel();
						ALandscapeProxy* Proxy = Cast<ALandscapeProxy>(Component->GetOwner());
						check(Proxy);
						FString ActorName = Proxy->GetActorLabel();
						FString FilePattern = FString::Format(TEXT("{0}/LandscapeLayers/{1}/{2}/{3}/Weightmaps/{4}/{5}-{6}-{7}[mip{8}]"), 
							{ FPaths::ProjectSavedDir(), CurrentTime.ToString(), WorldName, ParentLandscapeActorName, AllocInfo.GetLayerName().ToString(), ActorName, Component->GetName(), InWeightmap->GetName(), InMipLevel});

						FFileHelper::EColorChannel ColorChannel = GetWeightmapColorChannel(AllocInfo);
						FFileHelper::CreateBitmap(*(FilePattern + "_a(pre).bmp"), SizeU, SizeV, InOldData, /*SubRectangle = */nullptr, &IFileManager::Get(), /*OutFilename = */nullptr, /*bool bInWriteAlpha = */true, ColorChannel);
						FFileHelper::CreateBitmap(*(FilePattern + "_b(post).bmp"), SizeU, SizeV, InNewData, /*SubRectangle = */nullptr, &IFileManager::Get(), /*OutFilename = */nullptr, /*bool bInWriteAlpha = */true, ColorChannel);
			
						if (bDumpDiffDetails)
						{
							static const TCHAR* Channels = TEXT("RGBA");
							int32 NumDifferentPixels = 0;
							uint8 MaxDiff = 0;
							FStringBuilderBase StrBuilder;
							FIntPoint MaxDiffUV(ForceInit);
							for (int32 V = 0; V < SizeV; ++V)
							{
								for (int32 U = 0; U < SizeU; ++U)
								{
									const FColor* OldDataPtr = InOldData + (V * SizeU + U);
									const FColor* NewDataPtr = InNewData + (V * SizeU + U);
									if (*OldDataPtr != *NewDataPtr)
									{
										uint32 OldValueAsUInt32 = OldDataPtr->ToPackedRGBA();
										uint8 OldValue = (OldValueAsUInt32 >> ((3 - AllocInfo.WeightmapTextureChannel) * 8)) & 0xff;
										uint32 NewValueAsUInt32 = NewDataPtr->ToPackedRGBA();
										uint8 NewValue = (NewValueAsUInt32 >> ((3 - AllocInfo.WeightmapTextureChannel) * 8)) & 0xff;
										uint8 Diff = (NewValue > OldValue) ? NewValue - OldValue : OldValue - NewValue;
										if (Diff > 0)
										{
											if (Diff > MaxDiff)
											{
												MaxDiffUV = FIntPoint(U, V);
												MaxDiff = Diff;
											}

											TCHAR Channel = (AllocInfo.WeightmapTextureChannel == 0) ? TEXT('R') 
												: (AllocInfo.WeightmapTextureChannel == 1) ? TEXT('G') 
												: (AllocInfo.WeightmapTextureChannel == 2) ? TEXT('B') 
												: TEXT('A');

											StrBuilder.Appendf(TEXT("Pixel (%4u,%4u) : RGBA ((%3u,%3u,%3u,%3u) -> (%3u,%3u,%3u,%3u)) : channel %c (%3u -> %3u, absdiff %3u)\n"), 
												U, V, OldDataPtr->R, OldDataPtr->G, OldDataPtr->B, OldDataPtr->A, NewDataPtr->R, NewDataPtr->G, NewDataPtr->B, NewDataPtr->A, Channels[AllocInfo.WeightmapTextureChannel], OldValue, NewValue, Diff);

											++NumDifferentPixels;
										}
									}
								}
							}
							StrBuilder.InsertAt(0, FString::Printf(TEXT("----------------------------------------\n")));
							StrBuilder.InsertAt(0, FString::Printf(TEXT("Max diff (at %s) = %u (%1.3f%%)\n"), *MaxDiffUV.ToString(), MaxDiff, 100.0 * static_cast<float>(MaxDiff) / MAX_uint8));
							StrBuilder.InsertAt(0, FString::Printf(TEXT("Num diffs = %u\n"), NumDifferentPixels));
							StrBuilder.InsertAt(0, FString::Printf(TEXT("Layer %s is packed in channel %c\n"), *AllocInfo.GetLayerName().ToString(), Channels[AllocInfo.WeightmapTextureChannel]));
							FFileHelper::SaveStringToFile(StrBuilder.ToView(), *(FilePattern + "_diff.txt"));
						}
					}
				}
			}
		}
	}
}

void ALandscape::UpdateHeightDirtyData(ULandscapeComponent* InLandscapeComponent, UTexture2D const* InHeightmap, FColor const* InOldData, FColor const* InNewData)
{
	check(InOldData && InNewData);

	FLandscapeEditDataInterface LandscapeEdit(GetLandscapeInfo());
	const int32 X1 = InLandscapeComponent->GetSectionBase().X;
	const int32 X2 = X1 + ComponentSizeQuads;
	const int32 Y1 = InLandscapeComponent->GetSectionBase().Y;
	const int32 Y2 = Y1 + ComponentSizeQuads;
	const int32 ComponentWidth = ComponentSizeQuads + 1;
	const int32 DirtyDataSize = ComponentWidth * ComponentWidth;
	TUniquePtr<uint8[]> DirtyData = MakeUnique<uint8[]>(DirtyDataSize);
	const int32 SizeU = InHeightmap->Source.GetSizeX();
	const int32 SizeV = InHeightmap->Source.GetSizeY();
	const int32 HeightmapOffsetX = static_cast<int32>(InLandscapeComponent->HeightmapScaleBias.Z * SizeU);
	const int32 HeightmapOffsetY = static_cast<int32>(InLandscapeComponent->HeightmapScaleBias.W * SizeV);
	const uint8 DirtyHeight = 1 << 0;
	LandscapeEdit.GetDirtyData(X1, Y1, X2, Y2, DirtyData.Get(), 0);

	FLandscapeComponentDataInterface CDI(InLandscapeComponent);
	for (int32 X = 0; X < ComponentWidth; ++X)
	{
		for (int32 Y = 0; Y < ComponentWidth; ++Y)
		{
			int32 TexX, TexY;
			CDI.VertexXYToTexelXY(X, Y, TexX, TexY);
			TexX += HeightmapOffsetX;
			TexY += HeightmapOffsetY;
			int32 TexIndex = TexX + TexY * SizeU;
			check(TexIndex < SizeU * SizeV);
			if (InOldData[TexIndex] != InNewData[TexIndex])
			{
				DirtyData[X + Y * ComponentWidth] |= DirtyHeight;
			}
		}
	}

	LandscapeEdit.SetDirtyData(X1, Y1, X2, Y2, DirtyData.Get(), 0);
}

void ALandscape::OnDirtyHeightmap(FTextureToComponentHelper const& MapHelper, UTexture2D const* InHeightmap, FColor const* InOldData, FColor const* InNewData, int32 InMipLevel)
{
	int32 DumpHeightmapDiff = CVarLandscapeDumpHeightmapDiff.GetValueOnGameThread();
	const bool bDumpDiff = (DumpHeightmapDiff > 0);
	const bool bDumpDiffAllMips = (DumpHeightmapDiff > 1);
	const bool bDumpDiffDetails = CVarLandscapeDumpDiffDetails.GetValueOnGameThread();
	const bool bTrackDirty = CVarLandscapeTrackDirty.GetValueOnGameThread() != 0;
	ULandscapeSubsystem* LandscapeSubsystem = GetWorld()->GetSubsystem<ULandscapeSubsystem>();
	check(LandscapeSubsystem != nullptr);
	const FDateTime CurrentTime = LandscapeSubsystem->GetAppCurrentDateTime();

	if ((!bDumpDiff && !bTrackDirty)
		|| (bDumpDiff && !bDumpDiffAllMips && (InMipLevel > 0))
		|| (bTrackDirty && (InMipLevel > 0)))
	{
		return;
	}

	TArray<ULandscapeComponent*> const* Components = MapHelper.HeightmapToComponents.Find(InHeightmap);
	if (Components != nullptr)
	{
		for (ULandscapeComponent* Component : *Components)
		{
			if (bTrackDirty)
			{
				UpdateHeightDirtyData(Component, InHeightmap, InOldData, InNewData);
			}

			if (bDumpDiff)
			{
				FString WorldName = GetWorld()->GetName();
				FString ParentLandscapeActorName = GetActorLabel();
				ALandscapeProxy* Proxy = Cast<ALandscapeProxy>(Component->GetOwner());
				check(Proxy);
				FString ActorName = Proxy->GetActorLabel();
				FString FilePattern = FString::Format(TEXT("{0}/LandscapeLayers/{1}/{2}/{3}/Heightmaps/{4}-{5}-{6}[mip{7}]"), { FPaths::ProjectSavedDir(), CurrentTime.ToString(), WorldName, ParentLandscapeActorName, ActorName, Component->GetName(), InHeightmap->GetName(), InMipLevel });

				const int32 SizeU = InHeightmap->Source.GetSizeX() >> InMipLevel;
				const int32 SizeV = InHeightmap->Source.GetSizeY() >> InMipLevel;
				const int32 HeightmapOffsetX = static_cast<int32>(Component->HeightmapScaleBias.Z * SizeU);
				const int32 HeightmapOffsetY = static_cast<int32>(Component->HeightmapScaleBias.W * SizeV);
				const int32 ComponentWidth = ((SubsectionSizeQuads + 1) * NumSubsections) >> InMipLevel;
				FIntRect SubRegion(HeightmapOffsetX, HeightmapOffsetY, HeightmapOffsetX + ComponentWidth, HeightmapOffsetY + ComponentWidth);

				int32 NumDifferentPixels = 0;
				uint16 MaxHeightDiff = 0;
				FIntPoint MaxHeightDiffUV(ForceInit);
				uint8 MaxNormalDiff = 0;
				FIntPoint MaxNormalDiffUV(ForceInit);
				FStringBuilderBase StrBuilder;
				const FColor* OldDataStartPtr = InOldData + (HeightmapOffsetY * SizeU + HeightmapOffsetX);
				const FColor* NewDataStartPtr = InNewData + (HeightmapOffsetY * SizeU + HeightmapOffsetX);
				for (int32 V = 0; V < ComponentWidth; ++V)
				{
					for (int32 U = 0; U < ComponentWidth; ++U)
					{
						const FColor* OldDataPtr = OldDataStartPtr + (V * SizeU + U);
						const FColor* NewDataPtr = NewDataStartPtr + (V * SizeU + U);
						if (*OldDataPtr != *NewDataPtr)
						{
							uint16 OldHeight = ((static_cast<uint16>(OldDataPtr->R) << 8) | static_cast<uint16>(OldDataPtr->G));
							uint16 NewHeight = ((static_cast<uint16>(NewDataPtr->R) << 8) | static_cast<uint16>(NewDataPtr->G));
							uint16 HeightDiff = (NewHeight > OldHeight) ? NewHeight - OldHeight : OldHeight - NewHeight;
							if (HeightDiff > MaxHeightDiff)
							{
								MaxHeightDiffUV = FIntPoint(U, V);
								MaxHeightDiff = HeightDiff;
							}

							uint8 OldNormalX = (OldDataPtr->B);
							uint8 NewNormalX = (NewDataPtr->B);
							uint16 NormalXDiff = (NewNormalX > OldNormalX) ? NewNormalX - OldNormalX : OldNormalX - NewNormalX;
							if (NormalXDiff > MaxNormalDiff)
							{
								MaxNormalDiffUV = FIntPoint(U, V);
								MaxNormalDiff = NormalXDiff;
							}

							uint8 OldNormalY = (OldDataPtr->A);
							uint8 NewNormalY = (NewDataPtr->A);
							uint16 NormalYDiff = (NewNormalY > OldNormalY) ? NewNormalY - OldNormalY : OldNormalY - NewNormalY;
							if (NormalYDiff > MaxNormalDiff)
							{
								MaxNormalDiffUV = FIntPoint(U, V);
								MaxNormalDiff = NormalYDiff;
							}

							StrBuilder.Appendf(TEXT("Pixel (%4u,%4u) : Height (%5u -> %5u, absdiff %5u), Normal ((%3u,%3u) -> (%3u,%3u), absdiff %3u)\n"),
								U, V, OldHeight, NewHeight, HeightDiff, OldNormalX, OldNormalY, NewNormalX, NewNormalY, FMath::Max(NormalXDiff, NormalYDiff));

							++NumDifferentPixels;
						}
					}
				}

				if (NumDifferentPixels > 0)
				{
					FFileHelper::CreateBitmap(*(FilePattern + "_a(pre).bmp"), SizeU, SizeV, InOldData, &SubRegion, &IFileManager::Get(), /*OutFilename = */nullptr, /*bool bInWriteAlpha = */true);
					FFileHelper::CreateBitmap(*(FilePattern + "_b(post).bmp"), SizeU, SizeV, InNewData, &SubRegion, &IFileManager::Get(), /*OutFilename = */nullptr, /*bool bInWriteAlpha = */true);

					if (bDumpDiffDetails)
					{
						StrBuilder.InsertAt(0, FString::Printf(TEXT("----------------------------------------\n")));
						StrBuilder.InsertAt(0, FString::Printf(TEXT("Max normal diff (at %s) = %u (%1.3f%%)\n"), *MaxNormalDiffUV.ToString(), MaxNormalDiff, 100.0f * static_cast<float>(MaxNormalDiff) / MAX_uint8));
						StrBuilder.InsertAt(0, FString::Printf(TEXT("Max height diff (at %s) = %u (%1.3f%%)\n"), *MaxHeightDiffUV.ToString(), MaxHeightDiff, 100.0f * static_cast<float>(MaxHeightDiff) / MAX_uint16));
						StrBuilder.InsertAt(0, FString::Printf(TEXT("Num diffs = %u\n"), NumDifferentPixels));
						FFileHelper::SaveStringToFile(StrBuilder.ToView(), *(FilePattern + "_diff.txt"));
					}
				}
			}
		}
	}
}

bool ALandscape::HasTextureDataChanged(TArrayView<const FColor> InOldData, TArrayView<const FColor> InNewData, bool bInIsWeightmap, uint64 InPreviousHash, uint64& OutNewHash, TOptional<uint8>& OutChangedWeightmapChannelsMasks) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(ALandscape::HasTextureDataChanged);

	OutNewHash = FLandscapeEditLayerReadback::CalculateHash((uint8*)InNewData.GetData(), InNewData.Num() * sizeof(FColor));
	if (OutNewHash != InPreviousHash)
	{
		int32 TextureSize = InOldData.Num();
		check(TextureSize == InNewData.Num());

		// If necessary, perform a deep comparison to avoid taking into account precision issues as actual changes :
		if (bInIsWeightmap)
		{
			int32 DirtyWeightmapThreshold = CVarLandscapeDirtyWeightmapThreshold.GetValueOnGameThread();
			if ((DirtyWeightmapThreshold > 0) || OutChangedWeightmapChannelsMasks.IsSet())
			{
				bool bHasChanged = false;

				TRACE_CPUPROFILER_EVENT_SCOPE(DeepCompareWeightmap);
				for (int32 Index = 0; Index < TextureSize; ++Index)
				{
					const FColor& OldColor = InOldData[Index];
					const FColor& NewColor = InNewData[Index];
					if (OldColor != NewColor)
					{
						auto DiffChannel = [DirtyWeightmapThreshold] (uint8 InOldValue, uint8 InNewValue) -> bool
						{
							uint8 Diff = (InNewValue > InOldValue) ? (InNewValue - InOldValue) : (InOldValue - InNewValue);
							return (Diff > DirtyWeightmapThreshold);
						};

						uint8 DiffMask = ((uint8)(DiffChannel(OldColor.R, NewColor.R) ? 1 : 0) << 0)
							| ((uint8)(DiffChannel(OldColor.G, NewColor.G) ? 1 : 0) << 1)
							| ((uint8)(DiffChannel(OldColor.B, NewColor.B) ? 1 : 0) << 2)
							| ((uint8)(DiffChannel(OldColor.A, NewColor.A) ? 1 : 0) << 3);

						if (DiffMask != 0)
						{
							bHasChanged = true;
							if (OutChangedWeightmapChannelsMasks.IsSet())
							{
								*OutChangedWeightmapChannelsMasks |= DiffMask;
							}
							else
							{
								// no need to report which channel has been changed, early out : 
								return true;
							}
						}
					}
				}

				return bHasChanged;
			}
		}
		else 
		{
			int32 DirtyHeightmapHeightThreshold = CVarLandscapeDirtyHeightmapHeightThreshold.GetValueOnGameThread();
			int32 DirtyHeightmapNormalThreshold = CVarLandscapeDirtyHeightmapNormalThreshold.GetValueOnGameThread();
			if (DirtyHeightmapHeightThreshold > 0 || DirtyHeightmapNormalThreshold > 0)
			{
				TRACE_CPUPROFILER_EVENT_SCOPE(DeepCompareHeightmap);
				for (int32 Index = 0; Index < TextureSize; ++Index)
				{
					const FColor& OldColor = InOldData[Index];
					const FColor& NewColor = InNewData[Index];
					if (OldColor != NewColor)
					{
						uint16 OldHeight = ((static_cast<uint16>(OldColor.R) << 8) | static_cast<uint16>(OldColor.G));
						uint16 NewHeight = ((static_cast<uint16>(NewColor.R) << 8) | static_cast<uint16>(NewColor.G));
						if (uint16 Diff = (NewHeight > OldHeight) ? (NewHeight - OldHeight) : (OldHeight - NewHeight); Diff > DirtyHeightmapHeightThreshold)
						{
							return true;
						}
						if (uint8 Diff = (NewColor.B > OldColor.B) ? (NewColor.B - OldColor.B) : (OldColor.B - NewColor.B); Diff > DirtyHeightmapNormalThreshold)
						{
							return true;
						}
						if (uint8 Diff = (NewColor.A > OldColor.A) ? (NewColor.A - OldColor.A) : (OldColor.A - NewColor.A); Diff > DirtyHeightmapNormalThreshold)
						{
							return true;
						}
					}
				}

				// No significant difference detected : 
				return false;
			}
		}

		// Hash is different and we don't need a deep comparison : 
		return true;
	}

	// Hash is identical : 
	return false;
}

bool ALandscape::ResolveLayersTexture(
	FTextureToComponentHelper const& MapHelper,
	FLandscapeEditLayerReadback* InCPUReadback,
	UTexture2D* InOutputTexture,
	bool bIntermediateRender,
	TArray<FLandscapeEditLayerComponentReadbackResult>& InOutComponentReadbackResults,
	bool bIsWeightmap)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(LandscapeLayers_ResolveLayersTexture);

	InCPUReadback->Tick();

	const int32 CompletedReadbackNum = InCPUReadback->GetCompletedResultNum();

	bool bUserTriggered = false;

	if (TArray<ULandscapeComponent*> const * Components = bIsWeightmap ? MapHelper.WeightmapToComponents.Find(InOutputTexture) : MapHelper.HeightmapToComponents.Find(InOutputTexture))
	{
		for (ULandscapeComponent* Component : *Components)
		{
			if (Component->GetUserTriggeredChangeRequested())
			{
				bUserTriggered = true;
				break;
			}
		}
	}
	
	bool bChanged = false;
	TOptional<uint8> ChangedChannelsMask;
	if (bIsWeightmap)
	{
		// Request a precise report of which channel have changed if we need to dump the weightmap diffs :
		if ((CVarLandscapeDumpWeightmapDiff.GetValueOnGameThread() != 0) 
			|| CVarLandscapeTrackDirty.GetValueOnGameThread() != 0)
		{
			ChangedChannelsMask.Emplace(0);
		}
	}

	if (CompletedReadbackNum > 0)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(LandscapeLayers_PerformReadbacks);

		// Copy final result to texture source.
		TArray<TArray<FColor>> const& OutMipsData = InCPUReadback->GetResult(CompletedReadbackNum - 1);

		for (int8 MipIndex = 0; MipIndex < OutMipsData.Num(); ++MipIndex)
		{
			int32 TextureSize = OutMipsData[MipIndex].Num();
			if (TextureSize > 0)
			{
				uint8* TextureData = InOutputTexture->Source.LockMip(MipIndex);

				// Do dirty detection on first mip.
				// Don't do this for intermediate renders.
				if (MipIndex == 0 && !bIntermediateRender)
				{
					uint64 Hash = 0;
					if (HasTextureDataChanged(MakeArrayView(reinterpret_cast<const FColor*>(TextureData), TextureSize), MakeArrayView(OutMipsData[MipIndex].GetData(), TextureSize), bIsWeightmap, InCPUReadback->GetHash(), Hash, ChangedChannelsMask))
					{
						bChanged |= InCPUReadback->SetHash(Hash);
						check(bChanged);
					}

					if (bChanged)
					{
						// We're about to modify the texture's source data, the texture needs to know so that it can handle properly update cached platform data (additionally, the package needs to be dirtied) :
						if (GetDefault<ULandscapeSettings>()->LandscapeDirtyingMode == ELandscapeDirtyingMode::InLandscapeModeAndUserTriggeredChanges)
						{
							FLandscapeDirtyOnlyInModeScope Scope(GetLandscapeInfo(), !bUserTriggered);
							GetLandscapeInfo()->ModifyObject(InOutputTexture);
						}
						else
						{
							GetLandscapeInfo()->ModifyObject(InOutputTexture);
						}
					}
				}

				if (bChanged)
				{
					if (bIsWeightmap)
					{
						OnDirtyWeightmap(MapHelper, InOutputTexture, (FColor*)TextureData, OutMipsData[MipIndex].GetData(), MipIndex, ChangedChannelsMask.Get(0));
					}
					else
					{
						OnDirtyHeightmap(MapHelper, InOutputTexture, (FColor*)TextureData, OutMipsData[MipIndex].GetData(), MipIndex);
					}
				}

				TRACE_CPUPROFILER_EVENT_SCOPE(ReadbackToCPU);
				FMemory::Memcpy(TextureData, OutMipsData[MipIndex].GetData(), OutMipsData[MipIndex].Num() * sizeof(FColor));
			}
		}

		// Unlock all mips at once because there's a lock counter in FTextureSource that recomputes the content hash when reaching 0 (which means we'd recompute the hash several times over if we Lock/Unlock/Lock/Unloc/... for each mip ):
		for (int8 MipIndex = 0; MipIndex < OutMipsData.Num(); ++MipIndex)
		{
			if (OutMipsData[MipIndex].Num() > 0)
			{
				InOutputTexture->Source.UnlockMip(MipIndex);
			}
		}

		// change lighting guid to be the hash of the source data (so we can use lighting guid to detect when it actually changes)
		// TODO [chris.tchou] we should check(InOutputTexture->Source.bGuidIsHash); .. but it's not a public value currently.
 		InOutputTexture->SetLightingGuid(InOutputTexture->Source.GetId());

		// Find out whether some channels from this weightmap are now all zeros : 
		static constexpr uint32 AllChannelsAllZerosMask = 15;
		uint32 AllZerosTextureChannelMask = AllChannelsAllZerosMask;
		const bool bCheckForEmptyChannels = CVarLandscapeRemoveEmptyPaintLayersOnEdit.GetValueOnGameThread() != 0;
		if (bIsWeightmap && bCheckForEmptyChannels)
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(LandscapeLayers_AnalyzeWeightmap);
			const FColor* TextureData = reinterpret_cast<const FColor*>(InOutputTexture->Source.LockMipReadOnly(0));
			const int32 TexSize = OutMipsData[0].Num();
			// We can stop iterating as soon as all of the channels are non-zero :
			for (int32 Index = 0; (Index < TexSize) && (AllZerosTextureChannelMask != 0); ++Index)
			{
				AllZerosTextureChannelMask &= (((TextureData[Index].R == 0) ? 1 : 0) << 0)
					| (((TextureData[Index].G == 0) ? 1 : 0) << 1)
					| (((TextureData[Index].B == 0) ? 1 : 0) << 2)
					| (((TextureData[Index].A == 0) ? 1 : 0) << 3);
			}
			InOutputTexture->Source.UnlockMip(0);
		}

		// Process component flags from all result contexts.
		for (int32 ResultIndex = 0; ResultIndex < CompletedReadbackNum; ++ResultIndex)
		{
			const FLandscapeEditLayerReadback::FReadbackContext& ResultContext = InCPUReadback->GetResultContext(ResultIndex);
			for (const FLandscapeEditLayerReadback::FComponentReadbackContext& ComponentContext : ResultContext)
			{
				ULandscapeComponent** Component = GetLandscapeInfo()->XYtoComponentMap.Find(ComponentContext.ComponentKey);
				if (Component != nullptr && *Component != nullptr)
				{
					FLandscapeEditLayerComponentReadbackResult* ComponentReadbackResult = InOutComponentReadbackResults.FindByPredicate([LandscapeComponent = *Component](const FLandscapeEditLayerComponentReadbackResult& Element) { return Element.LandscapeComponent == LandscapeComponent; });
					if (ComponentReadbackResult == nullptr)
					{
						ComponentReadbackResult = &InOutComponentReadbackResults.Add_GetRef(FLandscapeEditLayerComponentReadbackResult(*Component, ELandscapeLayerUpdateMode::Update_None));
					}
					ComponentReadbackResult->UpdateModes |= ComponentContext.UpdateModes;
					ComponentReadbackResult->bModified |= bChanged ? 1 : 0;
				}
			}
		}

		// We need to find the weightmap layers that are effectively empty in order to let the component clean them up eventually :
		if (bIsWeightmap && bCheckForEmptyChannels && (AllZerosTextureChannelMask != 0))
		{
			// Only use the latest readback context, since it's the only one we've actually read back : 
			const FLandscapeEditLayerReadback::FReadbackContext& EffectiveResultContext = InCPUReadback->GetResultContext(CompletedReadbackNum - 1);
			while (AllZerosTextureChannelMask != 0)
			{
				int32 AllZerosTextureChannelIndex = NumBitsPerDWORD - 1 - FMath::CountLeadingZeros(AllZerosTextureChannelMask);
				for (const FLandscapeEditLayerReadback::FComponentReadbackContext& ComponentContext : EffectiveResultContext)
				{
					FName AllZerosLayerInfoName = ComponentContext.PerChannelLayerNames[AllZerosTextureChannelIndex];
					ULandscapeComponent** Component = GetLandscapeInfo()->XYtoComponentMap.Find(ComponentContext.ComponentKey);
					if (Component != nullptr && *Component != nullptr)
					{
						const TArray<FWeightmapLayerAllocationInfo>& WeightmapLayerAllocations = (*Component)->GetWeightmapLayerAllocations();
						const TArray<UTexture2D*>& WeightmapTextures = (*Component)->GetWeightmapTextures();
						for (const FWeightmapLayerAllocationInfo& WeightmapLayerAllocation : WeightmapLayerAllocations)
						{
							if (WeightmapLayerAllocation.IsAllocated())
							{
								UTexture2D* Texture = WeightmapTextures[WeightmapLayerAllocation.WeightmapTextureIndex];
								if ((Texture == InOutputTexture) && (AllZerosLayerInfoName == WeightmapLayerAllocation.LayerInfo->LayerName))
								{
									FLandscapeEditLayerComponentReadbackResult* ComponentReadbackResult = InOutComponentReadbackResults.FindByPredicate([LandscapeComponent = *Component](const FLandscapeEditLayerComponentReadbackResult& Element) { return Element.LandscapeComponent == LandscapeComponent; });
									check(ComponentReadbackResult != nullptr);

									// Mark this layer info within this component as being all-zero :
									ComponentReadbackResult->AllZeroLayers.AddUnique(WeightmapLayerAllocation.LayerInfo);
								}
							}
						}
					}
				}
				AllZerosTextureChannelMask &= ~((uint32)1 << AllZerosTextureChannelIndex);
			}
		}

		// Release the processed read backs
		InCPUReadback->ReleaseCompletedResults(CompletedReadbackNum);
	}

	return bChanged;
}

void ALandscape::PrepareComponentDataToExtractMaterialLayersCS(const TArray<ULandscapeComponent*>& InLandscapeComponents, const FLandscapeLayer& InLayer, int32 InCurrentWeightmapToProcessIndex, const FIntPoint& InLandscapeBase, FLandscapeTexture2DResource* InOutTextureData,
	TArray<FLandscapeLayerWeightmapExtractMaterialLayersComponentData>& OutComponentData, TMap<ULandscapeLayerInfoObject*, int32>& OutLayerInfoObjects)
{
	ULandscapeInfo* Info = GetLandscapeInfo();

	if (Info == nullptr)
	{
		return;
	}

	TArray<FLandscapeLayersCopyTextureParams> DeferredCopyTextures;

	const int32 LocalComponentSizeQuad = SubsectionSizeQuads * NumSubsections;
	const int32 LocalComponentSizeVerts = (SubsectionSizeQuads + 1) * NumSubsections;
	for (ULandscapeComponent* Component : InLandscapeComponents)
	{
		FLandscapeLayerComponentData* ComponentLayerData = Component->GetLayerData(InLayer.Guid);

		if (ComponentLayerData != nullptr)
		{
			if (ComponentLayerData->WeightmapData.Textures.IsValidIndex(InCurrentWeightmapToProcessIndex) && ComponentLayerData->WeightmapData.TextureUsages.IsValidIndex(InCurrentWeightmapToProcessIndex))
			{
				UTexture2D* LayerWeightmap = ComponentLayerData->WeightmapData.Textures[InCurrentWeightmapToProcessIndex];
				check(LayerWeightmap != nullptr);

				const ULandscapeWeightmapUsage* LayerWeightmapUsage = ComponentLayerData->WeightmapData.TextureUsages[InCurrentWeightmapToProcessIndex];
				check(LayerWeightmapUsage != nullptr);

				FIntPoint ComponentSectionBase = Component->GetSectionBase() - InLandscapeBase;
				FVector2D SourcePositionOffset(FMath::RoundToInt((float)ComponentSectionBase.X / LocalComponentSizeQuad), FMath::RoundToInt((float)ComponentSectionBase.Y / LocalComponentSizeQuad));
				FIntPoint SourceComponentVertexPosition = FIntPoint(static_cast<int32>(SourcePositionOffset.X * LocalComponentSizeVerts), static_cast<int32>(SourcePositionOffset.Y * LocalComponentSizeVerts));

				FLandscapeLayersCopyTextureParams& CopyTextureParams = DeferredCopyTextures.Add_GetRef(FLandscapeLayersCopyTextureParams(*LayerWeightmap->GetName(), LayerWeightmap->GetResource(), FString::Printf(TEXT("%s WeightmapScratchTexture"), *InLayer.Name.ToString()), InOutTextureData));
				// Only copy the size that's actually needed : 
				CopyTextureParams.CopySize.X = LayerWeightmap->GetResource()->GetSizeX();
				CopyTextureParams.CopySize.Y = LayerWeightmap->GetResource()->GetSizeY();
				// Copy from the top-left corner of the weightmap to the composited texture's position
				CopyTextureParams.DestPosition = SourceComponentVertexPosition;
				PrintLayersDebugTextureResource(FString::Printf(TEXT("LS Weight: %s WeightmapScratchTexture %s"), *InLayer.Name.ToString(), TEXT("WeightmapScratchTextureResource")), InOutTextureData, 0, false);

				for (const FWeightmapLayerAllocationInfo& WeightmapLayerAllocation : ComponentLayerData->WeightmapData.LayerAllocations)
				{
					if (WeightmapLayerAllocation.LayerInfo != nullptr && WeightmapLayerAllocation.IsAllocated() && ComponentLayerData->WeightmapData.Textures[WeightmapLayerAllocation.WeightmapTextureIndex] == LayerWeightmap)
					{
						const ULandscapeComponent* DestComponent = LayerWeightmapUsage->ChannelUsage[WeightmapLayerAllocation.WeightmapTextureChannel];
						check(DestComponent);

						FIntPoint DestComponentSectionBase = DestComponent->GetSectionBase() - InLandscapeBase;

						// Compute component top left vertex position from section base info
						FVector2D DestPositionOffset(FMath::RoundToInt((float)DestComponentSectionBase.X / LocalComponentSizeQuad), FMath::RoundToInt((float)DestComponentSectionBase.Y / LocalComponentSizeQuad));

						FLandscapeLayerWeightmapExtractMaterialLayersComponentData Data{
							SourceComponentVertexPosition,
							0,
							WeightmapLayerAllocation.WeightmapTextureChannel,
							FIntPoint(static_cast<int32>(DestPositionOffset.X * LocalComponentSizeVerts), static_cast<int32>(DestPositionOffset.Y * LocalComponentSizeVerts))
						};

						if (WeightmapLayerAllocation.LayerInfo == ALandscapeProxy::VisibilityLayer)
						{
							int32& NewLayerInfoObjectIndex = OutLayerInfoObjects.FindOrAdd(ALandscapeProxy::VisibilityLayer);
							NewLayerInfoObjectIndex = 0;
						}
						else
						{
							for (int32 LayerInfoSettingsIndex = 0; LayerInfoSettingsIndex < Info->Layers.Num(); ++LayerInfoSettingsIndex)
							{
								const FLandscapeInfoLayerSettings& InfoLayerSettings = Info->Layers[LayerInfoSettingsIndex];

								if (InfoLayerSettings.LayerInfoObj == WeightmapLayerAllocation.LayerInfo)
								{
									Data.DestinationPaintLayerIndex = LayerInfoSettingsIndex + 1; // due to visibility layer that is at 0
									int32& NewLayerinfoObjectIndex = OutLayerInfoObjects.FindOrAdd(WeightmapLayerAllocation.LayerInfo);
									NewLayerinfoObjectIndex = LayerInfoSettingsIndex + 1;

									break;
								}
							}
						}

						OutComponentData.Add(Data);
					}
				}
			}
		}
	}

	ExecuteCopyLayersTexture(MoveTemp(DeferredCopyTextures));
}

void ALandscape::PrepareComponentDataToPackMaterialLayersCS(int32 InCurrentWeightmapToProcessIndex, const FIntPoint& InLandscapeBase, const TArray<ULandscapeComponent*>& InAllLandscapeComponents, TArray<UTexture2D*>& OutProcessedWeightmaps,
	TArray<FLandscapeEditLayerReadback*>& OutProcessedCPUReadbacks, TArray<FLandscapeLayerWeightmapPackMaterialLayersComponentData>& OutComponentData)
{
	ULandscapeInfo* Info = GetLandscapeInfo();

	if (Info == nullptr)
	{
		return;
	}

	// Compute a mapping of all textures for the asked index and their usage
	TMap<UTexture2D*, ULandscapeWeightmapUsage*> WeightmapsToProcess;

	for (ULandscapeComponent* Component : InAllLandscapeComponents)
	{
		const TArray<UTexture2D*>& ComponentWeightmapTextures = Component->GetWeightmapTextures();
		const TArray<ULandscapeWeightmapUsage*>& ComponentWeightmapTexturesUsage = Component->GetWeightmapTexturesUsage();

		if (ComponentWeightmapTextures.IsValidIndex(InCurrentWeightmapToProcessIndex))
		{
			UTexture2D* ComponentWeightmapTexture = ComponentWeightmapTextures[InCurrentWeightmapToProcessIndex];
			ULandscapeWeightmapUsage* ComponentWeightmapTextureUsage = ComponentWeightmapTexturesUsage[InCurrentWeightmapToProcessIndex];
			check(ComponentWeightmapTextureUsage != nullptr);

			// It's possible the texture (and its usage, hence) has already been processed by a previous call to PrepareComponentDataToPackMaterialLayersCS with a different InCurrentWeightmapToProcessIndex
			//  since a texture can be shared by multiple components :
			if (!OutProcessedWeightmaps.Contains(ComponentWeightmapTexture) && (WeightmapsToProcess.Find(ComponentWeightmapTexture) == nullptr))
			{
				WeightmapsToProcess.Add(ComponentWeightmapTexture, ComponentWeightmapTextureUsage);
				OutProcessedWeightmaps.Add(ComponentWeightmapTexture);

				FLandscapeEditLayerReadback** CPUReadback = Component->GetLandscapeProxy()->WeightmapsCPUReadback.Find(ComponentWeightmapTexture);
				check(CPUReadback != nullptr);

				OutProcessedCPUReadbacks.Add(*CPUReadback);
			}
		}
	}

	TArray<const FWeightmapLayerAllocationInfo*> AlreadyProcessedAllocation;

	// Build for each texture what each channel should contains
	for (auto& ItPair : WeightmapsToProcess)
	{
		FLandscapeLayerWeightmapPackMaterialLayersComponentData Data;

		for (int32 WeightmapChannelIndex = 0; WeightmapChannelIndex < ULandscapeWeightmapUsage::NumChannels; ++WeightmapChannelIndex)
		{
			UTexture2D* ComponentWeightmapTexture = ItPair.Key;
			ULandscapeWeightmapUsage* ComponentWeightmapTextureUsage = ItPair.Value;

			// Clear out data to known values
			Data.ComponentVertexPositionX[WeightmapChannelIndex] = INDEX_NONE;
			Data.ComponentVertexPositionY[WeightmapChannelIndex] = INDEX_NONE;
			Data.SourcePaintLayerIndex[WeightmapChannelIndex] = INDEX_NONE;
			Data.WeightmapChannelToProcess[WeightmapChannelIndex] = INDEX_NONE;

			if (ComponentWeightmapTextureUsage->ChannelUsage[WeightmapChannelIndex] != nullptr)
			{
				const ULandscapeComponent* ChannelComponent = ComponentWeightmapTextureUsage->ChannelUsage[WeightmapChannelIndex];

				const TArray<FWeightmapLayerAllocationInfo>& ChannelLayerAllocations = ChannelComponent->GetWeightmapLayerAllocations();
				const TArray<UTexture2D*>& ChannelComponentWeightmapTextures = ChannelComponent->GetWeightmapTextures();

				for (const FWeightmapLayerAllocationInfo& ChannelLayerAllocation : ChannelLayerAllocations)
				{
					if (ChannelLayerAllocation.LayerInfo != nullptr && !AlreadyProcessedAllocation.Contains(&ChannelLayerAllocation) && ChannelComponentWeightmapTextures[ChannelLayerAllocation.WeightmapTextureIndex] == ComponentWeightmapTexture)
					{
						FIntPoint ComponentSectionBase = ChannelComponent->GetSectionBase() - InLandscapeBase;

						// Compute component top left vertex position from section base info
						int32 LocalComponentSizeQuad = ChannelComponent->SubsectionSizeQuads * NumSubsections;
						int32 LocalComponentSizeVerts = (ChannelComponent->SubsectionSizeQuads + 1) * NumSubsections;
						FVector2D PositionOffset(FMath::RoundToInt((float)ComponentSectionBase.X / LocalComponentSizeQuad), FMath::RoundToInt((float)ComponentSectionBase.Y / LocalComponentSizeQuad));

						Data.ComponentVertexPositionX[WeightmapChannelIndex] = static_cast<int32>(PositionOffset.X * LocalComponentSizeVerts);
						Data.ComponentVertexPositionY[WeightmapChannelIndex] = static_cast<int32>(PositionOffset.Y * LocalComponentSizeVerts);

						Data.WeightmapChannelToProcess[WeightmapChannelIndex] = ChannelLayerAllocation.WeightmapTextureChannel;

						AlreadyProcessedAllocation.Add(&ChannelLayerAllocation);

						if (ChannelLayerAllocation.LayerInfo == ALandscapeProxy::VisibilityLayer)
						{
							Data.SourcePaintLayerIndex[WeightmapChannelIndex] = 0; // Always store after the last weightmap index
						}
						else
						{
							for (int32 LayerInfoSettingsIndex = 0; LayerInfoSettingsIndex < Info->Layers.Num(); ++LayerInfoSettingsIndex)
							{
								const FLandscapeInfoLayerSettings& LayerInfo = Info->Layers[LayerInfoSettingsIndex];

								if (ChannelLayerAllocation.LayerInfo == LayerInfo.LayerInfoObj)
								{
									Data.SourcePaintLayerIndex[WeightmapChannelIndex] = LayerInfoSettingsIndex + 1; // due to visibility layer that is at 0
									break;
								}
							}
						}

						break;
					}
				}
			}
		}

		OutComponentData.Add(Data);
	}
}

void ALandscape::ReallocateLayersWeightmaps(FUpdateLayersContentContext& InUpdateLayersContentContext, const TArray<ULandscapeLayerInfoObject*>& InBrushRequiredAllocations, 
	const TMap<ULandscapeComponent*, TArray<ULandscapeLayerInfoObject*>>* InPerComponentAllocations, bool bInRestrictTextureSharingToTheseComponents)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(LandscapeLayers_ReallocateLayersWeightmaps);

	ULandscapeInfo* Info = GetLandscapeInfo();

	if (Info == nullptr)
	{
		return;
	}

	// Clear allocation data
	for (ULandscapeComponent* Component : InUpdateLayersContentContext.LandscapeComponentsWeightmapsToResolve)
	{
		TArray<FWeightmapLayerAllocationInfo>& BaseLayerAllocations = Component->GetWeightmapLayerAllocations();
		for (FWeightmapLayerAllocationInfo& BaseWeightmapAllocation : BaseLayerAllocations)
		{
			BaseWeightmapAllocation.Free();
		}

		TArray<TObjectPtr<ULandscapeWeightmapUsage>>& WeightmapTexturesUsage = Component->GetWeightmapTexturesUsage();
		for (int32 i = 0; i < WeightmapTexturesUsage.Num(); ++i)
		{
			ULandscapeWeightmapUsage* Usage = WeightmapTexturesUsage[i];
			check(Usage != nullptr);

			Usage->ClearUsage(Component);
		}
	}

	// Build a map of all the allocation per components
	TMap<ULandscapeComponent*, TArray<ULandscapeLayerInfoObject*>> LayerAllocsPerComponent;
	// If the job of building the per-component allocations has already been done, just use them : 
	if (InPerComponentAllocations != nullptr)
	{ 
		LayerAllocsPerComponent = *InPerComponentAllocations;
	}
	else
	{
	for (FLandscapeLayer& Layer : LandscapeEditLayers)
	{
		for (ULandscapeComponent* Component : InUpdateLayersContentContext.LandscapeComponentsWeightmapsToResolve)
		{
			TArray<ULandscapeLayerInfoObject*>* ComponentLayerAlloc = LayerAllocsPerComponent.Find(Component);
			if (ComponentLayerAlloc == nullptr)
			{
				TArray<ULandscapeLayerInfoObject*> NewLayerAllocs;
				ComponentLayerAlloc = &LayerAllocsPerComponent.Add(Component, NewLayerAllocs);
			}

			// No need for an allocation if the edit layer is invisible : 
			if (Layer.bVisible)
			{
				if (FLandscapeLayerComponentData* LayerComponentData = Component->GetLayerData(Layer.Guid))
				{
				for (const FWeightmapLayerAllocationInfo& LayerWeightmapAllocation : LayerComponentData->WeightmapData.LayerAllocations)
				{
					if (LayerWeightmapAllocation.LayerInfo != nullptr)
					{
						ComponentLayerAlloc->AddUnique(LayerWeightmapAllocation.LayerInfo);
					}
				}
			}
			}

			// Add the brush alloc also (only if !InMergeParams.bSkipBrush, but InBrushRequiredAllocations should be empty already if InMergeParams.bSkipBrush is true) :
			for (ULandscapeLayerInfoObject* BrushLayerInfo : InBrushRequiredAllocations)
			{
				if (BrushLayerInfo != nullptr)
				{
					ComponentLayerAlloc->AddUnique(BrushLayerInfo);
				}
			}
		}
	}
	}

	// Trim the components that don't need weightmaps anymore (e.g. all edit layers are made invisible : there were some components LandscapeComponentsWeightmapsToResolve but there aren't anymore now):
	InUpdateLayersContentContext.LandscapeComponentsWeightmapsToResolve.RemoveAll([&LayerAllocsPerComponent](ULandscapeComponent* Component)
	{
		TArray<ULandscapeLayerInfoObject*>* ComponentLayerAlloc = LayerAllocsPerComponent.Find(Component);
		check(ComponentLayerAlloc != nullptr);
		return ComponentLayerAlloc->IsEmpty();
	});

	// Determine if the Final layer need to add/remove some alloc
	for (auto& ItPair : LayerAllocsPerComponent)
	{
		ULandscapeComponent* Component = ItPair.Key;
		TArray<ULandscapeLayerInfoObject*>& ComponentLayerAlloc = ItPair.Value;
		TArray<FWeightmapLayerAllocationInfo>& ComponentBaseLayerAlloc = Component->GetWeightmapLayerAllocations();

		// Deal with the one that need removal
		for (int32 i = ComponentBaseLayerAlloc.Num() - 1; i >= 0; --i)
		{
			const FWeightmapLayerAllocationInfo& Alloc = ComponentBaseLayerAlloc[i];

			if (!ComponentLayerAlloc.Contains(Alloc.LayerInfo))
			{
				ComponentBaseLayerAlloc.RemoveAt(i);
			}
		}

		// Then add the new one
		for (ULandscapeLayerInfoObject* LayerAlloc : ComponentLayerAlloc)
		{
			const bool AllocExist = ComponentBaseLayerAlloc.ContainsByPredicate([&LayerAlloc](const FWeightmapLayerAllocationInfo& BaseLayerAlloc) { return (LayerAlloc == BaseLayerAlloc.LayerInfo); });

			if (!AllocExist)
			{
				ComponentBaseLayerAlloc.Add(FWeightmapLayerAllocationInfo(LayerAlloc));
			}
		}
	}

	// Realloc the weightmap so it will create proper texture (if needed) and will set the allocations information
	TSet<UTexture*> NewCreatedTextures;
	for (ULandscapeComponent* Component : InUpdateLayersContentContext.LandscapeComponentsWeightmapsToResolve)
	{
		// If requested, don't allow the component to use textures that have a weightmap in another component than those provided when reallocating weightmaps :
		TSet<ULandscapeComponent*> RestrictSharingToComponents;
		if (bInRestrictTextureSharingToTheseComponents)
		{
			LayerAllocsPerComponent.GetKeys(RestrictSharingToComponents);
		}
		NewCreatedTextures.Append(Component->ReallocateWeightmaps(/*DataInterface = */nullptr, /*InEditLayerGuid = */FGuid(), /*bInSaveToTransactionBuffer = */false, /*bool bInForceReallocate = */false, 
			/*InTargetProxy = */nullptr, /*InRestrictSharingToComponents = */bInRestrictTextureSharingToTheseComponents ? &RestrictSharingToComponents : nullptr));
	}

	// TODO: correctly only recreate what is required instead of everything..
	//GDisableAutomaticTextureMaterialUpdateDependencies = true;

	FTextureCompilingManager::Get().FinishCompilation(NewCreatedTextures.Array());
	FLandscapeTextureStreamingManager* TextureStreamingManager = GetWorld()->GetSubsystem<ULandscapeSubsystem>()->GetTextureStreamingManager();
	for (UTexture* Texture : NewCreatedTextures)
	{
		TextureStreamingManager->RequestTextureFullyStreamedInForever(Texture, /* bWaitForStreaming= */ true);
	}

	//GDisableAutomaticTextureMaterialUpdateDependencies = false;

	// Clean-up unused weightmap CPUReadback resources
	Info->ForEachLandscapeProxy([](ALandscapeProxy* Proxy)
	{
		TArray<UTexture2D*, TInlineAllocator<64>> EntriesToRemoveFromMap;
		for (auto& Pair : Proxy->WeightmapsCPUReadback)
		{
			UTexture2D* WeightmapTextureKey = Pair.Key;
			bool IsTextureReferenced = false;
			for (ULandscapeComponent* Component : Proxy->LandscapeComponents)
			{
				for (UTexture2D* WeightmapTexture : Component->GetWeightmapTextures(false))
				{
					if (WeightmapTexture == WeightmapTextureKey)
					{
						IsTextureReferenced = true;
						break;
					}
				}
			}
			if (!IsTextureReferenced)
			{
				EntriesToRemoveFromMap.Add(WeightmapTextureKey);
			}
		}

		if (EntriesToRemoveFromMap.Num())
		{
			for (UTexture2D* OldWeightmapTexture : EntriesToRemoveFromMap)
			{
				if (FLandscapeEditLayerReadback** CPUReadbackToDelete = Proxy->WeightmapsCPUReadback.Find(OldWeightmapTexture))
				{
					check(*CPUReadbackToDelete);
					delete* CPUReadbackToDelete;
					Proxy->WeightmapsCPUReadback.Remove(OldWeightmapTexture);
				}
			}
		}

		return true;
	});

	ValidateProxyLayersWeightmapUsage();

	InUpdateLayersContentContext.Refresh(FUpdateLayersContentContext::ERefreshFlags::RefreshWeightmapInfos | FUpdateLayersContentContext::ERefreshFlags::RefreshMapHelper);
}

void ALandscape::InitializeLayersWeightmapResources()
{
	ULandscapeInfo* Info = GetLandscapeInfo();

	if (Info == nullptr)
	{
		return;
	}

	// Destroy existing resource
	TArray<FTextureResource*> ResourceToDestroy;
	ResourceToDestroy.Add(CombinedLayersWeightmapAllMaterialLayersResource);
	ResourceToDestroy.Add(CurrentLayersWeightmapAllMaterialLayersResource);
	ResourceToDestroy.Add(WeightmapScratchExtractLayerTextureResource);
	ResourceToDestroy.Add(WeightmapScratchPackLayerTextureResource);

	for (FTextureResource* Resource : ResourceToDestroy)
	{
		if (Resource != nullptr)
		{
			ENQUEUE_RENDER_COMMAND(LandscapeLayers_Cmd_ReleaseResources)(
				[Resource](FRHICommandList& RHICmdList)
			{
				TRACE_CPUPROFILER_EVENT_SCOPE(LandscapeLayers_RT_ReleaseResources);
				Resource->ReleaseResource();
				delete Resource;
			});
		}
	}

	// Create resources

	int32 LayerCount = Info->Layers.Num() + 1; // due to visibility being stored at 0

	// Use the 1st one to compute the resource as they are all the same anyway
	UTextureRenderTarget2D* FirstWeightmapRT = WeightmapRTList[(int32)EWeightmapRTType::WeightmapRT_Scratch1];

	CombinedLayersWeightmapAllMaterialLayersResource = new FLandscapeTexture2DArrayResource(FirstWeightmapRT->SizeX, FirstWeightmapRT->SizeY, LayerCount, PF_G8, 1, /*bInNeedUAVs = */ true, /*bInNeedSRV = */ false);
	BeginInitResource(CombinedLayersWeightmapAllMaterialLayersResource);

	CurrentLayersWeightmapAllMaterialLayersResource = new FLandscapeTexture2DArrayResource(FirstWeightmapRT->SizeX, FirstWeightmapRT->SizeY, LayerCount, PF_G8, 1, /*bInNeedUAVs = */ true, /*bInNeedSRV = */ false);
	BeginInitResource(CurrentLayersWeightmapAllMaterialLayersResource);

	WeightmapScratchExtractLayerTextureResource = new FLandscapeTexture2DResource(FirstWeightmapRT->SizeX, FirstWeightmapRT->SizeY, PF_B8G8R8A8, 1, /*bInNeedUAVs = */ false, /*bInNeedSRV = */ false);
	BeginInitResource(WeightmapScratchExtractLayerTextureResource);

	int32 MipCount = 0;

	for (int32 MipRTIndex = (int32)EWeightmapRTType::WeightmapRT_Mip0; MipRTIndex < (int32)EWeightmapRTType::WeightmapRT_Count; ++MipRTIndex)
	{
		if (WeightmapRTList[MipRTIndex] != nullptr)
		{
			++MipCount;
		}
	}

	// Format for UAV can't be PF_B8G8R8A8 on Windows 7 so use PF_R8G8B8A8
	// We make the final copy out of this to a PF_R8G8B8A8 target with CopyTexturePS() instead of CopyLayersTexture() because a pixel shader will automatically handle the channel swizzling (where a RHICopyTexture won't)
	WeightmapScratchPackLayerTextureResource = new FLandscapeTexture2DResource(FirstWeightmapRT->SizeX, FirstWeightmapRT->SizeY, PF_R8G8B8A8, MipCount, /*bInNeedUAVs = */ true, /*bInNeedSRV = */ false);
	BeginInitResource(WeightmapScratchPackLayerTextureResource);
}

// Little struct that holds information common to PerformLayersWeightmapsLocalMerge, PerformLayersWeightmapsBatchedMerge and PerformLayersWeightmapsGlobalMerge
struct FEditLayersWeightmapMergeParams
{
	int32 WeightmapUpdateModes;
	bool bForceRender;
	bool bSkipBrush;
};

// Render-thread version of the data / functions we need for the local merge of edit layers : 
namespace EditLayersWeightmapLocalMerge_RenderThread
{
	using namespace UE::Landscape::EditLayers;

	struct FEditLayerInfo
	{
		int32 SourceWeightmapTextureIndex = INDEX_NONE; // The index in VisibleEditLayerWeightmapTextures of the texture to read from for this layer
		int32 SourceWeightmapTextureChannel = INDEX_NONE; // The channel of the texture to read from for this layer
		EWeightmapBlendMode BlendMode = EWeightmapBlendMode::Num; // See EEditLayerWeightmapBlendMode
		float Alpha = 1.0f; // Alpha value to be used in the blend
	};

	struct FComponentPaintLayerRenderInfo
	{
		// Name of the paint layer for debug purposes :
		FString Name;
		// Describes how to access each visible edit layer's weightmap and how to blend it in the final weightmap for this paint layer :
		TArray<FEditLayerInfo> VisibleEditLayerInfos;
		// Global index of this paint layer in the paint layer infos array : 
		int32 PaintLayerInfoIndex = INDEX_NONE;
	};

	struct FComponentRenderInfo
	{
		// Name of the component for debug purposes :
		FString Name;
		// The information needed to render each of the component's paint layers :
		TArray<FComponentPaintLayerRenderInfo> PaintLayerRenderInfos;
		// The unique textures referenced by this component's visible edit layer's weightmaps for all paint layers :
		TArray<FTexture2DResource*> VisibleEditLayerWeightmapTextures;
	};

	// For a given FTextureResolveBatchInfo, allows to identify a FComponentRenderInfo/FComponentPaintLayerRenderInfo pair (useful when recombining the weightmaps into the final -packed- weightmap)
	struct FComponentAndPaintLayerRenderInfoIdentifier
	{
		FComponentAndPaintLayerRenderInfoIdentifier() = default;
		FComponentAndPaintLayerRenderInfoIdentifier(int32 InComponentIndex, int32 InPaintLayerIndex)
			: ComponentIndex(InComponentIndex)
			, PaintLayerIndex(InPaintLayerIndex)
		{}
		bool IsValid() const { return (ComponentIndex != INDEX_NONE) && (PaintLayerIndex != INDEX_NONE); }

		bool operator == (const FComponentAndPaintLayerRenderInfoIdentifier& InOther) const { return (InOther.ComponentIndex == ComponentIndex) && (InOther.PaintLayerIndex == PaintLayerIndex); }
		bool operator != (const FComponentAndPaintLayerRenderInfoIdentifier& InOther) const { return !(*this == InOther); }

		int32 ComponentIndex = INDEX_NONE; // Index of a FComponentRenderInfo in ComponentToRenderInfos
		int32 PaintLayerIndex = INDEX_NONE; // Index of a FComponentPaintLayerRenderInfo in FComponentRenderInfo::PaintLayerRenderInfos
	};

	struct FTextureResolveInfo
	{
		FTextureResolveInfo(int32 InNumComponentsToRender)
			: ComponentToRenderInfoBitIndices(false, InNumComponentsToRender)
		{}

		void ValidatePerChannelSourceInfo(int32 InChannelIndex, const FComponentAndPaintLayerRenderInfoIdentifier& InComponentAndPaintLayerIdentifier)
		{
			for (int32 Index = 0; Index < 4; ++Index)
			{
				if (Index == InChannelIndex)
				{
					// Channel shouldn't be already assigned
					check(!PerChannelSourceWeightmapsIdentifiers[Index].IsValid());
				}
				else
				{
					// There should be no duplicates in channels : that would indicate that there are 2 identical component/layer info pair for 2 different texture channels
					check(PerChannelSourceWeightmapsIdentifiers[Index] != InComponentAndPaintLayerIdentifier);
				}
			}
		}

		void SetPerChannelSourceInfo(int32 InChannelIndex, const FComponentAndPaintLayerRenderInfoIdentifier& InComponentAndPaintLayerIdentifier)
		{
			ValidatePerChannelSourceInfo(InChannelIndex, InComponentAndPaintLayerIdentifier);
			PerChannelSourceWeightmapsIdentifiers[InChannelIndex] = InComponentAndPaintLayerIdentifier;
			ComponentToRenderInfoBitIndices[InComponentAndPaintLayerIdentifier.ComponentIndex] = true;
		}

	public:
		// Index of this texture in TextureToResolveInfos :
		int32 TextureToResolveInfoIndex = INDEX_NONE;
		// Texture that was created or updated that needs resolving : 
		FTexture2DResource* Texture = nullptr;
		// List of the 4 identifiers (one per weightmap channel) of a component/paint layer association in a given FTextureResolveBatchInfo that will be used 
		//  to recombine the individual weightmaps into the final -packed- one
		TStaticArray<FComponentAndPaintLayerRenderInfoIdentifier, 4> PerChannelSourceWeightmapsIdentifiers;
		// List of(up to) 4 unique component render info indices that are needed for reconstructing the 4 channels of this texture (index into ComponentToRenderInfos) :
		//  It's a bit array (1 bit per component to render info) to vastly optimize the division of texture resolve infos into batches, which is a O(N^2) operation : 
		TBitArray<> ComponentToRenderInfoBitIndices;
		// CPU readback utility to bring back the result on the CPU : 
		FLandscapeEditLayerReadback* CPUReadback = nullptr;
	};

	// Because of weightmaps being shared between one component and another (within a given landcape proxy), we have to group the components to render into batches
	//  where we'll render all of the paint layers into individual, 1-channel, scratch textures, which we'll then be able to re-assemble into the final, packed, weightmaps :
	struct FTextureResolveBatchInfo
	{
		FTextureResolveBatchInfo(int32 InNumComponentsToRender, int32 InBatchIndex)
			: BatchIndex(InBatchIndex)
			, ComponentToRenderInfoBitIndices(false, InNumComponentsToRender)
		{}

		void AddTexture(const FTextureResolveInfo& InTextureResolveInfo)
		{
			check(!TextureToResolveInfoIndices.Contains(InTextureResolveInfo.TextureToResolveInfoIndex));
			TextureToResolveInfoIndices.Add(InTextureResolveInfo.TextureToResolveInfoIndex);
			// Remember all the unique components that this texture needs for resolving:
			ComponentToRenderInfoBitIndices.CombineWithBitwiseOR(InTextureResolveInfo.ComponentToRenderInfoBitIndices, EBitwiseOperatorFlags::MinSize);
		}

	public:
		// Index of this batch in TextureResolveBatchInfos
		int32 BatchIndex = INDEX_NONE;

		// Indices (in ComponentToRenderInfos) of the components whose weightmaps we need to render within this batch in order to produce (and then resolve) the textures in TextureToResolveInfos:
		//  It's a bit array (1 bit per component to render info) to vastly optimize the division of texture resolve infos into batches, which is a O(N^2) operation : 
		TBitArray<> ComponentToRenderInfoBitIndices;
		// Indices (TextureToResolveInfos) of textures that need to be resolved / read back on the CPU :
		TArray<int32> TextureToResolveInfoIndices;
	};

	// Description of the entire merge pass :
	struct FMergeInfo
	{
		bool NeedsMerge() const
		{
			// If no edit layer or if no paint layer present on any edit layer, we've got nothing to do :
			bool bNeedsMerge = (MaxNumEditLayersTexturesToMerge > 0) && (MaxNumWeightmapArraysPerResolveTextureBatch > 0);
			check(!bNeedsMerge || !PaintLayerInfos.IsEmpty()); // If we need merging, we must have at least one paint layer
			return bNeedsMerge;
		}

	public:
		// Maximum number of visible edit layers that have to be merged for a single FComponentRenderInfo : 
		int32 MaxNumEditLayersTexturesToMerge = 0;

		// Maximum number of weightmap arrays that is needed for a given FTextureResolveBatchInfo (1 per FComponentRenderInfo in the batch) :
		int32 MaxNumWeightmapArraysPerResolveTextureBatch = 0;

		// Number of vertices per component : 
		FIntPoint ComponentSizeVerts = 0;

		// Number of sub sections for this landscape :
		uint32 NumSubsections = 1;

		// Number of mips for the weightmaps of this landscape
		int32 NumMips = 0;

		// List of batches of FTextureResolveInfo that needs to be resolved in the same pass. This allows massive saves on transient resources on large landscapes because those can be re-cycled from one pass to another :
		TArray<FTextureResolveBatchInfo> TextureResolveBatchInfos;

		// List of infos for each component that needs its paint layers to be rendered in order to be resolved :
		TArray<FComponentRenderInfo> ComponentToRenderInfos;

		// List of infos for each texture that needs to be resolved :
		TArray<FTextureResolveInfo> TextureToResolveInfos;

		// List of infos for each individual paint layer involved in the merge operation (including the visibility layer)
		TArray<FWeightmapPaintLayerInfo> PaintLayerInfos;

		// Not truly render-thread data because it references UTextures but it's just because FLandscapeEditLayerReadback were historically game-thread initiated so for as long as we'll use those for readback, we need to store this here : 
		TArray<FLandscapeLayersCopyReadbackTextureParams> DeferredCopyReadbackTextures;
	};

	// Render graph intermediate resources :
	struct FRDGResources
	{
		// Contains info about each individual paint layer 
		FRDGBufferRef PaintLayerInfosBuffer;
		FRDGBufferSRVRef PaintLayerInfosBufferSRV;

		// Texture array that can be reused from one component / one paint layer to another and that contains the list of all edit layers textures that need merging in a given pass :
		FRDGTextureRef EditLayersWeightmapsTextureArray;
		FRDGTextureSRVRef EditLayersWeightmapsTextureArraySRV;

		// List of temporary scratch texture arrays that store the output for all (edit layer-merged) active paint layer (one per landscape component) until they can be packed onto the final weightmap textures. 
		//  Can be reused from one batch to another :
		TArray<FRDGTextureRef> ScratchPaintLayerWeightmapTextureArrays;
		TArray<FRDGTextureSRVRef> ScratchPaintLayerWeightmapTextureArraysSRV;

		// Single scratch texture for the weightmap finalize operation (since we cannot directly write to the final weightmaps because they were not created with TexCreate_RenderTargetable) :
		FRDGTextureRef ScratchFinalWeightmapTexture;
		TArray<FRDGTextureSRVRef> ScratchFinalWeightmapTextureMipsSRV; // One SRV per mip level

		// Single structured buffer that will contain all possible FLandscapeEditLayerWeightmapMergeInfo we might need during the entire merge operation (this is to avoid
		//  too many individual buffer allocations/uploads when many components need to be merged : CPU optimization) :
		FRDGBufferRef EditLayerMergeInfosBuffer;
		FRDGBufferSRVRef EditLayerMergeInfosBufferSRV;
		int32 CurrentEditLayerMergeInfosBufferIndex = 0;

		// Single structured buffer that will contain all possible paint layer info indices we might need during the entire merge operation (this is to avoid
		//  too many individual buffer allocations/uploads when many components need to be merged : CPU optimization) :
		FRDGBufferRef PaintLayerInfoIndicesBuffer;
		FRDGBufferSRVRef PaintLayerInfoIndicesBufferSRV;
		int32 CurrentPaintLayerInfoIndicesBufferIndex = 0;

		// Dummy buffers :
		FRDGTextureSRVRef BlackDummyArraySRV;
	};

	TArray<FLandscapeEditLayerWeightmapMergeInfo> PrepareEditLayerWeightmapMergeInfosBufferData(const FMergeInfo& InLocalMergeInfo)
	{
		TArray<FLandscapeEditLayerWeightmapMergeInfo> EditLayerMergeInfos;

		// Batch by batch
		for (const FTextureResolveBatchInfo& TextureResolveBatchInfo : InLocalMergeInfo.TextureResolveBatchInfos)
		{
			// Component by component
			for (TConstSetBitIterator<> BitIt(TextureResolveBatchInfo.ComponentToRenderInfoBitIndices); BitIt; ++BitIt)
			{
				int32 ComponentRenderInfoIndex = BitIt.GetIndex();

				// Paint layer by paint layer
				for (const FComponentPaintLayerRenderInfo& ComponentPaintLayerRenderInfo : InLocalMergeInfo.ComponentToRenderInfos[ComponentRenderInfoIndex].PaintLayerRenderInfos)
				{
					// Update the edit layers merge info big buffer : 
					for (const FEditLayerInfo& EditLayerInfo : ComponentPaintLayerRenderInfo.VisibleEditLayerInfos)
					{
						FLandscapeEditLayerWeightmapMergeInfo& EditLayerMergeInfo = EditLayerMergeInfos.Emplace_GetRef();
						EditLayerMergeInfo.SourceWeightmapTextureIndex = EditLayerInfo.SourceWeightmapTextureIndex;
						EditLayerMergeInfo.SourceWeightmapTextureChannel = EditLayerInfo.SourceWeightmapTextureChannel;
						EditLayerMergeInfo.BlendMode = EditLayerInfo.BlendMode;
						EditLayerMergeInfo.Alpha = EditLayerInfo.Alpha;
					}
				}
			}
		}

		return EditLayerMergeInfos;
	}

	TArray<uint32> PreparePaintLayerInfoIndicesBufferData(const FMergeInfo& InLocalMergeInfo)
	{
		TArray<uint32> PaintLayerInfoIndices;

		// Batch by batch
		for (const FTextureResolveBatchInfo& TextureResolveBatchInfo : InLocalMergeInfo.TextureResolveBatchInfos)
		{
			// Texture by texture
			for (int32 TextureResolveInfoIndex : TextureResolveBatchInfo.TextureToResolveInfoIndices)
			{
				// Output channel by output channel
				const FTextureResolveInfo& TextureResolveInfo = InLocalMergeInfo.TextureToResolveInfos[TextureResolveInfoIndex];
				for (int32 ChannelIndex = 0; ChannelIndex < 4; ++ChannelIndex)
				{
					const FComponentAndPaintLayerRenderInfoIdentifier& ComponentAndPaintLayerRenderInfoIdentifier = TextureResolveInfo.PerChannelSourceWeightmapsIdentifiers[ChannelIndex];
					if (ComponentAndPaintLayerRenderInfoIdentifier.IsValid())
					{
						check(InLocalMergeInfo.ComponentToRenderInfos.IsValidIndex(ComponentAndPaintLayerRenderInfoIdentifier.ComponentIndex)); // this identifier must point to a valid render info in the FTextureResolveBatchInfo
						const FComponentRenderInfo& ComponentRenderInfo = InLocalMergeInfo.ComponentToRenderInfos[ComponentAndPaintLayerRenderInfoIdentifier.ComponentIndex];

						check(ComponentRenderInfo.PaintLayerRenderInfos.IsValidIndex(ComponentAndPaintLayerRenderInfoIdentifier.PaintLayerIndex)); // this identifier must point to a valid paint layer render info in the FComponentRenderInfo

						// Update the PaintLayerInfoIndices big buffer : 
						for (const FComponentPaintLayerRenderInfo& PaintLayerInfo : ComponentRenderInfo.PaintLayerRenderInfos)
						{
							check(PaintLayerInfo.PaintLayerInfoIndex < InLocalMergeInfo.PaintLayerInfos.Num());
							PaintLayerInfoIndices.Add(PaintLayerInfo.PaintLayerInfoIndex);
						}
					}
				}
			}
		}

		return PaintLayerInfoIndices;
	}

	void PrepareLayersWeightmapsLocalMergeRDGResources(const FMergeInfo& InLocalMergeInfo, FRDGBuilder& GraphBuilder, FRDGResources& OutResources)
	{
		{
			// Upload paint layer infos buffer once and for all since it's unchanged from one component to another : 
			OutResources.PaintLayerInfosBuffer = CreateStructuredBuffer(GraphBuilder, TEXT("LandscapePaintLayerInfosBuffer"), InLocalMergeInfo.PaintLayerInfos);
			OutResources.PaintLayerInfosBufferSRV = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(OutResources.PaintLayerInfosBuffer));
		}

		{
			// Allocate a texture array that can contain all edit layers textures to merge for any given component to render (this will be reused from one component to another) :
			int32 SizeZ = InLocalMergeInfo.MaxNumEditLayersTexturesToMerge;
			check(SizeZ > 0);

			// TODO [chris.tchou] this texture does not have to be a render target, but RDG does not support transient shader-resource-only/copy-populated textures yet -- see issue https://jira.it.epicgames.com/browse/UE-162198
			FRDGTextureDesc Desc = FRDGTextureDesc::Create2DArray(InLocalMergeInfo.ComponentSizeVerts, PF_B8G8R8A8, FClearValueBinding::None, TexCreate_ShaderResource | TexCreate_RenderTargetable, static_cast<uint16>(SizeZ), /*InNumMips = */1, /*InNumSamples = */1);
			OutResources.EditLayersWeightmapsTextureArray = GraphBuilder.CreateTexture(Desc, TEXT("LandscapeEditLayersWeightmapsTextureArray"));
			OutResources.EditLayersWeightmapsTextureArraySRV = GraphBuilder.CreateSRV(FRDGTextureSRVDesc::Create(OutResources.EditLayersWeightmapsTextureArray));
		}

		{
			// Allocate as many texture arrays as needed for a given texture resolve batch (this will be reused from one batch to another) :
			OutResources.ScratchPaintLayerWeightmapTextureArrays.Reserve(InLocalMergeInfo.MaxNumWeightmapArraysPerResolveTextureBatch);
			OutResources.ScratchPaintLayerWeightmapTextureArraysSRV.Reserve(InLocalMergeInfo.MaxNumWeightmapArraysPerResolveTextureBatch);
			// Each texture array (reusable from batch to batch) will contain at most a number of slices equals to the total number of active paint layers (including the visibility layer) : 
			int32 SizeZ = InLocalMergeInfo.PaintLayerInfos.Num();
			check(SizeZ > 0);
			for (int32 Index = 0; Index < InLocalMergeInfo.MaxNumWeightmapArraysPerResolveTextureBatch; ++Index)
			{
				FRDGTextureDesc Desc = FRDGTextureDesc::Create2DArray(InLocalMergeInfo.ComponentSizeVerts, PF_G8, FClearValueBinding::None, TexCreate_ShaderResource | TexCreate_RenderTargetable | TexCreate_TargetArraySlicesIndependently, static_cast<uint16>(SizeZ), /*InNumMips = */1, /*InNumSamples = */1);
				FRDGTextureRef& TextureRef = OutResources.ScratchPaintLayerWeightmapTextureArrays.Add_GetRef(GraphBuilder.CreateTexture(Desc, TEXT("LandscapeEditLayersScratchPaintLayerWeightmapTextureArray")));
				OutResources.ScratchPaintLayerWeightmapTextureArraysSRV.Add(GraphBuilder.CreateSRV(FRDGTextureSRVDesc::Create(TextureRef)));
			}
		}

		{
			// Allocate a single scratch texture will all of its mip for each individual texture we want to resolve :
			check(InLocalMergeInfo.NumMips > 0);
			FRDGTextureDesc Desc = FRDGTextureDesc::Create2D(InLocalMergeInfo.ComponentSizeVerts, PF_B8G8R8A8, FClearValueBinding::None, TexCreate_ShaderResource | TexCreate_RenderTargetable, static_cast<uint8>(InLocalMergeInfo.NumMips), /*InNumSamples = */1);
			OutResources.ScratchFinalWeightmapTexture = GraphBuilder.CreateTexture(Desc, TEXT("LandscapeEditLayersScratchFinalWeightmapTexture"));
			for (int32 MipLevel = 0; MipLevel < InLocalMergeInfo.NumMips; ++MipLevel)
			{
				OutResources.ScratchFinalWeightmapTextureMipsSRV.Add(GraphBuilder.CreateSRV(FRDGTextureSRVDesc::CreateForMipLevel(OutResources.ScratchFinalWeightmapTexture, MipLevel)));
			}
		}

		{
			// Allocate a single structured buffer that will contain all possible FLandscapeEditLayerWeightmapMergeInfo we might need during the entire merge operation. 
			//  Although CreateStructuredBuffer can be given a callback to provide its initial data, we need to build the source data array upfront, since it's not compatible with RDG immediate mode:
			OutResources.EditLayerMergeInfosBuffer = CreateStructuredBuffer(GraphBuilder, TEXT("LandscapeEditLayersMergeInfosBuffer"), PrepareEditLayerWeightmapMergeInfosBufferData(InLocalMergeInfo));
			OutResources.EditLayerMergeInfosBufferSRV = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(OutResources.EditLayerMergeInfosBuffer));
		}

		{
			// Allocate a single structured buffer that will contain all possible paint layer info indices we might need during the entire merge operation.
			//  Although CreateStructuredBuffer can be given a callback to provide its initial data, we need to build the source data array upfront, since it's not compatible with RDG immediate mode:
			OutResources.PaintLayerInfoIndicesBuffer = CreateStructuredBuffer(GraphBuilder, TEXT("LandscapeEditLayersPaintLayerInfoIndicesBuffer"), PreparePaintLayerInfoIndicesBufferData(InLocalMergeInfo));
			OutResources.PaintLayerInfoIndicesBufferSRV = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(OutResources.PaintLayerInfoIndicesBuffer));
		}

		{
			// Dummy buffers for avoiding missing shader bindings
			OutResources.BlackDummyArraySRV = GraphBuilder.CreateSRV(FRDGTextureSRVDesc::Create(GSystemTextures.GetBlackArrayDummy(GraphBuilder)));
		}
	}

	// Gather all textures we will want to write into or read from in the render graph and output them in OutTrackedTextures:
	void GatherLayersWeightmapsLocalMergeRDGTextures(const FMergeInfo& InLocalMergeInfo, TMap<FTexture2DResource*, FLandscapeRDGTrackedTexture>& OutTrackedTextures)
	{
		// Gather all source weightmaps :
		for (const FComponentRenderInfo& ComponentRenderInfo : InLocalMergeInfo.ComponentToRenderInfos)
		{
			for (FTexture2DResource* VisibleEditLayersWeightmapTexture : ComponentRenderInfo.VisibleEditLayerWeightmapTextures)
			{
				check(VisibleEditLayersWeightmapTexture != nullptr);
				FLandscapeRDGTrackedTexture* TrackedTexture = OutTrackedTextures.Find(VisibleEditLayersWeightmapTexture);
				if (TrackedTexture == nullptr)
				{
					TrackedTexture = &(OutTrackedTextures.Add(VisibleEditLayersWeightmapTexture, FLandscapeRDGTrackedTexture(VisibleEditLayersWeightmapTexture)));
				}
				TrackedTexture->bNeedsSRV = true;
			}
		}

		// Gather all destination weightmaps :
		for (const FTextureResolveInfo& TextureResolveInfo : InLocalMergeInfo.TextureToResolveInfos)
		{
			FLandscapeRDGTrackedTexture* TrackedTexture = OutTrackedTextures.Find(TextureResolveInfo.Texture);
			if (ensure(TrackedTexture == nullptr)) // Resolved textures should only be registered once
			{
				TrackedTexture = &(OutTrackedTextures.Add(TextureResolveInfo.Texture, FLandscapeRDGTrackedTexture(TextureResolveInfo.Texture)));
			}

			TrackedTexture->bNeedsSRV = true;
		}
	}

	void MergeEditLayersWeightmapsForBatch(const FTextureResolveBatchInfo& InTextureResolveBatchInfo, const FMergeInfo& InLocalMergeInfo,
		const TMap<FTexture2DResource*, FLandscapeRDGTrackedTexture>& InTrackedTextures, FRDGBuilder& GraphBuilder, FRDGResources& RDGResources)
	{
		RDG_EVENT_SCOPE(GraphBuilder, "Merge paint layers on %d components for batch %d", InTextureResolveBatchInfo.ComponentToRenderInfoBitIndices.CountSetBits(), InTextureResolveBatchInfo.BatchIndex);

		// For each component in the batch, perform the edit layers merge and write the resulting scratch weightmap :
		int32 IndexInBatch = 0;
		for (TConstSetBitIterator<> BitIt(InTextureResolveBatchInfo.ComponentToRenderInfoBitIndices); BitIt; ++BitIt, ++IndexInBatch)
		{
			int32 ComponentRenderInfoIndex = BitIt.GetIndex();

			const FComponentRenderInfo& ComponentRenderInfo = InLocalMergeInfo.ComponentToRenderInfos[ComponentRenderInfoIndex];
			RDG_EVENT_SCOPE(GraphBuilder, "Component %s", *ComponentRenderInfo.Name);

			// Prepare a texture array of texture array that will contain all the input textures we'll need for this component, regardless of the paint layer
			//  (done once per component since various paint layers could share the same texture) :
			{
				int32 NumTextures = ComponentRenderInfo.VisibleEditLayerWeightmapTextures.Num();
				for (int32 TextureIndex = 0; TextureIndex < NumTextures; ++TextureIndex)
				{
					const FTexture2DResource* PackedWeightmap = ComponentRenderInfo.VisibleEditLayerWeightmapTextures[TextureIndex];

					const FLandscapeRDGTrackedTexture* TrackedTexture = InTrackedTextures.Find(PackedWeightmap);
					check(TrackedTexture != nullptr);

					// We need to copy the (portion of the) layer's texture to the texture array : 
					FRHICopyTextureInfo CopyTextureInfo;
					CopyTextureInfo.Size = FIntVector(InLocalMergeInfo.ComponentSizeVerts.X, InLocalMergeInfo.ComponentSizeVerts.Y, 0);
					CopyTextureInfo.DestSliceIndex = TextureIndex;

					AddCopyTexturePass(GraphBuilder, TrackedTexture->ExternalTextureRef, RDGResources.EditLayersWeightmapsTextureArray, CopyTextureInfo);
				}
			}

			// We should have a single output scratch texture array reserved for this component in RDGResources.ScratchPaintLayerWeightmapTextureArrays already :
			check(RDGResources.ScratchPaintLayerWeightmapTextureArrays.IsValidIndex(IndexInBatch));
			FRDGTextureRef ScratchTextureArrayRef = RDGResources.ScratchPaintLayerWeightmapTextureArrays[IndexInBatch];

			// Paint layer by paint layer, merge the weightmaps from all the corresponding edit layers onto the corresponding scratch texture using the MergeEditLayers PS : 
			int32 NumComponentPaintLayers = ComponentRenderInfo.PaintLayerRenderInfos.Num();
			for (int32 ComponentPaintLayerIndex = 0; ComponentPaintLayerIndex < NumComponentPaintLayers; ++ComponentPaintLayerIndex)
			{
				const FComponentPaintLayerRenderInfo& ComponentPaintLayerRenderInfo = ComponentRenderInfo.PaintLayerRenderInfos[ComponentPaintLayerIndex];
				RDG_EVENT_SCOPE(GraphBuilder, "Merge %d edit layers for paint layer %s", ComponentPaintLayerRenderInfo.VisibleEditLayerInfos.Num(), *ComponentPaintLayerRenderInfo.Name);

				FLandscapeLayersWeightmapsMergeEditLayersPS::FParameters* MergeEditLayersPSParams = GraphBuilder.AllocParameters<FLandscapeLayersWeightmapsMergeEditLayersPS::FParameters>();
				// We'll write to a single slice of the texture array for this component, since we're acting paint layer by paint layer here :
				MergeEditLayersPSParams->RenderTargets[0] = FRenderTargetBinding(ScratchTextureArrayRef, ERenderTargetLoadAction::ENoAction, /*InMipIndex = */0, /*InArraySlice = */static_cast<int16>(ComponentPaintLayerIndex));
				MergeEditLayersPSParams->InNumEditLayers = ComponentPaintLayerRenderInfo.VisibleEditLayerInfos.Num();
				MergeEditLayersPSParams->InPackedWeightmaps = RDGResources.EditLayersWeightmapsTextureArraySRV;
				MergeEditLayersPSParams->InEditLayersMergeInfos = RDGResources.EditLayerMergeInfosBufferSRV;

				// EditLayerMergeInfosBuffer is already uploaded but we need to tell the shader where we're currently at in that big buffer :
				MergeEditLayersPSParams->InStartIndexInEditLayersMergeInfos = RDGResources.CurrentEditLayerMergeInfosBufferIndex; 
				// Update CurrentEditLayerMergeInfosBufferIndex so that the next paint layer starts after : 
				RDGResources.CurrentEditLayerMergeInfosBufferIndex += ComponentPaintLayerRenderInfo.VisibleEditLayerInfos.Num();

				FLandscapeLayersWeightmapsMergeEditLayersPS::MergeEditLayers(GraphBuilder, MergeEditLayersPSParams);
			}
		}
	}

	void FinalizeSingleWeightmap(const FTextureResolveBatchInfo& InTextureResolveBatchInfo, const FMergeInfo& InLocalMergeInfo,
		const FTextureResolveInfo& InTextureResolveInfo, FRDGBuilder& GraphBuilder, FRDGResources& RDGResources)
	{
		RDG_EVENT_SCOPE(GraphBuilder, "Finalize Weightmap %s", *InTextureResolveInfo.Texture->GetTextureName().ToString());

		FLandscapeLayersWeightmapsFinalizeWeightmapPS::FParameters* FinalizeWeightmapPSParams = GraphBuilder.AllocParameters<FLandscapeLayersWeightmapsFinalizeWeightmapPS::FParameters>();
		FinalizeWeightmapPSParams->RenderTargets[0] = FRenderTargetBinding(RDGResources.ScratchFinalWeightmapTexture, ERenderTargetLoadAction::ENoAction);
		FinalizeWeightmapPSParams->InPerChannelPaintLayerIndexInWeightmaps = FUintVector4(ForceInitToZero);
		FinalizeWeightmapPSParams->InPerChannelStartPaintLayerIndex = FUintVector4(ForceInitToZero);
		FinalizeWeightmapPSParams->InPerChannelNumPaintLayers = FUintVector4(ForceInitToZero);
		FinalizeWeightmapPSParams->InPaintLayerInfoIndices = RDGResources.PaintLayerInfoIndicesBufferSRV;
		FinalizeWeightmapPSParams->InPaintLayerInfos = RDGResources.PaintLayerInfosBufferSRV;
		FinalizeWeightmapPSParams->InValidTextureChannelsMask = 0;

		check(InTextureResolveInfo.PerChannelSourceWeightmapsIdentifiers.Num() == 4);
		for (int32 ChannelIndex = 0; ChannelIndex < 4; ++ChannelIndex)
		{
			const FComponentAndPaintLayerRenderInfoIdentifier& ComponentAndPaintLayerRenderInfoIdentifier = InTextureResolveInfo.PerChannelSourceWeightmapsIdentifiers[ChannelIndex];
			if (ComponentAndPaintLayerRenderInfoIdentifier.IsValid())
			{
				// Indicate this channel will need to be processed :
				FinalizeWeightmapPSParams->InValidTextureChannelsMask |= (1 << ChannelIndex);

				check(InLocalMergeInfo.ComponentToRenderInfos.IsValidIndex(ComponentAndPaintLayerRenderInfoIdentifier.ComponentIndex)); // this identifier must point to a valid render info in the FTextureResolveBatchInfo
				const FComponentRenderInfo& ComponentRenderInfo = InLocalMergeInfo.ComponentToRenderInfos[ComponentAndPaintLayerRenderInfoIdentifier.ComponentIndex];

				check(ComponentRenderInfo.PaintLayerRenderInfos.IsValidIndex(ComponentAndPaintLayerRenderInfoIdentifier.PaintLayerIndex)); // this identifier must point to a valid paint layer render info in the FComponentRenderInfo

				// The paint layer to process on this texture channel : 
				FinalizeWeightmapPSParams->InPerChannelPaintLayerIndexInWeightmaps[ChannelIndex] = ComponentAndPaintLayerRenderInfoIdentifier.PaintLayerIndex;
				// The total number of paint layers for this component/paint layer (for weight blending in-between paint layers):
				FinalizeWeightmapPSParams->InPerChannelNumPaintLayers[ChannelIndex] = ComponentRenderInfo.PaintLayerRenderInfos.Num();
				// The index at which we'll find the first paint layer info index in the PaintLayerInfoIndices big buffer for this channel : use the index where we're currently at in that big buffer :
				FinalizeWeightmapPSParams->InPerChannelStartPaintLayerIndex[ChannelIndex] = RDGResources.CurrentPaintLayerInfoIndicesBufferIndex;
				// And update the big buffer current index so that the next channel starts at the right location in the big buffer : 
				RDGResources.CurrentPaintLayerInfoIndicesBufferIndex += ComponentRenderInfo.PaintLayerRenderInfos.Num();

				// We should have a single output scratch texture array reserved for this component in RDGResources.ScratchPaintLayerWeightmapTextureArrays already :
				check(InTextureResolveBatchInfo.ComponentToRenderInfoBitIndices[ComponentAndPaintLayerRenderInfoIdentifier.ComponentIndex]); // This component should have been rendered in that batch!
				int32 ScratchPaintLayerWeightmapTextureIndex = InTextureResolveBatchInfo.ComponentToRenderInfoBitIndices.CountSetBits(0, ComponentAndPaintLayerRenderInfoIdentifier.ComponentIndex);
				check(RDGResources.ScratchPaintLayerWeightmapTextureArrays.IsValidIndex(ScratchPaintLayerWeightmapTextureIndex));
				FinalizeWeightmapPSParams->InPerChannelPaintLayerWeightmaps[ChannelIndex] = RDGResources.ScratchPaintLayerWeightmapTextureArraysSRV[ScratchPaintLayerWeightmapTextureIndex];
			}
			else
			{
				FinalizeWeightmapPSParams->InPerChannelPaintLayerWeightmaps[ChannelIndex] = RDGResources.BlackDummyArraySRV;
			}
		}

		FLandscapeLayersWeightmapsFinalizeWeightmapPS::FinalizeWeightmap(GraphBuilder, FinalizeWeightmapPSParams);
	}

	void GenerateSingleWeightmapMips(const FTextureResolveInfo& InTextureResolveInfo, const FMergeInfo& InLocalMergeInfo,
		FRDGBuilder& GraphBuilder, FRDGResources& RDGResources)
	{
		RDG_EVENT_SCOPE(GraphBuilder, "Generate mips for Weightmap %s", *InTextureResolveInfo.Texture->GetTextureName().ToString());
		check(RDGResources.ScratchFinalWeightmapTextureMipsSRV.Num() == InLocalMergeInfo.NumMips);

		FIntPoint CurrentMipSize = InLocalMergeInfo.ComponentSizeVerts;
		for (int32 MipLevel = 1; MipLevel < InLocalMergeInfo.NumMips; ++MipLevel)
		{
			CurrentMipSize.X >>= 1;
			CurrentMipSize.Y >>= 1;

			// Read from scratch weightmap texture (mip N - 1) -> write to scratch weightmap texture (mip N) :
			FLandscapeLayersWeightmapsGenerateMipsPS::FParameters* GenerateMipsPSParams = GraphBuilder.AllocParameters<FLandscapeLayersWeightmapsGenerateMipsPS::FParameters>();
			GenerateMipsPSParams->RenderTargets[0] = FRenderTargetBinding(RDGResources.ScratchFinalWeightmapTexture, ERenderTargetLoadAction::ENoAction, static_cast<uint8>(MipLevel));
			GenerateMipsPSParams->InCurrentMipSize = FUintVector2(CurrentMipSize.X, CurrentMipSize.Y);
			GenerateMipsPSParams->InNumSubsections = InLocalMergeInfo.NumSubsections;
			GenerateMipsPSParams->InSourceWeightmap = RDGResources.ScratchFinalWeightmapTextureMipsSRV[MipLevel - 1];

			FLandscapeLayersWeightmapsGenerateMipsPS::GenerateSingleMip(GraphBuilder, GenerateMipsPSParams);
		}
	}

	void CopyScratchToSourceWeightmap(const FTextureResolveInfo& InTextureResolveInfo, const TMap<FTexture2DResource*, FLandscapeRDGTrackedTexture>& InTrackedTextures,
		FRDGBuilder& GraphBuilder, FRDGResources& RDGResources)
	{
		RDG_EVENT_SCOPE(GraphBuilder, "Copy scratch to Weightmap %s", *InTextureResolveInfo.Texture->GetTextureName().ToString());

		const FLandscapeRDGTrackedTexture* TrackedTexture = InTrackedTextures.Find(InTextureResolveInfo.Texture);
		check((TrackedTexture != nullptr) && (TrackedTexture->ExternalTextureRef != nullptr));

		FRHICopyTextureInfo CopyTextureInfo;
		// We want to copy all mips : 
		CopyTextureInfo.NumMips = TrackedTexture->ExternalTextureRef->Desc.NumMips;

		AddCopyTexturePass(GraphBuilder, RDGResources.ScratchFinalWeightmapTexture, TrackedTexture->ExternalTextureRef, CopyTextureInfo);
	}

	void FinalizeAndResolveWeightmapsForBatch(const FTextureResolveBatchInfo& InTextureResolveBatchInfo, const FMergeInfo& InLocalMergeInfo,
		const TMap<FTexture2DResource*, FLandscapeRDGTrackedTexture>& InTrackedTextures, FRDGBuilder& GraphBuilder, FRDGResources& RDGResources)
	{
		RDG_EVENT_SCOPE(GraphBuilder, "Finalize %d weightmaps for batch %d", InTextureResolveBatchInfo.TextureToResolveInfoIndices.Num(), InTextureResolveBatchInfo.BatchIndex);

		for (int32 TextureResolveInfoIndex : InTextureResolveBatchInfo.TextureToResolveInfoIndices)
		{
			const FTextureResolveInfo& TextureResolveInfo = InLocalMergeInfo.TextureToResolveInfos[TextureResolveInfoIndex];
			RDG_EVENT_SCOPE(GraphBuilder, "Finalize / resolve %s", *TextureResolveInfo.Texture->GetTextureName().ToString());

			// Finalize the weightmap to the scratch texture (cannot directly write to the texture because it's not render-targetable) :
			FinalizeSingleWeightmap(InTextureResolveBatchInfo, InLocalMergeInfo, TextureResolveInfo, GraphBuilder, RDGResources);

			// Generate mips for this scratch texture :
			GenerateSingleWeightmapMips(TextureResolveInfo, InLocalMergeInfo, GraphBuilder, RDGResources);

			// And finally, copy to the output texture :
			CopyScratchToSourceWeightmap(TextureResolveInfo, InTrackedTextures, GraphBuilder, RDGResources);
		}
	}

} // End namespace EditLayersWeightmapLocalMerge_RenderThread

void ALandscape::PrepareLayersWeightmapsLocalMergeRenderThreadData(const FUpdateLayersContentContext& InUpdateLayersContentContext, const FEditLayersWeightmapMergeParams& InMergeParams, EditLayersWeightmapLocalMerge_RenderThread::FMergeInfo& OutRenderThreadData)
{
	using namespace EditLayersWeightmapLocalMerge_RenderThread;

	TRACE_CPUPROFILER_EVENT_SCOPE(LandscapeLayers_PrepareLayersWeightmapsLocalMergeRenderThreadData);

	ULandscapeInfo* LandscapeInfo = GetLandscapeInfo();
	check(LandscapeInfo != nullptr);

	// Number of vertices for each landscape component :
	int32 ComponentSizeVerts = (SubsectionSizeQuads + 1) * NumSubsections;
	OutRenderThreadData.ComponentSizeVerts = FIntPoint(ComponentSizeVerts, ComponentSizeVerts);
	OutRenderThreadData.NumMips = FMath::CeilLogTwo(ComponentSizeVerts) + 1;
	OutRenderThreadData.NumSubsections = NumSubsections;

	// Lookup table to retrieve, for a given paint layer, its index in OutRenderThreadData.PaintLayerInfos (we don't keep UObjects in OutRenderThreadData because it's a render-thread struct) :
	TMap<ULandscapeLayerInfoObject*, int32> PaintLayerToPaintLayerInfoIndex;

	// Lookup table to retrieve, for a given rendered component/paint layer, its FComponentRenderInfo/FComponentPaintLayerRenderInfo pair identifier (indices)
	TMap<TPair<ULandscapeComponent*, ULandscapeLayerInfoObject*>, FComponentAndPaintLayerRenderInfoIdentifier> ComponentAndPaintLayerToRenderInfoIndex;

	// Prepare per-landscape component render data : 
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(LandscapeLayers_PrepareWeightmapComponentRenderAndPaintLayerInfos);
		for (ULandscapeComponent* Component : InUpdateLayersContentContext.LandscapeComponentsWeightmapsToRender)
		{
			FComponentRenderInfo& ComponentRenderInfo = OutRenderThreadData.ComponentToRenderInfos.Emplace_GetRef();
			ComponentRenderInfo.Name = Component->GetName();

			// Build, for each of the paint layers, a list of the weightmaps from each of the edit layers that affect the final result :
			TMap<ULandscapeLayerInfoObject*, int32> PaintLayerToComponentPaintLayerRenderInfoIndex;
			for (const FLandscapeLayer& Layer : LandscapeEditLayers)
			{
				if (Layer.bVisible && !InMergeParams.bSkipBrush)
				{
					// Retrieve the input textures/channels that are needed for this component and this edit layer :
					const TArray<UTexture2D*>& ComponentEditLayerTextures = Component->GetWeightmapTextures(Layer.Guid);
					const TArray<FWeightmapLayerAllocationInfo>& ComponentEditLayerAllocations = Component->GetWeightmapLayerAllocations(Layer.Guid);
					int32 NumAllocations = ComponentEditLayerAllocations.Num();
					for (int32 AllocIndex = 0; AllocIndex < NumAllocations; ++AllocIndex)
					{
						const FWeightmapLayerAllocationInfo& ComponentEditLayerAllocation = ComponentEditLayerAllocations[AllocIndex];
						if (ComponentEditLayerAllocation.LayerInfo != nullptr)
						{
							// First, take note of the global paint layer information if it has not already been done :
							int32* PaintLayerInfoIndex = PaintLayerToPaintLayerInfoIndex.Find(ComponentEditLayerAllocation.LayerInfo);
							if (PaintLayerInfoIndex == nullptr)
							{
								FWeightmapPaintLayerInfo NewPaintLayerInfo;
								if (!ComponentEditLayerAllocation.LayerInfo->bNoWeightBlend)
								{
									NewPaintLayerInfo.Flags = EWeightmapPaintLayerFlags::IsWeightBlended;
								}
								if (ComponentEditLayerAllocation.LayerInfo == ALandscapeProxy::VisibilityLayer)
								{
									NewPaintLayerInfo.Flags = EWeightmapPaintLayerFlags::IsVisibilityLayer;
								}

								int32 NewPaintLayerInfoIndex = OutRenderThreadData.PaintLayerInfos.Add(NewPaintLayerInfo);
								PaintLayerInfoIndex = &PaintLayerToPaintLayerInfoIndex.Add(ComponentEditLayerAllocation.LayerInfo, NewPaintLayerInfoIndex);
								check(NewPaintLayerInfoIndex == *PaintLayerInfoIndex);
							}

							int32* ComponentPaintLayerRenderInfoIndex = PaintLayerToComponentPaintLayerRenderInfoIndex.Find(ComponentEditLayerAllocation.LayerInfo);
							// Add one entry for each paint layer if not already there :
							if (ComponentPaintLayerRenderInfoIndex == nullptr)
							{
								FComponentPaintLayerRenderInfo NewPaintLayerRenderInfo;
								NewPaintLayerRenderInfo.Name = ComponentEditLayerAllocation.LayerInfo->LayerName.ToString();
								// Remember which index this paint layer corresponds to in the global paint layer info array : 
								NewPaintLayerRenderInfo.PaintLayerInfoIndex = *PaintLayerInfoIndex;

								// Add it to the list and add an entry for it in the map:
								int32 NewPaintLayerRenderInfoIndex = ComponentRenderInfo.PaintLayerRenderInfos.Add(NewPaintLayerRenderInfo);
								ComponentPaintLayerRenderInfoIndex = &PaintLayerToComponentPaintLayerRenderInfoIndex.Add(ComponentEditLayerAllocation.LayerInfo, NewPaintLayerRenderInfoIndex);
								check(NewPaintLayerRenderInfoIndex == *ComponentPaintLayerRenderInfoIndex);

								// The next step will need an identifier for this rendered component/rendered paint layer pair:
								TPair<ULandscapeComponent*, ULandscapeLayerInfoObject*> ComponentAndPaintLayer(Component, ComponentEditLayerAllocation.LayerInfo);
								check(!ComponentAndPaintLayerToRenderInfoIndex.Contains(ComponentAndPaintLayer));
								ComponentAndPaintLayerToRenderInfoIndex.Add(ComponentAndPaintLayer, FComponentAndPaintLayerRenderInfoIdentifier(OutRenderThreadData.ComponentToRenderInfos.Num() - 1, NewPaintLayerRenderInfoIndex));
							}

							FComponentPaintLayerRenderInfo& ComponentPaintLayerRenderInfo = ComponentRenderInfo.PaintLayerRenderInfos[*ComponentPaintLayerRenderInfoIndex];

							// Add the texture we'll need to read from if not already there : 
							check(ComponentEditLayerTextures.IsValidIndex(ComponentEditLayerAllocation.WeightmapTextureIndex));
							UTexture2D* ComponentEditLayerTexture = ComponentEditLayerTextures[ComponentEditLayerAllocation.WeightmapTextureIndex];
							int32 TextureIndexInVisibleEditLayerTextures = ComponentRenderInfo.VisibleEditLayerWeightmapTextures.AddUnique(ComponentEditLayerTexture->GetResource()->GetTexture2DResource());

							// Add an entry for each edit layer that participates to this paint layer : 
							FEditLayerInfo NewEditLayerInfo;
							NewEditLayerInfo.SourceWeightmapTextureIndex = TextureIndexInVisibleEditLayerTextures;
							NewEditLayerInfo.SourceWeightmapTextureChannel = ComponentEditLayerAllocation.WeightmapTextureChannel;
							NewEditLayerInfo.Alpha = (ComponentEditLayerAllocation.LayerInfo == ALandscapeProxy::VisibilityLayer) ? 1.0f : Layer.WeightmapAlpha; // visibility can't affect or be affected by other paint layer weights
							const bool* BlendSubstractive = Layer.WeightmapLayerAllocationBlend.Find(ComponentEditLayerAllocation.LayerInfo);
							NewEditLayerInfo.BlendMode = ((BlendSubstractive != nullptr) && *BlendSubstractive) ? EWeightmapBlendMode::Subtractive : EWeightmapBlendMode::Additive;

							ComponentPaintLayerRenderInfo.VisibleEditLayerInfos.Add(NewEditLayerInfo);
						}
					}
				}
			}

			// Keep track of the maximum number of weightmaps we'll need to merge for a given component :
			OutRenderThreadData.MaxNumEditLayersTexturesToMerge = FMath::Max(ComponentRenderInfo.VisibleEditLayerWeightmapTextures.Num(), OutRenderThreadData.MaxNumEditLayersTexturesToMerge);
		}
	}

	// Collect all UTexture2D that we need to kick off readbacks for and create a FTextureResolveInfo for each:
	TSet<UTexture2D*> ProcessedTextures;
	TArray<UTexture2D*> TexturesNeedingReadback;
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(LandscapeLayers_PrepareWeightmapTextureResolveInfos);
		for (ULandscapeComponent* LandscapeComponentToResolve : InUpdateLayersContentContext.LandscapeComponentsWeightmapsToResolve)
		{
			ALandscapeProxy* Proxy = LandscapeComponentToResolve->GetLandscapeProxy();
			check(Proxy != nullptr);
			const TArray<UTexture2D*>& ComponentBaseWeightmapTextures = LandscapeComponentToResolve->GetWeightmapTextures();
			for (UTexture2D* ComponentWeightmap : ComponentBaseWeightmapTextures)
			{
				bool bIsAlreadyInSet = false;
				ProcessedTextures.Add(ComponentWeightmap, &bIsAlreadyInSet);
				if (!bIsAlreadyInSet)
				{
					FTexture2DResource* WeightmapResource = ComponentWeightmap->GetResource()->GetTexture2DResource();

					FTextureResolveInfo NewTextureResolveInfo(OutRenderThreadData.ComponentToRenderInfos.Num());
					NewTextureResolveInfo.TextureToResolveInfoIndex = OutRenderThreadData.TextureToResolveInfos.Num(); // It will be added at the end of the array 
					NewTextureResolveInfo.Texture = WeightmapResource;

					bool bDoResolve = false;

					// Start preparing the information we need for resolving this texture : we'll need to know how to reconstruct each of its channels :
					TObjectPtr<ULandscapeWeightmapUsage>* Usage = Proxy->WeightmapUsageMap.Find(ComponentWeightmap);
					check(Usage && *Usage);
					// Iterate over all components that participate to this texture's data :
					for (ULandscapeComponent* SourceComponent : (*Usage)->GetUniqueValidComponents())
					{
						// Iterate over all of its allocations to find out which corresponds to this texture :
						const TArray<UTexture2D*>& SourceComponentBaseWeightmapTextures = SourceComponent->GetWeightmapTextures();
						const TArray<FWeightmapLayerAllocationInfo>& SourceComponentBaseWeightmapAllocations = SourceComponent->GetWeightmapLayerAllocations();
						for (const FWeightmapLayerAllocationInfo& AllocationInfo : SourceComponentBaseWeightmapAllocations)
						{
							check(SourceComponentBaseWeightmapTextures.IsValidIndex(AllocationInfo.WeightmapTextureIndex));
							UTexture2D* SourceComponentWeightmap = SourceComponentBaseWeightmapTextures[AllocationInfo.WeightmapTextureIndex];
							// Same texture, we'll need this allocation
							if (AllocationInfo.IsAllocated() && (SourceComponentWeightmap == ComponentWeightmap))
							{
								TPair<ULandscapeComponent*, ULandscapeLayerInfoObject*> ComponentAndPaintLayer(SourceComponent, AllocationInfo.LayerInfo);
								FComponentAndPaintLayerRenderInfoIdentifier* ComponentAndPaintLayerIdentifier = ComponentAndPaintLayerToRenderInfoIndex.Find(ComponentAndPaintLayer);
								// All components needed to recompose this weightmap should be in ComponentToRenderInfos and have a valid paint layer info there:
								check((ComponentAndPaintLayerIdentifier != nullptr)
									&& OutRenderThreadData.ComponentToRenderInfos.IsValidIndex(ComponentAndPaintLayerIdentifier->ComponentIndex)
									&& OutRenderThreadData.ComponentToRenderInfos[ComponentAndPaintLayerIdentifier->ComponentIndex].PaintLayerRenderInfos.IsValidIndex(ComponentAndPaintLayerIdentifier->PaintLayerIndex));
								NewTextureResolveInfo.SetPerChannelSourceInfo(AllocationInfo.WeightmapTextureChannel, *ComponentAndPaintLayerIdentifier);

								// At least one channel to resolve : we need to resolve the texture : 
								bDoResolve = true;
							}
						}
					}

					if (bDoResolve)
					{
						// Setup the CPU readback if it does not already exist:
						FLandscapeEditLayerReadback** CPUReadback = Proxy->WeightmapsCPUReadback.Find(ComponentWeightmap);
						if (CPUReadback == nullptr)
						{
							// Lazily create the readback objects as required (ReallocateLayersWeightmaps might have created new weightmaps)
							FLandscapeEditLayerReadback* NewCPUReadback = new FLandscapeEditLayerReadback();
							const uint8* LockedMip = ComponentWeightmap->Source.LockMipReadOnly(0);
							const uint64 Hash = FLandscapeEditLayerReadback::CalculateHash(LockedMip, ComponentWeightmap->Source.GetSizeX() * ComponentWeightmap->Source.GetSizeY() * sizeof(FColor));
							NewCPUReadback->SetHash(Hash);
							ComponentWeightmap->Source.UnlockMip(0);
							CPUReadback = &Proxy->WeightmapsCPUReadback.Add(ComponentWeightmap, NewCPUReadback);
						}
						check(*CPUReadback != nullptr);

						// Register the CPU readback and add to our list of textures to resolve :
						NewTextureResolveInfo.CPUReadback = *CPUReadback;
						OutRenderThreadData.TextureToResolveInfos.Add(NewTextureResolveInfo);

						TexturesNeedingReadback.Add(ComponentWeightmap);
					}
				}
			}
		}
		check(OutRenderThreadData.TextureToResolveInfos.Num() == TexturesNeedingReadback.Num());
	}

	// Prepare the texture resolve batches: 
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(LandscapeLayers_PrepareWeightmapTextureResolveInfoBatches);

		int32 MaxComponentsPerResolveBatch = CVarLandscapeEditLayersMaxComponentsPerWeightmapResolveBatch.GetValueOnGameThread();

		// Copy the texture infos because TextureToResolveInfos indices need to remain stable at this point :
		TArray<FTextureResolveInfo> RemainingTextureToResolveInfos = OutRenderThreadData.TextureToResolveInfos;
		if (!RemainingTextureToResolveInfos.IsEmpty())
		{
			TBitArray<TInlineAllocator<1>> TempBitArray;
			TempBitArray.Reserve(OutRenderThreadData.ComponentToRenderInfos.Num());

			while (!RemainingTextureToResolveInfos.IsEmpty())
			{
				const FTextureResolveInfo& TextureResolveInfo = RemainingTextureToResolveInfos.Pop(EAllowShrinking::No);

				int32 BestBatchIndex = INDEX_NONE;
				int32 MinNumComponents = MAX_int32;

				// Iterate through all all batches and try to find which would be able to accept it and amongst those, which it would share the most components to render with:
				int32 NumBatches = OutRenderThreadData.TextureResolveBatchInfos.Num();
				for (int32 BatchIndex = 0; BatchIndex < NumBatches; ++BatchIndex)
				{
					const FTextureResolveBatchInfo& Batch = OutRenderThreadData.TextureResolveBatchInfos[BatchIndex];
					TempBitArray = TBitArray<>::BitwiseOR(Batch.ComponentToRenderInfoBitIndices, TextureResolveInfo.ComponentToRenderInfoBitIndices, EBitwiseOperatorFlags::MinSize);

					// If after adding its components, the batch still has less than MaxComponentsPerResolveBatch components to render, it can accept it
					int32 NumComponentsAfter = TempBitArray.CountSetBits();
					if (NumComponentsAfter <= MaxComponentsPerResolveBatch)
					{
						// Is this the best candidate so far?
						if (NumComponentsAfter < MinNumComponents)
						{
							BestBatchIndex = BatchIndex;
							MinNumComponents = NumComponentsAfter;
						}

						// If the number of components after addition of this texture is unchanged, it's a perfect match, we won't ever find a better batch so just stop there for this texture:
						if (NumComponentsAfter == Batch.ComponentToRenderInfoBitIndices.CountSetBits())
						{
							break;
						}
					}
				}

				// If we have found a batch, just add the texture to it, otherwise, add a new batch:
				FTextureResolveBatchInfo& SelectedBatch = (BestBatchIndex != INDEX_NONE) ? OutRenderThreadData.TextureResolveBatchInfos[BestBatchIndex]
					: OutRenderThreadData.TextureResolveBatchInfos.Add_GetRef(FTextureResolveBatchInfo(OutRenderThreadData.ComponentToRenderInfos.Num(), /*InBatchIndex = */OutRenderThreadData.TextureResolveBatchInfos.Num()));

				SelectedBatch.AddTexture(TextureResolveInfo);
				check(SelectedBatch.ComponentToRenderInfoBitIndices.CountSetBits() <= MaxComponentsPerResolveBatch);

				// Keep track of the maximum number of scratch texture arrays we'll need for any given batch :
				OutRenderThreadData.MaxNumWeightmapArraysPerResolveTextureBatch = FMath::Max(SelectedBatch.ComponentToRenderInfoBitIndices.CountSetBits(), OutRenderThreadData.MaxNumWeightmapArraysPerResolveTextureBatch);
			}
		}
	}

	// Finalize :
	{
		OutRenderThreadData.DeferredCopyReadbackTextures = PrepareLandscapeLayersCopyReadbackTextureParams(InUpdateLayersContentContext.MapHelper, TexturesNeedingReadback, /*bWeightmaps = */true);

		// Finally, update the material instances to take into account potentially new material combinations : 
		UpdateLayersMaterialInstances(InUpdateLayersContentContext.LandscapeComponentsWeightmapsToResolve);
	}
}

int32 ALandscape::PerformLayersWeightmapsLocalMerge(FUpdateLayersContentContext& InUpdateLayersContentContext, const FEditLayersWeightmapMergeParams& InMergeParams)
{
	using namespace EditLayersWeightmapLocalMerge_RenderThread;

	TRACE_CPUPROFILER_EVENT_SCOPE(LandscapeLayers_PerformLayersWeightmapsLocalMerge);

	// We need to perform layer reallocations before doing anything, since additional weightmaps might be added in the process, which might result in new components to render/resolve : 
	// TODO [jonathan.bard] : this is only possible to do so in local merge, since we don't yet support BP brushes, for which we need to call Render() in order to be able to know which layer allocations
	//  they need. As such, the global merge path is broken and cannot be fixed unless the BP brush interface is changed in order to inform the system, by advance, about what paint layer it needs
	// Make sure we have proper textures+allocations for all the final weightmaps we're about to resolve :
	ReallocateLayersWeightmaps(InUpdateLayersContentContext, /*InBrushRequiredAllocations = */TArray<ULandscapeLayerInfoObject*>(), /*InPerComponentAllocations = */nullptr, /*bInRestrictTextureSharingToTheseComponents = */false);

	FMergeInfo RenderThreadData;
	PrepareLayersWeightmapsLocalMergeRenderThreadData(InUpdateLayersContentContext, InMergeParams, RenderThreadData);

	if (RenderThreadData.NeedsMerge())
	{
		ENQUEUE_RENDER_COMMAND(PerformLayersWeightmapsLocalMerge)([RenderThreadData](FRHICommandListImmediate& RHICmdList)
		{
			FRDGBuilder GraphBuilder(RHICmdList, RDG_EVENT_NAME("PerformLayersWeightmapsLocalMerge"));

			// Prepare the GPU resources we will use during the local merge : 
			FRDGResources RDGResources;
			PrepareLayersWeightmapsLocalMergeRDGResources(RenderThreadData, GraphBuilder, RDGResources);

			// Get a list of all external textures (weightmaps) we will manipulate during the local merge : 
			TMap<FTexture2DResource*, FLandscapeRDGTrackedTexture> TrackedTextures;
			GatherLayersWeightmapsLocalMergeRDGTextures(RenderThreadData, TrackedTextures);

			// Start tracking those in the render graph :
			TrackLandscapeRDGTextures(GraphBuilder, TrackedTextures);

			// Process the components batch by batch in order to avoid over-allocating temporary textures :
			for (const FTextureResolveBatchInfo& TextureResolveBatchInfo : RenderThreadData.TextureResolveBatchInfos)
			{
				RDG_EVENT_SCOPE(GraphBuilder, "Process batch %d", TextureResolveBatchInfo.BatchIndex);

				// Perform all edit layers merges, for all paint layers of all the components in that batch :
				MergeEditLayersWeightmapsForBatch(TextureResolveBatchInfo, RenderThreadData, TrackedTextures, GraphBuilder, RDGResources);

				// Pack the temporary weightmaps and generate mips on the final texture :
				FinalizeAndResolveWeightmapsForBatch(TextureResolveBatchInfo, RenderThreadData, TrackedTextures, GraphBuilder, RDGResources);
			}

			GraphBuilder.Execute();
		});

		ExecuteCopyToReadbackTexture(RenderThreadData.DeferredCopyReadbackTextures);
	}

	return InMergeParams.WeightmapUpdateModes;
}

int32 ALandscape::PerformLayersWeightmapsGlobalMerge(FUpdateLayersContentContext& InUpdateLayersContentContext, const FEditLayersWeightmapMergeParams& InMergeParams)
{
	ULandscapeInfo* Info = GetLandscapeInfo();
	check(Info != nullptr);
	check(WeightmapRTList.Num() > 0);

	FIntRect LandscapeExtent;
	if (!Info->GetLandscapeExtent(LandscapeExtent.Min.X, LandscapeExtent.Min.Y, LandscapeExtent.Max.X, LandscapeExtent.Max.Y))
	{
		return 0;
	}

	TArray<ULandscapeLayerInfoObject*> BrushRequiredAllocations;
	int32 LayerCount = Info->Layers.Num() + 1; // due to visibility being stored at 0

	if (InMergeParams.WeightmapUpdateModes || InMergeParams.bForceRender)
	{
		UTextureRenderTarget2D* LandscapeScratchRT1 = WeightmapRTList[(int32)EWeightmapRTType::WeightmapRT_Scratch1];
		UTextureRenderTarget2D* LandscapeScratchRT2 = WeightmapRTList[(int32)EWeightmapRTType::WeightmapRT_Scratch2];
		UTextureRenderTarget2D* LandscapeScratchRT3 = WeightmapRTList[(int32)EWeightmapRTType::WeightmapRT_Scratch3];
		UTextureRenderTarget2D* EmptyRT = WeightmapRTList[(int32)EWeightmapRTType::WeightmapRT_Scratch_RGBA];
		FLandscapeLayersWeightmapShaderParameters PSShaderParams;
		FString SourceDebugName;
		FString DestDebugName;
		ClearLayersWeightmapTextureResource(TEXT("ClearRT RGBA"), EmptyRT->GameThread_GetRenderTargetResource());
		ClearLayersWeightmapTextureResource(TEXT("ClearRT R"), LandscapeScratchRT1->GameThread_GetRenderTargetResource());

		{
			TArray<FLandscapeLayersCopyTextureParams> DeferredCopyTextures;
			for (int32 LayerIndex = 0; LayerIndex < LayerCount; ++LayerIndex)
			{
				SourceDebugName = LandscapeScratchRT1->GetName();
				DestDebugName = FString::Printf(TEXT("Weight: Clear CombinedProcLayerWeightmapAllLayersResource %d, "), LayerIndex);

				FLandscapeLayersCopyTextureParams& CopyTextureParams = DeferredCopyTextures.Add_GetRef(FLandscapeLayersCopyTextureParams(SourceDebugName, LandscapeScratchRT1->GameThread_GetRenderTargetResource(), DestDebugName, CombinedLayersWeightmapAllMaterialLayersResource));
				CopyTextureParams.DestArrayIndex = LayerIndex;
				CopyTextureParams.SourceAccess = ERHIAccess::SRVMask;
				CopyTextureParams.DestAccess = ERHIAccess::UAVMask;
			}

			ExecuteCopyLayersTexture(MoveTemp(DeferredCopyTextures));
		}

		bool bHasWeightmapData = false;
		bool bFirstLayer = true;

		for (FLandscapeLayer& Layer : LandscapeEditLayers)
		{
			int8 CurrentWeightmapToProcessIndex = 0;
			bool HasFoundWeightmapToProcess = true; // try processing at least once

			TMap<ULandscapeLayerInfoObject*, int32> LayerInfoObjects; // <LayerInfoObj, LayerIndex>

			// Determine if some brush want to write to layer that we have currently no data on
			if (Layer.bVisible && !InMergeParams.bSkipBrush)
			{
				for (int32 LayerInfoSettingsIndex = 0; LayerInfoSettingsIndex < Info->Layers.Num(); ++LayerInfoSettingsIndex)
				{
					const FLandscapeInfoLayerSettings& InfoLayerSettings = Info->Layers[LayerInfoSettingsIndex];

					// It is possible that no layer info has been assigned so that InfoLayerSettings.LayerInfoObj == nullptr. In that case we don't consider it the layer here.
					if (InfoLayerSettings.LayerInfoObj != nullptr)
					{
						for (int32 i = 0; i < Layer.Brushes.Num(); ++i)
						{
							FLandscapeLayerBrush& Brush = Layer.Brushes[i];
							TOptional<int32> LayerInfoSettingsAllocatedIndex;

							// Note that we need to check visibility before we check by name, or else a name match could
							//  cause us to allocate the visibility layer at the wrong index.
							if (UE::Landscape::IsVisibilityLayer(InfoLayerSettings.LayerInfoObj))
							{
								if (Brush.AffectsVisibilityLayer() && !LayerInfoObjects.Contains(InfoLayerSettings.LayerInfoObj))
								{
									LayerInfoSettingsAllocatedIndex = GetVisibilityLayerAllocationIndex();
								}
							}
							else if (Brush.AffectsWeightmapLayer(InfoLayerSettings.GetLayerName()) && !LayerInfoObjects.Contains(InfoLayerSettings.LayerInfoObj))
							{
								LayerInfoSettingsAllocatedIndex = LayerInfoSettingsIndex + 1; // due to visibility layer that is at 0
							}

							if (LayerInfoSettingsAllocatedIndex.IsSet())
							{
								LayerInfoObjects.Add(InfoLayerSettings.LayerInfoObj, LayerInfoSettingsAllocatedIndex.GetValue());
								bHasWeightmapData = true;
							}
						}

						// Legacy global merge support for non-blueprint-brush renderers: allow edit layer to act as blueprint brush.
						ILandscapeBrushRenderCallAdapter_GlobalMergeLegacySupport* RenderCallAdapter = Cast<ILandscapeBrushRenderCallAdapter_GlobalMergeLegacySupport>(Layer.EditLayer);
						if (RenderCallAdapter && !LayerInfoObjects.Contains(InfoLayerSettings.LayerInfoObj))
						{
							TOptional<int32> LayerInfoSettingsAllocatedIndex;
							if (UE::Landscape::IsVisibilityLayer(InfoLayerSettings.LayerInfoObj))
							{
								if (RenderCallAdapter->AffectsVisibilityLayerAsBlueprintBrush())
								{
									LayerInfoSettingsAllocatedIndex = GetVisibilityLayerAllocationIndex();
								}
							}
							else if (RenderCallAdapter->AffectsWeightmapLayerAsBlueprintBrush(InfoLayerSettings.GetLayerName()))
							{
								LayerInfoSettingsAllocatedIndex = LayerInfoSettingsIndex + 1; // due to visibility layer that is at 0
							}

							if (LayerInfoSettingsAllocatedIndex.IsSet())
							{
								LayerInfoObjects.Add(InfoLayerSettings.LayerInfoObj, LayerInfoSettingsAllocatedIndex.GetValue());
								bHasWeightmapData = true;
							}
						}
					}
				}
			}

			// Track the layers that we have cleared (use a TBitArray in case we get more than 64 layers!)
			TBitArray<> ClearedLayers(0, Info->Layers.Num() + 1);

			// Loop until there is no more weightmap texture to process
			while (HasFoundWeightmapToProcess)
			{
				SourceDebugName = EmptyRT->GetName();
				DestDebugName = FString::Printf(TEXT("Weight: %s Clear WeightmapScratchExtractLayerTextureResource"), *Layer.Name.ToString());

				ExecuteCopyLayersTexture({ FLandscapeLayersCopyTextureParams(SourceDebugName, EmptyRT->GameThread_GetRenderTargetResource(), DestDebugName, WeightmapScratchExtractLayerTextureResource) });

				// Prepare compute shader data
				TArray<FLandscapeLayerWeightmapExtractMaterialLayersComponentData> ComponentsData;
				PrepareComponentDataToExtractMaterialLayersCS(InUpdateLayersContentContext.LandscapeComponentsWeightmapsToRender, Layer, CurrentWeightmapToProcessIndex, LandscapeExtent.Min, WeightmapScratchExtractLayerTextureResource,
					ComponentsData, LayerInfoObjects);

				HasFoundWeightmapToProcess = ComponentsData.Num() > 0;

				// Clear the current atlas if required
				if (CurrentWeightmapToProcessIndex == 0)
				{
					ClearLayersWeightmapTextureResource(TEXT("ClearRT"), LandscapeScratchRT1->GameThread_GetRenderTargetResource());
				}

				TArray<FLandscapeLayersCopyTextureParams> DeferredCopyTextures;
				// Important: for performance reason we only clear the layer we will write to, the other one might contain data but they will not be read during the blend phase
				if (ClearedLayers.CountSetBits() < LayerInfoObjects.Num())
				{
					for (auto& ItPair : LayerInfoObjects)
					{
						// Only clear the layers that we haven't already cleared
						const int32 LayerIndex = ItPair.Value;
						FBitReference ClearedLayerBit = ClearedLayers[LayerIndex];
						if (!ClearedLayerBit)
						{
							ClearedLayerBit = true;

							SourceDebugName = LandscapeScratchRT1->GetName();
							DestDebugName = FString::Printf(TEXT("Weight: %s Clear CurrentProcLayerWeightmapAllLayersResource %d, "), *Layer.Name.ToString(), LayerIndex);

							FLandscapeLayersCopyTextureParams& CopyTextureParams = DeferredCopyTextures.Add_GetRef(FLandscapeLayersCopyTextureParams(SourceDebugName, LandscapeScratchRT1->GameThread_GetRenderTargetResource(), DestDebugName, CurrentLayersWeightmapAllMaterialLayersResource));
							CopyTextureParams.DestAccess = ERHIAccess::UAVMask;
							CopyTextureParams.DestArrayIndex = LayerIndex;
						}
					}

					ExecuteCopyLayersTexture(MoveTemp(DeferredCopyTextures));
				}

				// Perform the compute shader
				if (ComponentsData.Num() > 0)
				{
					PrintLayersDebugTextureResource(FString::Printf(TEXT("LS Weight: %s WeightmapScratchTexture %s"), *Layer.Name.ToString(), TEXT("WeightmapScratchTextureResource")), WeightmapScratchExtractLayerTextureResource, 0, false);

					FLandscapeLayerWeightmapExtractMaterialLayersComputeShaderParameters CSExtractLayersShaderParams;
					CSExtractLayersShaderParams.AtlasWeightmapsPerLayer = CurrentLayersWeightmapAllMaterialLayersResource;
					CSExtractLayersShaderParams.ComponentWeightmapResource = WeightmapScratchExtractLayerTextureResource;
					CSExtractLayersShaderParams.ComputeShaderResource = new FLandscapeLayerWeightmapExtractMaterialLayersComputeShaderResource(ComponentsData);
					CSExtractLayersShaderParams.ComponentSize = (SubsectionSizeQuads + 1) * NumSubsections;

					BeginInitResource(CSExtractLayersShaderParams.ComputeShaderResource);

					FLandscapeLayerWeightmapExtractMaterialLayersCSDispatch_RenderThread CSDispatch(CSExtractLayersShaderParams);

					ENQUEUE_RENDER_COMMAND(LandscapeLayers_Cmd_ExtractLayers)(
						[CSDispatch](FRHICommandListImmediate& RHICmdList) mutable
					{
						TRACE_CPUPROFILER_EVENT_SCOPE(LandscapeLayers_RT_ExtractLayers);
						CSDispatch.ExtractLayers(RHICmdList);
					});

					++CurrentWeightmapToProcessIndex;
					bHasWeightmapData = true; // at least 1 CS was executed, so we can continue the processing
				}
			}

			// If we have data in at least one weight map layer
			if (LayerInfoObjects.Num() > 0)
			{
				for (auto& LayerInfoObject : LayerInfoObjects)
				{
					int32 LayerIndex = LayerInfoObject.Value;
					ULandscapeLayerInfoObject* LayerInfoObj = LayerInfoObject.Key;

					RHI_BREADCRUMB_EVENT_GAMETHREAD("LS Weight: %s PaintLayer: %s", Layer.Name, LayerInfoObj->LayerName);

					// Copy the layer we are working on
					SourceDebugName = FString::Printf(TEXT("Weight: %s PaintLayer: %s, CurrentProcLayerWeightmapAllLayersResource"), *Layer.Name.ToString(), *LayerInfoObj->LayerName.ToString());
					DestDebugName = LandscapeScratchRT1->GetName();

					{
						FLandscapeLayersCopyTextureParams CopyTextureParams(SourceDebugName, CurrentLayersWeightmapAllMaterialLayersResource, DestDebugName, LandscapeScratchRT1->GameThread_GetRenderTargetResource());
						CopyTextureParams.SourceAccess = ERHIAccess::UAVMask;
						CopyTextureParams.SourceArrayIndex = LayerIndex;
						ExecuteCopyLayersTexture({ MoveTemp(CopyTextureParams) });
					}
					PrintLayersDebugRT(FString::Printf(TEXT("LS Weight: %s CurrentProcLayerWeightmapAllLayersResource -> Paint Layer RT %s"), *Layer.Name.ToString(), *LandscapeScratchRT1->GetName()), LandscapeScratchRT1, 0, false);

					PSShaderParams.ApplyLayerModifiers = true;
					PSShaderParams.LayerVisible = Layer.bVisible;
					PSShaderParams.LayerAlpha = LayerInfoObj == ALandscapeProxy::VisibilityLayer ? 1.0f : Layer.WeightmapAlpha; // visibility can't be affected by weight

					DrawWeightmapComponentsToRenderTarget(FString::Printf(TEXT("LS Weight: %s PaintLayer: %s, %s += -> %s"), *Layer.Name.ToString(), *LayerInfoObj->LayerName.ToString(), *LandscapeScratchRT1->GetName(), *LandscapeScratchRT2->GetName()),
						InUpdateLayersContentContext.LandscapeComponentsWeightmapsToRender, LandscapeExtent.Min, LandscapeScratchRT1, nullptr, LandscapeScratchRT2, ERTDrawingType::RTAtlas, true, PSShaderParams, 0);

					PSShaderParams.ApplyLayerModifiers = false;

					// Combined Layer data with current stack
					SourceDebugName = FString::Printf(TEXT("Weight: %s PaintLayer: %s CombinedProcLayerWeightmap"), *Layer.Name.ToString(), *LayerInfoObj->LayerName.ToString());
					DestDebugName = LandscapeScratchRT1->GetName();

					{
						FLandscapeLayersCopyTextureParams CopyTextureParams(SourceDebugName, CombinedLayersWeightmapAllMaterialLayersResource, DestDebugName, LandscapeScratchRT1->GameThread_GetRenderTargetResource());
						CopyTextureParams.SourceAccess = ERHIAccess::UAVMask;
						CopyTextureParams.SourceArrayIndex = LayerIndex;
						ExecuteCopyLayersTexture({ MoveTemp(CopyTextureParams) });
					}
					PrintLayersDebugRT(FString::Printf(TEXT("LS Weight: %s CombinedProcLayerWeightmap -> Paint Layer RT %s"), *Layer.Name.ToString(), *LandscapeScratchRT1->GetName()), LandscapeScratchRT1, 0, false);

					// Combine with current status and copy back to the combined 2d resource array
					PSShaderParams.OutputAsSubstractive = false;

					if (!bFirstLayer)
					{
						const bool* BlendSubstractive = Layer.WeightmapLayerAllocationBlend.Find(LayerInfoObj);
						PSShaderParams.OutputAsSubstractive = BlendSubstractive != nullptr ? *BlendSubstractive : false;
					}

					DrawWeightmapComponentsToRenderTarget(FString::Printf(TEXT("LS Weight: %s PaintLayer: %s, %s += -> Combined %s"), *Layer.Name.ToString(), *LayerInfoObj->LayerName.ToString(), *LandscapeScratchRT2->GetName(), *LandscapeScratchRT3->GetName()),
						InUpdateLayersContentContext.LandscapeComponentsWeightmapsToRender, LandscapeExtent.Min, LandscapeScratchRT2, bFirstLayer ? nullptr : LandscapeScratchRT1, LandscapeScratchRT3, ERTDrawingType::RTAtlasToNonAtlas, true, PSShaderParams, 0);

					PSShaderParams.OutputAsSubstractive = false;

					SourceDebugName = FString::Printf(TEXT("Weight: %s PaintLayer: %s %s"), *Layer.Name.ToString(), *LayerInfoObj->LayerName.ToString(), *LandscapeScratchRT3->GetName());
					DestDebugName = TEXT("CombinedProcLayerWeightmap");

					// Handle brush blending
					if (Layer.bVisible && !InMergeParams.bSkipBrush)
					{
						// Draw each brush				
						for (int32 i = 0; i < Layer.Brushes.Num(); ++i)
						{
							// TODO: handle conversion/handling of RT not same size as internal size

							FLandscapeLayerBrush& Brush = Layer.Brushes[i];
							FLandscapeBrushParameters BrushParameters = BuildLandscapeBrushParameters(/*bIsHeightmapMerge = */false, LandscapeExtent, LandscapeScratchRT3, LayerInfoObj->LayerName);

							UTextureRenderTarget2D* BrushOutputRT = Brush.RenderLayer(BrushParameters);
							if (BrushOutputRT == nullptr || BrushOutputRT->SizeX != LandscapeScratchRT3->SizeX || BrushOutputRT->SizeY != LandscapeScratchRT3->SizeY)
							{
								continue;
							}

							ALandscapeBlueprintBrushBase* LandscapeBrush = Brush.GetBrush();
							check(LandscapeBrush != nullptr); // If we managed to render, the brush should be valid

							BrushRequiredAllocations.AddUnique(LayerInfoObj);

							INC_DWORD_STAT(STAT_LandscapeLayersRegenerateDrawCalls); // Brush RenderInitialize

							PrintLayersDebugRT(FString::Printf(TEXT("LS Weight: %s %s -> Brush %s"), *Layer.Name.ToString(), *LandscapeBrush->GetName(), *BrushOutputRT->GetName()), BrushOutputRT);

							// Copy result back if brush did not edit things in place.

							// Resolve back to Combined heightmap (it's unlikely, but possible that the brush returns the same RT as input and output, if it did various operations on it, in which case the copy is useless) :
							if (BrushOutputRT != LandscapeScratchRT3)
							{
								SourceDebugName = FString::Printf(TEXT("Weight: %s PaintLayer: %s Brush: %s"), *Layer.Name.ToString(), *LayerInfoObj->LayerName.ToString(), *BrushOutputRT->GetName());
								DestDebugName = LandscapeScratchRT3->GetName();
								ExecuteCopyLayersTexture({ FLandscapeLayersCopyTextureParams(SourceDebugName, BrushOutputRT->GameThread_GetRenderTargetResource(), DestDebugName, LandscapeScratchRT3->GameThread_GetRenderTargetResource()) });
								PrintLayersDebugRT(FString::Printf(TEXT("LS Weight: %s Component %s += -> Combined %s"), *Layer.Name.ToString(), *BrushOutputRT->GetName(), *LandscapeScratchRT3->GetName()), LandscapeScratchRT3);
							}
						}

						// Legacy global merge support for non-blueprint-brush renderers: allow edit layer to act as blueprint brush.
						ILandscapeBrushRenderCallAdapter_GlobalMergeLegacySupport* RenderCallAdapter = Cast<ILandscapeBrushRenderCallAdapter_GlobalMergeLegacySupport>(Layer.EditLayer);
						if (RenderCallAdapter)
						{
							FLandscapeBrushParameters BrushParameters = BuildLandscapeBrushParameters(
								/*bIsHeightmapMerge = */false, LandscapeExtent, LandscapeScratchRT3, LayerInfoObj->LayerName);
							
							UTextureRenderTarget2D* BrushOutputRT = RenderCallAdapter->RenderAsBlueprintBrush(BrushParameters, GetTransform());

							if (BrushOutputRT != nullptr && BrushOutputRT->SizeX == LandscapeScratchRT3->SizeX 
								&& BrushOutputRT->SizeY == LandscapeScratchRT3->SizeY)
							{
								BrushRequiredAllocations.AddUnique(LayerInfoObj);

								// Same conditional copy as for blueprint brushes
								if (BrushOutputRT != LandscapeScratchRT3)
								{
									SourceDebugName = FString::Printf(TEXT("Weight: %s PaintLayer: %s Brush: %s"), *Layer.Name.ToString(), *LayerInfoObj->LayerName.ToString(), *BrushOutputRT->GetName());
									DestDebugName = LandscapeScratchRT3->GetName();
									ExecuteCopyLayersTexture({ FLandscapeLayersCopyTextureParams(SourceDebugName, BrushOutputRT->GameThread_GetRenderTargetResource(), DestDebugName, LandscapeScratchRT3->GameThread_GetRenderTargetResource()) });
									PrintLayersDebugRT(FString::Printf(TEXT("LS Weight: %s Component %s += -> Combined %s"), *Layer.Name.ToString(), *BrushOutputRT->GetName(), *LandscapeScratchRT3->GetName()), LandscapeScratchRT3);
								}
							}
						}

						PrintLayersDebugRT(FString::Printf(TEXT("LS Weight: %s CombinedPostBrushProcLayerWeightmap -> Paint Layer RT %s"), *Layer.Name.ToString(), *LandscapeScratchRT3->GetName()), LandscapeScratchRT3, 0, false);

						SourceDebugName = FString::Printf(TEXT("Weight: %s PaintLayer: %s %s"), *Layer.Name.ToString(), *LayerInfoObj->LayerName.ToString(), *LandscapeScratchRT3->GetName());
						DestDebugName = TEXT("CombinedProcLayerWeightmap");

						FLandscapeLayersCopyTextureParams CopyTextureParams(SourceDebugName, LandscapeScratchRT3->GameThread_GetRenderTargetResource(), DestDebugName, CombinedLayersWeightmapAllMaterialLayersResource);
						CopyTextureParams.DestAccess = ERHIAccess::UAVMask;
						CopyTextureParams.DestArrayIndex = LayerIndex;
						ExecuteCopyLayersTexture({ MoveTemp(CopyTextureParams) });
					}

					DrawWeightmapComponentsToRenderTarget(FString::Printf(TEXT("LS Weight: %s Combined Scratch No Border to %s Combined Scratch with Border"), *LandscapeScratchRT3->GetName(), *LandscapeScratchRT1->GetName()),
						InUpdateLayersContentContext.LandscapeComponentsWeightmapsToRender, LandscapeExtent.Min, LandscapeScratchRT3, nullptr, LandscapeScratchRT1, ERTDrawingType::RTNonAtlasToAtlas, true, PSShaderParams, 0);

					FLandscapeLayersCopyTextureParams CopyTextureParams(SourceDebugName, LandscapeScratchRT1->GameThread_GetRenderTargetResource(), DestDebugName, CombinedLayersWeightmapAllMaterialLayersResource);
					CopyTextureParams.DestAccess = ERHIAccess::UAVMask;
					CopyTextureParams.DestArrayIndex = LayerIndex;
					ExecuteCopyLayersTexture({ MoveTemp(CopyTextureParams) });
				}

				PSShaderParams.ApplyLayerModifiers = false;
			}

			bFirstLayer = false;
		}

		ReallocateLayersWeightmaps(InUpdateLayersContentContext, BrushRequiredAllocations, /*InPerComponentAllocations = */nullptr, /*bInRestrictTextureSharingToTheseComponents = */false);

		// List of UTexture2D that we need to kick off readbacks for :
		TArray<UTexture2D*> TexturesNeedingReadback;

		if (bHasWeightmapData)
		{
			// Lazily create CPU read back objects as required
			for (ULandscapeComponent* Component : InUpdateLayersContentContext.LandscapeComponentsWeightmapsToRender)
			{
				const TArray<UTexture2D*>& ComponentWeightmapTextures = Component->GetWeightmapTextures();

				for (UTexture2D* WeightmapTexture : ComponentWeightmapTextures)
				{
					ALandscapeProxy* Proxy = Component->GetLandscapeProxy();
					FLandscapeEditLayerReadback** CPUReadback = Proxy->WeightmapsCPUReadback.Find(WeightmapTexture);

					if (CPUReadback == nullptr)
					{
						FLandscapeEditLayerReadback* NewCPUReadback = new FLandscapeEditLayerReadback();
						const uint8* LockedMip = WeightmapTexture->Source.LockMipReadOnly(0);
						const uint64 Hash = FLandscapeEditLayerReadback::CalculateHash(LockedMip, WeightmapTexture->Source.GetSizeX() * WeightmapTexture->Source.GetSizeY() * sizeof(FColor));
						NewCPUReadback->SetHash(Hash);
						WeightmapTexture->Source.UnlockMip(0);
						Proxy->WeightmapsCPUReadback.Add(WeightmapTexture, NewCPUReadback);
					}
				}
			}

			int8 CurrentWeightmapToProcessIndex = 0;
			bool HasFoundWeightmapToProcess = true; // try processing at least once

			TArray<float> WeightmapLayerWeightBlend;
			TArray<UTexture2D*> ProcessedWeightmaps;
			TArray<FLandscapeEditLayerReadback*> ProcessedCPUReadbackTextures;
			int32 NextTextureIndexToProcess = 0;

			// Generate the component data from the weightmap allocation that were done earlier and weight blend them if required (i.e renormalize)
			while (HasFoundWeightmapToProcess)
			{
				TArray<FLandscapeLayerWeightmapPackMaterialLayersComponentData> PackLayersComponentsData;
				PrepareComponentDataToPackMaterialLayersCS(CurrentWeightmapToProcessIndex, LandscapeExtent.Min, InUpdateLayersContentContext.LandscapeComponentsWeightmapsToRender, ProcessedWeightmaps, ProcessedCPUReadbackTextures, PackLayersComponentsData);
				HasFoundWeightmapToProcess = PackLayersComponentsData.Num() > 0;

				// Perform the compute shader
				if (PackLayersComponentsData.Num() > 0)
				{
					// Compute the weightblend mode of each layer for the compute shader
					if (WeightmapLayerWeightBlend.Num() != LayerCount)
					{
						WeightmapLayerWeightBlend.SetNum(LayerCount);

						for (int32 LayerInfoSettingsIndex = 0; LayerInfoSettingsIndex < Info->Layers.Num(); ++LayerInfoSettingsIndex)
						{
							const FLandscapeInfoLayerSettings& LayerInfo = Info->Layers[LayerInfoSettingsIndex];
							WeightmapLayerWeightBlend[LayerInfoSettingsIndex + 1] = LayerInfo.LayerInfoObj != nullptr ? (LayerInfo.LayerInfoObj->bNoWeightBlend ? 0.0f : 1.0f) : 1.0f;
						}

						WeightmapLayerWeightBlend[0] = 0.0f; // Blend of Visibility 
					}

					TArray<FVector2f> WeightmapTextureOutputOffset;

					// Compute each weightmap location so compute shader will be able to output at expected location
					const int32 WeightmapSizeX = WeightmapScratchPackLayerTextureResource->GetSizeX();
					const int32 WeightmapSizeY = WeightmapScratchPackLayerTextureResource->GetSizeY();
					const int32 ComponentSize = (SubsectionSizeQuads + 1) * NumSubsections;

					float ComponentY = 0;
					float ComponentX = 0;

					for (int32 i = 0; i < PackLayersComponentsData.Num(); ++i)
					{
						if (ComponentX + ComponentSize > WeightmapSizeX)
						{
							ComponentY += ComponentSize;
							ComponentX = 0;
						}

						// This should never happen as it would be a bug in the algo
						check(ComponentX + ComponentSize <= WeightmapSizeX);
						check(ComponentY + ComponentSize <= WeightmapSizeY);

						WeightmapTextureOutputOffset.Add(FVector2f(ComponentX, ComponentY));
						ComponentX += ComponentSize;
					}

					// Clear Pack texture
					SourceDebugName = *EmptyRT->GetName();
					DestDebugName = TEXT("Weight: Clear WeightmapScratchPackLayerTextureResource");

					CopyTexturePS(SourceDebugName, EmptyRT->GameThread_GetRenderTargetResource(), DestDebugName, WeightmapScratchPackLayerTextureResource);

					ENQUEUE_RENDER_COMMAND(LandscapeLayers_TransitionPackLayerResources)(
						[this](FRHICommandListImmediate& RHICmdList) mutable
					{
						RHICmdList.Transition(FRHITransitionInfo(CombinedLayersWeightmapAllMaterialLayersResource->TextureRHI, ERHIAccess::UAVMask, ERHIAccess::SRVMask));
						RHICmdList.Transition(FRHITransitionInfo(WeightmapScratchPackLayerTextureResource->TextureRHI, ERHIAccess::RTV, ERHIAccess::UAVMask));
					});

					FLandscapeLayerWeightmapPackMaterialLayersComputeShaderParameters CSPackLayersShaderParams;
					CSPackLayersShaderParams.AtlasWeightmapsPerLayer = CombinedLayersWeightmapAllMaterialLayersResource;
					CSPackLayersShaderParams.ComponentWeightmapResource = WeightmapScratchPackLayerTextureResource;
					CSPackLayersShaderParams.ComputeShaderResource = new FLandscapeLayerWeightmapPackMaterialLayersComputeShaderResource(PackLayersComponentsData, WeightmapLayerWeightBlend, WeightmapTextureOutputOffset);
					CSPackLayersShaderParams.ComponentSize = ComponentSize;
					BeginInitResource(CSPackLayersShaderParams.ComputeShaderResource);

					FLandscapeLayerWeightmapPackMaterialLayersCSDispatch_RenderThread CSDispatch(CSPackLayersShaderParams);

					ENQUEUE_RENDER_COMMAND(LandscapeLayers_Cmd_PackLayers)(
						[CSDispatch](FRHICommandListImmediate& RHICmdList) mutable
					{
						TRACE_CPUPROFILER_EVENT_SCOPE(LandscapeLayers_RT_PackLayers);
						CSDispatch.PackLayers(RHICmdList);
					});

					UTextureRenderTarget2D* CurrentRT = WeightmapRTList[(int32)EWeightmapRTType::WeightmapRT_Mip0];

					SourceDebugName = TEXT("WeightmapScratchTexture");
					DestDebugName = CurrentRT->GetName();

					ENQUEUE_RENDER_COMMAND(LandscapeLayers_TransitionCopyResources)(
						[this, CurrentRT](FRHICommandListImmediate& RHICmdList) mutable
					{
						RHICmdList.Transition(FRHITransitionInfo(CombinedLayersWeightmapAllMaterialLayersResource->TextureRHI, ERHIAccess::SRVMask, ERHIAccess::UAVMask));
						RHICmdList.Transition(FRHITransitionInfo(WeightmapScratchPackLayerTextureResource->TextureRHI, ERHIAccess::UAVMask, ERHIAccess::SRVMask));
						RHICmdList.Transition(FRHITransitionInfo(CurrentRT->GetRenderTargetResource()->TextureRHI, ERHIAccess::SRVMask, ERHIAccess::RTV));
					});

					CopyTexturePS(SourceDebugName, WeightmapScratchPackLayerTextureResource, DestDebugName, CurrentRT->GameThread_GetRenderTargetResource());

					ENQUEUE_RENDER_COMMAND(LandscapeLayers_TransitionMip0)(
						[this, CurrentRT](FRHICommandListImmediate& RHICmdList) mutable
					{
						RHICmdList.Transition(FRHITransitionInfo(CurrentRT->GetRenderTargetResource()->TextureRHI, ERHIAccess::RTV, ERHIAccess::SRVMask));
						RHICmdList.Transition(FRHITransitionInfo(WeightmapScratchPackLayerTextureResource->TextureRHI, ERHIAccess::SRVMask, ERHIAccess::RTV));
					});
					DrawWeightmapComponentToRenderTargetMips(WeightmapTextureOutputOffset, CurrentRT, true, PSShaderParams);

					int32 StartTextureIndex = NextTextureIndexToProcess;

					TArray<FLandscapeLayersCopyTextureParams> DeferredCopyTextures;
					for (; NextTextureIndexToProcess < ProcessedWeightmaps.Num(); ++NextTextureIndexToProcess)
					{
						UTexture2D* WeightmapTexture = ProcessedWeightmaps[NextTextureIndexToProcess];
						if (!InUpdateLayersContentContext.WeightmapsToResolve.Contains(WeightmapTexture))
						{
							continue;
						}

						FTextureResource* WeightmapTextureResource = WeightmapTexture ? WeightmapTexture->GetResource() : nullptr;
						if (WeightmapTextureResource == nullptr)
						{
							continue;
						}

						const int32 TextureSizeX = WeightmapTextureResource->GetSizeX();
						const int32 TextureSizeY = WeightmapTextureResource->GetSizeY();

						FIntPoint TextureTopLeftPositionInAtlas(
							static_cast<int32>(WeightmapTextureOutputOffset[NextTextureIndexToProcess - StartTextureIndex].X),
							static_cast<int32>(WeightmapTextureOutputOffset[NextTextureIndexToProcess - StartTextureIndex].Y));

						int32 CurrentMip = 0;

						for (int32 MipRTIndex = (int32)EWeightmapRTType::WeightmapRT_Mip0; MipRTIndex < (int32)EWeightmapRTType::WeightmapRT_Count; ++MipRTIndex)
						{
							CurrentRT = WeightmapRTList[MipRTIndex];

							if (CurrentRT != nullptr)
							{
								SourceDebugName = CurrentRT->GetName();
								DestDebugName = FString::Printf(TEXT("Weightmap Mip: %d"), CurrentMip);

								FLandscapeLayersCopyTextureParams& CopyTextureParams = DeferredCopyTextures.Add_GetRef(FLandscapeLayersCopyTextureParams(SourceDebugName, CurrentRT->GameThread_GetRenderTargetResource(), DestDebugName, WeightmapTexture->GetResource()));
								// Only copy the size that's actually needed : 
								CopyTextureParams.CopySize.X = TextureSizeX >> CurrentMip;
								CopyTextureParams.CopySize.Y = TextureSizeY >> CurrentMip;
								// Copy from the composited texture's position to the top-left corner of the heightmap
								CopyTextureParams.SourcePosition.X = TextureTopLeftPositionInAtlas.X >> CurrentMip;
								CopyTextureParams.SourcePosition.Y = TextureTopLeftPositionInAtlas.Y >> CurrentMip;
								CopyTextureParams.DestMip = static_cast<uint8>(CurrentMip);
								++CurrentMip;
							}
						}

						check(!TexturesNeedingReadback.Contains(WeightmapTexture));
						TexturesNeedingReadback.Add(WeightmapTexture);
					}

					ExecuteCopyLayersTexture(MoveTemp(DeferredCopyTextures));
				}

				++CurrentWeightmapToProcessIndex;
			}
		}

		// Prepare the UTexture2D readbacks we'll need to perform :
		TArray<FLandscapeLayersCopyReadbackTextureParams> DeferredCopyReadbackTextures = PrepareLandscapeLayersCopyReadbackTextureParams(InUpdateLayersContentContext.MapHelper, TexturesNeedingReadback, /*bWeightmaps = */true);
		ExecuteCopyToReadbackTexture(DeferredCopyReadbackTextures);

		UpdateLayersMaterialInstances(InUpdateLayersContentContext.LandscapeComponentsWeightmapsToResolve);
	}

	return InMergeParams.WeightmapUpdateModes;
}

int32 ALandscape::PerformLayersWeightmapsBatchedMerge(FUpdateLayersContentContext& InUpdateLayersContentContext, const FEditLayersWeightmapMergeParams& InMergeParams)
{
	using namespace UE::Landscape;
	using namespace UE::Landscape::EditLayers;
	using namespace UE::Landscape::EditLayers::Private;

	TRACE_CPUPROFILER_EVENT_SCOPE(ALandscape::PerformLayersWeightmapsBatchedMerge);
	RHI_BREADCRUMB_EVENT_GAMETHREAD("PerformLayersWeightmapsBatchedMerge");

	ULandscapeInfo* Info = GetLandscapeInfo();
	check(Info != nullptr);

	TArray<FEditLayerRendererState> RendererStates = GetEditLayerRendererStates(Info, InMergeParams.bSkipBrush);

	const TSet<FName> RequestedWeightmapLayerNames(GetTargetLayerNames(/*bInIncludeVisibilityLayer = */ true));

	// Prepare the merge : 
	TSet<ULandscapeComponent*> LandscapeComponentsWeightmapsToRenderBefore(InUpdateLayersContentContext.LandscapeComponentsWeightmapsToRender);
	FMergeRenderParams MergeRenderParams(/*bInIsHeightmapMerge = */false, /*InLandscape = */this, InUpdateLayersContentContext.LandscapeComponentsWeightmapsToRender, RendererStates, RequestedWeightmapLayerNames);
	FMergeRenderContext MergeRenderContext = PrepareEditLayersMergeRenderContext(MergeRenderParams); 
	if (!MergeRenderContext.IsValid())
	{
		return InMergeParams.WeightmapUpdateModes;
	}

	TArray<ULandscapeComponent*> FinalComponentsToResolve;
	FinalComponentsToResolve.Reserve(InUpdateLayersContentContext.LandscapeComponentsWeightmapsToResolve.Num());
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(ReallocateWeightmaps);
		TMap<ULandscapeComponent*, TArray<ULandscapeLayerInfoObject*>> PerComponentAllocations;
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(PrepareComponentAllocations);
			PerComponentAllocations.Reserve(MergeRenderContext.ComponentToTargetLayerBitIndices.Num());
			for (auto ItPair : MergeRenderContext.ComponentToTargetLayerBitIndices)
			{
				ULandscapeComponent* Component = ItPair.Key;
				const TBitArray<>& ComponentTargetLayerBitIndices = ItPair.Value;
				TArray<ULandscapeLayerInfoObject*>& ComponentAllocations = PerComponentAllocations.Add(Component, MergeRenderContext.ConvertTargetLayerBitIndicesToLayerInfos(ComponentTargetLayerBitIndices));
				if (!ComponentAllocations.IsEmpty())
				{
					FinalComponentsToResolve.Add(Component);
				}
			}
		}
		ReallocateLayersWeightmaps(InUpdateLayersContentContext, /*InBrushRequiredAllocations = */{}, &PerComponentAllocations, /*bInRestrictTextureSharingToTheseComponents = */true);

		checkf(Algo::AllOf(InUpdateLayersContentContext.LandscapeComponentsWeightmapsToRender, [&LandscapeComponentsWeightmapsToRenderBefore](ULandscapeComponent* InComponent) { return LandscapeComponentsWeightmapsToRenderBefore.Contains(InComponent); }), 
			TEXT("If this asserts, the list of components involved in the weightmaps being merged has changed, which would require re-running the merge preparation step. bInRestrictTextureSharingToTheseComponents should prevent this from happening"));
	}

	TSet<ULandscapeComponent*> ResolvedLandscapeComponents;
	ResolvedLandscapeComponents.Reserve(FinalComponentsToResolve.Num());
	// Key = texture, Value = channels mask resolved so far. If all channels have been resolved, we remove the entry as the texture is already fully resolved :
	TMap<UTexture2D*, uint8> TexturesNeedingResolve; 
	TexturesNeedingResolve.Reserve(InUpdateLayersContentContext.WeightmapsToResolve.Num());
	for (UTexture2D* Weightmap : InUpdateLayersContentContext.WeightmapsToResolve)
	{ 
		TexturesNeedingResolve.Add(Weightmap, 0);
	}

	// Callback executed each time a render batch is done computing the requested into, just before releasing the render resources : 
	auto OnRenderBatchGroupDone = [&ResolvedLandscapeComponents, &TexturesNeedingResolve, &InUpdateLayersContentContext, OnEditLayersMergedDelegate = &OnEditLayersMergedDelegate](const FMergeRenderContext::FOnRenderBatchTargetGroupDoneParams& InParams)
	{
		// We can now finalize the weightmaps : since we don't use SUPPORTS_LANDSCAPE_EDITORONLY_UBER_MATERIAL yet, each component that has been rendered should have a complete set of weightmaps that has been regenerated
		//  already so we need to repack those into the appropriate weightmap channels

		ALandscape* Landscape = InParams.MergeRenderContext->GetLandscape();

		// Cycle render targets one last time so we can read from the last RT we've written to :
		InParams.MergeRenderContext->CycleBlendRenderTargets();
		ULandscapeScratchRenderTarget* ReadRT = InParams.MergeRenderContext->GetBlendRenderTargetRead();

		OnEditLayersMergedDelegate->Broadcast(FOnLandscapeEditLayersMergedParams(ReadRT->GetRenderTarget(),
			/*InRenderAreaResolution = */InParams.RenderBatch->GetRenderTargetResolution(/*bInWithDuplicateBorders = */false), /*bInIsHeightmapMerge = */false));

		const int32 TotalNumSubsections = Landscape->NumSubsections * Landscape->NumSubsections;
		FIntPoint MinWeightmapResolution(MAX_int32, MAX_int32);
		FIntPoint MaxWeightmapResolution(MIN_int32, MIN_int32);

		struct FWeightmapResolveInfo
		{
			// Weightmap to resolve : 
			FTextureResource* TextureResource = nullptr;
			FString TextureResourceDebugName;
			// Slice index (in the batch's source texture array) that needs to be copied onto each individual channel of this weightmap:
			FIntVector4 SourceSliceIndexPerChannel = FIntVector4(INDEX_NONE);
			// The rects that correspond to the component to read (without border expansion) in the batch (source) texture (one per channel)
			TStaticArray<TArray<FIntRect, TInlineAllocator<4>>, 4> SourceSubsectionRectsPerChannel;
			// Indicates which channel(s) to resolve : 
			uint8 ChannelMask = 0;
			// Indicates whether this resolve operation should be done additively (e.g. there could be a first resolve on texture T for channels .rgb and another later on for channel .a : the former 
			//  would *not* be done additively but the latter, yes... you can thank weightmap channel sharing for all that complexity)
			bool bIsAdditiveResolve = false;
			// Indicates this resolve operation is the final one, i.e. all channels from this weightmap are now resolved so we can finalize the texture entirely (generate mips, final copy and readback) :
			bool bIsFinalResolve = false;
		};
		// List of weightmaps to resolve this batch and how to resolve them: 
		TMap<UTexture2D*, FWeightmapResolveInfo> WeightmapResolveInfosForBatch;
		TSet<ULandscapeComponent*> ComponentsResolvedInBatch;

		// Process the list of textures that are not yet resolved and find if there are components in this batch that we participate to it so we can resolve it either partially or fully : 
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(PrepareResolve);
			for (auto ItWeightmapNeedingResolve = TexturesNeedingResolve.CreateIterator(); ItWeightmapNeedingResolve; ++ItWeightmapNeedingResolve)
			{
				UTexture2D* Weightmap = ItWeightmapNeedingResolve.Key();
				uint8& AlreadyResolvedChannelMask = ItWeightmapNeedingResolve.Value();

				uint8 WeightmapChannelMask = InUpdateLayersContentContext.MapHelper.WeightmapToChannelMask.FindChecked(Weightmap);
				check((WeightmapChannelMask > 0) && (WeightmapChannelMask <= 15)); // a mask of 0 means nothing needs to be resolved, so it shouldn't ever be in the TexturesNeedingResolve list
			
				const TArray<ULandscapeComponent*>& ComponentsForWeightmap = InUpdateLayersContentContext.MapHelper.WeightmapToComponents.FindChecked(Weightmap);
				for (ULandscapeComponent* RenderedComponent : ComponentsForWeightmap)
				{
					// If the component is present in this batch, we can resolve its channels :
					if (InParams.RenderBatch->ComponentsToRender.Contains(RenderedComponent) 
						// The component was possibly rendered in a previous batch, in which case we don't have to resolved it again : 
						&& !ResolvedLandscapeComponents.Contains(RenderedComponent))
					{
						TArray<FIntRect, TInlineAllocator<4>> SourceSubsectionRects, DummySubsectionRects;
						InParams.RenderBatch->ComputeSubsectionRects(RenderedComponent, /*OutSubsectionRects = */SourceSubsectionRects, /*OutSubsectionRectsWithDuplicateBorders = */DummySubsectionRects);
						check(SourceSubsectionRects.Num() == TotalNumSubsections);

						FWeightmapResolveInfo* WeightmapResolveInfo = WeightmapResolveInfosForBatch.Find(Weightmap);
						if (WeightmapResolveInfo == nullptr)
						{
							// This is a new weightmap to resolve this batch, let's create the info : 
							WeightmapResolveInfo = &WeightmapResolveInfosForBatch.Add(Weightmap, FWeightmapResolveInfo());
							WeightmapResolveInfo->TextureResource = Weightmap->GetResource();
							WeightmapResolveInfo->TextureResourceDebugName = Weightmap->GetName();
							// We need the resolve operation to be additive if some channels have already been resolved in a previous batch: 
							WeightmapResolveInfo->bIsAdditiveResolve = (AlreadyResolvedChannelMask != 0);

							ALandscapeProxy* Proxy = RenderedComponent->GetLandscapeProxy();
							check(Proxy != nullptr);
							// Setup the CPU readback if it does not already exist:
							FLandscapeEditLayerReadback** CPUReadback = Proxy->WeightmapsCPUReadback.Find(Weightmap);
							if (CPUReadback == nullptr)
							{
								// Lazily create the readback objects as required (ReallocateLayersWeightmaps might have created new weightmaps)
								FLandscapeEditLayerReadback* NewCPUReadback = new FLandscapeEditLayerReadback();
								const uint8* LockedMip = Weightmap->Source.LockMipReadOnly(0);
								const uint64 Hash = FLandscapeEditLayerReadback::CalculateHash(LockedMip, Weightmap->Source.GetSizeX() * Weightmap->Source.GetSizeY() * sizeof(FColor));
								NewCPUReadback->SetHash(Hash);
								Weightmap->Source.UnlockMip(0);
								CPUReadback = &Proxy->WeightmapsCPUReadback.Add(Weightmap, NewCPUReadback);
							}
							check(*CPUReadback != nullptr);

							FIntPoint WeightmapResolution(Weightmap->Source.GetSizeX(), Weightmap->Source.GetSizeY());
							MinWeightmapResolution = MinWeightmapResolution.ComponentMin(WeightmapResolution);
							MaxWeightmapResolution = MaxWeightmapResolution.ComponentMax(WeightmapResolution);
						}


						// Select only the allocations of this component that involve this texture : 
						const TArray<UTexture2D*>& ComponentTextures = RenderedComponent->GetWeightmapTextures();
						const int32 WeightmapIndex = ComponentTextures.IndexOfByKey(Weightmap);
						check(WeightmapIndex != INDEX_NONE);
						const TArray<FWeightmapLayerAllocationInfo>& ComponentAllocationInfos = RenderedComponent->GetWeightmapLayerAllocations();
						TArray<FWeightmapLayerAllocationInfo, TInlineAllocator<4>> AllocationInfosForTexture;
						Algo::TransformIf(ComponentAllocationInfos, AllocationInfosForTexture,
							[WeightmapIndex](const FWeightmapLayerAllocationInfo& InAllocationInfo) { return (InAllocationInfo.LayerInfo != nullptr) && InAllocationInfo.IsAllocated() && (InAllocationInfo.WeightmapTextureIndex == WeightmapIndex); },
							[](const FWeightmapLayerAllocationInfo& InAllocationInfo) { return InAllocationInfo; });
						check(!AllocationInfosForTexture.IsEmpty() && (AllocationInfosForTexture.Num() <= 4));

						for (const FWeightmapLayerAllocationInfo& AllocationInfo : AllocationInfosForTexture)
						{
							check((AllocationInfo.WeightmapTextureChannel >= 0) && (AllocationInfo.WeightmapTextureChannel < 4));
							checkf(((WeightmapResolveInfo->ChannelMask & (1 << AllocationInfo.WeightmapTextureChannel)) == 0), TEXT("This channel has already been resolved, it shouldn't happen, it would mean that 2 allocations are using the same channel"));
							int32 SliceIndex = InParams.RenderGroupTargetLayerNames.IndexOfByKey(AllocationInfo.GetLayerName());
							checkf(SliceIndex != INDEX_NONE, TEXT("Couldn't find %s in the list of weightmaps that have been produced"), *AllocationInfo.GetLayerName().ToString());
							WeightmapResolveInfo->SourceSliceIndexPerChannel[AllocationInfo.WeightmapTextureChannel] = SliceIndex;
							WeightmapResolveInfo->SourceSubsectionRectsPerChannel[AllocationInfo.WeightmapTextureChannel] = SourceSubsectionRects;
							WeightmapResolveInfo->ChannelMask |= (1 << AllocationInfo.WeightmapTextureChannel);
						}
						check(WeightmapResolveInfo->ChannelMask != 0);
						// We can now consider these channels resolved for this texture : 
						AlreadyResolvedChannelMask |= WeightmapResolveInfo->ChannelMask;

						// If all of this weightmap's channels have been resolved, we can finalize it and remove it from our list of weightmaps to resolve :
						if (AlreadyResolvedChannelMask == WeightmapChannelMask)
						{
							WeightmapResolveInfo->bIsFinalResolve = true;
							ItWeightmapNeedingResolve.RemoveCurrent();
						}

						ComponentsResolvedInBatch.Add(RenderedComponent);
					}
				}
			}
		}

		if (!WeightmapResolveInfosForBatch.IsEmpty())
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(FinalizeWeightmaps);
			RHI_BREADCRUMB_EVENT_GAMETHREAD("FinalizeWeightmaps");

			check(MinWeightmapResolution == MaxWeightmapResolution);

			ENQUEUE_RENDER_COMMAND(LandscapeLayers_Cmd_WeightmapsPackWeightmap)(
				[ SourceResource = ReadRT->GetRenderTarget2DArray()->GetResource()
				, WeightmapResolveInfosForBatch
				, WeightmapResolution = MinWeightmapResolution
				, NumMips = (int32)FMath::CeilLogTwo(Landscape->SubsectionSizeQuads) + 1
				, TotalNumSubsections = Landscape->NumSubsections * Landscape->NumSubsections
				, NumSubsections = Landscape->NumSubsections
				, ComponentSubsectionVerts = Landscape->SubsectionSizeQuads + 1 ]
				(FRHICommandListImmediate& InRHICmdList)
			{
				FRDGBuilder GraphBuilder(InRHICmdList, RDG_EVENT_NAME("WeightmapsFinalizeWeightmaps"));

				FRDGTextureSRVRef BlackDummySRVRef = GraphBuilder.CreateSRV(FRDGTextureSRVDesc::Create(GSystemTextures.GetBlackDummy(GraphBuilder)));
				FRDGTextureRef SourceTextureRef = GraphBuilder.RegisterExternalTexture(CreateRenderTarget(SourceResource->TextureRHI, TEXT("SourceTexture")));
				FRDGTextureSRVRef SourceTextureSRVRef = GraphBuilder.CreateSRV(FRDGTextureSRVDesc::Create(SourceTextureRef));
				FRDGTextureDesc Desc = FRDGTextureDesc::Create2D(WeightmapResolution, PF_B8G8R8A8, FClearValueBinding::Black, TexCreate_RenderTargetable | TexCreate_ShaderResource, NumMips);
				FRDGTextureRef PackedTextureRef = GraphBuilder.CreateTexture(Desc, TEXT("PackedWeightmap"));

				for (auto It : WeightmapResolveInfosForBatch)
				{
					const FWeightmapResolveInfo& WeightmapResolveInfo = It.Value;
					FRDGTextureRef DestinationTextureRef = GraphBuilder.RegisterExternalTexture(CreateRenderTarget(WeightmapResolveInfo.TextureResource->TextureRHI, TEXT("DestinationTexture")));

					RDG_EVENT_SCOPE(GraphBuilder, "%sFinalize %s", WeightmapResolveInfo.bIsFinalResolve ? TEXT("") : TEXT("(partially)"), *WeightmapResolveInfo.TextureResourceDebugName);
					{
						RDG_EVENT_SCOPE(GraphBuilder, "Pack %d channels %s", FMath::CountBits(WeightmapResolveInfo.ChannelMask), WeightmapResolveInfo.bIsAdditiveResolve ? TEXT("(additive)") : TEXT(""));

						// If the resolve is additive, it means we need to use the destination texture as an input to the pack operation, because we've stored the previous (partial) resolve in it :
						FRDGTextureSRVRef WeightmapBeingPackedSRVRef = WeightmapResolveInfo.bIsAdditiveResolve ? GraphBuilder.CreateSRV(FRDGTextureSRVDesc::Create(DestinationTextureRef)) : BlackDummySRVRef;

						// Operate sub-section by sub-section in order to both pack the 4 channels and duplicate borders :
						for (int32 SubSectionIndex = 0; SubSectionIndex < TotalNumSubsections; ++SubSectionIndex)
						{
							FIntPoint SubSection(SubSectionIndex % NumSubsections, SubSectionIndex / NumSubsections);
							const FIntRect OutputRect = FIntRect(SubSection * ComponentSubsectionVerts, SubSection * ComponentSubsectionVerts + FIntPoint(ComponentSubsectionVerts, ComponentSubsectionVerts));

							FLandscapeEditLayersWeightmapsPackWeightmapPS::FParameters* PSParams = GraphBuilder.AllocParameters<FLandscapeEditLayersWeightmapsPackWeightmapPS::FParameters>();
							PSParams->RenderTargets[0] = FRenderTargetBinding(PackedTextureRef, ERenderTargetLoadAction::ENoAction);
							PSParams->InSourceSliceIndices = WeightmapResolveInfo.SourceSliceIndexPerChannel;
							for (int32 ChannelIndex = 0; ChannelIndex < 4; ++ChannelIndex)
							{
								if ((WeightmapResolveInfo.SourceSliceIndexPerChannel[ChannelIndex] != INDEX_NONE))
								{
									const FIntRect& SourceSubSectionRect = WeightmapResolveInfo.SourceSubsectionRectsPerChannel[ChannelIndex][SubSectionIndex];
									PSParams->InSourcePixelOffsets[ChannelIndex] = FUintVector4(SourceSubSectionRect.Min.X, SourceSubSectionRect.Min.Y, 0, 0);
								}
								else
								{
									PSParams->InSourcePixelOffsets[ChannelIndex] = FUintVector4(0, 0, 0, 0);
								}
							}
							PSParams->InSubsectionPixelOffset = FUintVector2(OutputRect.Min.X, OutputRect.Min.Y);
							PSParams->InIsAdditive = WeightmapResolveInfo.bIsAdditiveResolve ? 1 : 0;
							PSParams->InSourceWeightmaps = SourceTextureSRVRef;
							PSParams->InWeightmapBeingPacked = WeightmapBeingPackedSRVRef;
							FLandscapeEditLayersWeightmapsPackWeightmapPS::PackWeightmapPS(GraphBuilder, PSParams, OutputRect);
						}
					}

					if (WeightmapResolveInfo.bIsFinalResolve)
					{
						// Only generate the mips if it's the final resolve : 
						if (NumMips > 1)
						{
							RDG_EVENT_SCOPE(GraphBuilder, "Generate %d remaining mips", NumMips - 1);

							FIntPoint CurrentMipSize = WeightmapResolution;
							for (int32 MipLevel = 1; MipLevel < NumMips; ++MipLevel)
							{
								CurrentMipSize.X >>= 1;
								CurrentMipSize.Y >>= 1;

								// Read from scratch weightmap texture (mip N - 1) -> write to scratch weightmap texture (mip N) :
								FLandscapeEditLayersWeightmapsGenerateMipsPS::FParameters* PSParams = GraphBuilder.AllocParameters<FLandscapeEditLayersWeightmapsGenerateMipsPS::FParameters>();
								PSParams->RenderTargets[0] = FRenderTargetBinding(PackedTextureRef, ERenderTargetLoadAction::ENoAction, static_cast<uint8>(MipLevel));
								PSParams->InCurrentMipSubsectionSize = FUintVector2(CurrentMipSize.X / NumSubsections, CurrentMipSize.Y / NumSubsections);
								PSParams->InSourceWeightmap = GraphBuilder.CreateSRV(FRDGTextureSRVDesc::CreateForMipLevel(PackedTextureRef, MipLevel - 1));

								FLandscapeEditLayersWeightmapsGenerateMipsPS::GenerateMipsPS(GraphBuilder, PSParams, CurrentMipSize);
							}
						}
					}

					{
						// We use the final texture as a temporary buffer when it's a partial resolve, so we only need to copy mip 0 then. All mips will be generated/copy when the resolve step is final :
						const int32 NumMipsToCopy = WeightmapResolveInfo.bIsFinalResolve ? NumMips : 1;
						RDG_EVENT_SCOPE(GraphBuilder, "Copy %d mips", NumMips);
						for (int32 MipLevel = 0; MipLevel < NumMips; ++MipLevel)
						{
							FRHICopyTextureInfo CopyInfo;
							CopyInfo.SourceMipIndex = MipLevel;
							CopyInfo.DestMipIndex = MipLevel;
							AddCopyTexturePass(GraphBuilder, PackedTextureRef, DestinationTextureRef, CopyInfo);
						}
					}
				}

				GraphBuilder.Execute();
			});
		}

		// Remember all components resolved this batch so that we don't have to resolve them ever again : 
		ResolvedLandscapeComponents.Append(ComponentsResolvedInBatch);
	};

	// Render everything now. Every time a group from a batch is done, the OnRenderBatchGroupDone callback is called : 
	MergeRenderContext.Render(OnRenderBatchGroupDone);

	check(ResolvedLandscapeComponents.Intersect(TSet<ULandscapeComponent*>(FinalComponentsToResolve)).Num() == ResolvedLandscapeComponents.Num());
	check(TexturesNeedingResolve.IsEmpty());

	// Prepare the UTexture2D readbacks we'll need to perform :
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(CopyToReadback);
		RHI_BREADCRUMB_EVENT_GAMETHREAD("CopyToReadback");

		TArray<FLandscapeLayersCopyReadbackTextureParams> DeferredCopyReadbackTextures = PrepareLandscapeLayersCopyReadbackTextureParams(InUpdateLayersContentContext.MapHelper, InUpdateLayersContentContext.WeightmapsToResolve.Array(), /*bWeightmaps = */true);
		ExecuteCopyToReadbackTexture(DeferredCopyReadbackTextures);
	}

	// Finally, update the material instances to take into account potentially new material combinations : 
	UpdateLayersMaterialInstances(InUpdateLayersContentContext.LandscapeComponentsWeightmapsToResolve);

	return InMergeParams.WeightmapUpdateModes;
}



int32 ALandscape::RegenerateLayersWeightmaps(FUpdateLayersContentContext& InUpdateLayersContentContext)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(LandscapeLayers_RegenerateLayersWeightmaps);
	const int32 WeightmapUpdateModes = LayerContentUpdateModes & ELandscapeLayerUpdateMode::Update_Weightmap_Types;
	const bool bSkipBrush = CVarLandscapeLayerBrushOptim.GetValueOnAnyThread() == 1 && (WeightmapUpdateModes == ELandscapeLayerUpdateMode::Update_Weightmap_Editing);
	const bool bForceRender = CVarForceLayersUpdate.GetValueOnAnyThread() != 0;

	ULandscapeInfo* Info = GetLandscapeInfo();

	if (WeightmapUpdateModes == 0 && !bForceRender)
	{
		return 0;
	}

	if (InUpdateLayersContentContext.LandscapeComponentsWeightmapsToResolve.Num() == 0 || Info == nullptr)
	{
		return WeightmapUpdateModes;
	}

	if (WeightmapUpdateModes || bForceRender)
	{
		RenderCaptureInterface::FScopedCapture RenderCapture((RenderCaptureLayersNextWeightmapDraws != 0), TEXT("LandscapeLayersWeightmapCapture"));
		RenderCaptureLayersNextWeightmapDraws = FMath::Max(0, RenderCaptureLayersNextWeightmapDraws - 1);

		FEditLayersWeightmapMergeParams MergeParams;
		MergeParams.WeightmapUpdateModes = WeightmapUpdateModes;
		MergeParams.bForceRender = bForceRender;
		MergeParams.bSkipBrush = bSkipBrush;

		switch (CurrentEditLayersMergeMode)
		{
		case ELandscapeEditLayersMergeMode::GlobalMerge:
		{
			return PerformLayersWeightmapsGlobalMerge(InUpdateLayersContentContext, MergeParams);
		}
		case ELandscapeEditLayersMergeMode::LocalMerge:
		{
			return PerformLayersWeightmapsLocalMerge(InUpdateLayersContentContext, MergeParams);
		}
		case ELandscapeEditLayersMergeMode::BatchedMerge:
		{
			return PerformLayersWeightmapsBatchedMerge(InUpdateLayersContentContext, MergeParams);
		}
		default:
			check(false);
		}
	}

	return 0;
}

void ALandscape::UpdateForChangedWeightmaps(const TArrayView<FLandscapeEditLayerComponentReadbackResult>& InComponentReadbackResults)
{
	TArray<ULandscapeComponent*> ComponentsNeedingMaterialInstanceUpdates;

	for (const FLandscapeEditLayerComponentReadbackResult& ComponentReadbackResult : InComponentReadbackResults)
	{
		// If the source data has changed, mark the component as needing a collision layer data update:
		//  - If ELandscapeComponentUpdateFlag::Component_Update_Weightmap_Collision is passed, it will be done immediately
		//  - If not, at least the component's collision layer data will still get updated eventually, when the flag is finally passed :
		if (ComponentReadbackResult.bModified)
		{
			ComponentReadbackResult.LandscapeComponent->SetPendingLayerCollisionDataUpdate(true);
		}

		// If this component has a layer with only zeros, remove it so that we don't end up with weightmaps we don't end up using :
		if (!ComponentReadbackResult.AllZeroLayers.IsEmpty())
		{
			const TArray<FWeightmapLayerAllocationInfo>& ComponentWeightmapLayerAllocations = ComponentReadbackResult.LandscapeComponent->GetWeightmapLayerAllocations(FGuid());
			for (ULandscapeLayerInfoObject* AllZeroLayerInfo : ComponentReadbackResult.AllZeroLayers)
			{
				check(AllZeroLayerInfo != nullptr);
				// Find the index for this layer in this component.
				int32 AllZeroLayerIndex = ComponentWeightmapLayerAllocations.IndexOfByPredicate(
					[AllZeroLayerInfo](const FWeightmapLayerAllocationInfo& Allocation) { return Allocation.LayerInfo == AllZeroLayerInfo; });
				check(AllZeroLayerIndex != INDEX_NONE);

				ComponentReadbackResult.LandscapeComponent->DeleteLayerAllocation(FGuid(), AllZeroLayerIndex, /*bShouldDirtyPackage = */true);

				// We removed a weightmap allocation so the material instance for this landscape component needs updating :
				ComponentsNeedingMaterialInstanceUpdates.Add(ComponentReadbackResult.LandscapeComponent);
			}
		}

		const int32 WeightUpdateMode = ComponentReadbackResult.UpdateModes & (ELandscapeLayerUpdateMode::Update_Weightmap_All | ELandscapeLayerUpdateMode::Update_Weightmap_Editing | ELandscapeLayerUpdateMode::Update_Weightmap_Editing_NoCollision);
		if (IsUpdateFlagEnabledForModes(ELandscapeComponentUpdateFlag::Component_Update_Weightmap_Collision, WeightUpdateMode))
		{
			// Only update collision data if there was an actual change performed on the source data : 
			if (ComponentReadbackResult.LandscapeComponent->GetPendingLayerCollisionDataUpdate())
			{
				ComponentReadbackResult.LandscapeComponent->UpdateCollisionLayerData();
				ComponentReadbackResult.LandscapeComponent->SetPendingLayerCollisionDataUpdate(false);
			}
		}
	}

	UpdateLayersMaterialInstances(ComponentsNeedingMaterialInstanceUpdates);
}

void ULandscapeComponent::GetUsedPaintLayers(const FGuid& InLayerGuid, TArray<ULandscapeLayerInfoObject*>& OutUsedLayerInfos) const
{
	const TArray<FWeightmapLayerAllocationInfo>& AllocInfos = GetWeightmapLayerAllocations(InLayerGuid);
	for (const FWeightmapLayerAllocationInfo& AllocInfo : AllocInfos)
	{
		if (AllocInfo.LayerInfo != nullptr)
		{
			OutUsedLayerInfos.AddUnique(AllocInfo.LayerInfo);
		}
	}
}

uint32 ULandscapeComponent::ComputeWeightmapsHash()
{
	uint32 Hash = 0;
	const TArray<FWeightmapLayerAllocationInfo>& ComponentWeightmapAllocations = GetWeightmapLayerAllocations();
	for (const FWeightmapLayerAllocationInfo& AllocationInfo : ComponentWeightmapAllocations)
	{
		Hash = HashCombine(AllocationInfo.GetHash(), Hash);
	}

	const TArray<UTexture2D*>& ComponentWeightmapTextures = GetWeightmapTextures();
	const TArray<ULandscapeWeightmapUsage*>& ComponentWeightmapTextureUsage = GetWeightmapTexturesUsage();
	for (int32 i = 0; i < ComponentWeightmapTextures.Num(); ++i)
	{
		Hash = PointerHash(ComponentWeightmapTextures[i], Hash);
		Hash = PointerHash(ComponentWeightmapTextureUsage[i], Hash);
		for (int32 j = 0; j < ULandscapeWeightmapUsage::NumChannels; ++j)
		{
			Hash = PointerHash(ComponentWeightmapTextureUsage[i]->ChannelUsage[j], Hash);
		}
	}
	return Hash;
}

void ALandscape::UpdateLayersMaterialInstances(const TArray<ULandscapeComponent*>& InLandscapeComponents)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(LandscapeLayers_UpdateLayersMaterialInstances);
	TArray<ULandscapeComponent*> ComponentsToUpdate;

	// Compute Weightmap usage changes
	if (ULandscapeInfo* Info = GetLandscapeInfo())
	{
		for (ULandscapeComponent* LandscapeComponent : InLandscapeComponents)
		{
			uint32 NewHash = LandscapeComponent->ComputeWeightmapsHash();
			if (LandscapeComponent->WeightmapsHash != NewHash)
			{
				ComponentsToUpdate.Add(LandscapeComponent);
				LandscapeComponent->WeightmapsHash = NewHash;
			}
		}
	}

	if (ComponentsToUpdate.Num() == 0)
	{
		return;
	}

	// we're not having the material update context recreate render states because we will manually do it for only our components
	TArray<FComponentRecreateRenderStateContext> RecreateRenderStateContexts;
	RecreateRenderStateContexts.Reserve(ComponentsToUpdate.Num());

	for (ULandscapeComponent* Component : ComponentsToUpdate)
	{
		RecreateRenderStateContexts.Emplace(Component);
	}
	TOptional<FMaterialUpdateContext> MaterialUpdateContext;
	MaterialUpdateContext.Emplace(FMaterialUpdateContext::EOptions::Default & ~FMaterialUpdateContext::EOptions::RecreateRenderStates);

	bool bHasUniformExpressionUpdatePending = false;

	for (ULandscapeComponent* Component : ComponentsToUpdate)
	{
		int32 MaxLOD = FMath::CeilLogTwo(SubsectionSizeQuads + 1) - 1;
		decltype(Component->MaterialPerLOD) NewMaterialPerLOD;
		Component->LODIndexToMaterialIndex.SetNumUninitialized(MaxLOD + 1);
		int8 LastLODIndex = INDEX_NONE;

		UMaterialInterface* BaseMaterial = Component->GetLandscapeMaterial();
		UMaterialInterface* LOD0Material = Component->GetLandscapeMaterial(0);

		for (int32 LODIndex = 0; LODIndex <= MaxLOD; ++LODIndex)
		{
			UMaterialInterface* CurrentMaterial = Component->GetLandscapeMaterial(static_cast<int8>(LODIndex));

			// if we have a LOD0 override, do not let the base material override it, it should override everything!
			if (CurrentMaterial == BaseMaterial && BaseMaterial != LOD0Material)
			{
				CurrentMaterial = LOD0Material;
			}

			const int8* MaterialLOD = NewMaterialPerLOD.Find(CurrentMaterial);

			if (MaterialLOD != nullptr)
			{
				Component->LODIndexToMaterialIndex[LODIndex] = *MaterialLOD > LastLODIndex ? *MaterialLOD : LastLODIndex;
			}
			else
			{
				int32 AddedIndex = NewMaterialPerLOD.Num();
				NewMaterialPerLOD.Add(CurrentMaterial, static_cast<int8>(LODIndex));
				Component->LODIndexToMaterialIndex[LODIndex] = static_cast<int8>(AddedIndex);
				LastLODIndex = static_cast<int8>(AddedIndex);
			}
		}

		Component->MaterialPerLOD = NewMaterialPerLOD;

		Component->MaterialInstances.SetNumZeroed(Component->MaterialPerLOD.Num()); 
		int8 MaterialIndex = 0;

		const TArray<FWeightmapLayerAllocationInfo>& WeightmapBaseLayerAllocation = Component->GetWeightmapLayerAllocations();

		const TArray<UTexture2D*>& ComponentWeightmapTextures = Component->GetWeightmapTextures();

		for (auto& ItPair : Component->MaterialPerLOD)
		{
			const int8 MaterialLOD = ItPair.Value;

			// Find or set a matching MIC in the Landscape's map.
			UMaterialInstanceConstant* CombinationMaterialInstance = Component->GetCombinationMaterial(&MaterialUpdateContext.GetValue(), WeightmapBaseLayerAllocation, MaterialLOD, false);

			if (CombinationMaterialInstance != nullptr)
			{
				UMaterialInstanceConstant* MaterialInstance = Component->MaterialInstances[MaterialIndex];
				bool NeedToCreateMIC = MaterialInstance == nullptr;

				if (NeedToCreateMIC)
				{
					// Create the instance for this component, that will use the layer combination instance.
					MaterialInstance = NewObject<ULandscapeMaterialInstanceConstant>(this);
					Component->MaterialInstances[MaterialIndex] = MaterialInstance;
				}

				MaterialInstance->SetParentEditorOnly(CombinationMaterialInstance);

				MaterialUpdateContext.GetValue().AddMaterialInstance(MaterialInstance); // must be done after SetParent				

				FLinearColor Masks[4] = { FLinearColor(1.0f, 0.0f, 0.0f, 0.0f), FLinearColor(0.0f, 1.0f, 0.0f, 0.0f), FLinearColor(0.0f, 0.0f, 1.0f, 0.0f), FLinearColor(0.0f, 0.0f, 0.0f, 1.0f) };

				// Set the layer mask
				for (int32 AllocIdx = 0; AllocIdx < WeightmapBaseLayerAllocation.Num(); AllocIdx++)
				{
					const FWeightmapLayerAllocationInfo& Allocation = WeightmapBaseLayerAllocation[AllocIdx];
					MaterialInstance->SetVectorParameterValueEditorOnly(FName(*FString::Printf(TEXT("LayerMask_%s"), *Allocation.GetLayerName().ToString())), Masks[Allocation.WeightmapTextureChannel]);
				}

				// Set the weightmaps
				for (int32 i = 0; i < ComponentWeightmapTextures.Num(); i++)
				{
					MaterialInstance->SetTextureParameterValueEditorOnly(FName(*FString::Printf(TEXT("Weightmap%d"), i)), ComponentWeightmapTextures[i]);
				}

				if (NeedToCreateMIC)
				{
					MaterialInstance->PostEditChange();
				}
				else
				{
					bHasUniformExpressionUpdatePending = true;
					MaterialInstance->RecacheUniformExpressions(true);
				}
			}

			++MaterialIndex;
		}

		if (Component->MaterialPerLOD.Num() == 0)
		{
			Component->MaterialInstances.Empty(1);
			Component->MaterialInstances.Add(nullptr);
			Component->LODIndexToMaterialIndex.Empty(1);
			Component->LODIndexToMaterialIndex.Add(0);
		}

		Component->EditToolRenderData.UpdateDebugColorMaterial(Component);
	}

	// End material update
	MaterialUpdateContext.Reset();

	// Recreate the render state for our components, needed to update the static drawlist which has cached the MaterialRenderProxies
	// Must be after the FMaterialUpdateContext is destroyed
	RecreateRenderStateContexts.Empty();

	if (bHasUniformExpressionUpdatePending)
	{
		ENQUEUE_RENDER_COMMAND(LandscapeLayers_Cmd_UpdateMaterial)(
			[](FRHICommandList& RHICmdList)
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(LandscapeLayers_RT_UpdateMaterial);
			FMaterialRenderProxy::UpdateDeferredCachedUniformExpressions();
		});
	}
}

void ALandscape::ResolveLayersWeightmapTexture(
	FTextureToComponentHelper const& MapHelper,
	TSet<UTexture2D*> const& WeightmapsToResolve,
	bool bIntermediateRender,
	TArray<FLandscapeEditLayerComponentReadbackResult>& InOutComponentReadbackResults)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(LandscapeLayers_ResolveLayersWeightmapTexture);

	ULandscapeInfo* Info = GetLandscapeInfo();
	if (Info == nullptr)
	{
		return;
	}

	TArray<ULandscapeComponent*> ChangedComponents;
	for (UTexture2D* Weightmap : WeightmapsToResolve)
	{
		ALandscapeProxy* LandscapeProxy = Weightmap->GetTypedOuter<ALandscapeProxy>();
		check(LandscapeProxy);
		if (FLandscapeEditLayerReadback** CPUReadback = LandscapeProxy->WeightmapsCPUReadback.Find(Weightmap))
		{
			const bool bChanged = ResolveLayersTexture(MapHelper, *CPUReadback, Weightmap, bIntermediateRender, InOutComponentReadbackResults, /*bIsWeightmap = */true);
			if (bChanged)
			{
				ChangedComponents.Append(MapHelper.WeightmapToComponents[Weightmap]);
			}
		}
	}

	// Weightmaps shouldn't invalidate lighting
	const bool bInvalidateLightingCache = false;
	InvalidateGeneratedComponentData(ChangedComponents, bInvalidateLightingCache);
}

bool ALandscape::HasLayersContent() const
{
	return LandscapeEditLayers.Num() > 0;
}

void ALandscape::UpdateCachedHasLayersContent(bool bInCheckComponentDataIntegrity)
{
	Super::UpdateCachedHasLayersContent(bInCheckComponentDataIntegrity);

	// For consistency with the ALandscape::HasLayersContent() override above, make sure the cached bHasLayersContent boolean is also valid when we have at least one edit layer : 
	//  Otherwise, as ALandscapeProxy::UpdateCachedHasLayersContent relies on the presence of landscape components and in distributed landscape setups (one ALandscape + multiple ALandscapeStreamingProxy),  
	//  the "parent" ALandscape actor doesn't have any landscape component, hence it would have bHasLayersContent erroneously set to false (while ALandscape::HasLayersContent() would actually return true!)
	bHasLayersContent |= ALandscape::HasLayersContent();
}

void ALandscape::RequestLayersInitialization(bool bInRequestContentUpdate)
{
	if (!CanHaveLayersContent())
	{
		return;
	}

	bLandscapeLayersAreInitialized = false;
	LandscapeSplinesAffectedComponents.Empty();

	if (bInRequestContentUpdate)
	{
		RequestLayersContentUpdateForceAll();
	}
}

void ALandscape::RequestSplineLayerUpdate()
{
	if (HasLayersContent() && FindLayerOfType(ULandscapeEditLayerSplines::StaticClass()) != nullptr)
	{
		bSplineLayerUpdateRequested = true;
	}
}

void ALandscape::RequestLayersContentUpdate(ELandscapeLayerUpdateMode InUpdateMode)
{
	LayerContentUpdateModes |= InUpdateMode;
}

void ALandscape::RequestLayersContentUpdateForceAll(ELandscapeLayerUpdateMode InModeMask, bool bInUserTriggered)
{
	// Ignore Update requests while in PostLoad (to avoid dirtying package on load)
	if (FUObjectThreadContext::Get().IsRoutingPostLoad)
	{
		return;
	}

	if (!CanHaveLayersContent())
	{
		return;
	}

	const bool bUpdateWeightmap = (InModeMask & (ELandscapeLayerUpdateMode::Update_Weightmap_All | ELandscapeLayerUpdateMode::Update_Weightmap_Editing | ELandscapeLayerUpdateMode::Update_Weightmap_Editing_NoCollision)) != 0;
	const bool bUpdateHeightmap = (InModeMask & (ELandscapeLayerUpdateMode::Update_Heightmap_All | ELandscapeLayerUpdateMode::Update_Heightmap_Editing | ELandscapeLayerUpdateMode::Update_Heightmap_Editing_NoCollision)) != 0;
	const bool bUpdateWeightCollision = (InModeMask & (ELandscapeLayerUpdateMode::Update_Weightmap_All | ELandscapeLayerUpdateMode::Update_Weightmap_Editing)) != 0;
	const bool bUpdateHeightCollision = (InModeMask & (ELandscapeLayerUpdateMode::Update_Heightmap_All | ELandscapeLayerUpdateMode::Update_Heightmap_Editing)) != 0;
	const bool bUpdateAllHeightmap = (InModeMask & ELandscapeLayerUpdateMode::Update_Heightmap_All) != 0;
	const bool bUpdateAllWeightmap = (InModeMask & ELandscapeLayerUpdateMode::Update_Weightmap_All) != 0;
	const bool bUpdateClientUdpateEditing = (InModeMask & ELandscapeLayerUpdateMode::Update_Client_Editing) != 0;
	if (ULandscapeInfo* LandscapeInfo = GetLandscapeInfo())
	{
		LandscapeInfo->ForEachLandscapeProxy([bUpdateHeightmap, bUpdateWeightmap, bUpdateAllHeightmap, bUpdateAllWeightmap, bUpdateHeightCollision, bUpdateWeightCollision, bUpdateClientUdpateEditing, bInUserTriggered](ALandscapeProxy* Proxy)
		{
			if (Proxy)
			{
				for (ULandscapeComponent* Component : Proxy->LandscapeComponents)
				{
					if (bUpdateHeightmap)
					{
						Component->RequestHeightmapUpdate(bUpdateAllHeightmap, bUpdateHeightCollision, bInUserTriggered);
					}

					if (bUpdateWeightmap)
					{
						Component->RequestWeightmapUpdate(bUpdateAllWeightmap, bUpdateWeightCollision, bInUserTriggered);
					}

					if (bUpdateClientUdpateEditing)
					{
						Component->RequestEditingClientUpdate(bInUserTriggered);
					}
				}
			}
			return true;
		});
	}

	RequestLayersContentUpdate(InModeMask);
}

bool ALandscape::IsUpdateFlagEnabledForModes(ELandscapeComponentUpdateFlag InFlag, uint32 InUpdateModes)
{
	if (InUpdateModes & ELandscapeLayerUpdateMode::Update_Heightmap_All)
	{
		const uint32 HeightmapAllFlags = ELandscapeComponentUpdateFlag::Component_Update_Heightmap_Collision | ELandscapeComponentUpdateFlag::Component_Update_Recreate_Collision | ELandscapeComponentUpdateFlag::Component_Update_Client;
		if (HeightmapAllFlags & InFlag)
		{
			return true;
		}
	}

	if (InUpdateModes & ELandscapeLayerUpdateMode::Update_Heightmap_Editing)
	{
		const uint32 HeightmapEditingFlags = ELandscapeComponentUpdateFlag::Component_Update_Heightmap_Collision | ELandscapeComponentUpdateFlag::Component_Update_Client_Editing;
		if (HeightmapEditingFlags & InFlag)
		{
			return true;
		}
	}

	if (InUpdateModes & ELandscapeLayerUpdateMode::Update_Weightmap_All)
	{
		const uint32 WeightmapAllFlags = ELandscapeComponentUpdateFlag::Component_Update_Weightmap_Collision | ELandscapeComponentUpdateFlag::Component_Update_Recreate_Collision | ELandscapeComponentUpdateFlag::Component_Update_Client;
		if (WeightmapAllFlags & InFlag)
		{
			return true;
		}
	}

	if (InUpdateModes & ELandscapeLayerUpdateMode::Update_Weightmap_Editing)
	{
		const uint32 WeightmapEditingFlags = ELandscapeComponentUpdateFlag::Component_Update_Weightmap_Collision | ELandscapeComponentUpdateFlag::Component_Update_Client_Editing;
		if (WeightmapEditingFlags & InFlag)
		{
			return true;
		}
	}

	if (InUpdateModes & ELandscapeLayerUpdateMode::Update_Client_Editing)
	{
		const uint32 WeightmapEditingFlags = ELandscapeComponentUpdateFlag::Component_Update_Client_Editing;
		if (WeightmapEditingFlags & InFlag)
		{
			return true;
		}
	}

	if (InUpdateModes & ELandscapeLayerUpdateMode::Update_Client_Deferred)
	{
		const uint32 DeferredClientUpdateFlags = ELandscapeComponentUpdateFlag::Component_Update_Client;
		if (DeferredClientUpdateFlags & InFlag)
		{
			return true;
		}
	}

	if (InUpdateModes & (ELandscapeLayerUpdateMode::Update_Heightmap_Editing_NoCollision | ELandscapeLayerUpdateMode::Update_Weightmap_Editing_NoCollision))
	{
		const uint32 EditingNoCollisionFlags = ELandscapeComponentUpdateFlag::Component_Update_Approximated_Bounds;
		if (EditingNoCollisionFlags & InFlag)
		{
			return true;
		}
	}

	return false;
}

void ULandscapeComponent::ClearUpdateFlagsForModes(uint32 InModeMask)
{
	LayerUpdateFlagPerMode &= ~InModeMask;
}

void ULandscapeComponent::RequestDeferredClientUpdate()
{
	LayerUpdateFlagPerMode |= ELandscapeLayerUpdateMode::Update_Client_Deferred;
}

void ULandscapeComponent::RequestEditingClientUpdate(bool bInUserTriggered)
{
	bUserTriggeredChangeRequested = bInUserTriggered; 
	
	LayerUpdateFlagPerMode |= ELandscapeLayerUpdateMode::Update_Client_Editing;
	if (ALandscape* LandscapeActor = GetLandscapeActor())
	{
		LandscapeActor->RequestLayersContentUpdate(ELandscapeLayerUpdateMode::Update_Client_Editing);
	}
}

void ULandscapeComponent::RequestHeightmapUpdate(bool bUpdateAll, bool bUpdateCollision, bool bInUserTriggered)
{
	bUserTriggeredChangeRequested = bInUserTriggered;
	if (bUpdateAll || bUpdateCollision)
	{
		LayerUpdateFlagPerMode |= ELandscapeLayerUpdateMode::Update_Heightmap_Editing;
		LayerUpdateFlagPerMode |= ELandscapeLayerUpdateMode::Update_Heightmap_All;
	}
	else
	{
		LayerUpdateFlagPerMode |= ELandscapeLayerUpdateMode::Update_Heightmap_Editing_NoCollision;
	}
	if (ALandscape* LandscapeActor = GetLandscapeActor())
	{
		LandscapeActor->RequestLayersContentUpdate(bUpdateCollision ? ELandscapeLayerUpdateMode::Update_Heightmap_Editing : ELandscapeLayerUpdateMode::Update_Heightmap_Editing_NoCollision);
		if (bUpdateAll)
		{
			LandscapeActor->RequestLayersContentUpdate(ELandscapeLayerUpdateMode::Update_Heightmap_All);
		}
	}
}

void ULandscapeComponent::RequestWeightmapUpdate(bool bUpdateAll, bool bUpdateCollision, bool bInUserTriggered)
{
	bUserTriggeredChangeRequested = bInUserTriggered;
	
	if (bUpdateAll || bUpdateCollision)
	{
		LayerUpdateFlagPerMode |= ELandscapeLayerUpdateMode::Update_Weightmap_Editing;
		LayerUpdateFlagPerMode |= ELandscapeLayerUpdateMode::Update_Weightmap_All;
	}
	else
	{
		LayerUpdateFlagPerMode |= ELandscapeLayerUpdateMode::Update_Weightmap_Editing_NoCollision;
	}
	if (ALandscape* LandscapeActor = GetLandscapeActor())
	{
		LandscapeActor->RequestLayersContentUpdate(bUpdateCollision ? ELandscapeLayerUpdateMode::Update_Weightmap_Editing : ELandscapeLayerUpdateMode::Update_Weightmap_Editing_NoCollision);
		if (bUpdateAll)
		{
			LandscapeActor->RequestLayersContentUpdate(ELandscapeLayerUpdateMode::Update_Weightmap_All);
		}
	}
}

void ALandscape::MonitorLandscapeEdModeChanges()
{
	bool bRequiredEditingClientFullUpdate = false;
	if (LandscapeEdModeInfo.ViewMode != GLandscapeViewMode)
	{
		LandscapeEdModeInfo.ViewMode = GLandscapeViewMode;
		bRequiredEditingClientFullUpdate = true;
	}

	ELandscapeToolTargetType NewValue = LandscapeEdMode ? LandscapeEdMode->GetLandscapeToolTargetType() : ELandscapeToolTargetType::Invalid;
	if (LandscapeEdModeInfo.ToolTarget != NewValue)
	{
		LandscapeEdModeInfo.ToolTarget = NewValue;
		bRequiredEditingClientFullUpdate = true;
	}

	const FLandscapeLayer* SelectedLayer = LandscapeEdMode ? LandscapeEdMode->GetLandscapeSelectedLayer() : nullptr;
	FGuid NewSelectedLayer = SelectedLayer && SelectedLayer->bVisible ? SelectedLayer->Guid : FGuid();
	if (LandscapeEdModeInfo.SelectedLayer != NewSelectedLayer)
	{
		LandscapeEdModeInfo.SelectedLayer = NewSelectedLayer;
		bRequiredEditingClientFullUpdate = true;
	}

	TWeakObjectPtr<ULandscapeLayerInfoObject> NewLayerInfoObject;
	if (LandscapeEdMode)
	{
		NewLayerInfoObject = LandscapeEdMode->GetSelectedLandscapeLayerInfo();
	}
	if (LandscapeEdModeInfo.SelectedLayerInfoObject != NewLayerInfoObject)
	{
		LandscapeEdModeInfo.SelectedLayerInfoObject = NewLayerInfoObject;
		bRequiredEditingClientFullUpdate = true;
	}

	if (bRequiredEditingClientFullUpdate && (LandscapeEdModeInfo.ViewMode == ELandscapeViewMode::LayerContribution))
	{
		RequestLayersContentUpdateForceAll(ELandscapeLayerUpdateMode::Update_Client_Editing);
	}
}

void ALandscape::MonitorShaderCompilation()
{
	// Do not monitor changes when not editing Landscape
	if (!LandscapeEdMode)
	{
		return;
	}

	// If doing editing while shader are compiling or at load of a map, it's possible we will need another update pass after shader are completed to see the correct result
	const int32 RemainingShadersThisFrame = GShaderCompilingManager->GetNumRemainingJobs();
	if (!WasCompilingShaders && RemainingShadersThisFrame > 0)
	{
		WasCompilingShaders = true;
	}
	else if (WasCompilingShaders)
	{
		WasCompilingShaders = false;
		RequestLayersContentUpdateForceAll();
	}
}

void ULandscapeComponent::GetLandscapeComponentNeighborsToRender(TSet<ULandscapeComponent*>& OutNeighborComponents) const
{
	ULandscapeInfo* LandscapeInfo = GetLandscapeInfo();
	FIntPoint ComponentKey = GetComponentKey();

	for (int32 IndexX = ComponentKey.X - 1; IndexX <= ComponentKey.X + 1; ++IndexX)
	{
		for (int32 IndexY = ComponentKey.Y - 1; IndexY <= ComponentKey.Y + 1; ++IndexY)
		{
			ULandscapeComponent* Result = LandscapeInfo->XYtoComponentMap.FindRef(FIntPoint(IndexX, IndexY));
			if (Result && Result != this)
			{
				OutNeighborComponents.Add(Result);
			}
		}
	}
}

void ULandscapeComponent::GetLandscapeComponentNeighbors3x3(TStaticArray<ULandscapeComponent*, 9>& OutNeighborComponents) const
{
	ULandscapeInfo* LandscapeInfo = GetLandscapeInfo();
	FIntPoint ComponentKey = GetComponentKey();

	int32 LinearIndex = 0;
	for (int32 IndexY = ComponentKey.Y - 1; IndexY <= ComponentKey.Y + 1; ++IndexY)
	{
		for (int32 IndexX = ComponentKey.X - 1; IndexX <= ComponentKey.X + 1; ++IndexX)
		{
			OutNeighborComponents[LinearIndex] = LandscapeInfo->XYtoComponentMap.FindRef(FIntPoint(IndexX, IndexY));
			++LinearIndex;
		}
	}
}

void ULandscapeComponent::GetLandscapeComponentWeightmapsToRender(TSet<ULandscapeComponent*>& OutWeightmapComponents) const
{
	// Fill with Components that share the same weightmaps so that the Resolve of Weightmap Texture doesn't resolve null data.
	for (ULandscapeWeightmapUsage* Usage : GetWeightmapTexturesUsage(/*InReturnEditingWeightmap = */false))
	{
		for (int32 Channel = 0; Channel < ULandscapeWeightmapUsage::NumChannels; ++Channel)
		{
			if (Usage != nullptr && Usage->ChannelUsage[Channel] != nullptr)
			{
				ULandscapeComponent* Component = Usage->ChannelUsage[Channel];
				OutWeightmapComponents.Add(Component);
			}
		}
	}
}

void ALandscape::FWaitingForResourcesNotificationHelper::Notify(ALandscape* InLandscape, FLandscapeNotificationManager* InNotificationManager, ELandscapeNotificationType InNotificationType, const FText& InNotificationText)
{
	// We need to wait until layers texture resources are ready to initialize the landscape to avoid taking the sizes and format of the default texture:
	static constexpr double TimeBeforeDisplayingWaitingForResourcesNotification = 3.0;

	WaitingForResourcesStartTime = FSlateApplicationBase::IsInitialized() ? FSlateApplicationBase::Get().GetCurrentTime() : 0.0f;
	if (!Notification.IsValid())
	{
		Notification = MakeShared<FLandscapeNotification>(InLandscape, InNotificationType);
		Notification->NotificationText = InNotificationText;
		Notification->NotificationStartTime = WaitingForResourcesStartTime + TimeBeforeDisplayingWaitingForResourcesNotification;
	}
	InNotificationManager->RegisterNotification(Notification);
}

void ALandscape::FWaitingForResourcesNotificationHelper::Reset()
{
	Notification.Reset();
	WaitingForResourcesStartTime = -1.0;
}

void ALandscape::UpdateLayersContent(bool bInWaitForStreaming, bool bInSkipMonitorLandscapeEdModeChanges, bool bIntermediateRender, bool bFlushRender)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(LandscapeLayers_UpdateLayersContent);

	bool bHideNotifications = true;
	ON_SCOPE_EXIT
	{
		// Make sure that we don't leave any notification behind when we leave this function without explicitly displaying one :
		if (bHideNotifications)
		{
			WaitingForTexturesNotificationHelper.Reset();
			WaitingForEditLayerResourcesNotificationHelper.Reset();
			InvalidShadingModelNotification.Reset();
		}

		// If nothing to do, let's do some garbage collecting on async readback tasks so that we slowly get rid of staging textures 
		//  (don't do it while waiting for read backs because something might prevent us from updating the readbacks (e.g. waiting for resources to compiling...), which would 
		//  lead to FLandscapeEditReadbackTaskPool's frame count increasing while readback tasks don't have the chance to complete, leading to the "readback leak" warning to incorrectly be triggered) :
		if(IsUpToDate())
		{
			FLandscapeEditLayerReadback::GarbageCollectTasks();
		}
	};

	// Note : no early-out allowed before this : even if not actually updating edit layers, we need to poll our resources in order to make sure we register to streaming events when needed: 
	bool bResourcesReady = PrepareTextureResources(bInWaitForStreaming);

	ULandscapeInfo* LandscapeInfo = GetLandscapeInfo();
	if ((LandscapeInfo == nullptr) || !CanHaveLayersContent() || !LandscapeInfo->AreAllComponentsRegistered() || !FApp::CanEverRender() || !LandscapeInfo->SupportsLandscapeEditing())
	{
		return;
	}

	UWorld* World = GetWorld();
	check(World != nullptr);
	ULandscapeSubsystem* LandscapeSubsystem = World->GetSubsystem<ULandscapeSubsystem>();
	check(LandscapeSubsystem != nullptr);
	FLandscapeNotificationManager* LandscapeNotificationManager = LandscapeSubsystem->GetNotificationManager();

	// Make sure Update doesn't dirty Landscape packages when not in Landscape Ed Mode
	FLandscapeDirtyOnlyInModeScope DirtyOnlyInMode(LandscapeInfo);

	// If we went from local merge to global merge or vice versa, we need to reinitialize layers : 
	ELandscapeEditLayersMergeMode EditLayersMergeMode = GetEditLayersMergeMode();
	// If we toggled normal capture, we need to reinitialize layers
	bool bUseNormalCapture = HasNormalCaptureBPBrushLayer();
	if ((CurrentEditLayersMergeMode != EditLayersMergeMode)
		|| (bLandscapeLayersAreInitializedForNormalCapture != bUseNormalCapture))
	{
		RequestLayersInitialization(/*bInRequestContentUpdate = */true);
		CurrentEditLayersMergeMode = EditLayersMergeMode;
		bLandscapeLayersAreInitializedForNormalCapture = bUseNormalCapture;
	}

	if (!bLandscapeLayersAreInitialized)
	{
		InitializeLayers(bUseNormalCapture);
	}

	if (!bLandscapeLayersAreInitialized)
	{
		// we failed to initialize layers, cannot continue
		return;
	}

	if (!bInSkipMonitorLandscapeEdModeChanges)
	{
		MonitorLandscapeEdModeChanges();
	}
	MonitorShaderCompilation();

	// Make sure Brush get a chance to request an update of the landscape
	for (FLandscapeLayer& Layer : LandscapeEditLayers)
	{
		for (FLandscapeLayerBrush& Brush : Layer.Brushes)
		{
			if (ALandscapeBlueprintBrushBase* LandscapeBrush = Brush.GetBrush())
			{
				LandscapeBrush->PushDeferredLayersContentUpdate();
			}
		}
	}

	// Make sure weightmap usages that need updating are processed before doing any update on the landscape : 
	UpdateProxyLayersWeightmapUsage();

	if (bSplineLayerUpdateRequested)
	{
		if (const FLandscapeLayer* SplinesLayer = FindLayerOfType(ULandscapeEditLayerSplines::StaticClass()))
		{
			// We need the spline layer resources to all be ready before updating it:
			if (!PrepareLayersTextureResources({ *SplinesLayer }, bInWaitForStreaming))
			{
				return;
			}

			UpdateLandscapeSplines();
			bSplineLayerUpdateRequested = false;
		}
	}

	const bool bProcessReadbacks = FLandscapeEditLayerReadback::HasWork();
	const bool bForceRender = CVarForceLayersUpdate.GetValueOnAnyThread() != 0;

	// User triggered change has been completely processed, resetting user triggered flag on all components.
	if(IsUpToDate())
	{
		GetLandscapeInfo()->ForAllLandscapeComponents(
			[this](ULandscapeComponent* Component) -> void
			{
				if (Component->GetUserTriggeredChangeRequested())
				{
					check(Component->GetLayerUpdateFlagPerMode() == 0);
					Component->SetUserTriggeredChangeRequested(/* bInUserTriggered = */false);	
				}
			}
		);

		if (!bForceRender)
		{
			return;
		}
	}

	// The Edit layers shaders only work on SM5 : cancel any update that might happen when SM5+ shading model is not active :
	if (World->GetFeatureLevel() < ERHIFeatureLevel::SM5)
	{
		if (LandscapeNotificationManager)
		{
			if (!InvalidShadingModelNotification.IsValid())
			{
				InvalidShadingModelNotification = MakeShared<FLandscapeNotification>(this, ELandscapeNotificationType::ShadingModelInvalid);
				static const FText NotificationText(LOCTEXT("InvalidShadingModel", "Cannot update landscape with a feature level less than SM5"));
				InvalidShadingModelNotification->NotificationText = NotificationText;
			}
			LandscapeNotificationManager->RegisterNotification(InvalidShadingModelNotification);
			bHideNotifications = false;
		}
		return;
	}
	else
	{
		InvalidShadingModelNotification.Reset();
	}

	bResourcesReady &= PrepareLayersTextureResources(bInWaitForStreaming);
	if (!bResourcesReady && LandscapeNotificationManager)
	{
		WaitingForTexturesNotificationHelper.Notify(this, LandscapeNotificationManager, ELandscapeNotificationType::LandscapeTextureResourcesNotReady, LOCTEXT("WaitForLandscapeTextureResources", "Waiting for texture resources to be ready"));
		bHideNotifications = false;
	}
	else
	{
		WaitingForTexturesNotificationHelper.Reset();
	}

	bResourcesReady &= PrepareLayersResources(World->GetFeatureLevel(), bInWaitForStreaming);
	if (!bResourcesReady && LandscapeNotificationManager)
	{
		WaitingForEditLayerResourcesNotificationHelper.Notify(this, LandscapeNotificationManager, ELandscapeNotificationType::LandscapeEditLayerResourcesNotReady, LOCTEXT("WaitForLandscapeEditLayerResources", "Waiting for edit layer resources to be ready"));
		bHideNotifications = false;
	}
	else
	{
		WaitingForEditLayerResourcesNotificationHelper.Reset();
	}

	if (!bResourcesReady)
	{
		return;
	}

	// Gather mappings between heightmaps/weightmaps and components
	FTextureToComponentHelper MapHelper(*LandscapeInfo);

	// Poll and complete any outstanding resolve work
	// If bIntermediateRender then we want to flush all work here before we do the intermediate render later on
	// if bFlushRender then we skip this because we will flush later anyway
	if (bProcessReadbacks && (bIntermediateRender || !bFlushRender))
	{
		// These flags might look like they're being mixed up but they're not!
		const bool bDoIntermediateRender = false; // bIntermediateRender flag is for the work queued up this frame not the delayed resolves
		const bool bDoFlushRender = bIntermediateRender; // Flush before we do an intermediate render later in this frame

		// Flushing once all readback tasks is much faster than asking each to do it so start by doing just this :
		if (bDoFlushRender)
		{
			FLandscapeEditLayerReadback::FlushAllReadbackTasks();
		}

		TArray<FLandscapeEditLayerComponentReadbackResult> ComponentReadbackResults;
		ResolveLayersHeightmapTexture(MapHelper, MapHelper.Heightmaps, bDoIntermediateRender, ComponentReadbackResults);
		ResolveLayersWeightmapTexture(MapHelper, MapHelper.Weightmaps, bDoIntermediateRender, ComponentReadbackResults);
		LayerContentUpdateModes |= UpdateAfterReadbackResolves(ComponentReadbackResults);
	}

	if (LayerContentUpdateModes == 0 && !bForceRender)
	{
		return;
	}

	bool bUpdateAll = LayerContentUpdateModes & Update_All;
	bool bPartialUpdate = !bForceRender && !bUpdateAll && CVarLandscapeLayerOptim.GetValueOnAnyThread() == 1;

	FUpdateLayersContentContext UpdateLayersContentContext(MapHelper, bPartialUpdate);

	// Regenerate any heightmaps and weightmaps
	int32 ProcessedModes = 0;
	ProcessedModes |= RegenerateLayersHeightmaps(UpdateLayersContentContext);
	ProcessedModes |= RegenerateLayersWeightmaps(UpdateLayersContentContext);
	ProcessedModes |= (LayerContentUpdateModes & ELandscapeLayerUpdateMode::Update_Client_Deferred);
	ProcessedModes |= (LayerContentUpdateModes & ELandscapeLayerUpdateMode::Update_Client_Editing);

	// If we are flushing then read back resolved textures immediately
	if (bFlushRender || CVarLandscapeForceFlush.GetValueOnGameThread() != 0)
	{
		// Flushing once all readback tasks is much faster than asking each to do it so start by doing just this :
		FLandscapeEditLayerReadback::FlushAllReadbackTasks();
		// When flushing, don't bother resolving textures that weren't requested to be updated in the first place. This reduces the workload when doing a flushing intermediate render for a heightmap
		//  tool (smooth/flatten/... tool), for example, by not resolving weightmaps then. 
		//  We cannot do this in the non-flush case above, because LayerContentUpdateModes might have changed since the readbacks have been requested so we still need to perform the readbacks on all textures
		if ((LayerContentUpdateModes & ELandscapeLayerUpdateMode::Update_Heightmap_Types) != 0)
		{
			ResolveLayersHeightmapTexture(UpdateLayersContentContext.MapHelper, UpdateLayersContentContext.HeightmapsToResolve, bIntermediateRender, UpdateLayersContentContext.AllLandscapeComponentReadbackResults);
		}
		if ((LayerContentUpdateModes & ELandscapeLayerUpdateMode::Update_Weightmap_Types) != 0)
		{
			ResolveLayersWeightmapTexture(UpdateLayersContentContext.MapHelper, UpdateLayersContentContext.WeightmapsToResolve, bIntermediateRender, UpdateLayersContentContext.AllLandscapeComponentReadbackResults);
		}
	}

	// Clear processed mode flags
	LayerContentUpdateModes &= ~ProcessedModes;
	for (ULandscapeComponent* Component : UpdateLayersContentContext.AllLandscapeComponentsToResolve)
	{
		Component->ClearUpdateFlagsForModes(ProcessedModes);
	}

	// Apply post resolve updates
	const uint32 ToProcessModes = UpdateAfterReadbackResolves(UpdateLayersContentContext.AllLandscapeComponentReadbackResults);
	LayerContentUpdateModes |= ToProcessModes;
	if (LandscapeEdMode)
	{
		LandscapeEdMode->PostUpdateLayerContent();
	}

	// Additional validation that at the end of an update, we haven't screwed up anything in the weightmap allocations/usages : 
	ValidateProxyLayersWeightmapUsage();
}

// not thread safe
struct FEnableCollisionHashOptimScope
{
	FEnableCollisionHashOptimScope(ULandscapeHeightfieldCollisionComponent* InCollisionComponent)
	{
		CollisionComponent = InCollisionComponent;
		if (CollisionComponent)
		{
			// not reentrant
			check(!CollisionComponent->bEnableCollisionHashOptim);
			CollisionComponent->bEnableCollisionHashOptim = true;
		}
	}

	~FEnableCollisionHashOptimScope()
	{
		if (CollisionComponent)
		{
			CollisionComponent->bEnableCollisionHashOptim = false;
		}
	}

private:
	ULandscapeHeightfieldCollisionComponent* CollisionComponent;
};

uint32 ALandscape::UpdateCollisionAndClients(const TArrayView<FLandscapeEditLayerComponentReadbackResult>& InComponentReadbackResults)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(LandscapeLayers_PostResolve_CollisionAndClients);

	bool bAllClientsUpdated = true;

	const uint16 DefaultHeightValue = LandscapeDataAccess::GetTexHeight(0.f);
	const uint8 MaxLayerContributingValue = UINT8_MAX;
	const float HeightValueNormalizationFactor = 1.f / (0.5f * UINT16_MAX);
	TArray<uint16> HeightData;
	TArray<uint8> LayerContributionMaskData;

	for (const FLandscapeEditLayerComponentReadbackResult& ComponentReadbackResult : InComponentReadbackResults)
	{
		ULandscapeComponent* LandscapeComponent = ComponentReadbackResult.LandscapeComponent;
		
		bool bDeferClientUpdateForComponent = false;
		bool bDoUpdateClient = true;
		if (IsUpdateFlagEnabledForModes(ELandscapeComponentUpdateFlag::Component_Update_Recreate_Collision, ComponentReadbackResult.UpdateModes))
		{
			if (ULandscapeHeightfieldCollisionComponent* CollisionComp = LandscapeComponent->GetCollisionComponent())
			{
				FEnableCollisionHashOptimScope Scope(CollisionComp);
				bDoUpdateClient = CollisionComp->RecreateCollision();
			}
		}

		if (bDoUpdateClient && IsUpdateFlagEnabledForModes(ELandscapeComponentUpdateFlag::Component_Update_Client, ComponentReadbackResult.UpdateModes))
		{
			if (!GUndo)
			{
				if (ULandscapeHeightfieldCollisionComponent* CollisionComp = LandscapeComponent->GetCollisionComponent())
				{
					FNavigationSystem::UpdateComponentData(*CollisionComp);
					CollisionComp->SnapFoliageInstances();
				}
			}
			else
			{
				bDeferClientUpdateForComponent = true;
				bAllClientsUpdated = false;
			}
		}

		if (IsUpdateFlagEnabledForModes(ELandscapeComponentUpdateFlag::Component_Update_Client_Editing, ComponentReadbackResult.UpdateModes))
		{
			if (LandscapeEdModeInfo.ViewMode == ELandscapeViewMode::LayerContribution)
			{
				check(ComponentSizeQuads == LandscapeComponent->ComponentSizeQuads);
				const int32 Stride = (1 + ComponentSizeQuads);
				const int32 ArraySize = Stride * Stride;
				if (LayerContributionMaskData.Num() != ArraySize)
				{
					LayerContributionMaskData.AddZeroed(ArraySize);
				}
				uint8* LayerContributionMaskDataPtr = LayerContributionMaskData.GetData();
				const int32 X1 = LandscapeComponent->GetSectionBase().X;
				const int32 X2 = X1 + ComponentSizeQuads;
				const int32 Y1 = LandscapeComponent->GetSectionBase().Y;
				const int32 Y2 = Y1 + ComponentSizeQuads;
				bool bLayerContributionWrittenData = false;

				ULandscapeInfo* Info = LandscapeComponent->GetLandscapeInfo();
				check(Info);
				FLandscapeEditDataInterface LandscapeEdit(Info);

				if (LandscapeEdModeInfo.SelectedLayer.IsValid())
				{
					FScopedSetLandscapeEditingLayer Scope(this, LandscapeEdModeInfo.SelectedLayer);
					if (LandscapeEdModeInfo.ToolTarget == ELandscapeToolTargetType::Heightmap)
					{
						if (HeightData.Num() != ArraySize)
						{
							HeightData.AddZeroed(ArraySize);
						}
						LandscapeEdit.GetHeightDataFast(X1, Y1, X2, Y2, HeightData.GetData(), Stride);
						for (int i = 0; i < ArraySize; ++i)
						{
							LayerContributionMaskData[i] = HeightData[i] != DefaultHeightValue ? (uint8)(FMath::Pow(FMath::Clamp((HeightValueNormalizationFactor * FMath::Abs(HeightData[i] - DefaultHeightValue)), 0.f, (float)1.f), 0.25f) * MaxLayerContributingValue) : 0;
						}
						bLayerContributionWrittenData = true;
					}
					else if (LandscapeEdModeInfo.ToolTarget == ELandscapeToolTargetType::Weightmap || LandscapeEdModeInfo.ToolTarget == ELandscapeToolTargetType::Visibility)
					{
						ULandscapeLayerInfoObject* LayerObject = (LandscapeEdModeInfo.ToolTarget == ELandscapeToolTargetType::Visibility) ? ALandscapeProxy::VisibilityLayer : LandscapeEdModeInfo.SelectedLayerInfoObject.Get();
						if (LayerObject)
						{
							LandscapeEdit.GetWeightDataFast(LayerObject, X1, Y1, X2, Y2, LayerContributionMaskData.GetData(), Stride);
							bLayerContributionWrittenData = true;
						}
					}
				}
				if (!bLayerContributionWrittenData)
				{
					FMemory::Memzero(LayerContributionMaskDataPtr, ArraySize);
				}
				LandscapeEdit.SetLayerContributionData(X1, Y1, X2, Y2, LayerContributionMaskDataPtr, 0);
			}
		}

		if (bDeferClientUpdateForComponent)
		{
			LandscapeComponent->RequestDeferredClientUpdate();
		}
	}

	// Some clients not updated so return the Deferred flag to trigger processing next update.
	return bAllClientsUpdated ? 0 : ELandscapeLayerUpdateMode::Update_Client_Deferred;
}

uint32 ALandscape::UpdateAfterReadbackResolves(const TArrayView<FLandscapeEditLayerComponentReadbackResult>& InComponentReadbackResults)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(LandscapeLayers_PostResolve_Updates);

	uint32 NewUpdateFlags = 0;

	if (InComponentReadbackResults.Num())
	{
		UpdateForChangedHeightmaps(InComponentReadbackResults);
		UpdateForChangedWeightmaps(InComponentReadbackResults);

		GetLandscapeInfo()->UpdateAllAddCollisions();

		NewUpdateFlags |= UpdateCollisionAndClients(InComponentReadbackResults);
	}

	return NewUpdateFlags;
}

void ALandscape::InitializeLayers(bool bUseNormalCapture)
{
	check(HasLayersContent());

	if (CreateLayersRenderingResource(bUseNormalCapture))
	{
		InitializeLandscapeLayersWeightmapUsage();
		bLandscapeLayersAreInitialized = true;
	}
}

void ALandscape::OnPreSave()
{
	ForceUpdateLayersContent();
}

void ALandscape::ForceUpdateLayersContent(bool bIntermediateRender)
{
	const bool bWaitForStreaming = true;
	const bool bInSkipMonitorLandscapeEdModeChanges = true;
	const bool bFlushRender = true;

	UpdateLayersContent(bWaitForStreaming, bInSkipMonitorLandscapeEdModeChanges, bIntermediateRender, bFlushRender);
}

void ALandscape::ForceLayersFullUpdate()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(ALandscape::ForceLayersFullUpdate);

	FAssetCompilingManager::Get().FinishAllCompilation();

	FStreamingManagerCollection& StreamingManagers = IStreamingManager::Get();
	StreamingManagers.UpdateResourceStreaming(GetWorld()->GetDeltaSeconds(), /* bProcessEverything */ true);
	StreamingManagers.BlockTillAllRequestsFinished();

	RequestSplineLayerUpdate();
	RequestLayersContentUpdateForceAll();
	ForceUpdateLayersContent(/* bIntermediateRender */ false);
}

void ALandscape::TickLayers(float DeltaTime)
{
	check(GIsEditor);

	if (!bEnableEditorLayersTick)
	{
		return;
	}

	UWorld* World = GetWorld();
	if (World && !World->IsPlayInEditor() && GetLandscapeInfo() && GEditor->PlayWorld == nullptr)
	{
		if (CVarLandscapeSimulatePhysics.GetValueOnAnyThread() == 1)
		{
			World->bShouldSimulatePhysics = true;
		}

		UpdateLayersContent();
	}
}

#endif

void ALandscapeProxy::BeginDestroy()
{
#if WITH_EDITORONLY_DATA
	if (CanHaveLayersContent())
	{
		// Note that this fence and the BeginDestroy/FinishDestroy is only actually used by the derived class ALandscape
		// It could be moved there
		ReleaseResourceFence.BeginFence();
	}
#endif

	Super::BeginDestroy();
}

bool ALandscapeProxy::IsReadyForFinishDestroy()
{
	bool bReadyForFinishDestroy = Super::IsReadyForFinishDestroy();

#if WITH_EDITORONLY_DATA
	if (CanHaveLayersContent())
	{
		if (bReadyForFinishDestroy)
		{
			bReadyForFinishDestroy = ReleaseResourceFence.IsFenceComplete();
		}
	}
#endif

	return bReadyForFinishDestroy;
}

void ALandscapeProxy::FinishDestroy()
{
#if WITH_EDITORONLY_DATA
	if (CanHaveLayersContent())
	{
		check(ReleaseResourceFence.IsFenceComplete());

		for (auto& ItPair : HeightmapsCPUReadback)
		{
			FLandscapeEditLayerReadback* HeightmapCPUReadback = ItPair.Value;
			delete HeightmapCPUReadback;
		}
		HeightmapsCPUReadback.Empty();

		for (auto& ItPair : WeightmapsCPUReadback)
		{
			FLandscapeEditLayerReadback* WeightmapCPUReadback = ItPair.Value;
			delete WeightmapCPUReadback;
		}
		WeightmapsCPUReadback.Empty();
	}
#endif

	Super::FinishDestroy();
}

#if WITH_EDITOR
bool ALandscapeProxy::CanHaveLayersContent() const
{
	if (HasAnyFlags(RF_ClassDefaultObject | RF_ArchetypeObject))
	{
		return false;
	}

	if (const ALandscape* LandscapeActor = GetLandscapeActor())
	{
		return LandscapeActor->bCanHaveLayersContent;
	}

	return false;
}

bool ALandscapeProxy::HasLayersContent() const
{
	return bHasLayersContent || (GetLandscapeActor() != nullptr && GetLandscapeActor()->HasLayersContent());
}

void ALandscapeProxy::UpdateCachedHasLayersContent(bool InCheckComponentDataIntegrity)
{
	// In the case of InCheckComponentDataIntegrity we will loop through all components to make sure they all have the same state and in the other case we will assume that the 1st component represent the state of all the others.
	bHasLayersContent = (!LandscapeComponents.IsEmpty() && (LandscapeComponents[0] != nullptr)) ? LandscapeComponents[0]->HasLayersData() : false;

	if (InCheckComponentDataIntegrity)
	{
		for (const ULandscapeComponent* Component : LandscapeComponents)
		{
			check((Component == nullptr) || (bHasLayersContent == Component->HasLayersData()));
		}
	}
}

namespace
{
	bool DeleteUnusedLayersImpl(ULandscapeComponent* InComponent, const FGuid& InLayerGuid)
	{
		TArray<FWeightmapLayerAllocationInfo>& ComponentWeightmapLayerAllocations = InComponent->GetWeightmapLayerAllocations(InLayerGuid);
		bool bWasModified = false;

		for (int32 LayerIdx = 0; LayerIdx < ComponentWeightmapLayerAllocations.Num();)
		{
			const FWeightmapLayerAllocationInfo& Allocation = ComponentWeightmapLayerAllocations[LayerIdx];
			const TArray<TObjectPtr<UTexture2D>>& WeightmapTextures = InComponent->GetWeightmapTextures(InLayerGuid);
			UTexture2D* Texture = WeightmapTextures[Allocation.WeightmapTextureIndex];

			if (Texture == nullptr)
			{
				++LayerIdx;
				continue;
			}

			const uint8* MipDataPtr = Texture->Source.LockMipReadOnly(0);

			if (MipDataPtr == nullptr)
			{
				++LayerIdx;
				continue;
			}
				
			const uint8* const TextDataPtr = MipDataPtr + ChannelOffsets[Allocation.WeightmapTextureChannel];

			constexpr bool bShouldDirtyPackage = true;

			// If DeleteLayerIfAllZero returns true, We just removed the current layer allocation, so we need to iterate on the new current index.
			if (InComponent->DeleteLayerIfAllZero(InLayerGuid, TextDataPtr, Texture->GetSizeX(), LayerIdx, bShouldDirtyPackage))
			{
				bWasModified = true;
			}
			else
			{
				++LayerIdx;
			}

			Texture->Source.UnlockMip(0);
		}

		if (bWasModified)
		{
			InComponent->UpdateMaterialInstances();
			InComponent->MarkRenderStateDirty();
		}

		return bWasModified;
	}
}

void ALandscapeProxy::DeleteUnusedLayers()
{
	bool bWasModified = false;
	
	for (ULandscapeComponent* Component : LandscapeComponents)
	{
		if (Component == nullptr)
		{
			continue;
		}

		Component->ForEachLayer([Component, &bWasModified](const FGuid& LayerGuid, FLandscapeLayerComponentData& LayerData)
		{
			bWasModified = DeleteUnusedLayersImpl(Component, LayerGuid);
		});

		// Execute ClearUnusedLayersImpl on the final Layer.
		bWasModified = DeleteUnusedLayersImpl(Component, FGuid());

		if (bWasModified)
		{
			InvalidateNaniteRepresentation(false);
		}
	}
}

bool ALandscapeProxy::RemoveObsoleteLayers(const TSet<FGuid>& InExistingLayers)
{
	TSet<TPair<FGuid, FName>> ComponentLayers;
	for (ULandscapeComponent* Component : LandscapeComponents)
	{
		if (Component != nullptr)
		{
			Component->ForEachLayer([&](const FGuid& Guid, FLandscapeLayerComponentData& ComponentData) { ComponentLayers.Add(TPair<FGuid, FName>(Guid, ComponentData.DebugName)); });
		}
	}

	bool bModified = false;

	for (const TPair<FGuid, FName>& LayerGuidAndName : ComponentLayers)
	{
		if (!InExistingLayers.Contains(LayerGuidAndName.Key))
		{
			FFormatNamedArguments Arguments;
			Arguments.Add(TEXT("LayerName"), FText::FromString(LayerGuidAndName.Value.ToString()));
			Arguments.Add(TEXT("LayerGuid"), FText::FromString(LayerGuidAndName.Key.ToString(EGuidFormats::HexValuesInBraces)));
			Arguments.Add(TEXT("ProxyPackage"), FText::FromString(GetOutermost()->GetName()));

			FMessageLog("MapCheck").Info()
				->AddToken(FUObjectToken::Create(this))
				->AddToken(FTextToken::Create(FText::Format(LOCTEXT("MapCheck_Message_LandscapeProxyObsoleteLayer","Layer '{LayerName}' ('{LayerGuid}') was removed from LandscapeProxy because it doesn't match any of the LandscapeActor Layers. Please resave '{ProxyPackage}'."), Arguments)))
				->AddToken(FMapErrorToken::Create(FMapErrors::LandscapeComponentPostLoad_Warning));

			DeleteLayer(LayerGuidAndName.Key);
			bModified = true;
		}
	}

	if (bModified)
	{
		if (ALandscape* LandscapeActor = GetLandscapeActor())
		{
			LandscapeActor->RequestLayersContentUpdateForceAll();
		}
	}

	return bModified;
}

bool ALandscapeProxy::AddLayer(const FGuid& InLayerGuid)
{
	bool bModified = false;
	for (ULandscapeComponent* Component : LandscapeComponents)
	{
		if ((Component != nullptr) && !Component->GetLayerData(InLayerGuid))
		{
			const FLandscapeLayer* EditLayer = GetLandscapeActor() ? GetLandscapeActor()->GetLayerConst(InLayerGuid) : nullptr;
			Component->AddLayerData(InLayerGuid, FLandscapeLayerComponentData(EditLayer ? EditLayer->Name : FName()));
			bModified = true;
		}
	}

	UpdateCachedHasLayersContent();

	if (bModified)
	{
		InitializeLayerWithEmptyContent(InLayerGuid);
	}

	return bModified;
}

void ALandscapeProxy::DeleteLayer(const FGuid& InLayerGuid)
{
	for (ULandscapeComponent* Component : LandscapeComponents)
	{
		if (Component != nullptr)
		{
			const FLandscapeLayerComponentData* LayerComponentData = Component->GetLayerData(InLayerGuid);

			if (LayerComponentData != nullptr)
			{
				for (const FWeightmapLayerAllocationInfo& Allocation : LayerComponentData->WeightmapData.LayerAllocations)
				{
					UTexture2D* WeightmapTexture = LayerComponentData->WeightmapData.Textures[Allocation.WeightmapTextureIndex];
					TObjectPtr<ULandscapeWeightmapUsage>* Usage = WeightmapUsageMap.Find(WeightmapTexture);

					if (Usage != nullptr && (*Usage) != nullptr)
					{
						(*Usage)->ChannelUsage[Allocation.WeightmapTextureChannel] = nullptr;

						if ((*Usage)->IsEmpty())
						{
							WeightmapUsageMap.Remove(WeightmapTexture);
						}
					}
				}
				Component->RemoveLayerData(InLayerGuid);
			}
		}
	}

	UpdateCachedHasLayersContent();
}

void ALandscapeProxy::InitializeLayerWithEmptyContent(const FGuid& InLayerGuid)
{
	ULandscapeInfo* Info = GetLandscapeInfo();

	if (Info == nullptr)
	{
		return;
	}

	// Build a mapping between each Heightmaps and Component in them
	TMap<UTexture2D*, TArray<ULandscapeComponent*>> ComponentsPerHeightmaps;

	for (ULandscapeComponent* Component : LandscapeComponents)
	{
		if (Component != nullptr)
		{
			UTexture2D* ComponentHeightmapTexture = Component->GetHeightmap();
			TArray<ULandscapeComponent*>& ComponentList = ComponentsPerHeightmaps.FindOrAdd(ComponentHeightmapTexture);
			ComponentList.Add(Component);
		}
	}

	// Init layers with valid "empty" data
	TMap<UTexture2D*, UTexture2D*> CreatedHeightmapTextures; // < Final layer texture, New created texture for layer

	for (ULandscapeComponent* Component : LandscapeComponents)
	{
		if (Component != nullptr)
		{
			UTexture2D* ComponentHeightmap = Component->GetHeightmap();
			const TArray<ULandscapeComponent*>* ComponentsUsingHeightmap = ComponentsPerHeightmaps.Find(ComponentHeightmap);
			check(ComponentsUsingHeightmap != nullptr);

			Component->AddDefaultLayerData(InLayerGuid, *ComponentsUsingHeightmap, CreatedHeightmapTextures);
		}
	}
}

TArray<FName> ALandscapeProxy::SynchronizeUnmarkedSharedProperties(ALandscapeProxy* InLandscape)
{
	check(InLandscape != nullptr);
	TArray<FName> SynchronizedProperties;
	USceneComponent* OwnRootComponent = GetRootComponent();
	USceneComponent* ProxyRootComponent = InLandscape->GetRootComponent();

	if ((OwnRootComponent != nullptr) && (ProxyRootComponent != nullptr) && ProxyRootComponent->HasBeenInitialized())
	{
		FVector ProxyScale3D = ProxyRootComponent->GetComponentToWorld().GetScale3D();

		if (!OwnRootComponent->GetRelativeScale3D().Equals(ProxyScale3D))
		{
			OwnRootComponent->SetRelativeScale3D(ProxyScale3D);
			SynchronizedProperties.Emplace(TEXT("RelativeScale3D"));
		}
	}

	return SynchronizedProperties;
}

#endif

void ALandscape::BeginDestroy()
{
#if WITH_EDITOR
	if (CanHaveLayersContent())
	{
		if (CombinedLayersWeightmapAllMaterialLayersResource != nullptr)
		{
			BeginReleaseResource(CombinedLayersWeightmapAllMaterialLayersResource);
		}

		if (CurrentLayersWeightmapAllMaterialLayersResource != nullptr)
		{
			BeginReleaseResource(CurrentLayersWeightmapAllMaterialLayersResource);
		}

		if (WeightmapScratchExtractLayerTextureResource != nullptr)
		{
			BeginReleaseResource(WeightmapScratchExtractLayerTextureResource);
		}

		if (WeightmapScratchPackLayerTextureResource != nullptr)
		{
			BeginReleaseResource(WeightmapScratchPackLayerTextureResource);
		}

		// Use ResourceFence from base class		
	}
#endif

	Super::BeginDestroy();
}

void ALandscape::FinishDestroy()
{
#if WITH_EDITORONLY_DATA
	if (CanHaveLayersContent())
	{
		check(ReleaseResourceFence.IsFenceComplete());

		delete CombinedLayersWeightmapAllMaterialLayersResource;
		delete CurrentLayersWeightmapAllMaterialLayersResource;
		delete WeightmapScratchExtractLayerTextureResource;
		delete WeightmapScratchPackLayerTextureResource;

		CombinedLayersWeightmapAllMaterialLayersResource = nullptr;
		CurrentLayersWeightmapAllMaterialLayersResource = nullptr;
		WeightmapScratchExtractLayerTextureResource = nullptr;
		WeightmapScratchPackLayerTextureResource = nullptr;
	}
#endif

	Super::FinishDestroy();
}

bool ALandscape::IsUpToDate() const
{
	if (!FApp::CanEverRender())
	{
		return true;
	}

#if WITH_EDITORONLY_DATA
	if (CanHaveLayersContent() && GetWorld() != nullptr && !GetWorld()->IsGameWorld())
	{
		return LayerContentUpdateModes == 0 && !FLandscapeEditLayerReadback::HasWork();
	}
#endif

	return true;
}

#if WITH_EDITOR
bool ALandscape::IsLayerNameUnique(const FName& InName) const
{
	return Algo::CountIf(LandscapeEditLayers, [InName](const FLandscapeLayer& Layer) { return (Layer.Name == InName); }) == 0;
}

void ALandscape::SetLayerName(int32 InLayerIndex, const FName& InName)
{
	FLandscapeLayer* Layer = GetLayerInternal(InLayerIndex);
	ULandscapeInfo* LandscapeInfo = GetLandscapeInfo();
	if (!LandscapeInfo || !Layer || Layer->Name == InName)
	{
		return;
	}

	if (!IsLayerNameUnique(InName))
	{
		return;
	}

	Modify();
	Layer->Name = InName;
}

float ALandscape::GetLayerAlpha(int32 InLayerIndex, bool bInHeightmap) const
{
	// TODO [jonathan.bard] : replace by proper getter virtual GetLayerAlpha implementation on ULandscapeEditLayerSplines
	const FLandscapeLayer* SplinesReservedLayer = FindLayerOfType(ULandscapeEditLayerSplines::StaticClass());
	const FLandscapeLayer* Layer = GetLayerConst(InLayerIndex);
	if (Layer && SplinesReservedLayer != Layer)
	{
		return GetClampedLayerAlpha(bInHeightmap ? Layer->HeightmapAlpha : Layer->WeightmapAlpha, bInHeightmap);
	}
	return 1.0f;
}

float ALandscape::GetClampedLayerAlpha(float InAlpha, bool bInHeightmap) const
{
	float AlphaClamped = FMath::Clamp<float>(InAlpha, bInHeightmap ? -1.f : 0.f, 1.f);
	return AlphaClamped;
}

void ALandscape::SetLayerAlpha(int32 InLayerIndex, float InAlpha, bool bInHeightmap)
{
	FLandscapeLayer* Layer = GetLayerInternal(InLayerIndex);
	ULandscapeInfo* LandscapeInfo = GetLandscapeInfo();
	if (!LandscapeInfo || !Layer)
	{
		return;
	}
	const float InAlphaClamped = GetClampedLayerAlpha(InAlpha, bInHeightmap);
	float& LayerAlpha = bInHeightmap ? Layer->HeightmapAlpha : Layer->WeightmapAlpha;
	if (LayerAlpha == InAlphaClamped)
	{
		return;
	}

	Modify();
	LayerAlpha = InAlphaClamped;
	RequestLayersContentUpdateForceAll();
}

void ALandscape::SetLayerVisibility(int32 InLayerIndex, bool bInVisible, bool bInForIntermediateRender)
{
	FLandscapeLayer* Layer = GetLayerInternal(InLayerIndex);
	ULandscapeInfo* LandscapeInfo = GetLandscapeInfo();
	if (!LandscapeInfo || !Layer || Layer->bVisible == bInVisible)
	{
		return;
	}

	if (!bInForIntermediateRender)
	{
		Modify();
	}

	Layer->bVisible = bInVisible;

	if (!bInForIntermediateRender)
	{
		RequestLayersContentUpdateForceAll();
	}
}

void ALandscape::SetLayerLocked(int32 InLayerIndex, bool bLocked)
{
	FLandscapeLayer* Layer = GetLayerInternal(InLayerIndex);
	if (!Layer || Layer->bLocked == bLocked)
	{
		return;
	}

	Modify();
	Layer->bLocked = bLocked;
}

void ALandscape::SetLayerBlendMode(int32 InLayerIndex, ELandscapeBlendMode InBlendMode)
{
	FLandscapeLayer* Layer = GetLayerInternal(InLayerIndex);
	if (!Layer || Layer->BlendMode == InBlendMode)
	{
		return;
	}

	Modify();
	Layer->BlendMode = InBlendMode;
}

uint8 ALandscape::GetLayerCount() const
{
	return static_cast<uint8>(LandscapeEditLayers.Num());
}

FLandscapeLayer* ALandscape::GetLayerInternal(int32 InLayerIndex)
{
	if (LandscapeEditLayers.IsValidIndex(InLayerIndex))
	{
		return &LandscapeEditLayers[InLayerIndex];
	}
	return nullptr;
}

// Deprecated
const FLandscapeLayer* ALandscape::GetLayer(int32 InLayerIndex) const
{
	return GetLayerConst(InLayerIndex);
}

// Deprecated
const FLandscapeLayer* ALandscape::GetLayer(const FGuid& InLayerGuid) const
{
	return GetLayerConst(InLayerGuid);
}

// Deprecated
const FLandscapeLayer* ALandscape::GetLayer(const FName& InLayerName) const
{
	return GetLayerConst(InLayerName);
}

const FLandscapeLayer* ALandscape::GetLayerConst(int32 InLayerIndex) const
{
	if (LandscapeEditLayers.IsValidIndex(InLayerIndex))
	{
		return &LandscapeEditLayers[InLayerIndex];
	}
	return nullptr;
}

int32 ALandscape::GetLayerIndex(const FGuid& InLayerGuid) const
{
	return LandscapeEditLayers.IndexOfByPredicate([&InLayerGuid](const FLandscapeLayer& Other) { return Other.Guid == InLayerGuid; });
}

const FLandscapeLayer* ALandscape::GetLayerConst(const FGuid& InLayerGuid) const
{
	return LandscapeEditLayers.FindByPredicate([&InLayerGuid](const FLandscapeLayer& Other) { return Other.Guid == InLayerGuid; });
}

const FLandscapeLayer* ALandscape::GetLayerConst(const FName& InLayerName) const
{
	return LandscapeEditLayers.FindByPredicate([InLayerName](const FLandscapeLayer& Layer) { return Layer.Name == InLayerName; });
}

int32 ALandscape::GetLayerIndex(FName InLayerName) const
{
	return LandscapeEditLayers.IndexOfByPredicate([InLayerName](const FLandscapeLayer& Layer) { return Layer.Name == InLayerName; });
}

// Deprecated
void ALandscape::ForEachLayer(TFunctionRef<void(struct FLandscapeLayer&)> Fn)
{
	for (FLandscapeLayer& Layer : LandscapeEditLayers)
	{
		Fn(Layer);
	}
}

void ALandscape::ForEachLayerConst(TFunctionRef<bool(const FLandscapeLayer&)> Fn)
{
	for (FLandscapeLayer& Layer : LandscapeEditLayers)
	{
		if (!Fn(Layer))
		{
			return;
		}
	}
}

const FLandscapeLayer* ALandscape::FindLayerOfType(const TSubclassOf<ULandscapeEditLayerBase>& InLayerClass) const
{
	return LandscapeEditLayers.FindByPredicate([&InLayerClass](const FLandscapeLayer& InLayer) { check(InLayer.EditLayer != nullptr); return InLayer.EditLayer->GetClass()->IsChildOf(InLayerClass); });
}

TArray<const FLandscapeLayer*> ALandscape::GetLayersOfType(const TSubclassOf<ULandscapeEditLayerBase>& InLayerClass) const
{
	TArray<const FLandscapeLayer*> Result;
	Result.Reserve(LandscapeEditLayers.Num());
	Algo::TransformIf(LandscapeEditLayers, Result,
		[&InLayerClass](const FLandscapeLayer& InLayer) { check(InLayer.EditLayer != nullptr); return InLayer.EditLayer->GetClass()->IsChildOf(InLayerClass); },
		[](const FLandscapeLayer& InLayer) { return &InLayer; });
	return Result;
}

void ALandscape::DeleteLayers()
{
	for (int32 LayerIndex = LandscapeEditLayers.Num() - 1; LayerIndex >= 0; --LayerIndex)
	{
		DeleteLayer(LayerIndex);
	}
}

bool ALandscape::DeleteLayer(int32 InLayerIndex)
{
	ensure(HasLayersContent());

	const FLandscapeLayer* LayerStruct = GetLayerConst(InLayerIndex);
	if (!LayerStruct)
	{
		return false;
	}

	Modify();
	FGuid LayerGuid = LayerStruct->Guid;

	// Clean up Weightmap usage in LandscapeProxies
	if (ULandscapeInfo* LandscapeInfo = GetLandscapeInfo())
	{
		LandscapeInfo->ForEachLandscapeProxy([&LayerGuid](ALandscapeProxy* Proxy)
		{
			Proxy->DeleteLayer(LayerGuid);
			return true;
		});
	}

	// We're about to remove the layer from our list, which will invalidate our LayerStruct pointer.
	// We'll need to call OnLayerRemoved afterward, though, so keep pointer to the UObject.
	ULandscapeEditLayerBase* Layer = LayerStruct->EditLayer;

	// Remove layer from list
	LandscapeEditLayers.RemoveAt(InLayerIndex);
	LayerStruct = nullptr;

	if (Layer)
	{
		Layer->OnLayerRemoved();
	}

	// Request Update
	RequestLayersContentUpdateForceAll();

	return true;
}

void ALandscape::CollapseLayer(int32 InLayerIndex)
{
	FScopedSlowTask SlowTask(static_cast<float>(GetLandscapeInfo()->XYtoComponentMap.Num()), LOCTEXT("Landscape_CollapseLayer_SlowWork", "Collapsing Layer..."));
	SlowTask.MakeDialog();
	TArray<bool> BackupVisibility;
	TArray<bool> BackupBrushVisibility;
	for (int32 i = 0; i < LandscapeEditLayers.Num(); ++i)
	{
		BackupVisibility.Add(LandscapeEditLayers[i].bVisible);
		LandscapeEditLayers[i].bVisible = i == InLayerIndex || i == InLayerIndex - 1;
	}

	for (int32 i = 0; i < LandscapeEditLayers[InLayerIndex].Brushes.Num(); ++i)
	{
		BackupBrushVisibility.Add(LandscapeEditLayers[InLayerIndex].Brushes[i].GetBrush()->IsVisible());
		LandscapeEditLayers[InLayerIndex].Brushes[i].GetBrush()->SetIsVisible(false);
	}

	// Call Request Update on all components...
	GetLandscapeInfo()->ForAllLandscapeComponents([](ULandscapeComponent* LandscapeComponent)
	{
		LandscapeComponent->RequestWeightmapUpdate(false, false);
		LandscapeComponent->RequestHeightmapUpdate(false, false);
	});

	const bool bLocalIntermediateRender = true;
	ForceUpdateLayersContent(bLocalIntermediateRender);

	// Do copy
	{
		FLandscapeEditDataInterface DataInterface(GetLandscapeInfo());
		DataInterface.SetShouldDirtyPackage(true);

		TSet<UTexture2D*> ProcessedHeightmaps;
		FScopedSetLandscapeEditingLayer ScopeEditingLayer(this, LandscapeEditLayers[InLayerIndex - 1].Guid);
		GetLandscapeInfo()->ForAllLandscapeComponents([&](ULandscapeComponent* LandscapeComponent)
		{
			SlowTask.EnterProgressFrame(1.f);
			LandscapeComponent->CopyFinalLayerIntoEditingLayer(DataInterface, ProcessedHeightmaps);
		});
	}

	TArray<ALandscapeBlueprintBrushBase*> BrushesToMove;
	for (int32 i = 0; i < LandscapeEditLayers[InLayerIndex].Brushes.Num(); ++i)
	{
		ALandscapeBlueprintBrushBase* CurrentBrush = LandscapeEditLayers[InLayerIndex].Brushes[i].GetBrush();
		CurrentBrush->SetIsVisible(BackupBrushVisibility[i]);
		BrushesToMove.Add(CurrentBrush);
	}

	for (ALandscapeBlueprintBrushBase* Brush : BrushesToMove)
	{
		RemoveBrushFromLayer(InLayerIndex, Brush);
		AddBrushToLayer(InLayerIndex - 1, Brush);
	}

	for (int32 i = 0; i < LandscapeEditLayers.Num(); ++i)
	{
		LandscapeEditLayers[i].bVisible = BackupVisibility[i];
	}

	DeleteLayer(InLayerIndex);

	RequestLayersContentUpdateForceAll();
}

void ALandscape::GetUsedPaintLayers(int32 InLayerIndex, TArray<ULandscapeLayerInfoObject*>& OutUsedLayerInfos) const
{
	const FLandscapeLayer* Layer = GetLayerConst(InLayerIndex);
	if (Layer)
	{
		GetUsedPaintLayers(Layer->Guid, OutUsedLayerInfos);
	}
}

void ALandscape::GetUsedPaintLayers(const FGuid& InLayerGuid, TArray<ULandscapeLayerInfoObject*>& OutUsedLayerInfos) const
{
	ULandscapeInfo* LandscapeInfo = GetLandscapeInfo();
	if (!LandscapeInfo)
	{
		return;
	}

	LandscapeInfo->GetUsedPaintLayers(InLayerGuid, OutUsedLayerInfos);
}

void ALandscape::ClearPaintLayer(int32 InLayerIndex, ULandscapeLayerInfoObject* InLayerInfo)
{
	const FLandscapeLayer* Layer = GetLayerConst(InLayerIndex);
	if (Layer)
	{
		ClearPaintLayer(Layer->Guid, InLayerInfo);
	}
}

void ALandscape::ClearPaintLayer(const FGuid& InLayerGuid, ULandscapeLayerInfoObject* InLayerInfo)
{
	ULandscapeInfo* LandscapeInfo = GetLandscapeInfo();
	if (!LandscapeInfo)
	{
		return;
	}

	Modify();
	FScopedSetLandscapeEditingLayer Scope(this, InLayerGuid, [this] { RequestLayersContentUpdate(ELandscapeLayerUpdateMode::Update_Weightmap_All); });

	FLandscapeEditDataInterface LandscapeEdit(LandscapeInfo);
	LandscapeInfo->ForEachLandscapeProxy([&](ALandscapeProxy* Proxy)
	{
		Proxy->Modify();
		for (ULandscapeComponent* Component : Proxy->LandscapeComponents)
		{
			Component->DeleteLayer(InLayerInfo, LandscapeEdit);
		}
		return true;
	});
}

void ALandscape::ClearLayer(int32 InLayerIndex, TSet<TObjectPtr<ULandscapeComponent>>* InComponents, ELandscapeClearMode InClearMode)
{
	const FLandscapeLayer* Layer = GetLayerConst(InLayerIndex);
	if (Layer)
	{
		ClearLayer(Layer->Guid, InComponents, InClearMode);
	}
}

void ALandscape::ClearLayer(const FGuid& InLayerGuid, TSet<TObjectPtr<ULandscapeComponent>>* InComponents, ELandscapeClearMode InClearMode, bool bMarkPackageDirty)
{
	ensure(HasLayersContent());

	const FLandscapeLayer* Layer = GetLayerConst(InLayerGuid);
	ULandscapeInfo* LandscapeInfo = GetLandscapeInfo();
	if (!LandscapeInfo || !Layer)
	{
		return;
	}

	Modify(bMarkPackageDirty);
	FScopedSetLandscapeEditingLayer Scope(this, Layer->Guid, [this] { RequestLayersContentUpdate(ELandscapeLayerUpdateMode::Update_All); });

	TArray<uint16> NewHeightData;
	NewHeightData.AddZeroed(FMath::Square(ComponentSizeQuads + 1));
	uint16 ZeroValue = LandscapeDataAccess::GetTexHeight(0.f);
	for (uint16& NewHeightDataValue : NewHeightData)
	{
		NewHeightDataValue = ZeroValue;
	}

	TArray<uint16> NewHeightAlphaBlendData;
	TArray<uint8> NewHeightFlagsData;

	if (InClearMode & ELandscapeClearMode::Clear_Heightmap)
	{
		if (Layer->BlendMode == LSBM_AlphaBlend)
		{
			NewHeightAlphaBlendData.Init(MAX_uint16, FMath::Square(ComponentSizeQuads + 1));
			NewHeightFlagsData.AddZeroed(FMath::Square(ComponentSizeQuads + 1));
		}
	}

	TArray<ULandscapeComponent*> Components;
	if (InComponents)
	{
		TSet<ALandscapeProxy*> Proxies;
		Components.Reserve(InComponents->Num());
		for (ULandscapeComponent* Component : *InComponents)
		{
			if (Component)
			{
				Components.Add(Component);
				ALandscapeProxy* Proxy = Component->GetLandscapeProxy();
				if (!Proxies.Find(Proxy))
				{
					Proxies.Add(Proxy);
					Proxy->Modify(bMarkPackageDirty);
				}
			}
		}
	}
	else
	{
		LandscapeInfo->ForEachLandscapeProxy([&](ALandscapeProxy* Proxy)
		{
			Proxy->Modify(bMarkPackageDirty);
			Components.Append(Proxy->LandscapeComponents);
			return true;
		});
	}

	FLandscapeEditDataInterface LandscapeEdit(LandscapeInfo);
	FLandscapeDoNotDirtyScope DoNotDirtyScope(LandscapeEdit, !bMarkPackageDirty);
	for (ULandscapeComponent* Component : Components)
	{
		if (InClearMode & ELandscapeClearMode::Clear_Heightmap)
		{
			int32 MinX = MAX_int32;
			int32 MinY = MAX_int32;
			int32 MaxX = MIN_int32;
			int32 MaxY = MIN_int32;
			Component->GetComponentExtent(MinX, MinY, MaxX, MaxY);
			check(ComponentSizeQuads == (MaxX - MinX));
			check(ComponentSizeQuads == (MaxY - MinY));
			LandscapeEdit.SetHeightData(MinX, MinY, MaxX, MaxY, NewHeightData.GetData(), 0, false, nullptr, NewHeightAlphaBlendData.GetData(), NewHeightFlagsData.GetData());
		}

		if (InClearMode & ELandscapeClearMode::Clear_Weightmap)
		{
			// Clear weight maps
			for (FLandscapeInfoLayerSettings& LayerSettings : LandscapeInfo->Layers)
			{
				Component->DeleteLayer(LayerSettings.LayerInfoObj, LandscapeEdit);
			}
		}
	}
}

void ALandscape::ShowOnlySelectedLayer(int32 InLayerIndex)
{
	const FLandscapeLayer* VisibleLayer = GetLayerConst(InLayerIndex);
	if (VisibleLayer)
	{
		bool bModified = false;
		for (FLandscapeLayer& Layer : LandscapeEditLayers)
		{
			bool bDesiredVisible = (&Layer == VisibleLayer);
			if (Layer.bVisible != bDesiredVisible)
			{
				if (!bModified)
				{
					Modify();
					bModified = true;
		}
				Layer.bVisible = bDesiredVisible;
			}
		}
		if (bModified)
		{
		RequestLayersContentUpdateForceAll();
	}
}
}

void ALandscape::ShowAllLayers()
{
	if (LandscapeEditLayers.Num() > 0)
	{
		bool bModified = false;
		for (FLandscapeLayer& Layer : LandscapeEditLayers)
		{
			if (Layer.bVisible != true)
			{
				if (!bModified)
				{
					Modify();
					bModified = true;
				}
			Layer.bVisible = true;
		}
		}
		if (bModified)
		{
		RequestLayersContentUpdateForceAll();
	}
}
}

// Deprecated
void ALandscape::SetLandscapeSplinesReservedLayer(int32 InLayerIndex)
{
}

// Deprecated
const FLandscapeLayer* ALandscape::GetLandscapeSplinesReservedLayer() const
{
	return FindLayerOfType(ULandscapeEditLayerSplines::StaticClass());
}

// Deprecated
FLandscapeLayer* ALandscape::GetLandscapeSplinesReservedLayer()
{
	return const_cast<FLandscapeLayer*>(FindLayerOfType(ULandscapeEditLayerSplines::StaticClass()));
}

LANDSCAPE_API extern bool GDisableUpdateLandscapeMaterialInstances;

uint32 ULandscapeComponent::ComputeLayerHash(bool InReturnEditingHash) const
{
	UTexture2D* Heightmap = GetHeightmap(InReturnEditingHash);
	const uint8* MipData = Heightmap->Source.LockMipReadOnly(0);
	uint32 Hash = FCrc::MemCrc32(MipData, Heightmap->Source.GetSizeX() * Heightmap->Source.GetSizeY() * sizeof(FColor));
	Heightmap->Source.UnlockMip(0);

	// Copy to sort
	const TArray<UTexture2D*>& Weightmaps = GetWeightmapTextures(InReturnEditingHash);
	TArray<FWeightmapLayerAllocationInfo> AllocationInfos = GetWeightmapLayerAllocations(InReturnEditingHash);

	// Sort allocations infos by LayerInfo Path so the Weightmaps hahses get ordered properly
	AllocationInfos.Sort([](const FWeightmapLayerAllocationInfo& A, const FWeightmapLayerAllocationInfo& B)
	{
		FString PathA(A.LayerInfo ? A.LayerInfo->GetPathName() : FString());
		FString PathB(B.LayerInfo ? B.LayerInfo->GetPathName() : FString());

		return PathA < PathB;
	});

	for (const FWeightmapLayerAllocationInfo& AllocationInfo : AllocationInfos)
	{
		if (AllocationInfo.IsAllocated())
		{
			// Compute hash of actual data of the texture that is owned by the component (per Texture Channel)
			UTexture2D* Weightmap = Weightmaps[AllocationInfo.WeightmapTextureIndex];
			MipData = Weightmap->Source.LockMipReadOnly(0) + ChannelOffsets[AllocationInfo.WeightmapTextureChannel];
			TArray<uint8> ChannelData;
			ChannelData.AddDefaulted(Weightmap->Source.GetSizeX() * Weightmap->Source.GetSizeY());
			int32 TexSize = (SubsectionSizeQuads + 1) * NumSubsections;
			for (int32 TexY = 0; TexY < TexSize; TexY++)
			{
				for (int32 TexX = 0; TexX < TexSize; TexX++)
				{
					const int32 Index = (TexX + TexY * TexSize);

					ChannelData.GetData()[Index] = MipData[4 * Index];
				}
			}

			Hash = FCrc::MemCrc32(ChannelData.GetData(), Weightmap->GetSizeX() * Weightmap->GetSizeY(), Hash);
			Weightmap->Source.UnlockMip(0);
		}
	}

	return Hash;
}

void ALandscape::UpdateLandscapeSplines(const FGuid& InTargetLayer, bool bInUpdateOnlySelected, bool bInForceUpdateAllCompoments)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(LandscapeLayers_UpdateLandscapeSplines);
	check(CanHaveLayersContent());
	ULandscapeInfo* LandscapeInfo = GetLandscapeInfo();
	const FLandscapeLayer* SplinesLayer = FindLayerOfType(ULandscapeEditLayerSplines::StaticClass());
	FGuid TargetLayerGuid = (SplinesLayer != nullptr) ? SplinesLayer->Guid : InTargetLayer;
	const FLandscapeLayer* TargetLayer = GetLayerConst(TargetLayerGuid);
	if (LandscapeInfo && TargetLayer)
	{
		FScopedSetLandscapeEditingLayer Scope(this, TargetLayerGuid, [this] { this->RequestLayersContentUpdate(ELandscapeLayerUpdateMode::Update_All); });
		// Temporarily disable material instance updates since it will be done once at the end (requested by RequestLayersContentUpdateForceAll)
		GDisableUpdateLandscapeMaterialInstances = true;
		TSet<TObjectPtr<ULandscapeComponent>>* ModifiedComponent = nullptr;
		if (SplinesLayer != nullptr)
		{
			// Check that we can modify data
			if (!LandscapeInfo->AreAllComponentsRegistered())
			{
				return;
			}

			TMap<ULandscapeComponent*, uint32> PreviousHashes;
			{
				FLandscapeEditDataInterface LandscapeEdit(LandscapeInfo);

				LandscapeInfo->ForAllLandscapeComponents([&](ULandscapeComponent* Component)
				{
					// Was never computed
					if (Component->SplineHash == 0)
					{
						LandscapeInfo->ModifyObject(Component);
						Component->SplineHash = DefaultSplineHash;
					}

					PreviousHashes.Add(Component, Component->SplineHash);
					LandscapeInfo->ModifyObject(Component, false);
					Component->SplineHash = DefaultSplineHash;
				});
			}

			// Clear layers without affecting weightmap allocations
			const bool bMarkPackageDirty = false;
			ClearLayer(SplinesLayer->Guid, (!bInForceUpdateAllCompoments && LandscapeSplinesAffectedComponents.Num()) ? &LandscapeSplinesAffectedComponents : nullptr, Clear_All, bMarkPackageDirty);
			LandscapeSplinesAffectedComponents.Empty();
			ModifiedComponent = &LandscapeSplinesAffectedComponents;
			// For now, in Landscape Layer System Mode with a reserved layer for splines, we always update all the splines since we clear the whole layer first
			bInUpdateOnlySelected = false;

			// Apply splines without clearing up weightmap allocations
			LandscapeInfo->ApplySplines(bInUpdateOnlySelected, ModifiedComponent, bMarkPackageDirty);

			for (const TPair<ULandscapeComponent*, uint32>& Pair : PreviousHashes)
			{
				if (LandscapeSplinesAffectedComponents.Contains(Pair.Key))
				{
					uint32 NewHash = Pair.Key->ComputeLayerHash();
					if (NewHash != Pair.Value)
					{
						LandscapeInfo->MarkObjectDirty(Pair.Key);
					}
					Pair.Key->SplineHash = NewHash;
				}
				else if (Pair.Key->SplineHash == DefaultSplineHash && Pair.Value != DefaultSplineHash)
				{
					LandscapeInfo->MarkObjectDirty(Pair.Key);
				}
			}
		}
		else
		{
			LandscapeInfo->ApplySplines(bInUpdateOnlySelected, ModifiedComponent);
		}
		GDisableUpdateLandscapeMaterialInstances = false;
	}
}

FScopedSetLandscapeEditingLayer::FScopedSetLandscapeEditingLayer(ALandscape* InLandscape, const FGuid& InLayerGUID, TFunction<void()> InCompletionCallback)
	: Landscape(InLandscape)
	, CompletionCallback(MoveTemp(InCompletionCallback))
{
	if (Landscape.IsValid() && Landscape.Get()->CanHaveLayersContent())
	{
		PreviousLayerGUID = Landscape->GetEditingLayer();
		Landscape->SetEditingLayer(InLayerGUID);
	}
}

FScopedSetLandscapeEditingLayer::~FScopedSetLandscapeEditingLayer()
{
	if (Landscape.IsValid() && Landscape.Get()->CanHaveLayersContent())
	{
		Landscape->SetEditingLayer(PreviousLayerGUID);
		if (CompletionCallback)
		{
			CompletionCallback();
		}
	}
}

// Deprecated
bool ALandscape::IsEditingLayerReservedForSplines() const
{
	return false;
}

void ALandscape::SetEditingLayer(const FGuid& InLayerGuid)
{
	ensure(CanHaveLayersContent());

	ULandscapeInfo* LandscapeInfo = GetLandscapeInfo();
	if (!LandscapeInfo)
	{
		EditingLayer.Invalidate();
		return;
	}

	EditingLayer = InLayerGuid;
}

void ALandscape::SetGrassUpdateEnabled(bool bInGrassUpdateEnabled)
{
#if WITH_EDITORONLY_DATA
	bGrassUpdateEnabled = bInGrassUpdateEnabled;
#endif
}

const FGuid& ALandscape::GetEditingLayer() const
{
	return EditingLayer;
}

bool ALandscape::IsMaxLayersReached() const
{
	return LandscapeEditLayers.Num() >= GetDefault<ULandscapeSettings>()->MaxNumberOfLayers;
}

void ALandscape::CreateDefaultLayer()
{
	if (!CanHaveLayersContent())
	{
		return;
	}

	check(LandscapeEditLayers.Num() == 0); // We can only call this function if we have no layers

	CreateLayer(FName(TEXT("Layer")));
}

FLandscapeLayer* ALandscape::DuplicateLayerAndMoveBrushes(const FLandscapeLayer& InOtherLayer)
{
	ULandscapeInfo* LandscapeInfo = GetLandscapeInfo();
	if (!LandscapeInfo || !CanHaveLayersContent())
	{
		return nullptr;
	}

	if (IsMaxLayersReached())
	{
		UE_LOG(LogLandscape, Warning, TEXT("Cannot duplicate layer : %s as the max number of layers (%i) has been reached"), *InOtherLayer.Name.ToString(), GetDefault<ULandscapeSettings>()->MaxNumberOfLayers);
		return nullptr;
	}

	Modify();

	FLandscapeLayer NewLayer(InOtherLayer);
	NewLayer.Guid = FGuid::NewGuid();

	// Duplicate the internal edit layer object by hand : 
	check(InOtherLayer.EditLayer != nullptr);
	NewLayer.EditLayer = DuplicateObject(InOtherLayer.EditLayer, this, MakeUniqueObjectName(this, InOtherLayer.EditLayer->GetClass(), InOtherLayer.EditLayer->GetFName()));

	// Update owning landscape and reparent to landscape's level if necessary
	for (FLandscapeLayerBrush& Brush : NewLayer.Brushes)
	{
		Brush.SetOwner(this);
	}

	int32 AddedIndex = LandscapeEditLayers.Add(NewLayer);
	OnLayerCreatedInternal(NewLayer);

	return &LandscapeEditLayers[AddedIndex];
}

int32 ALandscape::CreateLayer(FName InName, const TSubclassOf<ULandscapeEditLayerBase>& InEditLayerClass)
{
	if (IsMaxLayersReached() || !CanHaveLayersContent())
	{
		return INDEX_NONE;
	}

	Modify();

	const UClass* EditLayerClass = (InEditLayerClass.Get() != nullptr) ? InEditLayerClass.Get() : ULandscapeEditLayer::StaticClass();
	int32 LayerIndex = LandscapeEditLayers.Emplace();
	FLandscapeLayer& NewLayer = LandscapeEditLayers[LayerIndex];
	NewLayer.Name = GenerateUniqueLayerName(InName);
	NewLayer.EditLayer = NewObject<ULandscapeEditLayerBase>(this, EditLayerClass, MakeUniqueObjectName(this, EditLayerClass));
	OnLayerCreatedInternal(NewLayer);
	return LayerIndex;
}

// Deprecated : use DuplicateLayerAndMoveBrushes 
int32 ALandscape::CreateLayerFrom(const FLandscapeLayer& InLayer)
{
	if (IsMaxLayersReached() || !CanHaveLayersContent())
	{
		return INDEX_NONE;
	}

	// Cannot create a layer whose Guid is already taken : 
	if (GetLayerConst(InLayer.Guid) != nullptr)
	{
		return INDEX_NONE;
	}

	check(InLayer.EditLayer != nullptr);

	int32 LayerIndex = LandscapeEditLayers.Add(InLayer);
	FLandscapeLayer& NewLayer = LandscapeEditLayers[LayerIndex];
	NewLayer.EditLayer = DuplicateObject<ULandscapeEditLayerBase>(InLayer.EditLayer, this, MakeUniqueObjectName(this, InLayer.EditLayer->GetClass()));
	OnLayerCreatedInternal(NewLayer);
	return LayerIndex;
}

void ALandscape::OnLayerCreatedInternal(FLandscapeLayer& Layer)
{
	// TODO: Might not be necessary eventually, if EditLayer has ability to trigger landscape updates
	// and has access to its guid.
	Layer.EditLayer->SetBackPointer(this);
	Layer.EditLayer->SetFlags(RF_Transactional);
	Layer.EditLayer->OnLayerCreated(Layer);

	// Create associated layer data in each landscape proxy
	if (ULandscapeInfo* LandscapeInfo = GetLandscapeInfo())
	{
		LandscapeInfo->ForEachLandscapeProxy([&Layer](ALandscapeProxy* Proxy)
		{
			Proxy->AddLayer(Layer.Guid);
			return true;
		});
	}

	// Request Update
	// Force update rendering resources
	RequestLayersInitialization(/*bInRequestContentUpdate = */true);
	RequestSplineLayerUpdate(); // Request a spline update as well, in case we already had spline actors and a spline layers was just created
}

void ALandscape::AddLayersToProxy(ALandscapeProxy* InProxy)
{
	ULandscapeInfo* LandscapeInfo = GetLandscapeInfo();
	if (!LandscapeInfo || !CanHaveLayersContent())
	{
		return;
	}

	check(InProxy != this);
	check(InProxy != nullptr);

	ForEachLayerConst([&](const FLandscapeLayer& Layer)
	{
		InProxy->AddLayer(Layer.Guid);
		return true;
	});

	// Force update rendering resources
	RequestLayersInitialization();
}

bool ALandscape::ReorderLayer(int32 InStartingLayerIndex, int32 InDestinationLayerIndex)
{
	if (InStartingLayerIndex != InDestinationLayerIndex &&
		LandscapeEditLayers.IsValidIndex(InStartingLayerIndex) &&
		LandscapeEditLayers.IsValidIndex(InDestinationLayerIndex))
	{
		Modify();
		FLandscapeLayer Layer = LandscapeEditLayers[InStartingLayerIndex];
		LandscapeEditLayers.RemoveAt(InStartingLayerIndex);
		LandscapeEditLayers.Insert(Layer, InDestinationLayerIndex);
		RequestLayersContentUpdateForceAll();
		return true;
	}
	return false;
}

FName ALandscape::GenerateUniqueLayerName(FName InName) const
{
	// If we are receiving a unique name, use it.
	if (InName != NAME_None && !LandscapeEditLayers.ContainsByPredicate([InName](const FLandscapeLayer& Layer) { return Layer.Name == InName; }))
	{
		return InName;
	}

	FString BaseName = (InName == NAME_None) ? "Layer" : InName.ToString();
	FName NewName;
	int32 LayerIndex = 0;
	do
	{
		++LayerIndex;
		NewName = FName(*FString::Printf(TEXT("%s%d"), *BaseName, LayerIndex));
	} while (LandscapeEditLayers.ContainsByPredicate([NewName](const FLandscapeLayer& Layer) { return Layer.Name == NewName; }));

	return NewName;
}

bool ALandscape::IsLayerBlendSubstractive(int32 InLayerIndex, const TWeakObjectPtr<ULandscapeLayerInfoObject>& InLayerInfoObj) const
{
	const FLandscapeLayer* Layer = GetLayerConst(InLayerIndex);

	if (Layer == nullptr)
	{
		return false;
	}

	const bool* AllocationBlend = Layer->WeightmapLayerAllocationBlend.Find(InLayerInfoObj.Get());

	if (AllocationBlend != nullptr)
	{
		return (*AllocationBlend);
	}

	return false;
}

void ALandscape::SetLayerSubstractiveBlendStatus(int32 InLayerIndex, bool InStatus, const TWeakObjectPtr<ULandscapeLayerInfoObject>& InLayerInfoObj)
{
	FLandscapeLayer* Layer = GetLayerInternal(InLayerIndex);

	if (Layer == nullptr)
	{
		return;
	}

	Modify();
	bool* AllocationBlend = Layer->WeightmapLayerAllocationBlend.Find(InLayerInfoObj.Get());

	if (AllocationBlend == nullptr)
	{
		Layer->WeightmapLayerAllocationBlend.Add(InLayerInfoObj.Get(), InStatus);
	}
	else
	{
		*AllocationBlend = InStatus;
	}

	RequestLayersContentUpdateForceAll(ELandscapeLayerUpdateMode::Update_Weightmap_All);
}

void ALandscape::ReplaceLayerSubstractiveBlendStatus(ULandscapeLayerInfoObject* InFromLayerInfo, ULandscapeLayerInfoObject* InToLayerInfo, bool bInShouldDirtyPackage)
{
	Modify(bInShouldDirtyPackage);

	for (FLandscapeLayer& Layer : LandscapeEditLayers)
	{
		bool OutValue;
		if (Layer.WeightmapLayerAllocationBlend.RemoveAndCopyValue(InFromLayerInfo, OutValue))
		{
			Layer.WeightmapLayerAllocationBlend.Add(InToLayerInfo, OutValue);
		}
	}
}

bool ALandscape::ReorderLayerBrush(int32 InLayerIndex, int32 InStartingLayerBrushIndex, int32 InDestinationLayerBrushIndex)
{
	if (FLandscapeLayer* Layer = GetLayerInternal(InLayerIndex))
	{
		if (InStartingLayerBrushIndex != InDestinationLayerBrushIndex &&
			Layer->Brushes.IsValidIndex(InStartingLayerBrushIndex) &&
			Layer->Brushes.IsValidIndex(InDestinationLayerBrushIndex))
		{
			Modify();
			FLandscapeLayerBrush MovingBrush = Layer->Brushes[InStartingLayerBrushIndex];
			Layer->Brushes.RemoveAt(InStartingLayerBrushIndex);
			Layer->Brushes.Insert(MovingBrush, InDestinationLayerBrushIndex);
			RequestLayersContentUpdateForceAll();
			return true;
		}
	}
	return false;
}

int32 ALandscape::GetBrushLayer(const ALandscapeBlueprintBrushBase* InBrush) const
{
	for (int32 LayerIndex = 0; LayerIndex < LandscapeEditLayers.Num(); ++LayerIndex)
	{
		for (const FLandscapeLayerBrush& Brush : LandscapeEditLayers[LayerIndex].Brushes)
		{
			if (Brush.GetBrush() == InBrush)
			{
				return LayerIndex;
			}
		}
	}

	return INDEX_NONE;
}

void ALandscape::AddBrushToLayer(int32 InLayerIndex, ALandscapeBlueprintBrushBase* InBrush)
{
	check(GetBrushLayer(InBrush) == INDEX_NONE);
	if (FLandscapeLayer* Layer = GetLayerInternal(InLayerIndex))
	{
		Modify();
		Layer->Brushes.Add(FLandscapeLayerBrush(InBrush));
		InBrush->SetOwningLandscape(this);
		RequestLayersContentUpdateForceAll();
	}
}

void ALandscape::RemoveBrush(ALandscapeBlueprintBrushBase* InBrush)
{
	int32 LayerIndex = GetBrushLayer(InBrush);
	if (LayerIndex != INDEX_NONE)
	{
		RemoveBrushFromLayer(LayerIndex, InBrush);
	}
}

void ALandscape::RemoveBrushFromLayer(int32 InLayerIndex, ALandscapeBlueprintBrushBase* InBrush)
{
	int32 BrushIndex = GetBrushIndexForLayer(InLayerIndex, InBrush);
	if (BrushIndex != INDEX_NONE)
	{
		RemoveBrushFromLayer(InLayerIndex, BrushIndex);
	}
}

void ALandscape::RemoveBrushFromLayer(int32 InLayerIndex, int32 InBrushIndex)
{
	if (FLandscapeLayer* Layer = GetLayerInternal(InLayerIndex))
	{
		if (Layer->Brushes.IsValidIndex(InBrushIndex))
		{
			Modify();
			ALandscapeBlueprintBrushBase* Brush = Layer->Brushes[InBrushIndex].GetBrush();
			Layer->Brushes.RemoveAt(InBrushIndex);
			if (Brush != nullptr)
			{
				Brush->SetOwningLandscape(nullptr);
			}
			RequestLayersContentUpdateForceAll();
		}
	}
}

int32 ALandscape::GetBrushIndexForLayer(int32 InLayerIndex, ALandscapeBlueprintBrushBase* InBrush)
{
	if (const FLandscapeLayer* Layer = GetLayerConst(InLayerIndex))
	{
		for (int32 i = 0; i < Layer->Brushes.Num(); ++i)
		{
			if (Layer->Brushes[i].GetBrush() == InBrush)
			{
				return i;
			}
		}
	}

	return INDEX_NONE;
}


void ALandscape::OnBlueprintBrushChanged()
{
#if WITH_EDITORONLY_DATA
	LandscapeBlueprintBrushChangedDelegate.Broadcast();
	RequestLayersContentUpdateForceAll();
#endif
}

void ALandscape::OnLayerInfoSplineFalloffModulationChanged(ULandscapeLayerInfoObject* InLayerInfo)
{
	ULandscapeInfo* LandscapeInfo = GetLandscapeInfo();

	if (!LandscapeInfo)
	{
		return;
	}

	ALandscape* Landscape = LandscapeInfo->LandscapeActor.Get();
	if (!Landscape || !Landscape->HasLayersContent())
	{
		return;
	}

	bool bUsedForSplines = false;
	LandscapeInfo->ForAllSplineActors([&](TScriptInterface<ILandscapeSplineInterface> SplineOwner)
	{
		bUsedForSplines |= (SplineOwner->GetSplinesComponent() && SplineOwner->GetSplinesComponent()->IsUsingLayerInfo(InLayerInfo));
	});

	if (bUsedForSplines)
	{
		Landscape->RequestSplineLayerUpdate();
	}
}

ALandscapeBlueprintBrushBase* ALandscape::GetBrushForLayer(int32 InLayerIndex, int32 InBrushIndex) const
{
	if (const FLandscapeLayer* Layer = GetLayerConst(InLayerIndex))
	{
		if (Layer->Brushes.IsValidIndex(InBrushIndex))
		{
			return Layer->Brushes[InBrushIndex].GetBrush();
		}
	}
	return nullptr;
}

TArray<ALandscapeBlueprintBrushBase*> ALandscape::GetBrushesForLayer(int32 InLayerIndex) const
{
	TArray<ALandscapeBlueprintBrushBase*> Brushes;
	if (const FLandscapeLayer* Layer = GetLayerConst(InLayerIndex))
	{
		Brushes.Reserve(Layer->Brushes.Num());
		for (const FLandscapeLayerBrush& Brush : Layer->Brushes)
		{
			Brushes.Add(Brush.GetBrush());
		}
	}
	return Brushes;
}

ALandscapeBlueprintBrushBase* FLandscapeLayerBrush::GetBrush() const
{
#if WITH_EDITORONLY_DATA
	return BlueprintBrush;
#else
	return nullptr;
#endif
}

void FLandscapeLayerBrush::SetOwner(ALandscape* InOwner)
{
#if WITH_EDITORONLY_DATA
	if (BlueprintBrush && InOwner)
	{
		if (BlueprintBrush->GetTypedOuter<ULevel>() != InOwner->GetTypedOuter<ULevel>())
		{
			BlueprintBrush->Rename(nullptr, InOwner->GetTypedOuter<ULevel>());
		}
		BlueprintBrush->SetOwningLandscape(InOwner);
	}
#endif
}

bool FLandscapeLayerBrush::Affects(ELandscapeToolTargetType InTargetType, const FName& InWeightmapLayerName) const
{
#if WITH_EDITORONLY_DATA
	if ((BlueprintBrush != nullptr) && BlueprintBrush->IsVisible())
	{
		switch (InTargetType)
		{
			case ELandscapeToolTargetType::Heightmap:
			{
				return BlueprintBrush->AffectsHeightmap();
			}
			case ELandscapeToolTargetType::Weightmap:
			{
				return BlueprintBrush->AffectsWeightmap() && ((InWeightmapLayerName == NAME_None) || BlueprintBrush->AffectsWeightmapLayer(InWeightmapLayerName));
			}
			case ELandscapeToolTargetType::Visibility:
			{
				return BlueprintBrush->AffectsVisibilityLayer();
			}
			default:
				check(false);
		}
	}
#endif // WITH_EDITORONLY_DATA
	return false;
}

bool FLandscapeLayerBrush::AffectsHeightmap() const
{
#if WITH_EDITORONLY_DATA
	return BlueprintBrush && BlueprintBrush->IsVisible() && BlueprintBrush->AffectsHeightmap();
#else
	return false;
#endif
}

bool FLandscapeLayerBrush::AffectsWeightmapLayer(const FName& InWeightmapLayerName) const
{
#if WITH_EDITORONLY_DATA
	return BlueprintBrush && BlueprintBrush->IsVisible() && BlueprintBrush->AffectsWeightmap() && BlueprintBrush->AffectsWeightmapLayer(InWeightmapLayerName);
#else
	return false;
#endif
}

bool FLandscapeLayerBrush::AffectsVisibilityLayer() const
{
#if WITH_EDITORONLY_DATA
	return BlueprintBrush && BlueprintBrush->IsVisible() && BlueprintBrush->AffectsVisibilityLayer();
#else
	return false;
#endif
}

UTextureRenderTarget2D* FLandscapeLayerBrush::RenderLayer(const FLandscapeBrushParameters& InParameters)
{
#if WITH_EDITORONLY_DATA
	TRACE_CPUPROFILER_EVENT_SCOPE(LandscapeLayers_LayerBrushRender);
	if ((BlueprintBrush == nullptr)
		|| ((InParameters.LayerType == ELandscapeToolTargetType::Heightmap) && !AffectsHeightmap())
		|| ((InParameters.LayerType == ELandscapeToolTargetType::Weightmap) && !AffectsWeightmapLayer(InParameters.WeightmapLayerName))
		|| ((InParameters.LayerType == ELandscapeToolTargetType::Visibility) && !AffectsVisibilityLayer()))
	{
		return nullptr;
	}

	return BlueprintBrush->Execute(InParameters);
#else  // WITH_EDITORONLY_DATA
	return nullptr;
#endif // !WITH_EDITORONLY_DATA

}

#endif // WITH_EDITOR


#undef LOCTEXT_NAMESPACE
