// Copyright Epic Games, Inc. All Rights Reserved.

#include "ChaosClothAsset/ClothAsset.h"
#include "ChaosClothAsset/ClothAssetBuilder.h"
#include "ChaosClothAsset/CollectionClothFacade.h"
#include "ChaosClothAsset/ClothComponent.h"
#include "ChaosClothAsset/ClothGeometryTools.h"
#include "ChaosClothAsset/ClothAssetPrivate.h"
#include "ChaosClothAsset/ClothSimulationModel.h"
#include "Chaos/CollectionPropertyFacade.h"
#include "Animation/Skeleton.h"
#if WITH_EDITORONLY_DATA
#include "Animation/AnimationAsset.h"
#endif
#include "Engine/RendererSettings.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/SkinnedAssetAsyncCompileUtils.h"
#include "Features/IModularFeatures.h"
#include "GeometryCollection/ManagedArrayCollection.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "Rendering/SkeletalMeshModel.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "UObject/Package.h"
#include "EngineUtils.h"
#include "Engine/Engine.h"
#if WITH_EDITOR
#include "IMeshBuilderModule.h"
#include "DerivedDataCacheInterface.h"
#endif

#include UE_INLINE_GENERATED_CPP_BY_NAME(ClothAsset)

// If Chaos cloth asset derived data needs to be rebuilt (new format, serialization differences, etc.) replace the version GUID below with a new one. 
// In case of merge conflicts with DDC versions, you MUST generate a new GUID and set this new GUID as the version.
#define CHAOS_CLOTH_ASSET_DERIVED_DATA_VERSION TEXT("5C041E93563044A69646B5E98F152B7C")


namespace UE::Chaos::ClothAsset::Private
{
bool bClothCollectionOnlyCookPropertyFacade = true;
FAutoConsoleVariableRef CVarClothCollectionOnlyCookPropertyFacade(
	TEXT("p.ClothCollectionOnlyCookPropertyFacade"),
	bClothCollectionOnlyCookPropertyFacade,
	TEXT("Default setting for culling propertys on the cloth collection during the cook. Default[false]"));


const TCHAR* MinLodQualityLevelCVarName = TEXT("p.ClothAsset.MinLodQualityLevel");
const TCHAR* MinLodQualityLevelScalabilitySection = TEXT("ViewDistanceQuality");
int32 MinLodQualityLevel = -1;
FAutoConsoleVariableRef CVarClothAssetMinLodQualityLevel(
	MinLodQualityLevelCVarName,
	MinLodQualityLevel,
	TEXT("The quality level for the Min stripping LOD. \n"),
	FConsoleVariableDelegate::CreateStatic(&UChaosClothAsset::OnLodStrippingQualityLevelChanged),
	ECVF_Scalability);
	
::Chaos::FChaosArchive& Serialize(::Chaos::FChaosArchive& Ar, TArray<TSharedRef<FManagedArrayCollection>>& ClothCollections)
{
	Ar.UsingCustomVersion(FUE5MainStreamObjectVersion::GUID);

	if (Ar.IsLoading() && Ar.CustomVer(FUE5MainStreamObjectVersion::GUID) < FUE5MainStreamObjectVersion::ClothCollectionSingleLodSchema)
	{
		// Cloth assets before this version had a single ClothCollection with a completely different schema.
		ClothCollections.Empty(1);
		TSharedRef<FManagedArrayCollection>& ClothCollection = ClothCollections.Emplace_GetRef(MakeShared<FManagedArrayCollection>());
		ClothCollection->Serialize(Ar);

		// Now we're just going to hard reset and define a new schema.
		ClothCollection->Reset();
		FCollectionClothFacade ClothFacade(ClothCollection);
		ClothFacade.DefineSchema();

		return Ar;
	}
	else
	{
		// This is following Serialize for Arrays
		ClothCollections.CountBytes(Ar);
		int32 SerializeNum = Ar.IsLoading() ? 0 : ClothCollections.Num();
		Ar << SerializeNum;
		if (SerializeNum == 0)
		{
			// if we are loading, then we have to reset the size to 0, in case it isn't currently 0
			if (Ar.IsLoading())
			{
				ClothCollections.Empty();
			}
			return Ar;
		}
		check(SerializeNum >= 0);

		if (Ar.IsError() || SerializeNum < 0)
		{
			Ar.SetError();
			return Ar;
		}
		if (Ar.IsLoading())
		{
			// Required for resetting ArrayNum
			ClothCollections.Empty(SerializeNum);

			for (int32 i = 0; i < SerializeNum; i++)
			{
				TSharedRef<FManagedArrayCollection>& ClothCollection = ClothCollections.Emplace_GetRef(MakeShared<FManagedArrayCollection>());
				ClothCollection->Serialize(Ar);
			}
		}
		else
		{
			check(SerializeNum == ClothCollections.Num());

			for (int32 i = 0; i < SerializeNum; i++)
			{
				ClothCollections[i]->Serialize(Ar);
			}
		}

		return Ar;
	}
}

TArray<TSharedRef<FManagedArrayCollection>> TrimOnCook(const FString InAssetName, const TArray<TSharedRef<FManagedArrayCollection>>& InClothCollections)
{
	int32 Index = 0;
#if WITH_EDITORONLY_DATA
	if (bClothCollectionOnlyCookPropertyFacade)
	{
		TArray<TSharedRef<FManagedArrayCollection>> OutputCollections;
		for (TSharedRef<FManagedArrayCollection> ClothCollection : InClothCollections)
		{
			TSharedPtr<FManagedArrayCollection> PropertyCollection(new FManagedArrayCollection());
			::Chaos::Softs::FCollectionPropertyMutableFacade CollectionPropertyMutableFacade(PropertyCollection);
			CollectionPropertyMutableFacade.Copy(*ClothCollection);
			OutputCollections.Add(TSharedRef<FManagedArrayCollection>(PropertyCollection.ToSharedRef()));
			UE_LOG(LogChaosClothAsset, Display, TEXT("TrimOnCook[ON] %s:[%d] [size:%d]"),
				*InAssetName, Index++, PropertyCollection->GetAllocatedSize());
		}
		return OutputCollections;
	}
#endif
	for (TSharedRef<FManagedArrayCollection> ClothCollection : InClothCollections)
	{
		UE_LOG(LogChaosClothAsset, Display, TEXT("TrimOnCook [OFF] %s:[%d] [size:%d]"),
			*InAssetName, Index++, ClothCollection->GetAllocatedSize());
	}
	return InClothCollections;
}

}

