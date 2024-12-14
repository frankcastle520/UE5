// Copyright Epic Games, Inc. All Rights Reserved.

#include "LandscapeBlueprintBrushBase.h"
#include "CoreMinimal.h"
#include "LandscapeProxy.h"
#include "Landscape.h"
#include "LandscapeEditResourcesSubsystem.h"
#include "LandscapeEditTypes.h"
#include "LandscapeInfo.h"
#include "LandscapeLayerInfoObject.h"
#include "LandscapePrivate.h"
#include "ProfilingDebugging/RealtimeGPUProfiler.h"
#include "Misc/MapErrors.h"
#include "Misc/UObjectToken.h"
#include "Logging/MessageLog.h"
#include "Logging/TokenizedMessage.h"
#include "Engine/Engine.h"
#include "TextureResource.h"
#include "RHITransition.h"
#include "Materials/MaterialExpressionLandscapeVisibilityMask.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(LandscapeBlueprintBrushBase)

#define LOCTEXT_NAMESPACE "Landscape"

#if WITH_EDITOR
static const uint32 InvalidLastRequestLayersContentUpdateFrameNumber = 0;

static TAutoConsoleVariable<int32> CVarLandscapeBrushPadding(
	TEXT("landscape.BrushFramePadding"),
	5,
	TEXT("The number of frames to wait before pushing a full Landscape update when a brush is calling RequestLandscapeUpdate"));
#endif


// ----------------------------------------------------------------------------------

FLandscapeBrushParameters::FLandscapeBrushParameters(bool bInIsHeightmapMerge, const FTransform& InRenderAreaWorldTransform, const FIntPoint& InRenderAreaSize, UTextureRenderTarget2D* InCombinedResult, const FName& InWeightmapLayerName /*= FName()*/)
	: RenderAreaWorldTransform(InRenderAreaWorldTransform)
	, RenderAreaSize(InRenderAreaSize)
	, CombinedResult(InCombinedResult)
	, LayerType(bInIsHeightmapMerge ? ELandscapeToolTargetType::Heightmap : (InWeightmapLayerName == UMaterialExpressionLandscapeVisibilityMask::ParameterName) ? ELandscapeToolTargetType::Visibility : ELandscapeToolTargetType::Weightmap)
	, WeightmapLayerName(InWeightmapLayerName)
{}


// ----------------------------------------------------------------------------------

ALandscapeBlueprintBrushBase::ALandscapeBlueprintBrushBase(const FObjectInitializer& ObjectInitializer)
	: UpdateOnPropertyChange(true)
	, AffectHeightmap(false)
	, AffectWeightmap(false)
	, AffectVisibilityLayer(false)
#if WITH_EDITORONLY_DATA
	, OwningLandscape(nullptr)
	, bIsVisible(true)
	, LastRequestLayersContentUpdateFrameNumber(InvalidLastRequestLayersContentUpdateFrameNumber)
#endif // WITH_EDITORONLY_DATA
{
#if WITH_EDITOR
	USceneComponent* SceneComp = CreateDefaultSubobject<USceneComponent>(TEXT("RootComponent"));
	RootComponent = SceneComp;

	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_DuringPhysics;
	PrimaryActorTick.bStartWithTickEnabled = true;
	PrimaryActorTick.SetTickFunctionEnable(true);
	bIsEditorOnlyActor = true;
#endif // WITH_EDITOR

#if WITH_EDITORONLY_DATA
	bIsSpatiallyLoaded = false;
#endif // WITH_EDITORONLY_DATA
}

PRAGMA_DISABLE_DEPRECATION_WARNINGS

