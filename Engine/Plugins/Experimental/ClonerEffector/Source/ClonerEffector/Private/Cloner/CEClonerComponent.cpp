// Copyright Epic Games, Inc. All Rights Reserved.

#include "Cloner/CEClonerComponent.h"

#include "Async/Async.h"
#include "Cloner/CEClonerActor.h"
#include "Cloner/Extensions/CEClonerExtensionBase.h"
#include "Cloner/Layouts/CEClonerLayoutBase.h"
#include "Components/BillboardComponent.h"
#include "Components/DynamicMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/Texture2D.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "NiagaraMeshRendererProperties.h"
#include "NiagaraSystem.h"
#include "Settings/CEClonerEffectorSettings.h"
#include "Subsystems/CEClonerSubsystem.h"
#include "UDynamicMesh.h"
#include "UObject/Package.h"

#if WITH_EDITOR
#include "Editor/EditorEngine.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Materials/Material.h"
#include "Misc/MessageDialog.h"
#include "Misc/ScopedSlowTask.h"
#include "ScopedTransaction.h"
#include "Widgets/Notifications/SNotificationList.h"
#endif

UCEClonerComponent::FOnClonerInitialized UCEClonerComponent::OnClonerInitializedDelegate;
UCEClonerComponent::FOnClonerLayoutLoaded UCEClonerComponent::OnClonerLayoutLoadedDelegate;
UCEClonerComponent::FOnClonerMeshUpdated UCEClonerComponent::OnClonerMeshUpdatedDelegate;

DEFINE_LOG_CATEGORY_STATIC(LogCEClonerComponent, Log, All);

#define LOCTEXT_NAMESPACE "CEClonerComponent"

UCEClonerComponent::UCEClonerComponent()
	: UNiagaraComponent()
{
	CastShadow = true;
	bReceivesDecals = true;
	bAutoActivate = true;
	bHiddenInGame = false;

#if WITH_EDITOR
	// Do not show bounding box around cloner for better visibility
	SetIsVisualizationComponent(true);

	// Disable use of bounds to focus to avoid de-zoom
	SetIgnoreBoundsForEditorFocus(true);
#endif

	bIsEditorOnly = false;

	// Show sprite for this component to visualize it when empty
#if WITH_EDITORONLY_DATA
	bVisualizeComponent = true;
#endif

	if (!IsTemplate())
	{
		UCEClonerSubsystem::OnClonerSetEnabled().AddUObject(this, &UCEClonerComponent::OnClonerSetEnabled);
		USceneComponent::MarkRenderStateDirtyEvent.AddUObject(this, &UCEClonerComponent::OnRenderStateDirty);

		// Bind to delegate to detect material changes
#if WITH_EDITOR
		FCoreUObjectDelegates::OnObjectPropertyChanged.RemoveAll(this);
		FCoreUObjectDelegates::OnObjectPropertyChanged.AddUObject(this, &UCEClonerComponent::OnActorPropertyChanged);

		UMaterial::OnMaterialCompilationFinished().AddUObject(this, &UCEClonerComponent::OnMaterialCompiled);
#endif

		// Apply default layout
		const TArray<FName> LayoutNames = GetClonerLayoutNames();
		LayoutName = !LayoutNames.IsEmpty() ? LayoutNames[0] : NAME_None;
	}
}

void UCEClonerComponent::PostInitProperties()
{
	Super::PostInitProperties();

	if (HasAnyFlags(RF_ClassDefaultObject))
	{
		// Register new type def for niagara

		constexpr ENiagaraTypeRegistryFlags MeshFlags =
			ENiagaraTypeRegistryFlags::AllowAnyVariable |
			ENiagaraTypeRegistryFlags::AllowParameter;

		FNiagaraTypeRegistry::Register(FNiagaraTypeDefinition(StaticEnum<ECEClonerMeshRenderMode>()), MeshFlags);
		FNiagaraTypeRegistry::Register(FNiagaraTypeDefinition(StaticEnum<ECEClonerGridConstraint>()), MeshFlags);
		FNiagaraTypeRegistry::Register(FNiagaraTypeDefinition(StaticEnum<ECEClonerPlane>()), MeshFlags);
		FNiagaraTypeRegistry::Register(FNiagaraTypeDefinition(StaticEnum<ECEClonerAxis>()), MeshFlags);
		FNiagaraTypeRegistry::Register(FNiagaraTypeDefinition(StaticEnum<ECEClonerEasing>()), MeshFlags);
		FNiagaraTypeRegistry::Register(FNiagaraTypeDefinition(StaticEnum<ECEClonerMeshAsset>()), MeshFlags);
		FNiagaraTypeRegistry::Register(FNiagaraTypeDefinition(StaticEnum<ECEClonerMeshSampleData>()), MeshFlags);
		FNiagaraTypeRegistry::Register(FNiagaraTypeDefinition(StaticEnum<ECEClonerEffectorType>()), MeshFlags);
		FNiagaraTypeRegistry::Register(FNiagaraTypeDefinition(StaticEnum<ECEClonerTextureSampleChannel>()), MeshFlags);
		FNiagaraTypeRegistry::Register(FNiagaraTypeDefinition(StaticEnum<ECEClonerCompareMode>()), MeshFlags);
		FNiagaraTypeRegistry::Register(FNiagaraTypeDefinition(StaticEnum<ECEClonerEffectorMode>()), MeshFlags);
		FNiagaraTypeRegistry::Register(FNiagaraTypeDefinition(StaticEnum<ECEClonerSpawnLoopMode>()), MeshFlags);
		FNiagaraTypeRegistry::Register(FNiagaraTypeDefinition(StaticEnum<ECEClonerSpawnBehaviorMode>()), MeshFlags);
		FNiagaraTypeRegistry::Register(FNiagaraTypeDefinition(StaticEnum<ECEClonerEffectorPushDirection>()), MeshFlags);
	}
}

void UCEClonerComponent::PostLoad()
{
	Super::PostLoad();

	InitializeCloner();
}

#if WITH_EDITOR
void UCEClonerComponent::PostEditImport()
{
	SetAsset(nullptr);

	Super::PostEditImport();

	RegisterTicker();

	ForceUpdateCloner();
}

void UCEClonerComponent::PostDuplicate(bool bInPIE)
{
	SetAsset(nullptr);

	Super::PostDuplicate(bInPIE);

	RegisterTicker();

	ForceUpdateCloner();
}

void UCEClonerComponent::PostEditUndo()
{
	Super::PostEditUndo();

	// Reregister ticker in case this object was destroyed then undo
	RegisterTicker();

	ForceUpdateCloner();
}

const TCEPropertyChangeDispatcher<UCEClonerComponent> UCEClonerComponent::PropertyChangeDispatcher =
{
	{ GET_MEMBER_NAME_CHECKED(UCEClonerComponent, bEnabled), &UCEClonerComponent::OnEnabledChanged },
	{ GET_MEMBER_NAME_CHECKED(UCEClonerComponent, Seed), &UCEClonerComponent::OnSeedChanged },
	{ GET_MEMBER_NAME_CHECKED(UCEClonerComponent, Color), &UCEClonerComponent::OnColorChanged },
	/** Layout */
	{ GET_MEMBER_NAME_CHECKED(UCEClonerComponent, LayoutName), &UCEClonerComponent::OnLayoutNameChanged },
	{ GET_MEMBER_NAME_CHECKED(UCEClonerComponent, bVisualizerSpriteVisible), &UCEClonerComponent::OnVisualizerSpriteVisibleChanged },
};

void UCEClonerComponent::PostEditChangeProperty(FPropertyChangedEvent& InPropertyChangedEvent)
{
	Super::PostEditChangeProperty(InPropertyChangedEvent);

	PropertyChangeDispatcher.OnPropertyChanged(this, InPropertyChangedEvent);
}
#endif

void UCEClonerComponent::OnComponentCreated()
{
	Super::OnComponentCreated();

	InitializeCloner();
}

void UCEClonerComponent::OnComponentDestroyed(bool bInDestroyingHierarchy)
{
	Super::OnComponentDestroyed(bInDestroyingHierarchy);

#if WITH_EDITOR
	UMaterial::OnMaterialCompilationFinished().RemoveAll(this);
#endif
}

void UCEClonerComponent::UpdateClonerRenderState()
{
	/**
	 * Perform a mesh update when asset is valid,
	 * An update is not already ongoing,
	 * Meshes are out of date after an attachment tree update,
	 * Tree is up to date
	 */
	if (!GetAsset()
		|| IsGarbageCollectingAndLockingUObjectHashTables()
		|| bClonerMeshesUpdating
		|| !bClonerMeshesDirty
		|| ClonerTree.Status != ECEClonerAttachmentStatus::Updated)
	{
		return;
	}

#if WITH_EDITOR
	UpdateDirtyMeshesAsync();
#else
	OnDirtyMeshesUpdated(true);
#endif
}