UChaosClothAsset::UChaosClothAsset(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, MinQualityLevelLOD(0)
	, DisableBelowMinLodStripping(FPerPlatformBool(false))
	, MinLod(0)
#if WITH_EDITORONLY_DATA
	, MeshModel(MakeShareable(new FSkeletalMeshModel()))
#endif
{
	// Setup a single LOD's Cloth Collection
	TSharedRef<FManagedArrayCollection>& ClothCollection = GetClothCollections().Emplace_GetRef(MakeShared<FManagedArrayCollection>());
	UE::Chaos::ClothAsset::FCollectionClothFacade ClothFacade(ClothCollection);
	ClothFacade.DefineSchema();

	// Add the LODInfo for the default LOD 0
	LODInfo.SetNum(1);

	// Set default skeleton (must be done after having added the LOD)
	constexpr bool bRebuildModels = false;
	constexpr bool bRebindMeshes = false;
	SetReferenceSkeleton(nullptr, bRebuildModels, bRebindMeshes);

	MinQualityLevelLOD.SetQualityLevelCVarForCooking(UE::Chaos::ClothAsset::Private::MinLodQualityLevelCVarName, UE::Chaos::ClothAsset::Private::MinLodQualityLevelScalabilitySection);
}

UChaosClothAsset::UChaosClothAsset(FVTableHelper& Helper)
	: Super(Helper)
{
}

PRAGMA_DISABLE_DEPRECATION_WARNINGS
UChaosClothAsset::~UChaosClothAsset() = default;
PRAGMA_ENABLE_DEPRECATION_WARNINGS

FSkeletalMeshLODInfo* UChaosClothAsset::GetLODInfo(int32 Index)
{
	return LODInfo.IsValidIndex(Index) ? &LODInfo[Index] : nullptr;
}

const FSkeletalMeshLODInfo* UChaosClothAsset::GetLODInfo(int32 Index) const
{
	return LODInfo.IsValidIndex(Index) ? &LODInfo[Index] : nullptr;
}

FMatrix UChaosClothAsset::GetComposedRefPoseMatrix(FName InBoneName) const
{
	FMatrix LocalPose(FMatrix::Identity);

	if (InBoneName != NAME_None)
	{
		const int32 BoneIndex = GetRefSkeleton().FindBoneIndex(InBoneName);
		if (BoneIndex != INDEX_NONE)
		{
			return GetComposedRefPoseMatrix(BoneIndex);
		}
		// TODO: Might need to add sockets like on the SkeletalMesh
	}

	return LocalPose;
}

void UChaosClothAsset::Serialize(FArchive& Ar)
{
	Super::Serialize(Ar);

	bool bCooked = Ar.IsCooking();
	Ar << bCooked;

	if (bCooked && Ar.IsSaving())
	{
		TArray<TSharedRef<FManagedArrayCollection>> OutputCollections = 
			UE::Chaos::ClothAsset::Private::TrimOnCook(GetPathName(), GetClothCollections());
		Chaos::FChaosArchive ChaosArchive(Ar);
		UE::Chaos::ClothAsset::Private::Serialize(ChaosArchive, OutputCollections);
	}
	else
	{
		Chaos::FChaosArchive ChaosArchive(Ar);
		UE::Chaos::ClothAsset::Private::Serialize(ChaosArchive, GetClothCollections());
	}

	Ar << GetRefSkeleton();

	if (bCooked && !IsTemplate() && !Ar.IsCountingMemory())  // Counting of these resources are done in GetResourceSizeEx, so skip these when counting memory
	{
		if (Ar.IsLoading())
		{
			SetResourceForRendering(MakeUnique<FSkeletalMeshRenderData>());
		}
		GetResourceForRendering()->Serialize(Ar, this);

		if (!ClothSimulationModel.IsValid())
		{
			ClothSimulationModel = MakeShared<FChaosClothSimulationModel>();
		}
		UScriptStruct* const Struct = FChaosClothSimulationModel::StaticStruct();
		Struct->SerializeTaggedProperties(Ar, (uint8*)ClothSimulationModel.Get(), Struct, nullptr);
	}
}

void UChaosClothAsset::PostLoad()
{
	Super::PostLoad();
}

#if WITH_EDITOR
void UChaosClothAsset::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	if (PropertyChangedEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(UChaosClothAsset, PhysicsAsset))
	{
		ReregisterComponents();
	}
	Super::PostEditChangeProperty(PropertyChangedEvent);
	InvalidateDataflowContents();
}
#endif // #if WITH_EDITOR