#if WITH_EDITOR
UTextureRenderTarget2D* ALandscapeBlueprintBrushBase::Execute(const FLandscapeBrushParameters& InParameters)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(ALandscapeBlueprintBrushBase::Execute);
	if ((InParameters.CombinedResult == nullptr) || (OwningLandscape == nullptr))
	{
		return nullptr;
	}

	// Do the render params require a new call to initialize?
	const FIntPoint NewLandscapeRenderTargetSize = FIntPoint(InParameters.CombinedResult->SizeX, InParameters.CombinedResult->SizeY);
	if (!CurrentRenderAreaWorldTransform.Equals(InParameters.RenderAreaWorldTransform) || (CurrentRenderAreaSize != InParameters.RenderAreaSize) || CurrentRenderTargetSize != NewLandscapeRenderTargetSize)
	{
		CurrentRenderAreaWorldTransform = InParameters.RenderAreaWorldTransform;
		CurrentRenderAreaSize = InParameters.RenderAreaSize;
		CurrentRenderTargetSize = NewLandscapeRenderTargetSize;

		TGuardValue<bool> AutoRestore(GAllowActorScriptExecutionInEditor, true);
		Initialize(CurrentRenderAreaWorldTransform, CurrentRenderAreaSize, CurrentRenderTargetSize);
	}

	// Time to render :
	FString LayerDetailString;
	if (InParameters.LayerType != ELandscapeToolTargetType::Heightmap)
	{
		LayerDetailString = FString::Format(TEXT(" ({0})"), { *InParameters.WeightmapLayerName.ToString() });
	}
	UTextureRenderTarget2D* Result = nullptr;
	{
		RHI_BREADCRUMB_EVENT_GAMETHREAD("BP Render (%s): %s", UEnum::GetValueAsString(InParameters.LayerType), LayerDetailString);

		TGuardValue<bool> AutoRestore(GAllowActorScriptExecutionInEditor, true);
		Result = RenderLayer(InParameters);
	}

	// If the BP brush failed to render, we still need to passthrough from the read RT to the write RT in order not to lose what has been merged so far : 
	if ((Result != nullptr) &&
		((Result->SizeX != InParameters.CombinedResult->SizeX) || (Result->SizeY != InParameters.CombinedResult->SizeY)))
	{
		UE_LOG(LogLandscape, Warning, TEXT("In landscape %s, the BP brush %s failed to render for (%s%s). Make sure the brush properly implements RenderLayer and returns a render target of the appropriate size: expected (%i, %i), actual (%i, %i). This brush will be skipped until then."), 
			*OwningLandscape->GetActorLabel(), *GetActorLabel(), *UEnum::GetValueAsString(InParameters.LayerType), *LayerDetailString, InParameters.CombinedResult->SizeX, InParameters.CombinedResult->SizeY, Result->SizeX, Result->SizeY );
		Result = nullptr;
	}

	return Result;
}
#endif // WITH_EDITOR

// Deprecated
UTextureRenderTarget2D* ALandscapeBlueprintBrushBase::Render_Implementation(bool InIsHeightmap, UTextureRenderTarget2D* InCombinedResult, const FName& InWeightmapLayerName)
{
	return nullptr;
}

UTextureRenderTarget2D* ALandscapeBlueprintBrushBase::RenderLayer_Implementation(const FLandscapeBrushParameters& InParameters)
{
	return RenderLayer_Native(InParameters);
}

UTextureRenderTarget2D* ALandscapeBlueprintBrushBase::RenderLayer_Native(const FLandscapeBrushParameters& InParameters)
{
	const bool bIsHeightmap = InParameters.LayerType == ELandscapeToolTargetType::Heightmap;

	// Without any implementation, we call the former Render method so content created before the deprecation will still work as expected.
	return Render(bIsHeightmap, InParameters.CombinedResult, InParameters.WeightmapLayerName);
}

PRAGMA_ENABLE_DEPRECATION_WARNINGS

void ALandscapeBlueprintBrushBase::Initialize_Implementation(const FTransform& InLandscapeTransform, const FIntPoint& InLandscapeSize, const FIntPoint& InLandscapeRenderTargetSize)
{
	Initialize_Native(InLandscapeTransform, InLandscapeSize, InLandscapeRenderTargetSize);
}

