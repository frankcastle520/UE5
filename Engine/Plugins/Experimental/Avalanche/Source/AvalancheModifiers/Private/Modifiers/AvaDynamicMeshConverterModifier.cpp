// Copyright Epic Games, Inc. All Rights Reserved.

#include "Modifiers/AvaDynamicMeshConverterModifier.h"

#include "Async/Async.h"
#include "Components/BrushComponent.h"
#include "Components/DynamicMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "Engine/StaticMesh.h"
#include "Extensions/AvaSceneTreeUpdateModifierExtension.h"
#include "GeometryScript/MeshAssetFunctions.h"
#include "GeometryScript/MeshBasicEditFunctions.h"
#include "ProceduralMeshComponent.h"

#if WITH_EDITOR
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "Dialogs/DlgPickAssetPath.h"
#endif

#define LOCTEXT_NAMESPACE "AvaDynamicMeshConverterModifier"

FAvaDynamicMeshConverterModifierComponentState::FAvaDynamicMeshConverterModifierComponentState(UPrimitiveComponent* InPrimitiveComponent)
	: Component(InPrimitiveComponent)
{
	if (Component.IsValid())
	{
		if (const AActor* ComponentOwner = InPrimitiveComponent->GetOwner())
		{
			bActorHiddenInGame = ComponentOwner->IsHidden();
#if WITH_EDITOR
			bActorHiddenInEditor = ComponentOwner->IsTemporarilyHiddenInEditor();
#endif
			if (const USceneComponent* RootComponent = ComponentOwner->GetRootComponent())
			{
				bComponentVisible = RootComponent->IsVisible();
				bComponentHiddenInGame = RootComponent->bHiddenInGame;
			}

			if (const AActor* ParentActor = ComponentOwner->GetAttachParentActor())
			{
				ActorRelativeTransform = ComponentOwner->GetActorTransform().GetRelativeTransform(ParentActor->GetActorTransform());
			}
		}
	}
}

void UAvaDynamicMeshConverterModifier::OnModifierCDOSetup(FActorModifierCoreMetadata& InMetadata)
{
	Super::OnModifierCDOSetup(InMetadata);

	InMetadata.SetName(TEXT("DynamicMeshConverter"));
	InMetadata.SetCategory(TEXT("Conversion"));
	InMetadata.AllowTick(true);
#if WITH_EDITOR
	InMetadata.SetDescription(LOCTEXT("ModifierDescription", "Converts various actor mesh types into a single dynamic mesh, this is an heavy operation"));
#endif
	InMetadata.SetCompatibilityRule([](const AActor* InActor)->bool
	{
		return InActor && !InActor->FindComponentByClass<UDynamicMeshComponent>();
	});
}

void UAvaDynamicMeshConverterModifier::OnModifierAdded(EActorModifierCoreEnableReason InReason)
{
	Super::OnModifierAdded(InReason);

	AddDynamicMeshComponent();

	AddExtension<FAvaRenderStateUpdateModifierExtension>(this);

	if (FAvaSceneTreeUpdateModifierExtension* SceneExtension = AddExtension<FAvaSceneTreeUpdateModifierExtension>(this))
	{
		TrackedActor.ReferenceContainer = EAvaReferenceContainer::Other;
		TrackedActor.ReferenceActorWeak = SourceActorWeak.Get();
		TrackedActor.bSkipHiddenActors = false;
		SceneExtension->TrackSceneTree(0, &TrackedActor);
	}
}

void UAvaDynamicMeshConverterModifier::OnModifierEnabled(EActorModifierCoreEnableReason InReason)
{
	Super::OnModifierEnabled(InReason);
}