void UCEClonerComponent::UpdateClonerAttachmentTree(bool bInReset)
{
#if WITH_EDITOR
	if (ClonerTree.Status == ECEClonerAttachmentStatus::Updated)
	{
		ClonerTree.Status = ECEClonerAttachmentStatus::Outdated;
	}

	if (bInReset)
	{
		ClonerTree.Reset();
		ClonerTree.Status = ECEClonerAttachmentStatus::Outdated;
	}

	UpdateAttachmentTree();
#endif
}

void UCEClonerComponent::UpdateAttachmentTree()
{
	if (ClonerTree.Status != ECEClonerAttachmentStatus::Outdated)
	{
		return;
	}
	ClonerTree.Status = ECEClonerAttachmentStatus::Updating;

	// Invalidate all, to see what is outdated and what is still invalid
	for (TPair<TWeakObjectPtr<AActor>, FCEClonerAttachmentItem>& AttachmentPair : ClonerTree.ItemAttachmentMap)
	{
		AttachmentPair.Value.Status = ECEClonerAttachmentStatus::Invalid;
	}

	// Update root attachment items
	TArray<AActor*> RootChildren;
	GetOrderedRootActors(RootChildren);

	TArray<TObjectPtr<UStaticMesh>> NewCombinesMeshes;
	TArray<TWeakObjectPtr<AActor>> NewRootActors;
	for (int32 RootIdx = 0; RootIdx < RootChildren.Num(); RootIdx++)
	{
		AActor* RootChild = RootChildren[RootIdx];

		if (!RootChild)
		{
			continue;
		}

		UpdateActorAttachment(RootChild, nullptr);

		// Lets find the old root idx
		const int32 OldIdx = ClonerTree.RootActors.Find(RootChild);
		TObjectPtr<UStaticMesh> CombineBakedMesh = nullptr;
		if (OldIdx != INDEX_NONE)
		{
			CombineBakedMesh = ClonerTree.MergedBakedMeshes[OldIdx].Get();

			// Did we rearrange stuff ?
			if (RootIdx != OldIdx)
			{
				bClonerMeshesDirty = true;
			}
		}
		NewCombinesMeshes.Add(CombineBakedMesh);
		NewRootActors.Add(RootChild);
	}

	// Did we remove any root actors ?
	if (ClonerTree.RootActors.Num() != NewRootActors.Num())
	{
		bClonerMeshesDirty = true;
	}

	// Did we need to update meshes
	TArray<TWeakObjectPtr<AActor>> ClonedActors;
	ClonerTree.ItemAttachmentMap.GenerateKeyArray(ClonedActors);
	for (const TWeakObjectPtr<AActor>& ClonedActorWeak : ClonedActors)
	{
		FCEClonerAttachmentItem* ClonedItem = ClonerTree.ItemAttachmentMap.Find(ClonedActorWeak);

		if (!ClonedItem)
		{
			continue;
		}

		AActor* ClonedActor = ClonedActorWeak.Get();

		if (ClonedItem->Status == ECEClonerAttachmentStatus::Invalid)
		{
			InvalidateBakedStaticMesh(ClonedActor);
			UnbindActorDelegates(ClonedActor);
			ClonerTree.ItemAttachmentMap.Remove(ClonedItem->ItemActor);
			SetActorVisibility(ClonedActor, true);
		}
		else if (ClonedItem->Status == ECEClonerAttachmentStatus::Outdated)
		{
			if (ClonedItem->MeshStatus == ECEClonerAttachmentStatus::Outdated)
			{
				ClonerTree.DirtyItemAttachments.Add(ClonedItem->ItemActor);
				InvalidateBakedStaticMesh(ClonedActor);
			}
			bClonerMeshesDirty = true;
			ClonedItem->Status = ECEClonerAttachmentStatus::Updated;
		}
	}

	// Did we remove an attachment ?
	if (ClonedActors.Num() != ClonerTree.ItemAttachmentMap.Num())
	{
		bClonerMeshesDirty = true;
	}

	if (!ClonerTree.DirtyItemAttachments.IsEmpty())
	{
		bClonerMeshesDirty = true;
	}

	ClonerTree.RootActors = NewRootActors;
	ClonerTree.MergedBakedMeshes = NewCombinesMeshes;
	ClonerTree.Status = ECEClonerAttachmentStatus::Updated;
}

void UCEClonerComponent::UpdateActorAttachment(AActor* InActor, AActor* InParent)
{
	if (!InActor)
	{
		return;
	}

	const AActor* ClonerActor = GetOwner();
	const FTransform& ClonerTransform = ClonerActor->GetActorTransform();

	// Here order is not important
	TArray<AActor*> ChildrenActors;
	InActor->GetAttachedActors(ChildrenActors, true, false);

	FCEClonerAttachmentItem* AttachmentItem = ClonerTree.ItemAttachmentMap.Find(InActor);
	if (AttachmentItem)
	{
		AttachmentItem->Status = ECEClonerAttachmentStatus::Updated;

		// Check Root is the same
		const bool bIsRoot = InParent == nullptr;
		if (AttachmentItem->bRootItem != bIsRoot)
		{
			InvalidateBakedStaticMesh(InActor);
			AttachmentItem->bRootItem = bIsRoot;
			AttachmentItem->Status = ECEClonerAttachmentStatus::Outdated;
		}

		// Check parent is the same
		if (AttachmentItem->ParentActor.Get() != InParent)
		{
			InvalidateBakedStaticMesh(InParent);
			InvalidateBakedStaticMesh(AttachmentItem->ParentActor.Get());
			AttachmentItem->ParentActor = InParent;
			AttachmentItem->Status = ECEClonerAttachmentStatus::Outdated;
		}

		// Check transform is the same
		const FTransform ActorTransform = InActor->GetActorTransform().GetRelativeTransform(ClonerTransform);
		if (!ActorTransform.Equals(AttachmentItem->ActorTransform))
		{
			// invalidate if not root, else change transform in mesh renderer
			if (!bIsRoot)
			{
				InvalidateBakedStaticMesh(InActor);
			}
			AttachmentItem->ActorTransform = ActorTransform;
			AttachmentItem->Status = ECEClonerAttachmentStatus::Outdated;
		}
	}
	else
	{
		AttachmentItem = &ClonerTree.ItemAttachmentMap.Add(InActor, FCEClonerAttachmentItem());
		AttachmentItem->ItemActor = InActor;
		AttachmentItem->ParentActor = InParent;
		AttachmentItem->ActorTransform = InActor->GetActorTransform().GetRelativeTransform(ClonerTransform);
		AttachmentItem->MeshStatus = ECEClonerAttachmentStatus::Outdated;
		AttachmentItem->bRootItem = InParent == nullptr;
		AttachmentItem->Status = ECEClonerAttachmentStatus::Outdated;
		InvalidateBakedStaticMesh(InActor);
		BindActorDelegates(InActor);
		SetActorVisibility(InActor, false);
	}

	if (AttachmentItem->ChildrenActors.Num() != ChildrenActors.Num())
	{
		InvalidateBakedStaticMesh(InActor);
	}

	AttachmentItem->ChildrenActors.Empty(ChildrenActors.Num());
	for (AActor* ChildActor : ChildrenActors)
	{
		AttachmentItem->ChildrenActors.Add(ChildActor);
		UpdateActorAttachment(ChildActor, InActor);
	}
}

void UCEClonerComponent::BindActorDelegates(AActor* InActor)
{
	if (!InActor)
	{
		return;
	}

	InActor->OnDestroyed.AddUniqueDynamic(this, &UCEClonerComponent::OnActorDestroyed);

#if WITH_EDITOR
	// Detect static mesh change
	TArray<UStaticMeshComponent*> StaticMeshComponents;
	InActor->GetComponents(StaticMeshComponents, false);
	for (UStaticMeshComponent* StaticMeshComponent : StaticMeshComponents)
	{
		if (!StaticMeshComponent->OnStaticMeshChanged().IsBoundToObject(this))
		{
			StaticMeshComponent->OnStaticMeshChanged().AddUObject(this, &UCEClonerComponent::OnMeshChanged, InActor);
		}
	}
#endif

	// Detect dynamic mesh change
	TArray<UDynamicMeshComponent*> DynamicMeshComponents;
	InActor->GetComponents(DynamicMeshComponents, false);
	for (UDynamicMeshComponent* DynamicMeshComponent : DynamicMeshComponents)
	{
		if (!DynamicMeshComponent->OnMeshChanged.IsBoundToObject(this))
		{
			UStaticMeshComponent* NullComponent = nullptr;
			DynamicMeshComponent->OnMeshChanged.AddUObject(this, &UCEClonerComponent::OnMeshChanged, NullComponent, InActor);
		}
	}

	// Detect components transform
	TArray<USceneComponent*> SceneComponents;
	InActor->GetComponents(SceneComponents, /** IncludeChildren */false);
	for (USceneComponent* SceneComponent : SceneComponents)
	{
		if (!SceneComponent->TransformUpdated.IsBoundToObject(this))
		{
			SceneComponent->TransformUpdated.AddUObject(this, &UCEClonerComponent::OnComponentTransformed);
		}
	}
}