void UChaosClothAsset::GetResourceSizeEx(FResourceSizeEx& CumulativeResourceSize)
{
	Super::GetResourceSizeEx(CumulativeResourceSize);

	if (GetResourceForRendering())
	{
		GetResourceForRendering()->GetResourceSizeEx(CumulativeResourceSize);
	}

	if (ClothSimulationModel)
	{
		ClothSimulationModel->GetResourceSizeEx(CumulativeResourceSize);
	}

#if !UE_BUILD_SHIPPING
	FString MemoryReport;
	MemoryReport.Appendf(TEXT("---- Memory report for Cloth Asset [%s] ----"), *this->GetName());

	FResourceSizeEx RenderDataResourceSize;
	if (GetResourceForRendering())
	{
		for (int32 LodIndex = 0; LodIndex < GetResourceForRendering()->LODRenderData.Num(); ++LodIndex)
		{
			FResourceSizeEx LODRenderDataResourceSize;
			GetResourceForRendering()->LODRenderData[LodIndex].GetResourceSizeEx(LODRenderDataResourceSize);
			MemoryReport.Appendf(TEXT("\n LODRenderData LOD%d size: %ld bytes"), LodIndex, LODRenderDataResourceSize.GetTotalMemoryBytes());
		}

		GetResourceForRendering()->GetResourceSizeEx(RenderDataResourceSize);
	}
	MemoryReport.Appendf(TEXT("\n Total RenderData size: %ld bytes"), RenderDataResourceSize.GetTotalMemoryBytes());

	FResourceSizeEx ClothSimulationModelResourceSize;
	if (ClothSimulationModel)
	{
		for (int32 LodIndex = 0; LodIndex < ClothSimulationModel->GetNumLods(); ++LodIndex)
		{
			FResourceSizeEx ClothSimulationLodModelResourceSize;
			ClothSimulationModel->ClothSimulationLodModels[LodIndex].GetResourceSizeEx(ClothSimulationLodModelResourceSize);

			const FName Tag(*FString::Printf(TEXT("ClothSimulationLodModel[%d]"), LodIndex));
			MemoryReport.Appendf(TEXT("\n ClothSimulationLodModel LOD%d size: %ld bytes"), LodIndex, ClothSimulationLodModelResourceSize.GetTotalMemoryBytes());
		}

		ClothSimulationModel->GetResourceSizeEx(ClothSimulationModelResourceSize);
	}
	MemoryReport.Appendf(TEXT("\n Total ClothSimulationModel size: %ld bytes"), ClothSimulationModelResourceSize.GetTotalMemoryBytes());

	const int64 TotalResourceSize = RenderDataResourceSize.GetTotalMemoryBytes() + ClothSimulationModelResourceSize.GetTotalMemoryBytes();
	MemoryReport.Appendf(
		TEXT("\n Total resource size for Cloth Asset [%s]: %ld bytes (%.3f MB)"),
		*this->GetName(),
		TotalResourceSize,
		(float)TotalResourceSize / (1024.f * 1024.f));

	const int64 TotalSize = CumulativeResourceSize.GetTotalMemoryBytes();
	MemoryReport.Appendf(
		TEXT("\n Total size for Cloth Asset [%s]: %ld bytes (%.3f MB)"),
		*this->GetName(),
		TotalSize,
		(float)TotalSize / (1024.f * 1024.f));

	UE_LOG(LogChaosClothAsset, Display, TEXT("\n%s"), *MemoryReport);
#endif
}

void UChaosClothAsset::BeginPostLoadInternal(FSkinnedAssetPostLoadContext& Context)
{
#if WITH_EDITOR
	TRACE_CPUPROFILER_EVENT_SCOPE(UChaosClothAsset::BeginPostLoadInternal);

	checkf(IsInGameThread(), TEXT("Cannot execute function UChaosClothAsset::BeginPostLoadInternal asynchronously. Asset: %s"), *GetFullName());
	SetInternalFlags(EInternalObjectFlags::Async);

	// Lock all properties that should not be modified/accessed during async post-load
	AcquireAsyncProperty();

	// This scope allows us to use any locked properties without causing stalls
	FSkinnedAssetAsyncBuildScope AsyncBuildScope(this);

	// Make sure that the collection is still compatible and valid
	bool bAnyInvalidLods = false;
	if (GetClothCollections().IsEmpty())
	{
		UE_LOG(LogChaosClothAsset, Warning, TEXT("Invalid Cloth Collection (no LODs) found while loading Cloth Asset %s."), *GetFullName());
		TSharedRef<FManagedArrayCollection>& ClothCollection = GetClothCollections().Emplace_GetRef(MakeShared<FManagedArrayCollection>());
		UE::Chaos::ClothAsset::FCollectionClothFacade ClothFacade(ClothCollection);
		ClothFacade.DefineSchema();
		bAnyInvalidLods = true;
	}

	const int32 NumLods = GetClothCollections().Num();
	check(NumLods >= 1);  // The default LOD 0 should be present now if it ever was missing
	LODInfo.SetNum(NumLods);  // Always keep a matching number of LODInfos

	bool bAnyInvalidSkeletons = false;
	for (int32 LODIndex = 0; LODIndex < NumLods; ++LODIndex)
	{
		TSharedRef<FManagedArrayCollection>& ClothCollection = GetClothCollections()[LODIndex];

		UE::Chaos::ClothAsset::FCollectionClothFacade ClothFacade(ClothCollection);
		if (!ClothFacade.IsValid())
		{
			UE_LOG(LogChaosClothAsset, Warning, TEXT("Invalid Cloth Collection found at LOD %i while loading Cloth Asset %s."), LODIndex, *GetFullName());
			ClothCollection = MakeShared<FManagedArrayCollection>();
			ClothFacade = UE::Chaos::ClothAsset::FCollectionClothFacade(ClothCollection);
			ClothFacade.DefineSchema();
			bAnyInvalidLods = true;
			bAnyInvalidSkeletons = true;
		}
		else if (ClothFacade.GetSkeletalMeshPathName().IsEmpty())
		{
			bAnyInvalidSkeletons = true;
		}
	}
	if (bAnyInvalidLods)
	{
		SetPhysicsAsset(PhysicsAsset);  // Re-update the collection with the physics asset information if any
	}
	if (bAnyInvalidSkeletons)
	{
		constexpr bool bRebuildModels = false;
		constexpr bool bRebindMeshes = true;  // Best to rebind the mesh when reloading broken data
		SetReferenceSkeleton(nullptr, bRebuildModels, bRebindMeshes);
	}

	// We're done touching the ClothCollections, so can unlock for read
	ReleaseAsyncProperty((uint64)EClothAssetAsyncProperties::ClothCollection, ESkinnedAssetAsyncPropertyLockType::WriteOnly);

	BuildClothSimulationModel();  // TODO: Cache ClothSimulationModel?

	BuildMeshModel();

	// Convert PerPlatForm data to PerQuality if perQuality data have not been serialized.
	// Also test default value, since PerPlatformData can have Default !=0 and no PerPlatform data overrides.
	const bool bConvertMinLODData = (MinQualityLevelLOD.PerQuality.Num() == 0 && MinQualityLevelLOD.Default == 0) && (MinLod.PerPlatform.Num() != 0 || MinLod.Default != 0);
	if (IsMinLodQualityLevelEnable() && bConvertMinLODData)
	{
		constexpr bool bRequireAllPlatformsKnownTrue = true;
		MinQualityLevelLOD.ConvertQualityLevelDataUsingCVar(MinLod.PerPlatform, MinLod.Default, bRequireAllPlatformsKnownTrue);
	}
#endif // #if WITH_EDITOR
}