void UAvaDynamicMeshConverterModifier::RestorePreState()
{
	UAvaGeometryBaseModifier::RestorePreState();

	for (FAvaDynamicMeshConverterModifierComponentState& ConvertedComponent : ConvertedComponents)
	{
		if (ConvertedComponent.Component.IsValid())
		{
			AActor* ComponentActor = ConvertedComponent.Component->GetOwner();

			if (ComponentActor != GetModifiedActor())
			{
				// Hide actor but do not hide ourselves
				ComponentActor->SetHidden(ConvertedComponent.bActorHiddenInGame);
#if WITH_EDITOR
				ComponentActor->SetIsTemporarilyHiddenInEditor(ConvertedComponent.bActorHiddenInEditor);
#endif
			}
			else if (USceneComponent* RootComponent = ComponentActor->GetRootComponent())
			{
				// In the meantime hide root component but later hide component itself
				RootComponent->SetHiddenInGame(ConvertedComponent.bComponentHiddenInGame);
				RootComponent->SetVisibility(ConvertedComponent.bComponentVisible);
			}
		}
	}
}

void UAvaDynamicMeshConverterModifier::OnModifierRemoved(EActorModifierCoreDisableReason InReason)
{
	Super::OnModifierRemoved(InReason);

	if (InReason != EActorModifierCoreDisableReason::Destroyed)
	{
		RemoveDynamicMeshComponent();
	}
}

bool UAvaDynamicMeshConverterModifier::IsModifierDirtyable() const
{
	const double CurrentTime = FPlatformTime::Seconds();

	if (TransformUpdateInterval > 0
		&& CurrentTime - LastTransformUpdateTime > TransformUpdateInterval)
	{
		const_cast<UAvaDynamicMeshConverterModifier*>(this)->LastTransformUpdateTime = CurrentTime;

		for (const FAvaDynamicMeshConverterModifierComponentState& ConvertedComponent : ConvertedComponents)
		{
			const UPrimitiveComponent* PrimitiveComponent = ConvertedComponent.Component.Get();
			if (!PrimitiveComponent)
			{
				continue;
			}

			const AActor* ChildActor = PrimitiveComponent->GetOwner();
			if (!ChildActor)
			{
				continue;
			}

			const AActor* ParentActor = ChildActor->GetAttachParentActor();
			FTransform ExpectedTransform = FTransform::Identity;

			if (ParentActor)
			{
				ExpectedTransform = ChildActor->GetActorTransform().GetRelativeTransform(ParentActor->GetActorTransform());
			}

			if (!ConvertedComponent.ActorRelativeTransform.Equals(ExpectedTransform, 0.01))
			{
				return true;
			}
		}
	}

	return Super::IsModifierDirtyable();
}

void UAvaDynamicMeshConverterModifier::OnSceneTreeTrackedActorChildrenChanged(int32 InIdx, const TSet<TWeakObjectPtr<AActor>>& InPreviousChildrenActors, const TSet<TWeakObjectPtr<AActor>>& InNewChildrenActors)
{
	if (bIncludeAttachedActors)
	{
		MarkModifierDirty();
	}
}

void UAvaDynamicMeshConverterModifier::Apply()
{
	const AActor* ActorModified = GetModifiedActor();

	if (!IsMeshValid())
	{
		Fail(LOCTEXT("InvalidDynamicMeshComponent", "Invalid dynamic mesh component on modified actor"));
		return;
	}

	UDynamicMeshComponent* DynMeshComponent = GetMeshComponent();

	if (!IsValid(DynMeshComponent))
	{
		Fail(LOCTEXT("InvalidDynamicMeshComponent", "Invalid dynamic mesh component on modified actor"));
		return;
	}

	TArray<TWeakObjectPtr<UMaterialInterface>> MaterialsWeak;
	if (!ConvertComponents(MaterialsWeak))
	{
		Fail(LOCTEXT("ConversionFailed", "Conversion to dynamic mesh failed"));
		return;
	}

	for (int32 MatIndex = 0; MatIndex < MaterialsWeak.Num(); MatIndex++)
	{
		UMaterialInterface* Material = MaterialsWeak[MatIndex].Get();
		DynMeshComponent->SetMaterial(MatIndex, Material);
	}

	for (const FAvaDynamicMeshConverterModifierComponentState& OutConvert : ConvertedComponents)
	{
		if (OutConvert.Component.IsValid())
		{
			AActor* ComponentActor = OutConvert.Component->GetOwner();

			// Hide converted component
			if (bHideConvertedMesh)
			{
				if (ComponentActor != ActorModified)
				{
					ComponentActor->SetHidden(true);
#if WITH_EDITOR
					ComponentActor->SetIsTemporarilyHiddenInEditor(true);
#endif
				}
				else if (USceneComponent* RootComponent = ComponentActor->GetRootComponent())
				{
					RootComponent->SetHiddenInGame(true);
					RootComponent->SetVisibility(false);
				}
			}
		}
	}

	Next();
}

