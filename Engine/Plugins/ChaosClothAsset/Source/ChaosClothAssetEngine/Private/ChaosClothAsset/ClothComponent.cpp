// Copyright Epic Games, Inc. All Rights Reserved.

#include "ChaosClothAsset/ClothComponent.h"
#include "ChaosClothAsset/ClothAsset.h"
#include "ChaosClothAsset/ClothAssetInteractor.h"
#include "ChaosClothAsset/ClothAssetPrivate.h"
#include "ChaosClothAsset/CollisionSources.h"
#include "ChaosClothAsset/ClothSimulationModel.h"
#include "ChaosClothAsset/ClothSimulationProxy.h"
#include "Chaos/CollectionPropertyFacade.h"
#include "Components/SkeletalMeshComponent.h"
#include "GeometryCollection/ManagedArrayCollection.h"
#include "HAL/IConsoleManager.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "SkeletalRenderPublic.h"
#include "Dataflow/DataflowSimulationManager.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "Stats/Stats.h"

CSV_DECLARE_CATEGORY_MODULE_EXTERN(ENGINE_API, Animation);

UChaosClothComponent::UChaosClothComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
#if WITH_EDITORONLY_DATA
	, bSimulateInEditor(0)
#endif
	, bUseAttachedParentAsPoseComponent(1)  // By default use the parent component as leader pose component
	, bWaitForParallelTask(0)
	, bEnableSimulation(1)
	, bSuspendSimulation(0)
	, bBindToLeaderComponent(0)
	, CollisionSources(MakeUnique<UE::Chaos::ClothAsset::FCollisionSources>(this))
{
	PrimaryComponentTick.EndTickGroup = TG_PostPhysics;
}

UChaosClothComponent::UChaosClothComponent(FVTableHelper& Helper)
	: Super(Helper)
{
}

UChaosClothComponent::~UChaosClothComponent() = default;

void UChaosClothComponent::SetClothAsset(UChaosClothAsset* InClothAsset)
{
	SetSkinnedAssetAndUpdate(InClothAsset);

	PRAGMA_DISABLE_DEPRECATION_WARNINGS
#if WITH_EDITORONLY_DATA
		ClothAsset = InClothAsset;
#endif
	PRAGMA_ENABLE_DEPRECATION_WARNINGS
}

UChaosClothAsset* UChaosClothComponent::GetClothAsset() const
{
	return Cast<UChaosClothAsset>(GetSkinnedAsset());
}

bool UChaosClothComponent::IsSimulationSuspended() const
{
	return bSuspendSimulation || !IsSimulationEnabled();
}

bool UChaosClothComponent::IsSimulationEnabled() const
{
	static IConsoleVariable* const CVarClothPhysics = IConsoleManager::Get().FindConsoleVariable(TEXT("p.ClothPhysics"));
	// If the console variable doesn't exist, default to simulation enabled.
	return bEnableSimulation && ClothSimulationProxy.IsValid() && (!CVarClothPhysics || CVarClothPhysics->GetBool());
}

void UChaosClothComponent::ResetConfigProperties()
{
	if (IsRegistered())
	{
		if (GetClothAsset())
		{
			const TArray<TSharedRef<FManagedArrayCollection>>& ClothCollections = GetClothAsset()->GetClothCollections();
			PropertyCollections.Reset();
			PropertyCollections.Reserve(ClothCollections.Num());
			CollectionPropertyFacades.Reset();
			CollectionPropertyFacades.Reserve(ClothCollections.Num());
			for (const TSharedRef<FManagedArrayCollection>& ClothCollection : ClothCollections)
			{
				TSharedPtr<FManagedArrayCollection>& PropertyCollection = PropertyCollections.Add_GetRef(MakeShared<FManagedArrayCollection>());

				::Chaos::Softs::FCollectionPropertyMutableFacade CollectionPropertyMutableFacade(PropertyCollection);
				CollectionPropertyMutableFacade.Copy(*ClothCollection);

				CollectionPropertyFacades.Add(MakeShared<::Chaos::Softs::FCollectionPropertyFacade>(PropertyCollection));
			}
			check(ClothOutfitInteractor);
			ClothOutfitInteractor->SetProperties(CollectionPropertyFacades);
		}
		else
		{
			check(ClothOutfitInteractor);
			ClothOutfitInteractor->ResetProperties();
			PropertyCollections.Reset();
			CollectionPropertyFacades.Reset();
		}
	}
	else
	{
		UE_LOG(LogChaosClothAsset, Warning, TEXT("Chaos Cloth Component [%s]: Trying to reset runtime config properties without being registered."), *GetName());
	}
}