void UChaosClothAsset::ExecutePostLoadInternal(FSkinnedAssetPostLoadContext& Context)
{
#if WITH_EDITOR
	TRACE_CPUPROFILER_EVENT_SCOPE(UChaosClothAsset::ExecutePostLoadInternal);

	// This scope allows us to use any locked properties without causing stalls
	FSkinnedAssetAsyncBuildScope AsyncBuildScope(this);

	if (!GetOutermost()->bIsCookedForEditor)
	{
		if (GetResourceForRendering() == nullptr)
		{
			CacheDerivedData(&Context);
			Context.bHasCachedDerivedData = true;
		}
	}
#endif // WITH_EDITOR
}

void UChaosClothAsset::FinishPostLoadInternal(FSkinnedAssetPostLoadContext& Context)
{
#if WITH_EDITOR
	TRACE_CPUPROFILER_EVENT_SCOPE(UChaosClothAsset::FinishPostLoadInternal);

	checkf(IsInGameThread(), TEXT("Cannot execute function UChaosClothAsset::FinishPostLoadInternal asynchronously. Asset: %s"), *this->GetFullName());
	ClearInternalFlags(EInternalObjectFlags::Async);

	// This scope allows us to use any locked properties without causing stalls
	FSkinnedAssetAsyncBuildScope AsyncBuildScope(this);
#endif

	if (FApp::CanEverRender())
	{
		InitResources();
	}
	else
	{
		// Update any missing data when cooking.
		UpdateUVChannelData(false);
	}

	CalculateInvRefMatrices();
	CalculateBounds();

#if WITH_EDITOR
	ReleaseAsyncProperty();
#endif
}

void UChaosClothAsset::BeginDestroy()
{
	check(IsInGameThread());

	Super::BeginDestroy();

	// Release the mesh's render resources now
	ReleaseResources();
}

bool UChaosClothAsset::IsReadyForFinishDestroy()
{
	if (!Super::IsReadyForFinishDestroy())
	{
		return false;
	}

	ReleaseResources();

	// see if we have hit the resource flush fence
	return ReleaseResourcesFence.IsFenceComplete();
}

void UChaosClothAsset::InitResources()
{
	LLM_SCOPE_BYNAME(TEXT("ClothAsset/InitResources"));

	// Build the material channel data used by the texture streamer
	UpdateUVChannelData(false);

	if (SkeletalMeshRenderData.IsValid())
	{
		TArray<UMorphTarget*> DummyMorphTargets;  // Not even used by InitResources() at the moment
		SkeletalMeshRenderData->InitResources(false, DummyMorphTargets, this);
	}
}

void UChaosClothAsset::ReleaseResources()
{
	if (SkeletalMeshRenderData && SkeletalMeshRenderData->IsInitialized())
	{
		if (GIsEditor && !GIsPlayInEditorWorld)
		{
			// Flush the rendering command to be sure there is no command left that can create/modify a rendering resource
			FlushRenderingCommands();
		}

		SkeletalMeshRenderData->ReleaseResources();

		// Insert a fence to signal when these commands completed
		ReleaseResourcesFence.BeginFence();
	}
}