void UCEClonerComponent::UnbindActorDelegates(AActor* InActor) const
{
	if (!InActor)
	{
		return;
	}

	InActor->OnDestroyed.RemoveAll(this);

#if WITH_EDITOR
	TArray<UStaticMeshComponent*> StaticMeshComponents;
	InActor->GetComponents(StaticMeshComponents, false);
	for (UStaticMeshComponent* StaticMeshComponent : StaticMeshComponents)
	{
		StaticMeshComponent->OnStaticMeshChanged().RemoveAll(this);
	}
#endif

	TArray<UDynamicMeshComponent*> DynamicMeshComponents;
	InActor->GetComponents(DynamicMeshComponents, false);
	for (UDynamicMeshComponent* DynamicMeshComponent : DynamicMeshComponents)
	{
		DynamicMeshComponent->OnMeshChanged.RemoveAll(this);
	}

	TArray<USceneComponent*> SceneComponents;
	InActor->GetComponents(SceneComponents, /** IncludeChildren */false);
	for (USceneComponent* SceneComponent : SceneComponents)
	{
		SceneComponent->TransformUpdated.RemoveAll(this);
	}
}

void UCEClonerComponent::SetActorVisibility(AActor* InActor, bool bInVisibility)
{
	if (!InActor)
	{
		return;
	}

#if WITH_EDITOR
	InActor->SetIsTemporarilyHiddenInEditor(!bInVisibility);
#endif
	InActor->SetActorHiddenInGame(!bInVisibility);
}

void UCEClonerComponent::OnActorDestroyed(AActor* InDestroyedActor)
{
	if (ClonerTree.ItemAttachmentMap.Contains(InDestroyedActor))
	{
		InvalidateBakedStaticMesh(InDestroyedActor);
		UnbindActorDelegates(InDestroyedActor);
		ClonerTree.ItemAttachmentMap.Remove(InDestroyedActor);
		SetActorVisibility(InDestroyedActor, true);
		bClonerMeshesDirty = true;
	}
}

#if WITH_EDITOR
void UCEClonerComponent::OnActorPropertyChanged(UObject* InObject, FPropertyChangedEvent& InPropertyChangedEvent)
{
	OnMaterialChanged(InObject);
}

void UCEClonerComponent::OnMaterialCompiled(UMaterialInterface* InMaterial)
{
	OnMaterialChanged(InMaterial);
}
#endif

void UCEClonerComponent::OnMaterialChanged(UObject* InObject)
{
	if (!IsValid(InObject))
	{
		return;
	}

	const AActor* ClonerActor = GetOwner();

	if (!ClonerActor)
	{
		return;
	}

	AActor* ActorChanged = Cast<AActor>(InObject);
	ActorChanged = ActorChanged ? ActorChanged : InObject->GetTypedOuter<AActor>();

	if (!ActorChanged)
	{
		return;
	}

	FCEClonerAttachmentItem* AttachmentItem = ClonerTree.ItemAttachmentMap.Find(ActorChanged);

	if (!AttachmentItem)
	{
		return;
	}

	TArray<UPrimitiveComponent*> PrimitiveComponents;
	ActorChanged->GetComponents(PrimitiveComponents, /** IncludeChildrenActors */false);

	int32 MatIdx = 0;
	bool bMaterialChanged = false;
	TArray<TWeakObjectPtr<UMaterialInterface>> NewMaterials;
	NewMaterials.Reserve(PrimitiveComponents.Num());
	UMaterialInterface* DefaultMaterial = LoadObject<UMaterialInterface>(nullptr, UCEClonerEffectorSettings::DefaultMaterialPath);

	TArray<TWeakObjectPtr<UMaterialInterface>> UnsetMaterialsWeak;
	for (UPrimitiveComponent* PrimitiveComponent : PrimitiveComponents)
	{
		if (!PrimitiveComponent || !FCEMeshBuilder::HasAnyGeometry(PrimitiveComponent))
		{
			continue;
		}

		for (int32 MatIndex = 0; MatIndex < PrimitiveComponent->GetNumMaterials(); MatIndex++)
		{
			UMaterialInterface* PreviousMaterial = PrimitiveComponent->GetMaterial(MatIndex);
			UMaterialInterface* NewMaterial = PreviousMaterial;

			if (FilterSupportedMaterial(NewMaterial, DefaultMaterial))
			{
				UnsetMaterialsWeak.Add(PreviousMaterial);
			}

			if (!AttachmentItem->BakedMaterials.IsValidIndex(MatIdx)
				|| AttachmentItem->BakedMaterials[MatIdx] != NewMaterial)
			{
				bMaterialChanged = true;
			}

			NewMaterials.Add(NewMaterial);
			MatIdx++;
		}
	}

	// Show warning for unset materials
	if (!UnsetMaterialsWeak.IsEmpty())
	{
		FireMaterialWarning(ActorChanged, UnsetMaterialsWeak);
	}

	if (bMaterialChanged)
	{
		UE_LOG(LogCEClonerComponent, Log, TEXT("%s : Detected material change for %s"), *ClonerActor->GetActorNameOrLabel(), *ActorChanged->GetActorNameOrLabel());

		if (NewMaterials.Num() == AttachmentItem->BakedMaterials.Num())
		{
			AttachmentItem->BakedMaterials = NewMaterials;
		}
		else
		{
			AttachmentItem->MeshStatus = ECEClonerAttachmentStatus::Outdated;
		}

		InvalidateBakedStaticMesh(ActorChanged);
	}
}

void UCEClonerComponent::OnMeshChanged(UStaticMeshComponent*, AActor* InActor)
{
	if (!InActor)
	{
		return;
	}

	const AActor* ClonerActor = GetOwner();

	if (!ClonerActor)
	{
		return;
	}

	if (FCEClonerAttachmentItem* Item = ClonerTree.ItemAttachmentMap.Find(InActor))
	{
		UE_LOG(LogCEClonerComponent, Log, TEXT("%s : Detected mesh change for %s"), *ClonerActor->GetActorNameOrLabel(), *InActor->GetActorNameOrLabel());

		Item->MeshStatus = ECEClonerAttachmentStatus::Outdated;
        InvalidateBakedStaticMesh(InActor);
		ClonerTree.DirtyItemAttachments.Add(Item->ItemActor);
	}
}

void UCEClonerComponent::GetOrderedRootActors(TArray<AActor*>& OutActors) const
{
	const AActor* ClonerActor = GetOwner();
	if (!ClonerActor)
	{
		return;
	}

	UCEClonerSubsystem* ClonerSubsystem = UCEClonerSubsystem::Get();
	if (!ClonerSubsystem)
	{
		return;
	}

	const UCEClonerSubsystem::FOnGetOrderedActors& CustomActorResolver = ClonerSubsystem->GetCustomActorResolver();

	if (CustomActorResolver.IsBound())
	{
		OutActors = CustomActorResolver.Execute(ClonerActor);
	}
	else
	{
		ClonerActor->GetAttachedActors(OutActors, true, false);
	}
}

AActor* UCEClonerComponent::GetRootActor(AActor* InActor) const
{
	if (InActor == nullptr)
	{
		return nullptr;
	}
	if (const FCEClonerAttachmentItem* Item = ClonerTree.ItemAttachmentMap.Find(InActor))
	{
		if (Item->bRootItem)
		{
			return InActor;
		}

		return GetRootActor(Item->ParentActor.Get());
	}
	return nullptr;
}

void UCEClonerComponent::InvalidateBakedStaticMesh(AActor* InActor)
{
	if (!InActor)
	{
		return;
	}

	if (const FCEClonerAttachmentItem* FoundItem = ClonerTree.ItemAttachmentMap.Find(InActor))
	{
		if (FoundItem->bRootItem || !FoundItem->ParentActor.IsValid())
		{
			const int32 RootIdx = ClonerTree.RootActors.Find(InActor);
			if (ClonerTree.MergedBakedMeshes.IsValidIndex(RootIdx))
			{
				ClonerTree.MergedBakedMeshes[RootIdx] = nullptr;
				bClonerMeshesDirty = true;
			}
		}
		else
		{
			InvalidateBakedStaticMesh(FoundItem->ParentActor.Get());
		}
	}
}