#if WITH_EDITOR
void UAvaDynamicMeshConverterModifier::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	const FName MemberName = PropertyChangedEvent.GetMemberPropertyName();

	static const FName SourceActorName = GET_MEMBER_NAME_CHECKED(UAvaDynamicMeshConverterModifier, SourceActorWeak);

	if (MemberName == SourceActorName)
	{
		OnSourceActorChanged();
	}
}

void UAvaDynamicMeshConverterModifier::ConvertToStaticMeshAsset()
{
	using namespace UE::Geometry;

	UDynamicMeshComponent* DynMeshComponent = GetMeshComponent();
	const AActor* OwningActor = GetModifiedActor();

	if (!OwningActor || !DynMeshComponent)
	{
		return;
	}

	// generate name for asset
	const FString NewNameSuggestion = TEXT("SM_MotionDesign_") + OwningActor->GetActorNameOrLabel();
	FString PackageName = FString(TEXT("/Game/Meshes/")) + NewNameSuggestion;
	FString AssetName;

	const FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools");
	AssetToolsModule.Get().CreateUniqueAssetName(PackageName, TEXT(""), PackageName, AssetName);

	const TSharedPtr<SDlgPickAssetPath> PickAssetPathWidget =
		SNew(SDlgPickAssetPath)
		.Title(LOCTEXT("ConvertToStaticMeshPickName", "Choose New StaticMesh Location"))
		.DefaultAssetPath(FText::FromString(PackageName));

	if (PickAssetPathWidget->ShowModal() != EAppReturnType::Ok)
	{
		return;
	}

	// get input name provided by user
	FString UserPackageName = PickAssetPathWidget->GetFullAssetPath().ToString();
	FName MeshName(*FPackageName::GetLongPackageAssetName(UserPackageName));

	// is input name valid ?
	if (MeshName == NAME_None)
	{
		// Use default if invalid
		UserPackageName = PackageName;
		MeshName = *AssetName;
	}

	const FDynamicMesh3* MeshIn = DynMeshComponent->GetMesh();

	// empty mesh do not export
	if (!MeshIn || MeshIn->TriangleCount() == 0)
	{
		return;
	}

	// find/create package
	UPackage* Package = CreatePackage(*UserPackageName);
	check(Package);

	// Create StaticMesh object
	UStaticMesh* DestinationMesh = NewObject<UStaticMesh>(Package, MeshName, RF_Public | RF_Standalone);
	UDynamicMesh* SourceMesh = DynMeshComponent->GetDynamicMesh();

	// export options
	FGeometryScriptCopyMeshToAssetOptions AssetOptions;
	AssetOptions.bReplaceMaterials = false;
	AssetOptions.bEnableRecomputeNormals = false;
	AssetOptions.bEnableRecomputeTangents = false;
	AssetOptions.bEnableRemoveDegenerates = true;

	// LOD options
	FGeometryScriptMeshWriteLOD TargetLOD;
	TargetLOD.LODIndex = 0;

	EGeometryScriptOutcomePins OutResult;

	UGeometryScriptLibrary_StaticMeshFunctions::CopyMeshToStaticMesh(SourceMesh, DestinationMesh, AssetOptions, TargetLOD, OutResult);
	DestinationMesh->GetBodySetup()->AggGeom = DynMeshComponent->GetBodySetup()->AggGeom;

	if (OutResult == EGeometryScriptOutcomePins::Success)
	{
		// Notify asset registry of new asset
		FAssetRegistryModule::AssetCreated(DestinationMesh);
	}
}
#endif