void UChaosClothAsset::CalculateInvRefMatrices()
{
	auto GetRefPoseMatrix = [this](int32 BoneIndex)->FMatrix
	{
		check(BoneIndex >= 0 && BoneIndex < GetRefSkeleton().GetRawBoneNum());
		FTransform BoneTransform = GetRefSkeleton().GetRawRefBonePose()[BoneIndex];
		BoneTransform.NormalizeRotation();  // Make sure quaternion is normalized!
		return BoneTransform.ToMatrixWithScale();
	};

	const int32 NumRealBones = GetRefSkeleton().GetRawBoneNum();

	RefBasesInvMatrix.Empty(NumRealBones);
	RefBasesInvMatrix.AddUninitialized(NumRealBones);

	// Reset cached mesh-space ref pose
	TArray<FMatrix> ComposedRefPoseMatrices;
	ComposedRefPoseMatrices.SetNumUninitialized(NumRealBones);

	// Precompute the Mesh.RefBasesInverse
	for (int32 BoneIndex = 0; BoneIndex < NumRealBones; ++BoneIndex)
	{
		// Render the default pose
		ComposedRefPoseMatrices[BoneIndex] = GetRefPoseMatrix(BoneIndex);

		// Construct mesh-space skeletal hierarchy
		if (BoneIndex > 0)
		{
			int32 Parent = GetRefSkeleton().GetRawParentIndex(BoneIndex);
			ComposedRefPoseMatrices[BoneIndex] = ComposedRefPoseMatrices[BoneIndex] * ComposedRefPoseMatrices[Parent];
		}

		FVector XAxis, YAxis, ZAxis;
		ComposedRefPoseMatrices[BoneIndex].GetScaledAxes(XAxis, YAxis, ZAxis);
		if (XAxis.IsNearlyZero(UE_SMALL_NUMBER) &&
			YAxis.IsNearlyZero(UE_SMALL_NUMBER) &&
			ZAxis.IsNearlyZero(UE_SMALL_NUMBER))
		{
			// This is not allowed, warn them
			UE_LOG(
				LogChaosClothAsset,
				Warning,
				TEXT("Reference Pose for asset %s for joint (%s) includes NIL matrix. Zero scale isn't allowed on ref pose."),
				*GetPathName(),
				*GetRefSkeleton().GetBoneName(BoneIndex).ToString());
		}

		// Precompute inverse so we can use from-refpose-skin vertices
		RefBasesInvMatrix[BoneIndex] = FMatrix44f(ComposedRefPoseMatrices[BoneIndex].Inverse());
	}
}

void UChaosClothAsset::CalculateBounds()
{
	using namespace UE::Chaos::ClothAsset;

	FBox BoundingBox(ForceInit);

	for (const TSharedRef<const FManagedArrayCollection>& ClothCollection : const_cast<const UChaosClothAsset*>(this)->GetClothCollections())
	{
		const FCollectionClothConstFacade Cloth(ClothCollection);
		const TConstArrayView<FVector3f> RenderPositionArray = Cloth.GetRenderPosition();

		for (const FVector3f& RenderPosition : RenderPositionArray)
		{
			BoundingBox += (FVector)RenderPosition;
		}
	}

	Bounds = FBoxSphereBounds(BoundingBox);
}

void UChaosClothAsset::Build(TArray<FChaosClothAssetLodTransitionDataCache>* InOutTransitionCache)
{
	using namespace UE::Chaos::ClothAsset;

#if WITH_EDITOR
	FSkinnedAssetAsyncBuildScope AsyncBuildScope(this);

	FSkinnedAssetBuildContext Context;
	BeginBuildInternal(Context);
#else
	ReleaseResources();
#endif

	// Set a new Guid to invalidate the DDC
	AssetGuid = FGuid::NewGuid();

	// Rebuild matrices
	CalculateInvRefMatrices();

	// Update bounds
	CalculateBounds();

	// Add LODs to the render data
	const int32 NumLods = FMath::Max(GetClothCollections().Num(), 1);  // The render data will always look for at least one default LOD 0

	// Rebuild LOD Infos
	LODInfo.Reset(NumLods);
	LODInfo.AddDefaulted(NumLods);  // TODO: Expose some properties to fill up the LOD infos

	// Build simulation model
	BuildClothSimulationModel(InOutTransitionCache);

#if WITH_EDITOR
	// Rebuild LOD Model
	BuildMeshModel();

	// Load/save render data from/to DDC
	ExecuteBuildInternal(Context);
#endif

	if (FApp::CanEverRender())
	{
		InitResources();
	}

#if WITH_EDITOR
	FinishBuildInternal(Context);
#endif

	// Re-register any components using this asset to restart the simulation with the updated asset
	ReregisterComponents();
}

#if WITH_EDITOR
void UChaosClothAsset::ExecuteBuildInternal(FSkinnedAssetBuildContext& Context)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UChaosClothAsset::ExecuteBuildInternal);

	// This scope allows us to use any locked properties without causing stalls
	FSkinnedAssetAsyncBuildScope AsyncBuildScope(this);

	// rebuild render data from imported model
	CacheDerivedData(&Context);

	// Build the material channel data used by the texture streamer
	UpdateUVChannelData(true);
}

void UChaosClothAsset::BeginBuildInternal(FSkinnedAssetBuildContext& Context)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UChaosClothAsset::BeginBuildInternal);

	SetInternalFlags(EInternalObjectFlags::Async);
	
	// Unregister all instances of this component
	Context.RecreateRenderStateContext = MakeUnique<FSkinnedMeshComponentRecreateRenderStateContext>(this, false);

	// Release the render data resources.
	ReleaseResources();

	// Flush the resource release commands to the rendering thread to ensure that the build doesn't occur while a resource is still
	// allocated, and potentially accessing the UChaosClothAsset.
	ReleaseResourcesFence.Wait();

	// Lock all properties that should not be modified/accessed during async post-load
	AcquireAsyncProperty();
}

void UChaosClothAsset::FinishBuildInternal(FSkinnedAssetBuildContext& Context)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UChaosClothAsset::FinishBuildInternal);

	ClearInternalFlags(EInternalObjectFlags::Async);

	ReleaseAsyncProperty();
}
#endif // #if WITH_EDITOR