#if WITH_EDITOR
void UChaosClothComponent::UpdateConfigProperties()
{
	if (IsRegistered())
	{
		if (GetClothAsset())
		{
			const TArray<TSharedRef<FManagedArrayCollection>>& ClothCollections = GetClothAsset()->GetClothCollections();
			if (ClothCollections.Num() == PropertyCollections.Num())
			{
				check(CollectionPropertyFacades.Num() == ClothCollections.Num());
				for (int32 LodIndex = 0; LodIndex < ClothCollections.Num(); ++LodIndex)
				{
					CollectionPropertyFacades[LodIndex]->UpdateProperties(ClothCollections[LodIndex].ToSharedPtr());
				}
			}
		}
	}
}
#endif

void UChaosClothComponent::WaitForExistingParallelClothSimulation_GameThread()
{
	// Should only kick new parallel cloth simulations from game thread, so should be safe to also wait for existing ones there.
	check(IsInGameThread());
	HandleExistingParallelSimulation();
}

void UChaosClothComponent::RecreateClothSimulationProxy()
{
	if (IsRegistered())
	{
		ClothSimulationProxy.Reset();

		if (GetClothAsset())
		{
			const TSharedPtr<const FChaosClothSimulationModel> ClothSimulationModel = GetClothAsset()->GetClothSimulationModel();
			if (ClothSimulationModel && ClothSimulationModel->GetNumLods())
			{
				// Create the simulation proxy (note CreateClothSimulationProxy() can be overloaded)
				ClothSimulationProxy = CreateClothSimulationProxy();
			}
		}
	}
	else
	{
		UE_LOG(LogChaosClothAsset, Warning, TEXT("Chaos Cloth Component [%s]: Trying to recreate the simulation proxy without being registered."), *GetName());
	}
}

void UChaosClothComponent::PostLoad()
{
	Super::PostLoad();

#if WITH_EDITORONLY_DATA
	PRAGMA_DISABLE_DEPRECATION_WARNINGS
		ClothAsset = GetClothAsset();
	PRAGMA_ENABLE_DEPRECATION_WARNINGS
#endif
}

#if WITH_EDITOR
void UChaosClothComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	// Set the skinned asset pointer with the alias pointer (must happen before the call to Super::PostEditChangeProperty)
	if (const FProperty* const Property = PropertyChangedEvent.Property)
	{
PRAGMA_DISABLE_DEPRECATION_WARNINGS
		if (Property->GetFName() == GET_MEMBER_NAME_CHECKED(UChaosClothComponent, ClothAsset))
		{
			SetClothAsset(ClothAsset);
		}
PRAGMA_ENABLE_DEPRECATION_WARNINGS
		if (Property->GetFName() == GET_MEMBER_NAME_CHECKED(UChaosClothComponent, bSimulateInEditor))
		{
			bTickInEditor = bSimulateInEditor;
		}
	}

	Super::PostEditChangeProperty(PropertyChangedEvent);
}

bool UChaosClothComponent::CanEditChange(const FProperty* InProperty) const
{
	if (!Super::CanEditChange(InProperty))
	{
		return false;
	}

	const FName& Name = InProperty->GetFName();

	if (Name == GET_MEMBER_NAME_CHECKED(ThisClass, SimulationAsset))
	{
		static const auto CVarEnableSimulationDataflow = IConsoleManager::Get().FindConsoleVariable(TEXT("p.Dataflow.EnableSimulation"));
		return CVarEnableSimulationDataflow->GetBool();
	}

	return true;
}
#endif // WITH_EDITOR