void ALandscapeBlueprintBrushBase::RequestLandscapeUpdate(bool bInUserTriggered)
{
#if WITH_EDITOR
	UE_LOG(LogLandscape, Verbose, TEXT("ALandscapeBlueprintBrushBase::RequestLandscapeUpdate"));
	if (OwningLandscape)
	{
		uint32 ModeMask = 0;
		if (CanAffectHeightmap())
		{
			ModeMask |= ELandscapeLayerUpdateMode::Update_Heightmap_Editing_NoCollision;
		}
		if (CanAffectWeightmap() || CanAffectVisibilityLayer())
		{
			ModeMask |= ELandscapeLayerUpdateMode::Update_Weightmap_Editing_NoCollision;
		}
		if (ModeMask)
		{
			OwningLandscape->RequestLayersContentUpdateForceAll((ELandscapeLayerUpdateMode)ModeMask, bInUserTriggered);
			// Just in case differentiate between 0 (default value and frame number)
			LastRequestLayersContentUpdateFrameNumber = GFrameNumber == InvalidLastRequestLayersContentUpdateFrameNumber ? GFrameNumber + 1 : GFrameNumber;
		}
	}
#endif // WITH_EDITOR
}

void ALandscapeBlueprintBrushBase::SetCanAffectHeightmap(bool bInCanAffectHeightmap)
{
#if WITH_EDITORONLY_DATA
	if (bInCanAffectHeightmap != AffectHeightmap)
	{
		Modify();
		AffectHeightmap = bInCanAffectHeightmap;
		if (OwningLandscape)
		{
			OwningLandscape->OnBlueprintBrushChanged();
		}
	}
#endif // WITH_EDITORONLY_DATA
}

void ALandscapeBlueprintBrushBase::SetCanAffectWeightmap(bool bInCanAffectWeightmap)
{
#if WITH_EDITORONLY_DATA
	if (bInCanAffectWeightmap != AffectWeightmap)
	{
		Modify();
		AffectWeightmap = bInCanAffectWeightmap;
		if (OwningLandscape)
		{
			OwningLandscape->OnBlueprintBrushChanged();
		}
	}
#endif // WITH_EDITORONLY_DATA
}

void ALandscapeBlueprintBrushBase::SetCanAffectVisibilityLayer(bool bInCanAffectVisibilityLayer)
{
#if WITH_EDITORONLY_DATA
	if (bInCanAffectVisibilityLayer != AffectVisibilityLayer)
	{
		Modify();
		AffectVisibilityLayer = bInCanAffectVisibilityLayer;
		if (OwningLandscape)
		{
			OwningLandscape->OnBlueprintBrushChanged();
		}
	}
#endif // WITH_EDITORONLY_DATA
}

#if WITH_EDITOR

void ALandscapeBlueprintBrushBase::GetRendererStateInfo(const ULandscapeInfo* InLandscapeInfo,
	UE::Landscape::EditLayers::FEditLayerTargetTypeState& OutSupportedTargetTypeState, UE::Landscape::EditLayers::FEditLayerTargetTypeState& OutEnabledTargetTypeState, TArray<TSet<FName>>& OutRenderGroups) const
{
	// What can the brush do?
	if (CanAffectHeightmap())
	{
		OutSupportedTargetTypeState.AddTargetTypeMask(ELandscapeToolTargetTypeFlags::Heightmap);
	}
	if (CanAffectWeightmap())
	{
		OutSupportedTargetTypeState.AddTargetTypeMask(ELandscapeToolTargetTypeFlags::Weightmap);
	}
	if (CanAffectVisibilityLayer())
	{
		OutSupportedTargetTypeState.AddTargetTypeMask(ELandscapeToolTargetTypeFlags::Visibility);
	}

	// What does it currently do?
	if (AffectsHeightmap())
	{
		OutEnabledTargetTypeState.AddTargetTypeMask(ELandscapeToolTargetTypeFlags::Heightmap);
	}
	if (AffectsWeightmap())
	{
		OutEnabledTargetTypeState.AddTargetTypeMask(ELandscapeToolTargetTypeFlags::Weightmap);
	}
	if (AffectsVisibilityLayer())
	{
		OutEnabledTargetTypeState.AddTargetTypeMask(ELandscapeToolTargetTypeFlags::Visibility);
	}

	// Mark which weightmap is supported/enabled : 
	if (CanAffectWeightmap())
	{
		for (const FLandscapeInfoLayerSettings& InfoLayerSettings : InLandscapeInfo->Layers)
		{
			if (InfoLayerSettings.LayerInfoObj != nullptr)
			{
				FName WeightmapLayerName = InfoLayerSettings.GetLayerName();
				if (CanAffectWeightmapLayer(WeightmapLayerName))
				{
					OutSupportedTargetTypeState.AddWeightmap(WeightmapLayerName);
					if (AffectsWeightmapLayer(WeightmapLayerName))
					{
						OutEnabledTargetTypeState.AddWeightmap(WeightmapLayerName);
					}
				}
			}
		}
	}
}