#if WITH_EDITOR
void UChaosClothAsset::BuildMeshModel()
{
	using namespace UE::Chaos::ClothAsset;

	const int32 NumLods = const_cast<const UChaosClothAsset*>(this)->GetClothCollections().Num();

	// Clear current LOD models
	check(GetImportedModel());  // MeshModel should always be created in the Cloth Asset constructor WITH_EDITORONLY_DATA
	GetImportedModel()->LODModels.Reset(NumLods);

	// Get the running platform
	ITargetPlatformManagerModule& TargetPlatformManager = GetTargetPlatformManagerRef();
	ITargetPlatform* const TargetPlatform = TargetPlatformManager.GetRunningTargetPlatform();

	// Rebuild each LOD models
	for (int32 LodIndex = 0; LodIndex < NumLods; ++LodIndex)
	{
		GetImportedModel()->LODModels.Add(new FSkeletalMeshLODModel());
		BuildLODModel(TargetPlatform, LodIndex);
	}
}
#endif  // #if WITH_EDITOR

void UChaosClothAsset::BuildClothSimulationModel(TArray<FChaosClothAssetLodTransitionDataCache>* InOutTransitionCache)
{
	ClothSimulationModel = MakeShared<FChaosClothSimulationModel>(const_cast<const UChaosClothAsset*>(this)->GetClothCollections(), 
		GetRefSkeleton(), InOutTransitionCache);
}

const FMeshUVChannelInfo* UChaosClothAsset::GetUVChannelData(int32 MaterialIndex) const
{
	if (GetMaterials().IsValidIndex(MaterialIndex))
	{
		// TODO: enable ensure when UVChannelData is setup
		//ensure(GetMaterials()[MaterialIndex].UVChannelData.bInitialized);
		return &GetMaterials()[MaterialIndex].UVChannelData;
	}

	return nullptr;
}

FSkeletalMeshRenderData* UChaosClothAsset::GetResourceForRendering() const
{
	WaitUntilAsyncPropertyReleased(EClothAssetAsyncProperties::RenderData);
	return SkeletalMeshRenderData.Get();
}

void UChaosClothAsset::SetResourceForRendering(TUniquePtr<FSkeletalMeshRenderData>&& InSkeletalMeshRenderData)
{
	WaitUntilAsyncPropertyReleased(EClothAssetAsyncProperties::RenderData);
	SkeletalMeshRenderData = MoveTemp(InSkeletalMeshRenderData);
}

void UChaosClothAsset::WaitUntilAsyncPropertyReleased(EClothAssetAsyncProperties AsyncProperties, ESkinnedAssetAsyncPropertyLockType LockType) const
{
	// Cast strongly typed enum to uint64
	WaitUntilAsyncPropertyReleasedInternal((uint64)AsyncProperties, LockType);
}

FString UChaosClothAsset::GetAsyncPropertyName(uint64 Property) const
{
	return StaticEnum<EClothAssetAsyncProperties>()->GetNameByValue(Property).ToString();
}

bool UChaosClothAsset::IsMinLodQualityLevelEnable() const
{
	return (GEngine && GEngine->UseClothAssetMinLODPerQualityLevels);
}

void UChaosClothAsset::OnLodStrippingQualityLevelChanged(IConsoleVariable* Variable) 
{
#if WITH_EDITOR || PLATFORM_DESKTOP
	if (GEngine && GEngine->UseClothAssetMinLODPerQualityLevels)
	{
		for (TObjectIterator<UChaosClothAsset> It; It; ++It)
		{
			UChaosClothAsset* ClothAsset = *It;
			if (ClothAsset && ClothAsset->GetQualityLevelMinLod().PerQuality.Num() > 0)
			{
				FSkinnedMeshComponentRecreateRenderStateContext Context(ClothAsset, false);
			}
		}
	}
#endif
}

int32 UChaosClothAsset::GetMinLodIdx(bool bForceLowestLODIndex) const
{
	if (IsMinLodQualityLevelEnable())
	{
		return bForceLowestLODIndex ? GetQualityLevelMinLod().GetLowestValue() : GetQualityLevelMinLod().GetValue(UE::Chaos::ClothAsset::Private::MinLodQualityLevel);
	}
	else
	{
		return GetMinLod().GetValue();
	}
}

int32 UChaosClothAsset::GetPlatformMinLODIdx(const ITargetPlatform* InTargetPlatform) const
{
#if WITH_EDITOR
	check(InTargetPlatform);
	if (IsMinLodQualityLevelEnable())
	{
		// get all supported quality level from scalability + engine ini files
		return GetQualityLevelMinLod().GetValueForPlatform(InTargetPlatform);
	}
	else
	{
		return GetMinLod().GetValueForPlatform(*InTargetPlatform->IniPlatformName());
	}
#else
	return 0;
#endif
}

const FPerPlatformInt& UChaosClothAsset::GetMinLod() const
{
	return MinLod;
}

#if WITH_EDITOR
void UChaosClothAsset::CacheDerivedData(FSkinnedAssetCompilationContext* Context)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UChaosClothAsset::CacheDerivedData);
	check(Context);

	// Cache derived data for the running platform.
	ITargetPlatform* RunningPlatform = GetTargetPlatformManagerRef().GetRunningTargetPlatform();
	check(RunningPlatform);

	// Create the render data
	SetResourceForRendering(MakeUnique<FSkeletalMeshRenderData>());

	// Load render data from DDC, or generate it and save to DDC
	GetResourceForRendering()->Cache(RunningPlatform, this, Context);

}

void UChaosClothAsset::BuildLODModel(const ITargetPlatform* TargetPlatform, int32 LODIndex)
{
	check(GetImportedModel() && GetImportedModel()->LODModels.IsValidIndex(LODIndex));
	FBuilder::BuildLod(GetImportedModel()->LODModels[LODIndex], *this, LODIndex, TargetPlatform);
}