void UChaosClothComponent::OnRegister()
{
	LLM_SCOPE(ELLMTag::Chaos);

	// Register the component first, otherwise calls to ResetConfigProperties and RecreateClothSimulationProxy wouldn't work
	Super::OnRegister();

	// Update the component bone transforms (for colliders) from the cloth asset until these are animated from a leader component
	UpdateComponentSpaceTransforms();

	// Fill up the property collection with the original cloth asset properties
	ClothOutfitInteractor = NewObject<UChaosClothAssetInteractor>();
	ResetConfigProperties();

	// Create the proxy to start the simulation
	RecreateClothSimulationProxy();

	// Update render visibility, so that an empty LODs doesn't unnecessarily go to render
	UpdateVisibility();

	// Register the dataflow simulation interface
	UE::Dataflow::RegisterSimulationInterface(this);
}

void UChaosClothComponent::OnUnregister()
{
	Super::OnUnregister();

	// Release cloth simulation
	ClothSimulationProxy.Reset();

	// Release the runtime simulation collection and facade
	ClothOutfitInteractor->ResetProperties();
	CollectionPropertyFacades.Empty();
	PropertyCollections.Empty();

	// Unregister the dataflow simulation interface
	UE::Dataflow::UnregisterSimulationInterface(this);
}

bool UChaosClothComponent::IsComponentTickEnabled() const
{
	return bEnableSimulation && Super::IsComponentTickEnabled();
}

void UChaosClothComponent::TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	CSV_SCOPED_TIMING_STAT_EXCLUSIVE(Physics);
	QUICK_SCOPE_CYCLE_COUNTER(STAT_ClothComponentTick);
	
	// Tick USkinnedMeshComponent first so it will update the predicted lod
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	
	// Make sure that the previous frame simulation has completed
	HandleExistingParallelSimulation();
	
	if(!SimulationAsset.DataflowAsset)
	{
		// < This would be the right place to update the preset/use an interactor, ...etc.

		// Update the proxy and start the simulation parallel task
		StartNewParallelSimulation(DeltaTime);

		// Wait in tick function for the simulation results if required
		if (ShouldWaitForParallelSimulationInTickComponent())
		{
			HandleExistingParallelSimulation();
		}
	}

#if WITH_EDITOR
	if (TickType == LEVELTICK_ViewportsOnly && bTickOnceInEditor && !bSimulateInEditor)
	{
		// Only tick once in editor when requested. This is used to update from caches by the Chaos Cache Manager.
		bTickInEditor = false;
		bTickOnceInEditor = false;
	}
#endif
}

bool UChaosClothComponent::RequiresPreEndOfFrameSync() const
{
	if (!IsSimulationSuspended() && !ShouldWaitForParallelSimulationInTickComponent())
	{
		// By default we await the cloth task in TickComponent, but...
		// If we have cloth and have no game-thread dependencies on the cloth output, 
		// then we will wait for the cloth task in SendAllEndOfFrameUpdates.
		return true;
	}
	return Super::RequiresPreEndOfFrameSync();
}

void UChaosClothComponent::OnPreEndOfFrameSync()
{
	Super::OnPreEndOfFrameSync();

	HandleExistingParallelSimulation();
}