void UAvaDynamicMeshConverterModifier::SetSourceActorWeak(const TWeakObjectPtr<AActor>& InActor)
{
	if (InActor.Get() == SourceActorWeak.Get())
	{
		return;
	}

	SourceActorWeak = InActor;
	OnSourceActorChanged();
}

void UAvaDynamicMeshConverterModifier::SetComponentTypes(const TSet<EAvaDynamicMeshConverterModifierType>& InTypes)
{
	EAvaDynamicMeshConverterModifierType NewComponentType = EAvaDynamicMeshConverterModifierType::None;

	for (const EAvaDynamicMeshConverterModifierType Type : InTypes)
	{
		EnumAddFlags(NewComponentType, Type);
	}

	SetComponentType(static_cast<int32>(NewComponentType));
}

TSet<EAvaDynamicMeshConverterModifierType> UAvaDynamicMeshConverterModifier::GetComponentTypes() const
{
	TSet<EAvaDynamicMeshConverterModifierType> ComponentTypes
	{
		EAvaDynamicMeshConverterModifierType::StaticMeshComponent,
		EAvaDynamicMeshConverterModifierType::DynamicMeshComponent,
		EAvaDynamicMeshConverterModifierType::SkeletalMeshComponent,
		EAvaDynamicMeshConverterModifierType::BrushComponent,
		EAvaDynamicMeshConverterModifierType::ProceduralMeshComponent
	};

	for (TSet<EAvaDynamicMeshConverterModifierType>::TIterator It(ComponentTypes); It; ++It)
	{
		if (!HasFlag(*It))
		{
			It.RemoveCurrent();
		}
	}

	return ComponentTypes;
}

void UAvaDynamicMeshConverterModifier::SetComponentType(int32 InComponentType)
{
	if (InComponentType == ComponentType)
	{
		return;
	}

	ComponentType = InComponentType;
}

void UAvaDynamicMeshConverterModifier::SetFilterActorMode(EAvaDynamicMeshConverterModifierFilter InFilter)
{
	FilterActorMode = InFilter;
}

void UAvaDynamicMeshConverterModifier::SetFilterActorClasses(const TSet<TSubclassOf<AActor>>& InClasses)
{
	FilterActorClasses = InClasses;
}

void UAvaDynamicMeshConverterModifier::SetIncludeAttachedActors(bool bInInclude)
{
	bIncludeAttachedActors = bInInclude;
}

void UAvaDynamicMeshConverterModifier::SetHideConvertedMesh(bool bInHide)
{
	bHideConvertedMesh = bInHide;
}

void UAvaDynamicMeshConverterModifier::OnRenderStateUpdated(AActor* InActor, UActorComponent* InComponent)
{
	if (!IsValid(InActor) || !IsValid(InComponent))
	{
		return;
	}

	const UPrimitiveComponent* PrimitiveComponent = Cast<UPrimitiveComponent>(InComponent);
	if (!PrimitiveComponent)
	{
		return;
	}

	const UDynamicMeshComponent* DynamicMeshComponent = GetMeshComponent();
	if (PrimitiveComponent == DynamicMeshComponent)
	{
		return;
	}

	const AActor* SourceActor = SourceActorWeak.Get();
	if (!SourceActor)
	{
		return;
	}

	const bool bIsSourceActor = InActor == SourceActor;
	const bool bIsAttachedToSourceActor = bIncludeAttachedActors && InActor->IsAttachedTo(SourceActor);
	if (!bIsSourceActor && !bIsAttachedToSourceActor)
	{
		return;
	}

	MarkModifierDirty();
}