FString UChaosClothAsset::BuildDerivedDataKey(const ITargetPlatform* TargetPlatform)
{
	FString KeySuffix(TEXT(""));
	KeySuffix += AssetGuid.ToString();

	FString TmpPartialKeySuffix;
	//Synchronize the user data that are part of the key
	GetImportedModel()->SyncronizeLODUserSectionsData();

	// Model GUID is not generated so exclude GetImportedModel()->GetIdString() from DDC key.

	// Add the hashed string generated from the model data 
	TmpPartialKeySuffix = GetImportedModel()->GetLODModelIdString();
	KeySuffix += TmpPartialKeySuffix;

	//Add the max gpu bone per section
	const int32 MaxGPUSkinBones = FGPUBaseSkinVertexFactory::GetMaxGPUSkinBones(TargetPlatform);
	KeySuffix += FString::FromInt(MaxGPUSkinBones);
	// Add unlimited bone influences mode
	IMeshBuilderModule::GetForPlatform(TargetPlatform).AppendToDDCKey(KeySuffix, true);
	const bool bUnlimitedBoneInfluences = FGPUBaseSkinVertexFactory::GetUnlimitedBoneInfluences(TargetPlatform);
	KeySuffix += bUnlimitedBoneInfluences ? "1" : "0";

	// Include the global default bone influences limit in case any LODs don't set an explicit limit (highly likely)
	KeySuffix += FString::FromInt(GetDefault<URendererSettings>()->DefaultBoneInfluenceLimit.GetValueForPlatform(*TargetPlatform->IniPlatformName()));

	// Add LODInfoArray
	TmpPartialKeySuffix = TEXT("");
	TArray<FSkeletalMeshLODInfo>& LODInfos = GetLODInfoArray();
	for (int32 LODIndex = 0; LODIndex < GetLODNum(); ++LODIndex)
	{
		check(LODInfos.IsValidIndex(LODIndex));
		FSkeletalMeshLODInfo& LOD = LODInfos[LODIndex];
		LOD.BuildGUID = LOD.ComputeDeriveDataCacheKey(nullptr);	// TODO: FSkeletalMeshLODGroupSettings
		TmpPartialKeySuffix += LOD.BuildGUID.ToString(EGuidFormats::Digits);
	}
	KeySuffix += TmpPartialKeySuffix;

	//KeySuffix += GetHasVertexColors() ? "1" : "0";
	//KeySuffix += GetVertexColorGuid().ToString(EGuidFormats::Digits);

	//if (GetEnableLODStreaming(TargetPlatform))
	//{
	//	const int32 MaxNumStreamedLODs = GetMaxNumStreamedLODs(TargetPlatform);
	//	const int32 MaxNumOptionalLODs = GetMaxNumOptionalLODs(TargetPlatform);
	//	KeySuffix += *FString::Printf(TEXT("1%08x%08x"), MaxNumStreamedLODs, MaxNumOptionalLODs);
	//}
	//else
	//{
	//	KeySuffix += TEXT("0zzzzzzzzzzzzzzzz");
	//}

	//if (TargetPlatform->GetPlatformInfo().PlatformGroupName == TEXT("Desktop")
	//	&& GStripSkeletalMeshLodsDuringCooking != 0
	//	&& GSkeletalMeshKeepMobileMinLODSettingOnDesktop != 0)
	//{
	//	KeySuffix += TEXT("_MinMLOD");
	//}

	return FDerivedDataCacheInterface::BuildCacheKey(
		TEXT("CHAOSCLOTH"),
		CHAOS_CLOTH_ASSET_DERIVED_DATA_VERSION,
		*KeySuffix
	);
}

bool UChaosClothAsset::IsInitialBuildDone() const
{
	//We are consider built if we have a valid lod model
	return GetImportedModel() != nullptr &&
		GetImportedModel()->LODModels.Num() > 0 &&
		GetImportedModel()->LODModels[0].Sections.Num() > 0;
}
#endif // WITH_EDITOR

void UChaosClothAsset::SetPhysicsAsset(UPhysicsAsset* InPhysicsAsset)
{
	using namespace UE::Chaos::ClothAsset;

	PhysicsAsset = InPhysicsAsset;
	for (TSharedRef<FManagedArrayCollection>& ClothCollection : GetClothCollections())
	{
		FCollectionClothFacade Cloth(ClothCollection);
		Cloth.SetPhysicsAssetPathName(PhysicsAsset ? PhysicsAsset->GetPathName() : FString());
	}
}

void UChaosClothAsset::SetReferenceSkeleton(const FReferenceSkeleton* ReferenceSkeleton, bool bRebuildModels, bool bRebindMeshes)
{
	using namespace UE::Chaos::ClothAsset;

	// Update the reference skeleton
	if (ReferenceSkeleton)
	{
		GetRefSkeleton() = *ReferenceSkeleton;
	}
	else
	{
		// Create a default reference skeleton
		GetRefSkeleton().Empty(1);
		FReferenceSkeletonModifier ReferenceSkeletonModifier(GetRefSkeleton(), nullptr);

		FMeshBoneInfo MeshBoneInfo;
		constexpr const TCHAR* RootName = TEXT("Root");
		MeshBoneInfo.ParentIndex = INDEX_NONE;
#if WITH_EDITORONLY_DATA
		MeshBoneInfo.ExportName = RootName;
#endif
		MeshBoneInfo.Name = FName(RootName);
		ReferenceSkeletonModifier.Add(MeshBoneInfo, FTransform::Identity);

		bRebindMeshes = true; // Force the binding when a default reference skeleton is being created 
	}

	// Rebind the meshes
	if (bRebindMeshes)
	{
		for (TSharedRef<FManagedArrayCollection>& ClothCollection : GetClothCollections())
		{
			FClothGeometryTools::BindMeshToRootBone(ClothCollection, true, true);
		}
	}

	// Rebind the models
	if (bRebuildModels)
	{
		Build();
	}
}