FBoxSphereBounds UChaosClothComponent::CalcBounds(const FTransform& LocalToWorld) const
{
	QUICK_SCOPE_CYCLE_COUNTER(STAT_CalcClothComponentBounds);

	FBoxSphereBounds NewBounds(ForceInitToZero);

	// Use cached local bounds if possible
	if (bCachedWorldSpaceBoundsUpToDate || bCachedLocalBoundsUpToDate)
	{
		NewBounds = bCachedLocalBoundsUpToDate ?
			CachedWorldOrLocalSpaceBounds.TransformBy(LocalToWorld) :
			CachedWorldOrLocalSpaceBounds.TransformBy(CachedWorldToLocalTransform * LocalToWorld.ToMatrixWithScale());
	
		if (bIncludeComponentLocationIntoBounds)
		{
			NewBounds = NewBounds + FBoxSphereBounds(GetComponentLocation(), FVector(1.0f), 1.0f);
		}
	}
	else  // Calculate new bounds
	{
		FVector RootBoneOffset(ForceInitToZero);

		// If attached to a skeletal mesh component that uses fixed bounds, add the root bone translation
		if (const USkeletalMeshComponent* const SkeletalMeshComponent = Cast<USkeletalMeshComponent>(LeaderPoseComponent.Get()))
		{
			if (SkeletalMeshComponent->GetSkinnedAsset() && SkeletalMeshComponent->bComponentUseFixedSkelBounds)
			{
				RootBoneOffset = SkeletalMeshComponent->RootBoneTranslation; // Adjust bounds by root bone translation
			}
		}

		static IConsoleVariable* const CVarCacheLocalSpaceBounds = IConsoleManager::Get().FindConsoleVariable(TEXT("a.CacheLocalSpaceBounds"));
		const bool bCacheLocalSpaceBounds = CVarCacheLocalSpaceBounds ? (CVarCacheLocalSpaceBounds->GetInt() != 0) : true;

		const FTransform CachedBoundsTransform = bCacheLocalSpaceBounds ? FTransform::Identity : LocalToWorld;

		// Add render mesh bounds
		constexpr bool bHasValidBodies = false;
		NewBounds = CalcMeshBound((FVector3f)RootBoneOffset, bHasValidBodies, CachedBoundsTransform);

		if (bIncludeComponentLocationIntoBounds)
		{
			const FVector ComponentLocation = GetComponentLocation();
			const FBoxSphereBounds ComponentLocationBounds(ComponentLocation, FVector(1.), 1.);
			if (bCacheLocalSpaceBounds)
			{
				NewBounds = NewBounds.TransformBy(LocalToWorld);
				NewBounds = NewBounds + ComponentLocationBounds;
				NewBounds = NewBounds.TransformBy(LocalToWorld.ToInverseMatrixWithScale());
			}
			else
			{
				NewBounds = NewBounds + ComponentLocationBounds;
			}
		}

		// Add sim mesh bounds
		if (ClothSimulationProxy.IsValid())
		{
			const FBoxSphereBounds SimulationBounds = ClothSimulationProxy->CalculateBounds_AnyThread();
			if (SimulationBounds.SphereRadius > UE_SMALL_NUMBER)  // Don't add the simulation bounds if there are empty, otherwise it could unwillingly add the component's location
			{
				NewBounds = NewBounds + SimulationBounds.TransformBy(CachedBoundsTransform);
			}
		}

		CachedWorldOrLocalSpaceBounds = NewBounds;
		bCachedLocalBoundsUpToDate = bCacheLocalSpaceBounds;
		bCachedWorldSpaceBoundsUpToDate = !bCacheLocalSpaceBounds;

		if (bCacheLocalSpaceBounds)
		{
			CachedWorldToLocalTransform.SetIdentity();
			NewBounds = NewBounds.TransformBy(LocalToWorld);
		}
		else
		{
			CachedWorldToLocalTransform = LocalToWorld.ToInverseMatrixWithScale();
		}
	}
	return NewBounds;
}

void UChaosClothComponent::OnAttachmentChanged()
{
	if (bUseAttachedParentAsPoseComponent)
	{
		USkinnedMeshComponent* const AttachParentComponent = Cast<USkinnedMeshComponent>(GetAttachParent());
		SetLeaderPoseComponent(AttachParentComponent);  // If the cast fail, remove the current leader

		// When parented to a skeletal mesh, the anim setup needs re-initializing in order to use the follower's bones requirement
		if (USkeletalMeshComponent* const SkeletalMeshComponent = Cast<USkeletalMeshComponent>(AttachParentComponent))
		{
			SkeletalMeshComponent->RecalcRequiredBones(SkeletalMeshComponent->GetPredictedLODLevel());
		}
	}

	Super::OnAttachmentChanged();
}