void UCEClonerComponent::UpdateDirtyMeshesAsync()
{
	if (bClonerMeshesUpdating)
	{
		return;
	}

	bClonerMeshesUpdating = true;

	TSet<TWeakObjectPtr<AActor>> DirtyAttachments = ClonerTree.DirtyItemAttachments;
	ClonerTree.DirtyItemAttachments.Empty(DirtyAttachments.Num());

	// Update baked dynamic meshes on other thread
	TWeakObjectPtr<UCEClonerComponent> ThisWeak(this);
	Async(EAsyncExecution::ThreadPool, [ThisWeak, DirtyAttachments]()
	{
		UCEClonerComponent* This = ThisWeak.Get();

		if (!This)
		{
			return;
		}

		// update actor baked dynamic meshes
		bool bSuccess = true;
		for (const TWeakObjectPtr<AActor>& Attachment : DirtyAttachments)
		{
			AActor* DirtyActor = Attachment.Get();

			if (!DirtyActor)
			{
				continue;
			}

			if (IsGarbageCollectingAndLockingUObjectHashTables())
			{
				bSuccess = false;
				This->ClonerTree.DirtyItemAttachments.Add(Attachment);
				continue;
			}

			This->UpdateActorBakedDynamicMesh(DirtyActor);
		}

		// Create baked static mesh on main thread (required)
		Async(EAsyncExecution::TaskGraphMainThread, [ThisWeak, &bSuccess]()
		{
			UCEClonerComponent* This = ThisWeak.Get();

			if (!This)
			{
				return;
			}

			if (!bSuccess)
			{
				This->OnDirtyMeshesUpdated(false);
				return;
			}

			// Update actors baked static mesh
			for (int32 Idx = 0; Idx < This->ClonerTree.RootActors.Num(); Idx++)
			{
				if (IsGarbageCollectingAndLockingUObjectHashTables())
				{
					bSuccess = false;
					break;
				}

				const UStaticMesh* RootStaticMesh = This->ClonerTree.MergedBakedMeshes[Idx].Get();

				if (!RootStaticMesh)
				{
					AActor* RootActor = This->ClonerTree.RootActors[Idx].Get();
					This->UpdateRootActorBakedStaticMesh(RootActor);
				}
			}

			// update niagara asset
			This->OnDirtyMeshesUpdated(bSuccess);
		});
	});
}

void UCEClonerComponent::OnDirtyMeshesUpdated(bool bInSuccess)
{
	bClonerMeshesUpdating = false;

	// Update niagara parameters
	if (bInSuccess)
	{
		UpdateClonerMeshes();
	}
}

void UCEClonerComponent::UpdateActorBakedDynamicMesh(AActor* InActor)
{
	if (!InActor)
	{
		return;
	}

	const AActor* ClonerActor = GetOwner();

	if (!ClonerActor)
	{
		return;
	}

	FCEClonerAttachmentItem* AttachmentItem = ClonerTree.ItemAttachmentMap.Find(InActor);

	if (!AttachmentItem || AttachmentItem->MeshStatus != ECEClonerAttachmentStatus::Outdated)
	{
		return;
	}

	AttachmentItem->MeshStatus = ECEClonerAttachmentStatus::Updating;

	UE_LOG(LogCEClonerComponent, Log, TEXT("%s : Updating baked actor mesh %s"), *ClonerActor->GetActorNameOrLabel(), *InActor->GetActorNameOrLabel());

	UDynamicMesh* Mesh = NewObject<UDynamicMesh>();
	TArray<TWeakObjectPtr<UMaterialInterface>> MeshMaterials;

	MeshBuilder.AppendActor(InActor);
	MeshBuilder.BuildDynamicMesh(Mesh, MeshMaterials);
	MeshBuilder.Reset();

	AttachmentItem->BakedMesh = Mesh;

	TArray<TWeakObjectPtr<UMaterialInterface>> UnsetMaterials;
	UMaterialInterface* DefaultMaterial = LoadObject<UMaterialInterface>(nullptr, UCEClonerEffectorSettings::DefaultMaterialPath);
	if (FilterSupportedMaterials(MeshMaterials, UnsetMaterials, DefaultMaterial))
	{
		FireMaterialWarning(InActor, UnsetMaterials);
	}

	AttachmentItem->BakedMaterials = MoveTemp(MeshMaterials);

	// Was the mesh invalidated during the update process ?
	AttachmentItem->MeshStatus = AttachmentItem->MeshStatus == ECEClonerAttachmentStatus::Outdated ? ECEClonerAttachmentStatus::Outdated : ECEClonerAttachmentStatus::Updated; //-V547

	InvalidateBakedStaticMesh(AttachmentItem->ItemActor.Get());
}

void UCEClonerComponent::UpdateRootActorBakedStaticMesh(AActor* InRootActor)
{
	if (!InRootActor)
	{
		return;
	}

	const AActor* ClonerActor = GetOwner();

	if (!ClonerActor)
	{
		return;
	}

	const int32 RootIdx = ClonerTree.RootActors.Find(InRootActor);

	if (RootIdx == INDEX_NONE)
	{
		return;
	}

	const FCEClonerAttachmentItem* RootAttachmentItem = ClonerTree.ItemAttachmentMap.Find(InRootActor);

	if (!RootAttachmentItem)
	{
		return;
	}

	UE_LOG(LogCEClonerComponent, Log, TEXT("%s : Updating root merged baked mesh %s"), *ClonerActor->GetActorNameOrLabel(), *InRootActor->GetActorNameOrLabel());

	TArray<FCEClonerAttachmentItem*> AttachmentItems;
	GetActorAttachmentItems(InRootActor, AttachmentItems);

	for (FCEClonerAttachmentItem* AttachmentItem : AttachmentItems)
	{
		if (!AttachmentItem)
		{
			continue;
		}

		const UDynamicMesh* BakedDynamicMesh = AttachmentItem->BakedMesh;
		if (!BakedDynamicMesh)
		{
			continue;
		}

		FTransform MeshTransform = FTransform::Identity;
		if (AttachmentItem->ParentActor.IsValid())
		{
			const FTransform& ParentTransform = AttachmentItem->ParentActor->GetTransform();
			MeshTransform = AttachmentItem->ItemActor->GetTransform().GetRelativeTransform(ParentTransform);
		}

		MeshBuilder.AppendMesh(AttachmentItem->BakedMesh, AttachmentItem->BakedMaterials, MeshTransform);
	}

	UStaticMesh* Mesh = NewObject<UStaticMesh>();
	TArray<TWeakObjectPtr<UMaterialInterface>> MeshMaterials;
	bClonerMeshesDirty = MeshBuilder.BuildStaticMesh(Mesh, MeshMaterials);
	MeshBuilder.Reset();

	ClonerTree.MergedBakedMeshes[RootIdx] = Mesh;
}

bool UCEClonerComponent::FilterSupportedMaterials(TArray<TWeakObjectPtr<UMaterialInterface>>& InMaterials, TArray<TWeakObjectPtr<UMaterialInterface>>& OutUnsetMaterials, UMaterialInterface* InDefaultMaterial)
{
	check(InDefaultMaterial)

	OutUnsetMaterials.Reset(InMaterials.Num());

	for (int32 Index = 0; Index < InMaterials.Num(); Index++)
	{
		UMaterialInterface* PreviousMaterialInterface = InMaterials[Index].Get();

		UMaterialInterface* NewMaterialInterface = PreviousMaterialInterface;

		if (FilterSupportedMaterial(NewMaterialInterface, InDefaultMaterial))
		{
			// Add original material to unset list
			OutUnsetMaterials.Add(PreviousMaterialInterface);
		}

		// Replace material
		InMaterials[Index] = NewMaterialInterface;
	}

	return OutUnsetMaterials.IsEmpty();
}

bool UCEClonerComponent::FilterSupportedMaterial(UMaterialInterface*& InMaterial, UMaterialInterface* InDefaultMaterial)
{
	if (InMaterial && !IsMaterialUsageFlagSet(InMaterial))
	{
		// Replace material if dirtyable and not in read only location
		if (!IsMaterialDirtyable(InMaterial))
		{
			InMaterial = InDefaultMaterial;
		}

		return true;
	}

	return false;
}

void UCEClonerComponent::GetActorAttachmentItems(AActor* InActor, TArray<FCEClonerAttachmentItem*>& OutAttachmentItems)
{
	if (!InActor)
	{
		return;
	}

	FCEClonerAttachmentItem* AttachmentItem = ClonerTree.ItemAttachmentMap.Find(InActor);

	if (!AttachmentItem)
	{
		return;
	}

	OutAttachmentItems.Add(AttachmentItem);

	for (TWeakObjectPtr<AActor>& ChildActor : AttachmentItem->ChildrenActors)
	{
		if (ChildActor.IsValid())
		{
			GetActorAttachmentItems(ChildActor.Get(), OutAttachmentItems);
		}
	}
}