TArray<UE::Landscape::EditLayers::FEditLayerRenderItem> ALandscapeBlueprintBrushBase::GetRenderItems(const ULandscapeInfo* InLandscapeInfo) const
{
	using namespace UE::Landscape::EditLayers;

	FEditLayerTargetTypeState SupportedTargetTypeState, EnabledTargetTypeState;
	TArray<TSet<FName>> DummyRenderGroups;
	GetRendererStateInfo(InLandscapeInfo, SupportedTargetTypeState, EnabledTargetTypeState, DummyRenderGroups);

	// By default, for landscape BP brushes, we use FInputWorldArea::EType::Infinite, to indicate they can only reliably work when applied globally on the entire landscape
	//  This allows full backwards-compatibility but will prevent landscapes from benefiting from batched merge. Users will be able to indicate their brush works in a local fashion
	//  by overriding this and using another type of input world area
	FInputWorldArea InputWorldArea(FInputWorldArea::CreateInfinite());
	// By default, the brush only writes into the component itself (i.e. it renders to the area that it's currently being asked to render to):
	FOutputWorldArea OutputWorldArea(FOutputWorldArea::CreateLocalComponent());
	
	// Use EnabledTargetTypeState because we only want to tell what we'll actually be able to render to (instead of what we'd potentially be able to render to, i.e. what is "supported" by the brush) : 
	return { FEditLayerRenderItem(EnabledTargetTypeState, InputWorldArea, OutputWorldArea, /*bModifyExistingWeightmapsOnly = */false)};
}