void UChaosClothAsset::UpdateSkeletonFromCollection(bool bRebuildModels)
{
	using namespace UE::Chaos::ClothAsset;

	check(GetClothCollections().Num());
	FCollectionClothConstFacade ClothFacade(GetClothCollections()[0]);
	check(ClothFacade.IsValid());

	const FString& SkeletalMeshPathName = ClothFacade.GetSkeletalMeshPathName();
	USkeletalMesh* const SkeletalMesh = SkeletalMeshPathName.IsEmpty() ? nullptr :
		LoadObject<USkeletalMesh>(nullptr, *SkeletalMeshPathName, nullptr, LOAD_None, nullptr);

	SetSkeleton(SkeletalMesh ? SkeletalMesh->GetSkeleton() : nullptr); // For completion only, this is not being used and might mismatch the skeletal mesh's reference skeleton

	constexpr bool bRebindMeshes = false;  // The collection should contain the correct binding at the time SkeletalMeshPathName was set
	SetReferenceSkeleton(SkeletalMesh ? &SkeletalMesh->GetRefSkeleton() : nullptr, bRebuildModels, bRebindMeshes);
}

void UChaosClothAsset::CopySimMeshToRenderMesh(UMaterialInterface* Material)
{
	using namespace UE::Chaos::ClothAsset;
	check(GetClothCollections().Num());

	// Add a default material if none is specified
	const FString RenderMaterialPathName = Material ?
		Material->GetPathName() :
		FString(TEXT("/Engine/EditorMaterials/Cloth/CameraLitDoubleSided.CameraLitDoubleSided"));

	bool bAnyLodHasRenderMesh = false;
	for (TSharedRef<FManagedArrayCollection>& ClothCollection : GetClothCollections())
	{
		constexpr bool bSingleRenderPattern = true;
		FClothGeometryTools::CopySimMeshToRenderMesh(ClothCollection, RenderMaterialPathName, bSingleRenderPattern);
		bAnyLodHasRenderMesh = bAnyLodHasRenderMesh || FClothGeometryTools::HasRenderMesh(ClothCollection);
	}

	// Set new material
	Materials.Reset(1);
	if (bAnyLodHasRenderMesh)
	{
		if (UMaterialInterface* const LoadedMaterial = LoadObject<UMaterialInterface>(nullptr, *RenderMaterialPathName, nullptr, LOAD_None, nullptr))
		{
			Materials.Emplace(LoadedMaterial, true, false, LoadedMaterial->GetFName());
		}
	}
}

void UChaosClothAsset::ReregisterComponents()
{
	// Recreate the simulation proxies with the updated physics asset
	for (TObjectIterator<UChaosClothComponent> ObjectIterator; ObjectIterator; ++ObjectIterator)
	{
		if (UChaosClothComponent* const Component = *ObjectIterator)
		{
			if (Component->GetClothAsset() == this)
			{
				const FComponentReregisterContext Context(Component);  // Context goes out of scope, causing the Component to be re-registered
			}
		}
	}
}

#if WITH_EDITORONLY_DATA

void UChaosClothAsset::SetPreviewSceneSkeletalMesh(USkeletalMesh* Mesh)
{
	PreviewSceneSkeletalMesh = Mesh;
}

USkeletalMesh* UChaosClothAsset::GetPreviewSceneSkeletalMesh() const
{
	// Load the SkeletalMesh asset if it's not already loaded
	return PreviewSceneSkeletalMesh.LoadSynchronous();
}

void UChaosClothAsset::SetPreviewSceneAnimation(UAnimationAsset* Animation)
{
	PreviewSceneAnimation = Animation;
}

UAnimationAsset* UChaosClothAsset::GetPreviewSceneAnimation() const
{
	// Load the animation asset if it's not already loaded
	return PreviewSceneAnimation.LoadSynchronous();
}

#endif

TObjectPtr<UDataflowBaseContent> UChaosClothAsset::CreateDataflowContent()
{
	TObjectPtr<UDataflowSkeletalContent> SkeletalContent = UE::DataflowContextHelpers::CreateNewDataflowContent<UDataflowSkeletalContent>(this);

	SkeletalContent->SetDataflowOwner(this);
	SkeletalContent->SetTerminalAsset(this);

	WriteDataflowContent(SkeletalContent);
	
	return SkeletalContent;
}

void UChaosClothAsset::WriteDataflowContent(const TObjectPtr<UDataflowBaseContent>& DataflowContent) const
{
	if(const TObjectPtr<UDataflowSkeletalContent> SkeletalContent = Cast<UDataflowSkeletalContent>(DataflowContent))
	{
		SkeletalContent->SetDataflowAsset(DataflowAsset);
		SkeletalContent->SetDataflowTerminal(DataflowTerminal);

#if WITH_EDITORONLY_DATA
		SkeletalContent->SetAnimationAsset(GetPreviewSceneAnimation());
		SkeletalContent->SetSkeletalMesh(GetPreviewSceneSkeletalMesh());
#endif
	}
}

void UChaosClothAsset::ReadDataflowContent(const TObjectPtr<UDataflowBaseContent>& DataflowContent)
{
	if(const TObjectPtr<UDataflowSkeletalContent> SkeletalContent = Cast<UDataflowSkeletalContent>(DataflowContent))
	{
#if WITH_EDITORONLY_DATA
		PreviewSceneAnimation = SkeletalContent->GetAnimationAsset();
		PreviewSceneSkeletalMesh = SkeletalContent->GetSkeletalMesh();
#endif
	}
}