void UChaosClothComponent::RefreshBoneTransforms(FActorComponentTickFunction* /*TickFunction*/)
{
	MarkRenderDynamicDataDirty();

	bNeedToFlipSpaceBaseBuffers = true;
	bHasValidBoneTransform = false;
	FlipEditableSpaceBases();
	bHasValidBoneTransform = true;
}

void UChaosClothComponent::GetUpdateClothSimulationData_AnyThread(TMap<int32, FClothSimulData>& OutClothSimulData, FMatrix& OutLocalToWorld, float& OutBlendWeight)
{
	OutLocalToWorld = GetComponentToWorld().ToMatrixWithScale();

	const UChaosClothComponent* const LeaderPoseClothComponent = Cast<UChaosClothComponent>(LeaderPoseComponent.Get());
	if (LeaderPoseClothComponent && LeaderPoseClothComponent->ClothSimulationProxy && bBindToLeaderComponent)
	{
		OutBlendWeight = BlendWeight;
		OutClothSimulData = LeaderPoseClothComponent->ClothSimulationProxy->GetCurrentSimulationData_AnyThread();
	}
	else if (IsSimulationEnabled() && !bBindToLeaderComponent && ClothSimulationProxy)
	{
		OutBlendWeight = BlendWeight;
		OutClothSimulData = ClothSimulationProxy->GetCurrentSimulationData_AnyThread();
	}
	else
	{
		OutClothSimulData.Reset();
	}

	// Blend cloth out whenever the simulation data is invalid
	if (!OutClothSimulData.Num())
	{
		OutBlendWeight = 0.0f;
	}
}

void UChaosClothComponent::SetSkinnedAssetAndUpdate(USkinnedAsset* InSkinnedAsset, bool bReinitPose)
{
	if (InSkinnedAsset != GetSkinnedAsset())
	{
		// Note: It is not necessary to stop the current simulation here, since it will die off once the proxy is recreated

		// Change the skinned asset, dirty render states, ...etc.
		Super::SetSkinnedAssetAndUpdate(InSkinnedAsset, bReinitPose);

		if (IsRegistered())
		{
			// Update the component bone transforms (for colliders) from the new cloth asset
			UpdateComponentSpaceTransforms();

			// Fill up the property collection with the new cloth asset properties
			ResetConfigProperties();

			// Hard reset the simulation
			RecreateClothSimulationProxy();
		}

		// Update the component visibility in case the new render mesh has no valid LOD
		UpdateVisibility();
	}
}