void ALandscapeBlueprintBrushBase::RenderLayer(ILandscapeEditLayerRenderer::FRenderParams& InRenderParams)
{
	using namespace UE::Landscape;

	// By default, use the old way of rendering BP brushes : 

	// Swap the render targets so that the layer's input RT is the latest combined result :
	// The write render target will be accessed as ERHIAccess::CopyDest all along : 
	InRenderParams.MergeRenderContext->CycleBlendRenderTargets(/*InDesiredWriteAccess = */ERHIAccess::CopyDest);
	ULandscapeScratchRenderTarget* WriteRT = InRenderParams.MergeRenderContext->GetBlendRenderTargetWrite();
	ULandscapeScratchRenderTarget* CurrentLayerReadRT = InRenderParams.MergeRenderContext->GetBlendRenderTargetRead();

	const bool bIsHeightmapMerge = InRenderParams.MergeRenderContext->IsHeightmapMerge();

	// Because we only expose UTextureRenderTarget2D to BP, in the case of weightmaps, we need an additional scratch render target 2D that we will copy the 
	//  current result of each paint layer into, so that the BP can use it as its source : 
	ULandscapeEditResourcesSubsystem* LandscapeEditResourcesSubsystem = GEngine->GetEngineSubsystem<ULandscapeEditResourcesSubsystem>();
	check(LandscapeEditResourcesSubsystem != nullptr);
	ULandscapeScratchRenderTarget* CurrentLayerReadRT2D = CurrentLayerReadRT;

	// We might require an additional scratch render target (it will get freed/recycled at the end of this function when the AdditionalScratchTexture goes out of scope) : 
	TUniquePtr<FScratchRenderTargetScope> AdditionalScratchTexture;

	TArray<FName> EnabledWeightmaps;
	if (!bIsHeightmapMerge)
	{
		EnabledWeightmaps = InRenderParams.RendererState.GetEnabledTargetWeightmaps();

		check(CurrentLayerReadRT->IsTexture2DArray() && WriteRT->IsTexture2DArray());
		FScratchRenderTargetParams ScratchRenderTargetParams(TEXT("BPBrushScratchRT"), /*bInExactDimensions = */false, /*bInUseUAV = */false, /*bInTargetArraySlicesIndependently = */false,
			CurrentLayerReadRT->GetResolution(), /*InNumSlices = */0, CurrentLayerReadRT->GetFormat(), CurrentLayerReadRT->GetClearColor(), ERHIAccess::CopyDest);
		// We need a new scratch 2D texture in order to copy the merged result of a single target layer, so that the BP brush can use that as input (as it operates on texture 2D render targets only): 
		AdditionalScratchTexture = MakeUnique<FScratchRenderTargetScope>(ScratchRenderTargetParams);
		CurrentLayerReadRT2D = AdditionalScratchTexture->RenderTarget;

		// The original texture array will be accessed as ERHIAccess::CopySrc all along : 
		CurrentLayerReadRT->TransitionTo(ERHIAccess::CopySrc);
	}

	const int32 NumTargetLayers = InRenderParams.RenderGroupTargetLayerNames.Num();
	for (int32 TargetLayerIndex = 0; TargetLayerIndex < NumTargetLayers; ++TargetLayerIndex)
	{
		const FName TargetLayerName = InRenderParams.RenderGroupTargetLayerNames[TargetLayerIndex];
		RHI_BREADCRUMB_EVENT_GAMETHREAD("Render %s", TargetLayerName);

		// If necessary, copy from the texture array's slice to the scratch render target 2D : 
		if (!bIsHeightmapMerge)
		{
			RHI_BREADCRUMB_EVENT_GAMETHREAD("Copy Source (slice %i) -> %s", TargetLayerIndex, CurrentLayerReadRT2D->GetDebugName());

			ULandscapeScratchRenderTarget::FCopyFromScratchRenderTargetParams CopyParams(CurrentLayerReadRT);
			// Copy from the proper slice in the texture array : 
			CopyParams.SourceSliceIndex = TargetLayerIndex;
			CurrentLayerReadRT2D->CopyFrom(CopyParams);
			CurrentLayerReadRT2D->TransitionTo(ERHIAccess::SRVMask);
		}

		check(CurrentLayerReadRT2D->GetCurrentState() == ERHIAccess::SRVMask);
		check(WriteRT->GetCurrentState() == ERHIAccess::CopyDest);

		UTextureRenderTarget2D* ReadRT2D = CurrentLayerReadRT2D->GetRenderTarget2D();
		// If the BP brush failed to render, we still need to passthrough from the read RT to the write RT in order not to lose what has been merged so far : 
		UTextureRenderTarget2D* OutputRT2D = ReadRT2D;

		// Only render the target layer if it's effectively enabled for this merge : it's possible there are target layers in the render group that we don't support or are not enabled so we have to 
		//  do the validation here first :
		if (bIsHeightmapMerge || EnabledWeightmaps.Contains(TargetLayerName))
		{
			// Execute (i.e. (Initialize/)Render the BP brush) : 
			FLandscapeBrushParameters BrushParameters(bIsHeightmapMerge, InRenderParams.RenderAreaWorldTransform, InRenderParams.RenderAreaSectionRect.Size(), ReadRT2D, TargetLayerName);
			if (UTextureRenderTarget2D* BrushOutputRT2D = Execute(BrushParameters))
			{
				// Only consider the BP brush's result if it's valid :
				OutputRT2D = BrushOutputRT2D;
			}
		}

		// TODO: handle conversion/handling of RT not same size as internal size
		check((OutputRT2D->SizeX == OutputRT2D->SizeX) && (OutputRT2D->SizeY == ReadRT2D->SizeY));

		// Resolve back to the write RT 
		{
			RHI_BREADCRUMB_EVENT_GAMETHREAD("Copy BP Render Result -> %s (slice %i)", WriteRT->GetDebugName(), TargetLayerIndex);

			// The RT returned by the brush is in SRV state so we need a transition: 
			ENQUEUE_RENDER_COMMAND(TransitionToCopySrc)([Resource = OutputRT2D->GetResource()](FRHICommandListImmediate& InRHICmdList) mutable
			{
				InRHICmdList.Transition(FRHITransitionInfo(Resource->TextureRHI, ERHIAccess::SRVMask, ERHIAccess::CopySrc));
			});

			ULandscapeScratchRenderTarget::FCopyFromTextureParams CopyParams(OutputRT2D);
			CopyParams.DestSliceIndex = TargetLayerIndex;
			WriteRT->CopyFrom(CopyParams);

			// It's also expected we leave the RT return by the BP as SRV :
			ENQUEUE_RENDER_COMMAND(TransitionToSRV)([Resource = OutputRT2D->GetResource()](FRHICommandListImmediate& InRHICmdList) mutable
			{
				InRHICmdList.Transition(FRHITransitionInfo(Resource->TextureRHI, ERHIAccess::CopySrc, ERHIAccess::SRVMask));
			});
		}
	}

	// Leave the render targets in the state they're expected to be in: 
	WriteRT->TransitionTo(ERHIAccess::RTV);
	CurrentLayerReadRT->TransitionTo(ERHIAccess::SRVMask);
}