void UAvaDynamicMeshConverterModifier::OnSourceActorChanged()
{
	AActor* SourceActor = SourceActorWeak.Get();
	const AActor* ActorModified = GetModifiedActor();

	if (!SourceActor || !ActorModified)
	{
		return;
	}

	bHideConvertedMesh = SourceActor == ActorModified || SourceActor->IsAttachedTo(ActorModified);

	if (const FAvaSceneTreeUpdateModifierExtension* SceneExtension = GetExtension<FAvaSceneTreeUpdateModifierExtension>())
	{
		TrackedActor.ReferenceActorWeak = SourceActor;
		SceneExtension->CheckTrackedActorUpdate(0);
	}
}

bool UAvaDynamicMeshConverterModifier::ConvertComponents(TArray<TWeakObjectPtr<UMaterialInterface>>& OutMaterialsWeak)
{
	if (!IsMeshValid() || !SourceActorWeak.IsValid())
	{
		return false;
	}

	ConvertedComponents.Empty();
	MeshBuilder.Reset();

	UDynamicMeshComponent* DynamicMeshComponent = GetMeshComponent();
	const FTransform SourceTransform = DynamicMeshComponent->GetComponentTransform();

	// Get relevant actors
	TArray<AActor*> FilteredActors;
	GetFilteredActors(FilteredActors);

	if (HasFlag(EAvaDynamicMeshConverterModifierType::StaticMeshComponent))
	{
		TArray<UStaticMeshComponent*> Components;
		GetStaticMeshComponents(FilteredActors, Components);

		for (UStaticMeshComponent* Component : Components)
		{
			if (MeshBuilder.AppendComponent(Component, SourceTransform))
			{
				FAvaDynamicMeshConverterModifierComponentState State(Component);
				ConvertedComponents.Emplace(State);
			}
		}
	}

	if (HasFlag(EAvaDynamicMeshConverterModifierType::DynamicMeshComponent))
	{
		TArray<UDynamicMeshComponent*> Components;
		GetDynamicMeshComponents(FilteredActors, Components);

		for (UDynamicMeshComponent* Component : Components)
		{
			if (MeshBuilder.AppendComponent(Component, SourceTransform))
			{
				FAvaDynamicMeshConverterModifierComponentState State(Component);
				ConvertedComponents.Emplace(State);
			}
		}
	}

	if (HasFlag(EAvaDynamicMeshConverterModifierType::SkeletalMeshComponent))
	{
		TArray<USkeletalMeshComponent*> Components;
		GetSkeletalMeshComponents(FilteredActors, Components);

		for (USkeletalMeshComponent* Component : Components)
		{
			if (MeshBuilder.AppendComponent(Component, SourceTransform))
			{
				FAvaDynamicMeshConverterModifierComponentState State(Component);
				ConvertedComponents.Emplace(State);
			}
		}
	}

	if (HasFlag(EAvaDynamicMeshConverterModifierType::BrushComponent))
	{
		TArray<UBrushComponent*> Components;
		GetBrushComponents(FilteredActors, Components);

		for (UBrushComponent* Component : Components)
		{
			if (MeshBuilder.AppendComponent(Component, SourceTransform))
			{
				FAvaDynamicMeshConverterModifierComponentState State(Component);
				ConvertedComponents.Emplace(State);
			}
		}
	}

	if (HasFlag(EAvaDynamicMeshConverterModifierType::ProceduralMeshComponent))
	{
		TArray<UProceduralMeshComponent*> Components;
		GetProceduralMeshComponents(FilteredActors, Components);

		for (UProceduralMeshComponent* Component : Components)
		{
			if (MeshBuilder.AppendComponent(Component, SourceTransform))
			{
				FAvaDynamicMeshConverterModifierComponentState State(Component);
				ConvertedComponents.Emplace(State);
			}
		}
	}

	return MeshBuilder.BuildDynamicMesh(DynamicMeshComponent->GetDynamicMesh(), OutMaterialsWeak);
}