void UChaosClothComponent::GetAdditionalRequiredBonesForLeader(int32 LeaderLODIndex, TArray<FBoneIndexType>& InOutRequiredBones) const
{
	TArray<FBoneIndexType> RequiredBones;

	// Add the follower's bones (including sim and render mesh bones, both stored in the LODRenderData RequiredBones array)
	if (const FSkeletalMeshRenderData* const SkeletalMeshRenderData = GetSkeletalMeshRenderData())
	{
		const int32 MinLODIndex = ComputeMinLOD();
		const int32 MaxLODIndex = FMath::Max(GetNumLODs() - 1, MinLODIndex);

		const int32 LODIndex = FMath::Clamp(LeaderLODIndex, MinLODIndex, MaxLODIndex);

		if (SkeletalMeshRenderData->LODRenderData.IsValidIndex(LODIndex))
		{
			RequiredBones.Reserve(SkeletalMeshRenderData->LODRenderData[LODIndex].RequiredBones.Num());

			for (const FBoneIndexType RequiredBone : SkeletalMeshRenderData->LODRenderData[LODIndex].RequiredBones)
			{
				if (LeaderBoneMap.IsValidIndex(RequiredBone))
				{
					const int32 FollowerRequiredLeaderBone = LeaderBoneMap[RequiredBone];
					if (FollowerRequiredLeaderBone != INDEX_NONE)
					{
						RequiredBones.Add((FBoneIndexType)FollowerRequiredLeaderBone);
					}
				}
			}

			// Then sort array of required bones in hierarchy order
			RequiredBones.Sort();
		}
	}

	// Merge the physics asset bones (the leader's physics asset can be different to this component's cloth asset)
	if (const UPhysicsAsset* const PhysicsAsset = GetClothAsset() ? GetClothAsset()->GetPhysicsAsset() : nullptr)
	{
		if (const USkinnedAsset* const LeaderSkinnedAsset = ensure(LeaderPoseComponent.IsValid()) ? LeaderPoseComponent->GetSkinnedAsset() : nullptr)  // Needs the leader SkinnedAsset for the correct RefSkeleton
		{
			USkinnedMeshComponent::GetPhysicsRequiredBones(LeaderSkinnedAsset, PhysicsAsset, RequiredBones);
		}
	}

	if (RequiredBones.Num())
	{
		// Make sure all of these are in RequiredBones, note MergeInBoneIndexArrays requires the arrays to be sorted and bone must be unique
		MergeInBoneIndexArrays(InOutRequiredBones, RequiredBones);
	}
}

void UChaosClothComponent::FinalizeBoneTransform()
{
	Super::FinalizeBoneTransform();

	OnBoneTransformsFinalizedMC.Broadcast();
}

FDelegateHandle UChaosClothComponent::RegisterOnBoneTransformsFinalizedDelegate(const FOnBoneTransformsFinalizedMultiCast::FDelegate& Delegate)
{
	return OnBoneTransformsFinalizedMC.Add(Delegate);
}

void UChaosClothComponent::UnregisterOnBoneTransformsFinalizedDelegate(const FDelegateHandle& DelegateHandle)
{
	OnBoneTransformsFinalizedMC.Remove(DelegateHandle);
}

TSharedPtr<UE::Chaos::ClothAsset::FClothSimulationProxy> UChaosClothComponent::CreateClothSimulationProxy()
{
	using namespace UE::Chaos::ClothAsset;
	return MakeShared<FClothSimulationProxy>(*this);
}

void UChaosClothComponent::AddCollisionSource(USkinnedMeshComponent* SourceComponent, const UPhysicsAsset* SourcePhysicsAsset, bool bUseSphylsOnly)
{
	CollisionSources->Add(SourceComponent, SourcePhysicsAsset, bUseSphylsOnly);
}

void UChaosClothComponent::RemoveCollisionSources(const USkinnedMeshComponent* SourceComponent)
{
	CollisionSources->Remove(SourceComponent);
}

void UChaosClothComponent::RemoveCollisionSource(const USkinnedMeshComponent* SourceComponent, const UPhysicsAsset* SourcePhysicsAsset)
{
	CollisionSources->Remove(SourceComponent, SourcePhysicsAsset);
}

void UChaosClothComponent::ResetCollisionSources()
{
	CollisionSources->Reset();
}

void UChaosClothComponent::StartNewParallelSimulation(float DeltaTime)
{
	if (ClothSimulationProxy.IsValid())
	{
		CSV_SCOPED_TIMING_STAT(Animation, Cloth);
		const bool bIsSimulating = ClothSimulationProxy->Tick_GameThread(DeltaTime);
		const int32 CurrentLOD = GetPredictedLODLevel();

		if (bIsSimulating && CollectionPropertyFacades.IsValidIndex(CurrentLOD) && CollectionPropertyFacades[CurrentLOD].IsValid())
		{
			CollectionPropertyFacades[CurrentLOD]->ClearDirtyFlags();
		}
	}
}