FString ALandscapeBlueprintBrushBase::GetEditLayerRendererDebugName() const
{
	return GetActorNameOrLabel();
}

TArray<UE::Landscape::EditLayers::FEditLayerRendererState> ALandscapeBlueprintBrushBase::GetEditLayerRendererStates(const ULandscapeInfo* InLandscapeInfo, bool bInSkipBrush)
{
	using namespace UE::Landscape::EditLayers;

	if (OwningLandscape == nullptr)
	{
		return {};
	}

	FEditLayerRendererState RendererState(this, InLandscapeInfo);
	// Force the renderer to be fully disabled in case we are asked to skip the brush :
	if (bInSkipBrush)
	{
		RendererState.DisableTargetTypeMask(ELandscapeToolTargetTypeFlags::All);
	}
	return { RendererState };
}

void ALandscapeBlueprintBrushBase::PushDeferredLayersContentUpdate()
{
	// Avoid computing collision and client updates every frame
	// Wait until we didn't trigger any more landscape update requests (padding of a couple of frames)
	if (OwningLandscape != nullptr &&
		LastRequestLayersContentUpdateFrameNumber != InvalidLastRequestLayersContentUpdateFrameNumber &&
		LastRequestLayersContentUpdateFrameNumber + CVarLandscapeBrushPadding.GetValueOnAnyThread() <= GFrameNumber)
	{
		uint32 ModeMask = 0;
		if (AffectsHeightmap())
		{
			ModeMask |= ELandscapeLayerUpdateMode::Update_Heightmap_All;
		}
		if (AffectsWeightmap() || AffectsVisibilityLayer())
		{
			ModeMask |= ELandscapeLayerUpdateMode::Update_Weightmap_All;
		}
		if (ModeMask)
		{
			OwningLandscape->RequestLayersContentUpdateForceAll((ELandscapeLayerUpdateMode)ModeMask);
		}
		LastRequestLayersContentUpdateFrameNumber = InvalidLastRequestLayersContentUpdateFrameNumber;
	}
}

void ALandscapeBlueprintBrushBase::Tick(float DeltaSeconds)
{
	// Forward the Tick to the instances class of this BP
	if (GetClass()->HasAnyClassFlags(CLASS_CompiledFromBlueprint))
	{
		TGuardValue<bool> AutoRestore(GAllowActorScriptExecutionInEditor, true);
		ReceiveTick(DeltaSeconds);
	}

	Super::Tick(DeltaSeconds);
}