bool UAvaDynamicMeshConverterModifier::HasFlag(EAvaDynamicMeshConverterModifierType InFlag) const
{
	return EnumHasAnyFlags(static_cast<EAvaDynamicMeshConverterModifierType>(ComponentType), InFlag);
}

void UAvaDynamicMeshConverterModifier::AddDynamicMeshComponent()
{
	UDynamicMeshComponent* DynMeshComponent = GetMeshComponent();

	if (DynMeshComponent)
	{
		return;
	}

	AActor* ActorModified = GetModifiedActor();

	if (!IsValid(ActorModified))
	{
		return;
	}

#if WITH_EDITOR
	ActorModified->Modify();
	Modify();
#endif

	const UClass* const NewComponentClass = UDynamicMeshComponent::StaticClass();

	// Construct the new component and attach as needed
	DynMeshComponent = NewObject<UDynamicMeshComponent>(ActorModified
		, NewComponentClass
		, MakeUniqueObjectName(ActorModified, NewComponentClass, TEXT("DynamicMeshComponent"))
		, RF_Transactional);

	// Add to SerializedComponents array so it gets saved
	ActorModified->AddInstanceComponent(DynMeshComponent);
	DynMeshComponent->OnComponentCreated();
	DynMeshComponent->RegisterComponent();

	if (USceneComponent* RootComponent = ActorModified->GetRootComponent())
	{
		static const FAttachmentTransformRules AttachRules(EAttachmentRule::SnapToTarget, EAttachmentRule::SnapToTarget, EAttachmentRule::SnapToTarget, false);
		DynMeshComponent->AttachToComponent(RootComponent, AttachRules);
	}
	else
	{
		ActorModified->SetRootComponent(DynMeshComponent);
	}

	DynMeshComponent->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
	DynMeshComponent->SetGenerateOverlapEvents(true);

#if WITH_EDITOR
	// Rerun construction scripts
	ActorModified->RerunConstructionScripts();
#endif

	bComponentCreated = true;
}

void UAvaDynamicMeshConverterModifier::RemoveDynamicMeshComponent()
{
	UDynamicMeshComponent* DynMeshComponent = GetMeshComponent();

	if (!DynMeshComponent)
	{
		return;
	}

	// Did we create the component or was it already there
	if (!bComponentCreated)
	{
		return;
	}

	AActor* ActorModified = GetModifiedActor();

	if (!IsValid(ActorModified))
	{
		return;
	}

#if WITH_EDITOR
	ActorModified->Modify();
	Modify();
#endif

	const FDetachmentTransformRules DetachRules(EDetachmentRule::KeepWorld, false);
	DynMeshComponent->DetachFromComponent(DetachRules);

	ActorModified->RemoveInstanceComponent(DynMeshComponent);
	DynMeshComponent->DestroyComponent(false);

	bComponentCreated = false;
}

void UAvaDynamicMeshConverterModifier::GetFilteredActors(TArray<AActor*>& OutActors) const
{
	if (AActor* OriginActor = SourceActorWeak.Get())
	{
		OutActors.Add(OriginActor);
		if (bIncludeAttachedActors)
		{
			OriginActor->GetAttachedActors(OutActors, false, true);
		}
		// Filter actor class
		if (FilterActorMode != EAvaDynamicMeshConverterModifierFilter::None)
		{
			for (int32 Idx = OutActors.Num() - 1; Idx >= 0; Idx--)
			{
				const AActor* CurrentActor = OutActors[Idx];
				if (!IsValid(CurrentActor))
				{
					continue;
				}
				// Include this actor if it's in the filter class
				if (FilterActorMode == EAvaDynamicMeshConverterModifierFilter::Include)
				{
					if (!FilterActorClasses.Contains(CurrentActor->GetClass()))
					{
						OutActors.RemoveAt(Idx);
					}
				}
				else // Exclude this actor if it's in the filter class
				{
					if (FilterActorClasses.Contains(CurrentActor->GetClass()))
					{
						OutActors.RemoveAt(Idx);
					}
				}
			}
		}
	}
}