bool UCEClonerComponent::IsAllMergedMeshesValid() const
{
	for (const TObjectPtr<UStaticMesh>& MergedMesh : ClonerTree.MergedBakedMeshes)
	{
		if (!MergedMesh.Get())
		{
			return false;
		}
	}
	return true;
}

bool UCEClonerComponent::IsMaterialDirtyable(const UMaterialInterface* InMaterial)
{
	const UMaterial* BaseMaterial = InMaterial->GetMaterial_Concurrent();
	const FString ContentFolder = FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir());

	const UPackage* MaterialPackage = BaseMaterial->GetPackage();
	const FPackagePath& LoadedPath = MaterialPackage->GetLoadedPath();
	const FString PackagePath = FPaths::ConvertRelativePathToFull(LoadedPath.GetLocalFullPath());
	const FString MaterialPath = BaseMaterial->GetPathName();

	const bool bTransientPackage = MaterialPackage == GetTransientPackage() || MaterialPath.StartsWith("/Temp/");
	const bool bContentFolder = PackagePath.StartsWith(ContentFolder);

	return bTransientPackage || bContentFolder;
}

bool UCEClonerComponent::IsMaterialUsageFlagSet(const UMaterialInterface* InMaterial)
{
	if (InMaterial)
	{
		if (const UMaterial* Material = InMaterial->GetMaterial_Concurrent())
		{
			return Material->GetUsageByFlag(EMaterialUsage::MATUSAGE_NiagaraMeshParticles);
		}
	}

	return false;
}

void UCEClonerComponent::FireMaterialWarning(const AActor* InContextActor, const TArray<TWeakObjectPtr<UMaterialInterface>>& InUnsetMaterials)
{
	if (!IsValid(InContextActor) || InUnsetMaterials.IsEmpty())
	{
		return;
	}

	UE_LOG(LogCEClonerComponent, Warning, TEXT("%s : %i unsupported material(s) detected due to missing niagara usage flag (bUsedWithNiagaraMeshParticles) on actor (%s), see logs below"), *GetOwner()->GetActorNameOrLabel(), InUnsetMaterials.Num(), *InContextActor->GetActorNameOrLabel());

	const AActor* ClonerActor = GetOwner();
	for (const TWeakObjectPtr<UMaterialInterface>& UnsetMaterialWeak : InUnsetMaterials)
	{
		if (UMaterialInterface* UnsetMaterial = UnsetMaterialWeak.Get())
		{
			UE_LOG(LogCEClonerComponent, Warning, TEXT("%s : The following materials (%s) on actor (%s) does not have the usage flag (bUsedWithNiagaraMeshParticles) set to work with the cloner, set the flag and resave the asset to avoid this warning"), *ClonerActor->GetActorNameOrLabel(), *UnsetMaterial->GetMaterial()->GetPathName(), *InContextActor->GetActorNameOrLabel());
		}
	}

#if WITH_EDITOR
	// Fire warning notification when invalid materials are found and at least 5s has elapsed since last notification
	constexpr double MinNotificationElapsedTime = 5.0;
	const double CurrentTime = FApp::GetCurrentTime();

	if (CurrentTime - LastNotificationTime > MinNotificationElapsedTime)
	{
		LastNotificationTime = CurrentTime;
		ShowMaterialWarning(InUnsetMaterials.Num());
	}
#endif
}

#if WITH_EDITOR
void UCEClonerComponent::ShowMaterialWarning(int32 InMaterialCount)
{
	if (InMaterialCount > 0)
	{
		FNotificationInfo NotificationInfo(FText::Format(LOCTEXT("MaterialsMissingUsageFlag", "Detected {0} material(s) with missing usage flag required to work properly with cloner (See logs)"), InMaterialCount));
		NotificationInfo.ExpireDuration = 5.f;
		NotificationInfo.bFireAndForget = true;
		NotificationInfo.Image = FAppStyle::GetBrush("Icons.WarningWithColor");

		FSlateNotificationManager::Get().AddNotification(NotificationInfo);
	}
}

FName UCEClonerComponent::GetActiveExtensionsPropertyName()
{
	return GET_MEMBER_NAME_CHECKED(UCEClonerComponent, ActiveExtensions);
}

FName UCEClonerComponent::GetActiveLayoutPropertyName()
{
	return GET_MEMBER_NAME_CHECKED(UCEClonerComponent, ActiveLayout);
}

FName UCEClonerComponent::GetLayoutNamePropertyName()
{
	return GET_MEMBER_NAME_CHECKED(UCEClonerComponent, LayoutName);
}
#endif // WITH_EDITOR

void UCEClonerComponent::InitializeCloner()
{
	if (bClonerInitialized)
	{
		return;
	}

	bClonerInitialized = true;

	SetAsset(nullptr);

#if WITH_EDITOR
	OnVisualizerSpriteVisibleChanged();

	const AActor* Owner = GetOwner();

	// Skip init for preview actor
	if (Owner && Owner->bIsEditorPreviewActor)
	{
		return;
	}
#endif

	// Register a custom ticker to avoid using the component tick that needs the simulation to be solo
	TreeUpdateDeltaTime = TreeUpdateInterval;
	RegisterTicker();

	// Load layout after registering ticker to let attachment tree update, then layout rendering will occur with up to date attachment data
	OnLayoutNameChanged();

	OnClonerInitializedDelegate.Broadcast(this);
}

void UCEClonerComponent::RegisterTicker()
{
	FTSTicker::GetCoreTicker().RemoveTicker(ClonerTickerHandle);
	ClonerTickerHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &UCEClonerComponent::TickCloner));
}

bool UCEClonerComponent::TickCloner(float InDelta)
{
	if (!bClonerInitialized)
	{
		return false;
	}

	if (bEnabled)
	{
		TreeUpdateDeltaTime += InDelta;

		// Update attachment tree
		if (TreeUpdateDeltaTime >= TreeUpdateInterval)
		{
			TreeUpdateDeltaTime -= TreeUpdateInterval != 0.f ? TreeUpdateInterval : TreeUpdateDeltaTime;

			UpdateClonerAttachmentTree();
			UpdateClonerRenderState();
		}

		// Update layout parameters
		if (ActiveLayout && ActiveLayout->IsLayoutDirty())
		{
			ActiveLayout->UpdateLayoutParameters();
		}

		// Update extension parameters
		for (const TObjectPtr<UCEClonerExtensionBase>& ActiveExtension : ActiveExtensions)
		{
			if (ActiveExtension && ActiveExtension->IsExtensionDirty())
			{
				ActiveExtension->UpdateExtensionParameters();
			}
		}

		// Is a simulation reset needed
		if (bNeedsRefresh)
		{
			bNeedsRefresh = false;
			RequestClonerUpdate(/**Immediate*/true);
		}
	}

	return true;
}

void UCEClonerComponent::SetEnabled(bool bInEnable)
{
	if (bInEnable == bEnabled)
	{
		return;
	}

	bEnabled = bInEnable;
	OnEnabledChanged();
}

void UCEClonerComponent::SetTreeUpdateInterval(float InInterval)
{
	if (InInterval == TreeUpdateInterval)
	{
		return;
	}

	TreeUpdateInterval = InInterval;
}

void UCEClonerComponent::SetSeed(int32 InSeed)
{
	if (InSeed == Seed)
	{
		return;
	}

	Seed = InSeed;
	OnSeedChanged();
}

void UCEClonerComponent::SetColor(const FLinearColor& InColor)
{
	if (InColor.Equals(Color))
	{
		return;
	}

	Color = InColor;
	OnColorChanged();
}

void UCEClonerComponent::SetLayoutName(FName InLayoutName)
{
	if (LayoutName == InLayoutName)
	{
		return;
	}

	const TArray<FName> LayoutNames = GetClonerLayoutNames();
	if (!LayoutNames.Contains(InLayoutName))
	{
		return;
	}

	LayoutName = InLayoutName;
	OnLayoutNameChanged();
}

void UCEClonerComponent::SetLayoutClass(TSubclassOf<UCEClonerLayoutBase> InLayoutClass)
{
	if (!InLayoutClass.Get())
	{
		return;
	}

	if (const UCEClonerSubsystem* ClonerSubsystem = UCEClonerSubsystem::Get())
	{
		const FName NewLayoutName = ClonerSubsystem->FindLayoutName(InLayoutClass);

		if (!NewLayoutName.IsNone())
		{
			SetLayoutName(NewLayoutName);
		}
	}
}

TSubclassOf<UCEClonerLayoutBase> UCEClonerComponent::GetLayoutClass() const
{
	return ActiveLayout ? ActiveLayout->GetClass() : nullptr;
}