bool ALandscapeBlueprintBrushBase::ShouldTickIfViewportsOnly() const
{
	return true;
}

bool ALandscapeBlueprintBrushBase::IsLayerUpdatePending() const
{
	return GFrameNumber < LastRequestLayersContentUpdateFrameNumber + CVarLandscapeBrushPadding.GetValueOnAnyThread();
}

void ALandscapeBlueprintBrushBase::SetIsVisible(bool bInIsVisible)
{
	Modify();
	bIsVisible = bInIsVisible;
	if (OwningLandscape)
	{
		OwningLandscape->OnBlueprintBrushChanged();
	}
}

bool ALandscapeBlueprintBrushBase::CanAffectWeightmapLayer(const FName& InLayerName) const
{
	if (!CanAffectWeightmap())
	{
		return false;
	}

	// By default, it's the same implementation as AffectsWeightmapLayer : if the weightmap layer name is in our list, consider we can affect it :
	//  CanAffectWeightmapLayer can be overridden in child classes that don't use AffectedWeightmapLayers to list the weightmaps they can affect
	return AffectedWeightmapLayers.Contains(InLayerName);
}

bool ALandscapeBlueprintBrushBase::AffectsWeightmapLayer(const FName& InLayerName) const
{
	if (!CanAffectWeightmap())
	{
		return false;
	}

	// By default, it's the same implementation as CanAffectWeightmapLayer : if the weightmap layer name is in our list, consider we do affect it :
	//  AffectsWeightmapLayer can be overridden in child classes that don't use AffectedWeightmapLayers to list the weightmaps they're currently affecting :
	return AffectedWeightmapLayers.Contains(InLayerName);
}

void ALandscapeBlueprintBrushBase::PostEditMove(bool bFinished)
{
	Super::PostEditMove(bFinished);
	RequestLandscapeUpdate();
}

void ALandscapeBlueprintBrushBase::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	if (OwningLandscape && UpdateOnPropertyChange)
	{
		OwningLandscape->OnBlueprintBrushChanged();
	}
}

void ALandscapeBlueprintBrushBase::Destroyed()
{
	Super::Destroyed();
	if (OwningLandscape && !GIsReinstancing)
	{
		OwningLandscape->RemoveBrush(this);
	}
	OwningLandscape = nullptr;
}

void ALandscapeBlueprintBrushBase::CheckForErrors()
{
	Super::CheckForErrors();

	if (GetWorld() && !IsTemplate())
	{
		if (OwningLandscape == nullptr)
		{
			FMessageLog("MapCheck").Error()
				->AddToken(FUObjectToken::Create(this))
				->AddToken(FTextToken::Create(LOCTEXT("MapCheck_Message_MissingLandscape", "This brush requires a Landscape. Add one to the map or remove the brush actor.")))
				->AddToken(FMapErrorToken::Create(TEXT("LandscapeBrushMissingLandscape")));
		}
	}
}

void ALandscapeBlueprintBrushBase::GetRenderDependencies(TSet<UObject*>& OutDependencies)
{
	TArray<UObject*> BPDependencies;
	GetBlueprintRenderDependencies(BPDependencies);

	OutDependencies.Append(BPDependencies);
}

void ALandscapeBlueprintBrushBase::SetOwningLandscape(ALandscape* InOwningLandscape)
{
	if (OwningLandscape == InOwningLandscape)
	{
		return;
	}

	const bool bAlwaysMarkDirty = false;
	Modify(bAlwaysMarkDirty);

	if (OwningLandscape)
	{
		OwningLandscape->OnBlueprintBrushChanged();
	}

	OwningLandscape = InOwningLandscape;

	if (OwningLandscape)
	{
		OwningLandscape->OnBlueprintBrushChanged();
	}
}

ALandscape* ALandscapeBlueprintBrushBase::GetOwningLandscape() const
{
	return OwningLandscape;
}
#endif // WITH_EDITOR

#undef LOCTEXT_NAMESPACE