void UAvaDynamicMeshConverterModifier::GetStaticMeshComponents(const TArray<AActor*>& InActors, TArray<UStaticMeshComponent*>& OutComponents) const
{
	for (const AActor* Actor : InActors)
	{
		TArray<UStaticMeshComponent*> OutMeshComponents;
		Actor->GetComponents(OutMeshComponents, false);
		OutComponents.Append(OutMeshComponents);
	}
	// remove all invalid components
	OutComponents.RemoveAll([](const UStaticMeshComponent* InComponent)->bool
	{
#if WITH_EDITOR
		return !IsValid(InComponent) || InComponent->IsVisualizationComponent();
#else
		return !IsValid(InComponent);
#endif
	});
}

void UAvaDynamicMeshConverterModifier::GetDynamicMeshComponents(const TArray<AActor*>& InActors, TArray<UDynamicMeshComponent*>& OutComponents) const
{
	for (const AActor* Actor : InActors)
	{
		TArray<UDynamicMeshComponent*> OutMeshComponents;
		Actor->GetComponents(OutMeshComponents, false);
		OutComponents.Append(OutMeshComponents);
	}
	// remove all invalid components & and the modifier created component
	OutComponents.RemoveAll([this](const UDynamicMeshComponent* InComponent)->bool
	{
#if WITH_EDITOR
		return !IsValid(InComponent) || InComponent == GetMeshComponent() || InComponent->IsVisualizationComponent();
#else
		return !IsValid(InComponent) || InComponent == GetMeshComponent();
#endif
	});
}

void UAvaDynamicMeshConverterModifier::GetSkeletalMeshComponents(const TArray<AActor*>& InActors, TArray<USkeletalMeshComponent*>& OutComponents) const
{
	for (const AActor* Actor : InActors)
	{
		TArray<USkeletalMeshComponent*> OutMeshComponents;
		Actor->GetComponents(OutMeshComponents, false);
		OutComponents.Append(OutMeshComponents);
	}
	// remove all invalid components
	OutComponents.RemoveAll([this](const USkeletalMeshComponent* InComponent)->bool
	{
#if WITH_EDITOR
		return !IsValid(InComponent) || InComponent->IsVisualizationComponent();
#else
		return !IsValid(InComponent);
#endif
	});
}

void UAvaDynamicMeshConverterModifier::GetBrushComponents(const TArray<AActor*>& InActors, TArray<UBrushComponent*>& OutComponents) const
{
	for (const AActor* Actor : InActors)
	{
		TArray<UBrushComponent*> OutMeshComponents;
		Actor->GetComponents(OutMeshComponents, false);
		OutComponents.Append(OutMeshComponents);
	}
	// remove all invalid components
	OutComponents.RemoveAll([this](const UBrushComponent* InComponent)->bool
	{
#if WITH_EDITOR
		return !IsValid(InComponent) || InComponent->IsVisualizationComponent();
#else
		return !IsValid(InComponent);
#endif
	});
}

void UAvaDynamicMeshConverterModifier::GetProceduralMeshComponents(const TArray<AActor*>& InActors, TArray<UProceduralMeshComponent*>& OutComponents) const
{
	for (const AActor* Actor : InActors)
	{
		TArray<UProceduralMeshComponent*> OutMeshComponents;
		Actor->GetComponents(OutMeshComponents, false);
		OutComponents.Append(OutMeshComponents);
	}
	// remove all invalid components
	OutComponents.RemoveAll([this](const UProceduralMeshComponent* InComponent)->bool
	{
#if WITH_EDITOR
		return !IsValid(InComponent) || InComponent->IsVisualizationComponent();
#else
		return !IsValid(InComponent);
#endif
	});
}

#undef LOCTEXT_NAMESPACE