#if WITH_EDITOR
void UCEClonerComponent::SetVisualizerSpriteVisible(bool bInVisible)
{
	if (bVisualizerSpriteVisible == bInVisible)
	{
		return;
	}

	bVisualizerSpriteVisible = bInVisible;
	OnVisualizerSpriteVisibleChanged();
}
#endif

int32 UCEClonerComponent::GetMeshCount() const
{
	if (const UCEClonerLayoutBase* LayoutSystem = GetActiveLayout())
	{
		if (const UNiagaraMeshRendererProperties* MeshRenderer = LayoutSystem->GetMeshRenderer())
		{
			return MeshRenderer->Meshes.Num();
		}
	}

	return 0;
}

int32 UCEClonerComponent::GetAttachmentCount() const
{
	return ClonerTree.ItemAttachmentMap.Num();
}

#if WITH_EDITOR
void UCEClonerComponent::ForceUpdateCloner()
{
	UpdateClonerAttachmentTree();
	UpdateClonerRenderState();
	OnLayoutNameChanged();
}

void UCEClonerComponent::OpenClonerSettings()
{
	if (const UCEClonerEffectorSettings* ClonerSettings = GetDefault<UCEClonerEffectorSettings>())
	{
		ClonerSettings->OpenEditorSettingsWindow();
	}
}

void UCEClonerComponent::CreateDefaultActorAttached()
{
	const UCEClonerEffectorSettings* ClonerEffectorSettings = GetDefault<UCEClonerEffectorSettings>();
	if (!ClonerEffectorSettings || !ClonerEffectorSettings->GetSpawnDefaultActorAttached())
	{
		return;
	}

	// Only spawn if world is valid and not a preview actor
	UWorld* World = GetWorld();
	AActor* Owner = GetOwner();

	if (!IsValid(World)
		|| !IsValid(Owner)
		|| Owner->bIsEditorPreviewActor)
	{
		return;
	}

	// Only spawn if no actor is attached below it
	TArray<AActor*> AttachedActors;
	constexpr bool bReset = true;
	constexpr bool bRecursive = false;
	Owner->GetAttachedActors(AttachedActors, bReset, bRecursive);

	if (!AttachedActors.IsEmpty())
	{
		return;
	}

	UStaticMesh* DefaultStaticMesh = ClonerEffectorSettings->GetDefaultStaticMesh();
	UMaterialInterface* DefaultMaterial = ClonerEffectorSettings->GetDefaultMaterial();

	if (!DefaultStaticMesh || !DefaultMaterial)
	{
		return;
	}

	FScopedTransaction Transaction(LOCTEXT("CreateDefaultActorAttached", "Create cloner default actor attached"), !GIsTransacting);

	Modify();

	// Spawn attached actor with same flags as this actor
	FActorSpawnParameters SpawnParameters;
	SpawnParameters.Owner = Owner;
	SpawnParameters.ObjectFlags = GetFlags() | RF_Transactional;
	SpawnParameters.bTemporaryEditorActor = false;

	const FVector ClonerLocation = GetComponentLocation();
	const FRotator ClonerRotation = GetComponentRotation();

	if (AStaticMeshActor* DefaultActorAttached = World->SpawnActor<AStaticMeshActor>(ClonerLocation, ClonerRotation, SpawnParameters))
	{
		UStaticMeshComponent* StaticMeshComponent = DefaultActorAttached->GetStaticMeshComponent();
		StaticMeshComponent->SetStaticMesh(DefaultStaticMesh);
		StaticMeshComponent->SetMaterial(0, DefaultMaterial);

		DefaultActorAttached->SetMobility(EComponentMobility::Movable);
		DefaultActorAttached->AttachToActor(GetOwner(), FAttachmentTransformRules::KeepWorldTransform);

		FActorLabelUtilities::SetActorLabelUnique(DefaultActorAttached, TEXT("DefaultClone"));
	}
}

void UCEClonerComponent::ConvertToStaticMesh()
{
	if (!IsValid(this) || !bEnabled)
	{
		return;
	}

	FScopedSlowTask SlowTask(0.0f, LOCTEXT("ConvertToStaticMesh", "Converting cloner to static mesh"));
	SlowTask.MakeDialog();

	UE_LOG(LogCEClonerComponent, Log, TEXT("%s : Request ConvertToStaticMesh..."), *GetOwner()->GetActorNameOrLabel())

	if (UE::ClonerEffector::Conversion::ConvertClonerToStaticMesh(this))
	{
		UE_LOG(LogCEClonerComponent, Log, TEXT("%s : ConvertToStaticMesh Completed"), *GetOwner()->GetActorNameOrLabel())
	}
	else
	{
		UE_LOG(LogCEClonerComponent, Warning, TEXT("%s : ConvertToStaticMesh Failed"), *GetOwner()->GetActorNameOrLabel())
	}
}

void UCEClonerComponent::ConvertToDynamicMesh()
{
	if (!IsValid(this) || !bEnabled)
	{
		return;
	}

	FScopedSlowTask SlowTask(0.0f, LOCTEXT("ConvertToDynamicMesh", "Converting cloner to dynamic mesh"));
	SlowTask.MakeDialog();

	UE_LOG(LogCEClonerComponent, Log, TEXT("%s : Request ConvertToDynamicMesh..."), *GetOwner()->GetActorNameOrLabel())

	if (UE::ClonerEffector::Conversion::ConvertClonerToDynamicMesh(this))
	{
		UE_LOG(LogCEClonerComponent, Log, TEXT("%s : ConvertToDynamicMesh Completed"), *GetOwner()->GetActorNameOrLabel())
	}
	else
	{
		UE_LOG(LogCEClonerComponent, Warning, TEXT("%s : ConvertToDynamicMesh Failed"), *GetOwner()->GetActorNameOrLabel())
	}
}

void UCEClonerComponent::ConvertToStaticMeshes()
{
	if (!IsValid(this) || !bEnabled)
	{
		return;
	}

	FScopedSlowTask SlowTask(0.0f, LOCTEXT("ConvertToStaticMeshes", "Converting cloner to static meshes"));
	SlowTask.MakeDialog();

	UE_LOG(LogCEClonerComponent, Log, TEXT("%s : Request ConvertToStaticMeshes..."), *GetOwner()->GetActorNameOrLabel())

	if (!UE::ClonerEffector::Conversion::ConvertClonerToStaticMeshes(this).IsEmpty())
	{
		UE_LOG(LogCEClonerComponent, Log, TEXT("%s : ConvertToStaticMeshes Completed"), *GetOwner()->GetActorNameOrLabel())
	}
	else
	{
		UE_LOG(LogCEClonerComponent, Warning, TEXT("%s : ConvertToStaticMeshes Failed"), *GetOwner()->GetActorNameOrLabel())
	}
}

void UCEClonerComponent::ConvertToDynamicMeshes()
{
	if (!IsValid(this) || !bEnabled)
	{
		return;
	}

	FScopedSlowTask SlowTask(0.0f, LOCTEXT("ConvertToDynamicMeshes", "Converting cloner to dynamic meshes"));
	SlowTask.MakeDialog();

	UE_LOG(LogCEClonerComponent, Log, TEXT("%s : Request ConvertToDynamicMeshes..."), *GetOwner()->GetActorNameOrLabel())

	if (!UE::ClonerEffector::Conversion::ConvertClonerToDynamicMeshes(this).IsEmpty())
	{
		UE_LOG(LogCEClonerComponent, Log, TEXT("%s : ConvertToDynamicMeshes Completed"), *GetOwner()->GetActorNameOrLabel())
	}
	else
	{
		UE_LOG(LogCEClonerComponent, Warning, TEXT("%s : ConvertToDynamicMeshes Failed"), *GetOwner()->GetActorNameOrLabel())
	}
}

void UCEClonerComponent::ConvertToInstancedStaticMeshes()
{
	if (!IsValid(this) || !bEnabled)
	{
		return;
	}

	FScopedSlowTask SlowTask(0.0f, LOCTEXT("ConvertToInstancedStaticMeshes", "Converting cloner to instanced static meshes"));
	SlowTask.MakeDialog();

	UE_LOG(LogCEClonerComponent, Log, TEXT("%s : Request ConvertToInstancedStaticMeshes..."), *GetOwner()->GetActorNameOrLabel())

	if (!UE::ClonerEffector::Conversion::ConvertClonerToInstancedStaticMeshes(this).IsEmpty())
	{
		UE_LOG(LogCEClonerComponent, Log, TEXT("%s : ConvertToInstancedStaticMeshes Completed"), *GetOwner()->GetActorNameOrLabel())
	}
	else
	{
		UE_LOG(LogCEClonerComponent, Warning, TEXT("%s : ConvertToInstancedStaticMeshes Failed"), *GetOwner()->GetActorNameOrLabel())
	}
}
#endif