void UChaosClothComponent::HandleExistingParallelSimulation()
{
	if (bBindToLeaderComponent)
	{
		if (UChaosClothComponent* const LeaderComponent = Cast<UChaosClothComponent>(LeaderPoseComponent.Get()))
		{
			LeaderComponent->HandleExistingParallelSimulation();
		}
	}

	if (ClothSimulationProxy.IsValid())
	{
		ClothSimulationProxy->CompleteParallelSimulation_GameThread();
	}
}

bool UChaosClothComponent::ShouldWaitForParallelSimulationInTickComponent() const
{
	static IConsoleVariable* const CVarClothPhysicsWaitForParallelClothTask = IConsoleManager::Get().FindConsoleVariable(TEXT("p.ClothPhysics.WaitForParallelClothTask"));

	return bWaitForParallelTask || (CVarClothPhysicsWaitForParallelClothTask && CVarClothPhysicsWaitForParallelClothTask->GetBool());
}

void UChaosClothComponent::UpdateComponentSpaceTransforms()
{
	check(IsRegistered());

	if (!LeaderPoseComponent.IsValid() && GetClothAsset() && GetClothAsset()->GetResourceForRendering())
	{
		FSkeletalMeshLODRenderData& LODData = GetClothAsset()->GetResourceForRendering()->LODRenderData[GetPredictedLODLevel()];
		GetClothAsset()->FillComponentSpaceTransforms(GetClothAsset()->GetRefSkeleton().GetRefBonePose(), LODData.RequiredBones, GetEditableComponentSpaceTransforms());

		bNeedToFlipSpaceBaseBuffers = true; // Have updated space bases so need to flip
		FlipEditableSpaceBases();
		bHasValidBoneTransform = true;
	}
}

void UChaosClothComponent::UpdateVisibility()
{
	if (GetClothAsset() && GetClothAsset()->GetResourceForRendering())
	{
		const FSkeletalMeshRenderData* const SkeletalMeshRenderData = GetClothAsset()->GetResourceForRendering();
		const int32 FirstValidLODIdx = SkeletalMeshRenderData ? SkeletalMeshRenderData->GetFirstValidLODIdx(0) : INDEX_NONE;
		SetVisibility(FirstValidLODIdx != INDEX_NONE);
	}
	else
	{
		SetVisibility(false);
	}
}

UChaosClothAssetInteractor* UChaosClothComponent::GetClothOutfitInteractor()
{
	check(IsInGameThread());
	return ClothOutfitInteractor;
}

void UChaosClothComponent::BuildSimulationProxy()
{
	RecreateClothSimulationProxy();
}

void UChaosClothComponent::ResetSimulationProxy()
{
	ClothSimulationProxy.Reset();
}

void UChaosClothComponent::WriteToSimulation(const float DeltaTime, const bool bAsyncTask)
{
	if (ClothSimulationProxy.IsValid())
	{
		const bool bIsSimulating = ClothSimulationProxy->PreSimulate_GameThread(DeltaTime);
		const int32 CurrentLOD = GetPredictedLODLevel();

		if (bIsSimulating && CollectionPropertyFacades.IsValidIndex(CurrentLOD) && CollectionPropertyFacades[CurrentLOD].IsValid())
		{
			CollectionPropertyFacades[CurrentLOD]->ClearDirtyFlags();
		}
	}
}

void UChaosClothComponent::PreProcessSimulation(const float DeltaTime)
{
	if (ClothSimulationProxy.IsValid() && ClothSimulationProxy->HasCacheData())
	{
		WriteToSimulation(DeltaTime, false);
	}
}

void UChaosClothComponent::ReadFromSimulation(const float DeltaTime, const bool bAsyncTask)
{
	if (ClothSimulationProxy.IsValid())
	{
		ClothSimulationProxy->PostSimulate_GameThread();
	}
}

FDataflowSimulationProxy* UChaosClothComponent::GetSimulationProxy()
{
	return ClothSimulationProxy.Get();
}

const FDataflowSimulationProxy* UChaosClothComponent::GetSimulationProxy() const
{
	return ClothSimulationProxy.Get();
}