void UCEClonerComponent::RequestClonerUpdate(bool bInImmediate)
{
	if (!bEnabled)
	{
		return;
	}

	if (bInImmediate)
	{
		bNeedsRefresh = false;

		FNiagaraUserRedirectionParameterStore& UserParameterStore = GetOverrideParameters();
		UserParameterStore.PostGenericEditChange();
	}
	else
	{
		bNeedsRefresh = true;
	}
}

void UCEClonerComponent::OnEnabledChanged()
{
	if (bEnabled)
	{
		OnClonerEnabled();
	}
	else
	{
		OnClonerDisabled();
	}
}

void UCEClonerComponent::OnClonerEnabled()
{
	for (const TObjectPtr<UCEClonerExtensionBase>& ActiveExtension : ActiveExtensions)
	{
		ActiveExtension->ActivateExtension();
	}

	OnLayoutNameChanged();
}

void UCEClonerComponent::OnClonerDisabled()
{
	for (const TObjectPtr<UCEClonerExtensionBase>& ActiveExtension : ActiveExtensions)
	{
		ActiveExtension->DeactivateExtension();
	}

	DeactivateImmediate();
	SetAsset(nullptr);
}

void UCEClonerComponent::OnClonerSetEnabled(const UWorld* InWorld, bool bInEnabled, bool bInTransact)
{
	if (GetWorld() == InWorld)
	{
#if WITH_EDITOR
		if (bInTransact)
		{
			Modify();
		}
#endif

		SetEnabled(bInEnabled);
	}
}

void UCEClonerComponent::OnSeedChanged()
{
	if (!bEnabled)
	{
		return;
	}

	SetRandomSeedOffset(Seed);

	RequestClonerUpdate();
}

void UCEClonerComponent::OnColorChanged()
{
	SetColorParameter(TEXT("EffectorDefaultColor"), Color);
}

void UCEClonerComponent::OnLayoutNameChanged()
{
	if (!bEnabled)
	{
		return;
	}

	const TArray<FName> LayoutNames = GetClonerLayoutNames();

	// Set default if value does not exists
	if (!LayoutNames.Contains(LayoutName) && !LayoutNames.IsEmpty())
	{
		LayoutName = LayoutNames[0];
	}

	UCEClonerLayoutBase* NewActiveLayout = FindOrAddLayout(LayoutName);

	// Apply layout
	SetClonerActiveLayout(NewActiveLayout);
}

#if WITH_EDITOR
void UCEClonerComponent::OnVisualizerSpriteVisibleChanged()
{
	if (UTexture2D* SpriteTexture = LoadObject<UTexture2D>(nullptr, SpriteTexturePath))
	{
		CreateSpriteComponent(SpriteTexture);

		if (SpriteComponent)
		{
			if (SpriteComponent->Sprite != SpriteTexture)
			{
				SpriteComponent->SetSprite(SpriteTexture);
			}

			SpriteComponent->SetVisibility(bVisualizerSpriteVisible, false);
		}
	}
}
#endif

void UCEClonerComponent::OnRenderStateDirty(UActorComponent& InActorComponent)
{
	AActor* Owner = InActorComponent.GetOwner();
	const AActor* ClonerActor = GetOwner();

	if (!Owner || Owner->GetLevel() != ClonerActor->GetLevel())
	{
		return;
	}

	// Does it contain geometry that we can convert
	if (!FCEMeshBuilder::IsComponentSupported(&InActorComponent))
	{
		return;
	}

	FCEClonerAttachmentItem* Item = ClonerTree.ItemAttachmentMap.Find(Owner);

	if (!Item)
	{
		return;
	}

	UE_LOG(LogCEClonerComponent, Log, TEXT("%s : Render state changed for %s"), *ClonerActor->GetActorNameOrLabel(), *Owner->GetActorNameOrLabel());

	// Rebind delegates as new components might be available
	BindActorDelegates(Owner);

	Item->MeshStatus = ECEClonerAttachmentStatus::Outdated;
	InvalidateBakedStaticMesh(Owner);
	ClonerTree.DirtyItemAttachments.Add(Item->ItemActor);
}

void UCEClonerComponent::OnComponentTransformed(USceneComponent* InComponent, EUpdateTransformFlags InFlags, ETeleportType InTeleport)
{
	if (!InComponent || !InComponent->GetOwner() || InFlags == EUpdateTransformFlags::PropagateFromParent)
	{
		return;
	}

	AActor* Owner = InComponent->GetOwner();
	const AActor* RootActor = GetRootActor(Owner);

	// Skip update if root component has moved, since we can simply offset the mesh
	if (!RootActor || (RootActor == Owner && RootActor->GetRootComponent() == InComponent))
	{
		return;
	}

	bool bComponentSupported = FCEMeshBuilder::IsComponentSupported(InComponent);

	if (!bComponentSupported)
	{
		for (const TObjectPtr<USceneComponent>& ChildComponent : InComponent->GetAttachChildren())
		{
			if (FCEMeshBuilder::IsComponentSupported(ChildComponent))
			{
				bComponentSupported = true;
				break;
			}
		}
	}

	if (!bComponentSupported)
	{
		return;
	}

	FCEClonerAttachmentItem* Item = ClonerTree.ItemAttachmentMap.Find(Owner);

	if (!Item)
	{
		return;
	}

	UE_LOG(LogCEClonerComponent, Log, TEXT("%s : Transform state changed for %s"), *GetOwner()->GetActorNameOrLabel(), *Owner->GetActorNameOrLabel());

	Item->MeshStatus = ECEClonerAttachmentStatus::Outdated;
	InvalidateBakedStaticMesh(Owner);
	ClonerTree.DirtyItemAttachments.Add(Item->ItemActor);
}

UCEClonerLayoutBase* UCEClonerComponent::FindOrAddLayout(TSubclassOf<UCEClonerLayoutBase> InClass)
{
	const UCEClonerSubsystem* Subsystem = UCEClonerSubsystem::Get();

	if (!Subsystem)
	{
		return nullptr;
	}

	const FName ClassLayoutName = Subsystem->FindLayoutName(InClass);

	if (ClassLayoutName.IsNone())
	{
		return nullptr;
	}

	return FindOrAddLayout(ClassLayoutName);
}

UCEClonerLayoutBase* UCEClonerComponent::FindOrAddLayout(FName InLayoutName)
{
	if (IsTemplate())
	{
		return nullptr;
	}

	UCEClonerSubsystem* Subsystem = UCEClonerSubsystem::Get();
	if (!Subsystem)
	{
		return nullptr;
	}

	// Check cached layout instances
	UCEClonerLayoutBase* NewActiveLayout = nullptr;
	for (const TObjectPtr<UCEClonerLayoutBase>& LayoutInstance : LayoutInstances)
	{
		if (LayoutInstance && LayoutInstance->GetLayoutName() == InLayoutName)
		{
			NewActiveLayout = LayoutInstance;
			break;
		}
	}

	// Create new layout instance and cache it
	if (!NewActiveLayout)
	{
		NewActiveLayout = Subsystem->CreateNewLayout(InLayoutName, this);

		if (NewActiveLayout)
		{
			LayoutInstances.Add(NewActiveLayout);
		}
	}

	return NewActiveLayout;
}

UCEClonerExtensionBase* UCEClonerComponent::FindOrAddExtension(TSubclassOf<UCEClonerExtensionBase> InClass)
{
	const UCEClonerSubsystem* Subsystem = UCEClonerSubsystem::Get();

	if (!Subsystem)
	{
		return nullptr;
	}

	const FName ExtensionName = Subsystem->FindExtensionName(InClass);

	if (ExtensionName.IsNone())
	{
		return nullptr;
	}

	return FindOrAddExtension(ExtensionName);
}

UCEClonerExtensionBase* UCEClonerComponent::FindOrAddExtension(FName InExtensionName)
{
	// Check cached extension instances
	UCEClonerExtensionBase* NewActiveExtension = nullptr;
	for (TObjectPtr<UCEClonerExtensionBase>& ExtensionInstance : ExtensionInstances)
	{
		if (ExtensionInstance && ExtensionInstance->GetExtensionName() == InExtensionName)
		{
			NewActiveExtension = ExtensionInstance;
			break;
		}
	}

	// Create new extension instance and cache it
	if (!NewActiveExtension)
	{
		UCEClonerSubsystem* Subsystem = UCEClonerSubsystem::Get();
		if (!Subsystem)
		{
			return nullptr;
		}

		NewActiveExtension = Subsystem->CreateNewExtension(InExtensionName, this);
		ExtensionInstances.Add(NewActiveExtension);
	}

	return NewActiveExtension;
}

TArray<FName> UCEClonerComponent::GetClonerLayoutNames() const
{
	TArray<FName> LayoutNames;

	if (const UCEClonerSubsystem* Subsystem = UCEClonerSubsystem::Get())
	{
		LayoutNames = Subsystem->GetLayoutNames().Array();
	}

	return LayoutNames;
}

void UCEClonerComponent::RefreshClonerMeshes()
{
	if (!bClonerMeshesUpdating && !bClonerMeshesDirty)
	{
		UpdateClonerMeshes();
	}
}

UCEClonerExtensionBase* UCEClonerComponent::GetExtension(TSubclassOf<UCEClonerExtensionBase> InExtensionClass) const
{
	const UCEClonerSubsystem* Subsystem = UCEClonerSubsystem::Get();

	if (!Subsystem)
	{
		return nullptr;
	}

	const FName ExtensionName = Subsystem->FindExtensionName(InExtensionClass.Get());

	if (ExtensionName.IsNone())
	{
		return nullptr;
	}

	return GetExtension(ExtensionName);
}

UCEClonerExtensionBase* UCEClonerComponent::GetExtension(FName InExtensionName) const
{
	for (const TObjectPtr<UCEClonerExtensionBase>& ExtensionInstance : ExtensionInstances)
	{
		if (ExtensionInstance && ExtensionInstance->GetExtensionName() == InExtensionName)
		{
			return ExtensionInstance;
		}
	}

	return nullptr;
}

void UCEClonerComponent::OnActiveLayoutLoaded(UCEClonerLayoutBase* InLayout, bool bInSuccess)
{
	if (!InLayout)
	{
		return;
	}

	InLayout->OnLayoutLoadedDelegate().RemoveAll(this);

	if (!bInSuccess)
	{
		UE_LOG(LogCEClonerComponent, Warning, TEXT("%s : Cloner layout system failed to load %s - %s"), *GetOwner()->GetActorNameOrLabel(), *InLayout->GetLayoutName().ToString(), *InLayout->GetLayoutAssetPath());
		return;
	}

	UE_LOG(LogCEClonerComponent, Log, TEXT("%s : Cloner layout system loaded %s - %s"), *GetOwner()->GetActorNameOrLabel(), *InLayout->GetLayoutName().ToString(), *InLayout->GetLayoutAssetPath());

	OnClonerLayoutLoadedDelegate.Broadcast(this, InLayout);

	ActivateLayout(InLayout);
}

void UCEClonerComponent::ActivateLayout(UCEClonerLayoutBase* InLayout)
{
	// Must be valid and loaded
	if (!InLayout || !InLayout->IsLayoutLoaded())
	{
		return;
	}

	// Should match current active layout name
	if (LayoutName != InLayout->GetLayoutName())
	{
		return;
	}

	// Deactivate previous layout
	if (ActiveLayout && ActiveLayout->IsLayoutActive())
	{
		ActiveLayout->DeactivateLayout();
	}

	// Activate new layout
	InLayout->ActivateLayout();

	ActiveLayout = InLayout;

	UE_LOG(LogCEClonerComponent, Log, TEXT("%s : Cloner layout system changed %s - %s"), *GetOwner()->GetActorNameOrLabel(), *InLayout->GetLayoutName().ToString(), *InLayout->GetLayoutAssetPath());

	OnActiveLayoutChanged();

	bClonerMeshesDirty = true;
}

void UCEClonerComponent::OnActiveLayoutChanged()
{
	UCEClonerLayoutBase* Layout = GetActiveLayout();
	if (!Layout)
	{
		return;
	}

	OnSeedChanged();
	OnColorChanged();

	Layout->MarkLayoutDirty();

	TSet<TObjectPtr<UCEClonerExtensionBase>> PrevActiveExtensions(ActiveExtensions);
	ActiveExtensions.Empty();

	for (const TSubclassOf<UCEClonerExtensionBase>& ExtensionClass : Layout->GetSupportedExtensions())
	{
		if (UCEClonerExtensionBase* Extension = FindOrAddExtension(ExtensionClass.Get()))
		{
			if (!PrevActiveExtensions.Contains(Extension))
			{
				Extension->ActivateExtension();
			}

			Extension->MarkExtensionDirty();

			ActiveExtensions.Add(Extension);
			PrevActiveExtensions.Remove(Extension);
		}
	}

	for (const TObjectPtr<UCEClonerExtensionBase>& InactiveExtension : PrevActiveExtensions)
	{
		InactiveExtension->DeactivateExtension();
	}

	ActiveExtensions.StableSort([](const TObjectPtr<UCEClonerExtensionBase>& InExtensionA, const TObjectPtr<UCEClonerExtensionBase>& InExtensionB)
	{
		return InExtensionA->GetExtensionPriority() > InExtensionB->GetExtensionPriority();
	});
}

void UCEClonerComponent::UpdateClonerMeshes()
{
	const AActor* ClonerActor = GetOwner();

	if (!ClonerActor)
	{
		return;
	}

	UNiagaraSystem* ActiveSystem = GetAsset();

	if (!ActiveSystem || !ActiveLayout)
	{
		return;
	}

	if (ActiveLayout->GetSystem() != ActiveSystem)
	{
		UE_LOG(LogCEClonerComponent, Warning, TEXT("%s : Invalid system for cloner layout"), *ClonerActor->GetActorNameOrLabel());
		return;
	}

	UNiagaraMeshRendererProperties* MeshRenderer = ActiveLayout->GetMeshRenderer();

	if (!MeshRenderer)
	{
		UE_LOG(LogCEClonerComponent, Warning, TEXT("%s : Invalid mesh renderer for cloner system"), *ClonerActor->GetActorNameOrLabel());
		return;
	}

	if (bClonerMeshesDirty)
	{
		// Resize mesh array properly
		if (MeshRenderer->Meshes.Num() > ClonerTree.MergedBakedMeshes.Num())
		{
			MeshRenderer->Meshes.SetNum(ClonerTree.MergedBakedMeshes.Num());
		}

		// Set baked meshes in mesh renderer array
		for (int32 Idx = 0; Idx < ClonerTree.MergedBakedMeshes.Num(); Idx++)
		{
			UStaticMesh* StaticMesh = ClonerTree.MergedBakedMeshes[Idx].Get();

			FNiagaraMeshRendererMeshProperties& MeshProperties = !MeshRenderer->Meshes.IsValidIndex(Idx) ? MeshRenderer->Meshes.AddDefaulted_GetRef() : MeshRenderer->Meshes[Idx];
			MeshProperties.Mesh = StaticMesh && StaticMesh->GetNumTriangles(0) > 0 ? StaticMesh : nullptr;

			if (ClonerTree.RootActors.IsValidIndex(Idx))
			{
				if (const FCEClonerAttachmentItem* Item = ClonerTree.ItemAttachmentMap.Find(ClonerTree.RootActors[Idx]))
				{
					MeshProperties.Rotation = Item->ActorTransform.Rotator();
					MeshProperties.Scale = Item->ActorTransform.GetScale3D();
				}
			}
		}

		bClonerMeshesDirty = !ClonerTree.DirtyItemAttachments.IsEmpty();

		UE_LOG(LogCEClonerComponent, Log, TEXT("%s : Cloner mesh updated %i"), *ClonerActor->GetActorNameOrLabel(), ClonerTree.MergedBakedMeshes.Num())
	}

	for (const TObjectPtr<UCEClonerExtensionBase>& ActiveExtension : ActiveExtensions)
	{
		ActiveExtension->OnClonerMeshesUpdated();
	}

	// Set new number of meshes in renderer
	SetIntParameter(TEXT("MeshNum"), MeshRenderer->Meshes.Num());

#if WITH_EDITORONLY_DATA
	MeshRenderer->OnMeshChanged();

	// Used by other data interfaces to update their cached data
	MeshRenderer->OnChanged().Broadcast();
#else
	FNiagaraSystemUpdateContext ReregisterContext(ActiveSystem, /** bReset */ true);
#endif

	OnClonerMeshUpdatedDelegate.Broadcast(this);
}

void UCEClonerComponent::SetClonerActiveLayout(UCEClonerLayoutBase* InLayout)
{
	if (!InLayout)
	{
		return;
	}

	const AActor* ClonerActor = GetOwner();
	if (!ClonerActor)
	{
		return;
	}

	if (!InLayout->IsLayoutLoaded())
	{
		if (!InLayout->OnLayoutLoadedDelegate().IsBoundToObject(this))
		{
			InLayout->OnLayoutLoadedDelegate().AddUObject(this, &UCEClonerComponent::OnActiveLayoutLoaded);
		}

		InLayout->LoadLayout();

		return;
	}

	ActivateLayout(InLayout);
}

#undef LOCTEXT_NAMESPACE
