// Copyright Epic Games, Inc. All Rights Reserved.

#include "MuCO/CustomizableObjectInstanceUsage.h"
#include "MuCO/CustomizableObjectInstanceUsagePrivate.h"

#include "MuCO/CustomizableSkeletalComponent.h"
#include "MuCO/CustomizableObjectSystem.h"
#include "MuCO/CustomizableObjectInstancePrivate.h"
#include "MuCO/UnrealPortabilityHelpers.h"
#include "MuCO/CustomizableSkeletalComponentPrivate.h"
#include "AnimationRuntime.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/SkinnedAssetCommon.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "MuCO/CustomizableObject.h"
#include "MuCO/ICustomizableObjectModule.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "UObject/ObjectSaveContext.h"
#include "Stats/Stats.h"


ETickableTickType UCustomizableObjectInstanceUsagePrivate::GetTickableTickType() const
{ 
	return (HasAnyFlags(RF_ClassDefaultObject))
		? ETickableTickType::Never
		: ETickableTickType::Conditional;
}


void UCustomizableObjectInstanceUsagePrivate::Callbacks() const
{
	for (const UCustomizableObjectExtension* Extension : ICustomizableObjectModule::Get().GetRegisteredExtensions())
	{
		Extension->OnCustomizableObjectInstanceUsageUpdated(GetPublic());
	}
	
	if (GetPublic()->CustomizableSkeletalComponent)
	{
		GetPublic()->CustomizableSkeletalComponent->UpdatedDelegate.ExecuteIfBound();

		if (GetPublic()->UpdatedDelegate.IsBound() && GetPublic()->CustomizableSkeletalComponent->UpdatedDelegate.IsBound())
		{
			UE_LOG(LogMutable, Error, TEXT("The UpdatedDelegate is bound both in the UCustomizableObjectInstanceUsage and in its parent CustomizableSkeletalComponent. Only one should be bound."));
			ensure(false);
		}
	}
	
	GetPublic()->UpdatedDelegate.ExecuteIfBound();
}


UCustomizableObjectInstanceUsage::UCustomizableObjectInstanceUsage()
{
	Private = CreateDefaultSubobject<UCustomizableObjectInstanceUsagePrivate>(FName("Private"));
}


void UCustomizableObjectInstanceUsage::SetCustomizableObjectInstance(UCustomizableObjectInstance* CustomizableObjectInstance)
{
	if (CustomizableSkeletalComponent)
	{
		CustomizableSkeletalComponent->CustomizableObjectInstance = CustomizableObjectInstance;
	}
	else
	{
		UsedCustomizableObjectInstance = CustomizableObjectInstance;
	}
}


UCustomizableObjectInstance* UCustomizableObjectInstanceUsage::GetCustomizableObjectInstance() const
{
	if (CustomizableSkeletalComponent)
	{
		return CustomizableSkeletalComponent->CustomizableObjectInstance;
	}
	else
	{
		return UsedCustomizableObjectInstance;
	}
}


void UCustomizableObjectInstanceUsage::SetComponentIndex(int32 ComponentIndex)
{
	if (CustomizableSkeletalComponent)
	{
		CustomizableSkeletalComponent->ComponentIndex = ComponentIndex;
	}
	else
	{
		UsedComponentIndex = ComponentIndex;
	}
}


int32 UCustomizableObjectInstanceUsage::GetComponentIndex() const
{
	if (CustomizableSkeletalComponent)
	{
		return CustomizableSkeletalComponent->ComponentIndex;
	}
	else
	{
		return UsedComponentIndex;
	}
}


void UCustomizableObjectInstanceUsage::SetComponentName(const FName& Name)
{
	if (CustomizableSkeletalComponent)
	{
		CustomizableSkeletalComponent->SetComponentName(Name);
	}
	else
	{
		UsedComponentIndex = INDEX_NONE;
		UsedComponentName = Name;
	}
}


FName UCustomizableObjectInstanceUsage::GetComponentName() const
{
	if (CustomizableSkeletalComponent)
	{
		return CustomizableSkeletalComponent->GetComponentName();
	}
	else
	{
		if (UsedComponentIndex == INDEX_NONE)
		{
			return UsedComponentName;	
		}
		else
		{
			return FName(FString::FromInt(UsedComponentIndex));
		}
	}
}


void UCustomizableObjectInstanceUsagePrivate::SetPendingSetSkeletalMesh(bool bIsActive)
{
	if (GetPublic()->CustomizableSkeletalComponent)
	{
		GetPublic()->CustomizableSkeletalComponent->GetPrivate()->PendingSetSkeletalMesh() = bIsActive;
	}
	else
	{
		GetPublic()->bUsedPendingSetSkeletalMesh = bIsActive;
	}
}


bool UCustomizableObjectInstanceUsagePrivate::GetPendingSetSkeletalMesh() const
{
	if (GetPublic()->CustomizableSkeletalComponent)
	{
		return GetPublic()->CustomizableSkeletalComponent->GetPrivate()->PendingSetSkeletalMesh();
	}
	else
	{
		return GetPublic()->bUsedPendingSetSkeletalMesh;
	}
}


UCustomizableSkeletalComponent* UCustomizableObjectInstanceUsagePrivate::GetCustomizableSkeletalComponent()
{
	return GetPublic()->CustomizableSkeletalComponent;
}


void UCustomizableObjectInstanceUsagePrivate::SetCustomizableSkeletalComponent(UCustomizableSkeletalComponent* Component)
{
	GetPublic()->CustomizableSkeletalComponent = Component;
}


UCustomizableObjectInstanceUsage* UCustomizableObjectInstanceUsagePrivate::GetPublic()
{
	UCustomizableObjectInstanceUsage* Public = StaticCast<UCustomizableObjectInstanceUsage*>(GetOuter());
	check(Public);

	return Public;
}


const UCustomizableObjectInstanceUsage* UCustomizableObjectInstanceUsagePrivate::GetPublic() const
{
	UCustomizableObjectInstanceUsage* Public = StaticCast<UCustomizableObjectInstanceUsage*>(GetOuter());
	check(Public);

	return Public;
}


void UCustomizableObjectInstanceUsage::SetSkipSetReferenceSkeletalMesh(bool bSkip)
{
	if (CustomizableSkeletalComponent)
	{
		CustomizableSkeletalComponent->SetSkipSetReferenceSkeletalMesh(bSkip);
	}
	else
	{
		bUsedSkipSetReferenceSkeletalMesh = bSkip;
	}
}


bool UCustomizableObjectInstanceUsage::GetSkipSetReferenceSkeletalMesh() const
{
	if (CustomizableSkeletalComponent)
	{
		return CustomizableSkeletalComponent->GetSkipSetReferenceSkeletalMesh();
	}
	else
	{
		return bUsedSkipSetReferenceSkeletalMesh;
	}
}


void UCustomizableObjectInstanceUsage::SetSkipSetSkeletalMeshOnAttach(bool bSkip)
{
	if (CustomizableSkeletalComponent)
	{
		CustomizableSkeletalComponent->SetSkipSetSkeletalMeshOnAttach(bSkip);
	}
	else
	{
		bUsedSkipSetSkeletalMeshOnAttach = bSkip;
	}
}


bool UCustomizableObjectInstanceUsage::GetSkipSetSkeletalMeshOnAttach() const
{
	if (CustomizableSkeletalComponent)
	{
		return CustomizableSkeletalComponent->GetSkipSetReferenceSkeletalMesh();
	}
	else
	{
		return bUsedSkipSetSkeletalMeshOnAttach;
	}
}


void UCustomizableObjectInstanceUsage::AttachTo(USkeletalMeshComponent* SkeletalMeshComponent)
{
	if (CustomizableSkeletalComponent)
	{
		UE_LOG(LogMutable, Error, TEXT("Cannot change the attachment of a UCustomizableObjectInstanceUsage that has been automatically created by a CustomizableSkeletalComponent. Reattach the CustomizableSkeletalComponent instead."));
		ensure(false);
	}
	else
	{
		if (IsValid(SkeletalMeshComponent))
		{
			UsedSkeletalMeshComponent = SkeletalMeshComponent;
		}
		else
		{
			UsedSkeletalMeshComponent = nullptr;
		}

		// To mimic the behavior of UCustomizableSkeletalComponent::OnAttachmentChanged()
		GetPrivate()->SetPendingSetSkeletalMesh(true);
	}
}


USkeletalMeshComponent* UCustomizableObjectInstanceUsage::GetAttachParent() const
{
	if (CustomizableSkeletalComponent)
	{
		return Cast<USkeletalMeshComponent>(CustomizableSkeletalComponent->GetAttachParent());
	}
	else if(UsedSkeletalMeshComponent.IsValid())
	{
		return UsedSkeletalMeshComponent.Get();
	}

	return nullptr;
}


USkeletalMesh* UCustomizableObjectInstanceUsagePrivate::GetSkeletalMesh() const
{
	UCustomizableObjectInstance* CustomizableObjectInstance = GetPublic()->GetCustomizableObjectInstance();

	return CustomizableObjectInstance ? CustomizableObjectInstance->GetComponentMeshSkeletalMesh(GetPublic()->GetComponentName()) : nullptr;
}


bool RequiresReinitPose(USkeletalMesh* CurrentSkeletalMesh, USkeletalMesh* SkeletalMesh)
{
	if (CurrentSkeletalMesh == SkeletalMesh)
	{
		return false;
	}

	if (!CurrentSkeletalMesh || !SkeletalMesh)
	{
		return SkeletalMesh != nullptr;
	}

	if (CurrentSkeletalMesh->GetLODNum() != SkeletalMesh->GetLODNum())
	{
		return true;
	}

	const FSkeletalMeshRenderData* CurrentRenderData = CurrentSkeletalMesh->GetResourceForRendering();
	const FSkeletalMeshRenderData* NewRenderData = SkeletalMesh->GetResourceForRendering();
	if (!CurrentRenderData || !NewRenderData)
	{
		return false;
	}

	const int32 NumLODs = SkeletalMesh->GetLODNum();
	for (int32 LODIndex = 0; LODIndex < NumLODs; ++LODIndex)
	{
		if (CurrentRenderData->LODRenderData[LODIndex].RequiredBones != NewRenderData->LODRenderData[LODIndex].RequiredBones)
		{
			return true;
		}
	}

	return false;
}


void UCustomizableObjectInstanceUsagePrivate::SetSkeletalMeshAndOverrideMaterials(USkeletalMeshComponent& Parent, USkeletalMesh* SkeletalMesh, 
	const UCustomizableObjectInstance& CustomizableObjectInstance, bool* bOutSkeletalMeshUpdated, bool* bOutMaterialsUpdated)
{
	if (SkeletalMesh != Parent.GetSkeletalMeshAsset())
	{
		Parent.SetSkeletalMesh(SkeletalMesh, RequiresReinitPose(Parent.GetSkeletalMeshAsset(), SkeletalMesh));

		if (bOutSkeletalMeshUpdated)
		{
			*bOutSkeletalMeshUpdated = true;
		}
	}

	SetPendingSetSkeletalMesh(false);

	TArray<TObjectPtr<UMaterialInterface>> OldOverridenMaterials = Parent.OverrideMaterials;

	if (Parent.HasOverrideMaterials())
	{
#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
		if (Parent.GetClass()->GetFName() != FName(TEXT("SkeletalMeshComponentBudgeted"))) // Reduce unnecessary logging
		{
			UE_LOG(LogMutable, Warning, TEXT("Attaching Customizable Skeletal Component to Skeletal Mesh Component with overriden materials! Deleting overrides."));
		}
#endif

		// For some reason the reference skeletal mesh materials are added as override materials, clear them if necessary
		Parent.EmptyOverrideMaterials();
	}

	const UCustomizableObject* CustomizableObject = CustomizableObjectInstance.GetCustomizableObject();
	if (!CustomizableObject)
	{
		return;
	}
	
	const bool bIsTransientMesh = SkeletalMesh ? SkeletalMesh->HasAllFlags(EObjectFlags::RF_Transient) : false;
	const bool bUseOverrideMaterials = !bIsTransientMesh || (CustomizableObject->bEnableMeshCache && UCustomizableObjectSystem::IsMeshCacheEnabled());

	if (bUseOverrideMaterials)
	{
		if (FCustomizableInstanceComponentData* ComponentData = CustomizableObjectInstance.GetPrivate()->GetComponentData(GetPublic()->GetComponentName()))
		{
			for (int32 Index = 0; Index < ComponentData->OverrideMaterials.Num(); ++Index)
			{
				Parent.SetMaterial(Index, ComponentData->OverrideMaterials[Index]);
			}
		}
	}

	if (bOutMaterialsUpdated)
	{
		*bOutMaterialsUpdated = OldOverridenMaterials != Parent.OverrideMaterials;
	}
}


void UCustomizableObjectInstanceUsagePrivate::SetSkeletalMesh(USkeletalMesh* SkeletalMesh, bool* bOutSkeletalMeshUpdated, bool* bOutMaterialsUpdated)
{
	USkeletalMeshComponent* Parent = Cast<USkeletalMeshComponent>(GetPublic()->GetAttachParent());
	const UCustomizableObjectInstance* CustomizableObjectInstance = GetPublic()->GetCustomizableObjectInstance();

	if (Parent && CustomizableObjectInstance)
	{
		SetSkeletalMeshAndOverrideMaterials(*Parent, SkeletalMesh, *CustomizableObjectInstance, bOutSkeletalMeshUpdated, bOutMaterialsUpdated);
	}
}

void UCustomizableObjectInstanceUsagePrivate::SetPhysicsAsset(UPhysicsAsset* PhysicsAsset, bool* bOutPhysicsAssetUpdated)
{
	USkeletalMeshComponent* Parent = Cast<USkeletalMeshComponent>(GetPublic()->GetAttachParent());

	if (Parent && Parent->GetWorld() &&
		PhysicsAsset != Parent->GetPhysicsAsset())
	{
		Parent->SetPhysicsAsset(PhysicsAsset, true);
		
		if (bOutPhysicsAssetUpdated)
		{
			*bOutPhysicsAssetUpdated = true;
		}
	}
}


USkeletalMesh* UCustomizableObjectInstanceUsagePrivate::GetAttachedSkeletalMesh() const
{
	USkeletalMeshComponent* Parent = Cast<USkeletalMeshComponent>(GetPublic()->GetAttachParent());

	if (Parent)
	{
		return UE_MUTABLE_GETSKELETALMESHASSET(Parent);
	}

	return nullptr;
}


void UCustomizableObjectInstanceUsage::UpdateSkeletalMeshAsync(bool bNeverSkipUpdate)
{
	UCustomizableObjectInstance* CustomizableObjectInstance = GetCustomizableObjectInstance();

	if (CustomizableObjectInstance)
	{
		CustomizableObjectInstance->UpdateSkeletalMeshAsync(false, false);
	}
}


void UCustomizableObjectInstanceUsage::UpdateSkeletalMeshAsyncResult(FInstanceUpdateDelegate Callback, bool bIgnoreCloseDist, bool bForceHighPriority)
{
	UCustomizableObjectInstance* CustomizableObjectInstance = GetCustomizableObjectInstance();

	if (CustomizableObjectInstance)
	{
		CustomizableObjectInstance->UpdateSkeletalMeshAsyncResult(Callback, false, false);
	}
}


#if WITH_EDITOR
void UCustomizableObjectInstanceUsagePrivate::UpdateDistFromComponentToLevelEditorCamera(const FVector& CameraPosition)
{
	// We want instances in the editor to be generated
	if (!GetWorld() || GetWorld()->WorldType != EWorldType::Editor)
	{
		return;
	}

	UCustomizableObjectInstance* CustomizableObjectInstance = GetPublic()->GetCustomizableObjectInstance();

	if (CustomizableObjectInstance)
	{
		USkeletalMeshComponent* SkeletalMeshComponent = GetPublic()->GetAttachParent();
		AActor* ParentActor = SkeletalMeshComponent ? SkeletalMeshComponent->GetAttachmentRootActor() : nullptr;
		if (ParentActor && ParentActor->IsValidLowLevel())
		{
			// update distance to camera and set the instance as being used by a component
			CustomizableObjectInstance->GetPrivate()->SetCOInstanceFlags(UsedByComponent);

			float SquareDist = FVector::DistSquared(CameraPosition, ParentActor->GetActorLocation());
			CustomizableObjectInstance->GetPrivate()->MinSquareDistFromComponentToPlayer = 
				FMath::Min(SquareDist, CustomizableObjectInstance->GetPrivate()->MinSquareDistFromComponentToPlayer);
		}

		USkeletalMesh* AttachedSkeletalMesh = GetAttachedSkeletalMesh();
		const FName& ComponentName = GetPublic()->GetComponentName();

		const bool bInstanceGenerated = CustomizableObjectInstance->GetPrivate()->SkeletalMeshStatus != ESkeletalMeshStatus::NotGenerated;
		USkeletalMesh* GeneratedSkeletalMesh = bInstanceGenerated ? CustomizableObjectInstance->GetComponentMeshSkeletalMesh(ComponentName) :
			CustomizableObjectInstance->GetCustomizableObject()->GetComponentMeshReferenceSkeletalMesh(ComponentName);

		if (AttachedSkeletalMesh != GeneratedSkeletalMesh)
		{
			SetSkeletalMesh(GeneratedSkeletalMesh);
		}
	}
}


void UCustomizableObjectInstanceUsagePrivate::EditorUpdateComponent()
{
	UCustomizableObjectInstance* CustomizableObjectInstance = GetPublic()->GetCustomizableObjectInstance();

	if (CustomizableObjectInstance)
	{
		CustomizableObjectInstance->GetPrivate()->SetCOInstanceFlags(UsedByComponent);

		USkeletalMeshComponent* SkeletalMeshComponent = GetPublic()->GetAttachParent();
		AActor* ParentActor = SkeletalMeshComponent ? SkeletalMeshComponent->GetAttachmentRootActor() : nullptr;
		
		if (ParentActor)
		{
			USkeletalMesh* AttachedSkeletalMesh = GetAttachedSkeletalMesh();
			const FName& ComponentName = GetPublic()->GetComponentName();

			const bool bInstanceGenerated = CustomizableObjectInstance->GetPrivate()->SkeletalMeshStatus != ESkeletalMeshStatus::NotGenerated;
			USkeletalMesh* GeneratedSkeletalMesh = bInstanceGenerated ?
				CustomizableObjectInstance->GetComponentMeshSkeletalMesh(ComponentName) :
				GetPublic()->GetSkipSetReferenceSkeletalMesh() ?
					nullptr :
					CustomizableObjectInstance->GetCustomizableObject()->GetComponentMeshReferenceSkeletalMesh(ComponentName);

			if (AttachedSkeletalMesh != GeneratedSkeletalMesh)
			{
				SetSkeletalMesh(GeneratedSkeletalMesh);
			}
		}
	}
}
#endif


UCustomizableObjectInstanceUsagePrivate* UCustomizableObjectInstanceUsage::GetPrivate()
{
	return Private;
}


const UCustomizableObjectInstanceUsagePrivate* UCustomizableObjectInstanceUsage::GetPrivate() const
{
	return Private;
}


void UCustomizableObjectInstanceUsagePrivate::UpdateDistFromComponentToPlayer(const AActor* ViewCenter, const bool bForceEvenIfNotBegunPlay)
{
	UCustomizableObjectInstance* CustomizableObjectInstance = GetPublic()->GetCustomizableObjectInstance();

	if (CustomizableObjectInstance)
	{
		USkeletalMeshComponent* SkeletalMeshComponent = GetPublic()->GetAttachParent();
		AActor* ParentActor = SkeletalMeshComponent ? SkeletalMeshComponent->GetAttachmentRootActor() : nullptr;
		
		CustomizableObjectInstance->SetIsPlayerOrNearIt(false);

		if (ParentActor && ParentActor->IsValidLowLevel())
		{
			if (ParentActor->HasActorBegunPlay() || bForceEvenIfNotBegunPlay)
			{
				float SquareDist = FLT_MAX;

				if (ViewCenter && ViewCenter->IsValidLowLevel())
				{
					APawn* Pawn = Cast<APawn>(ParentActor);
					bool bIsPlayer = Pawn ? Pawn->IsPlayerControlled() : false;
					CustomizableObjectInstance->SetIsPlayerOrNearIt(bIsPlayer);

					if (bIsPlayer)
					{
						SquareDist = -0.01f; // Negative value to give the player character more priority than any other character
					}
					else
					{
						SquareDist = FVector::DistSquared(ViewCenter->GetActorLocation(), ParentActor->GetActorLocation());
					}
				}
				else if (bForceEvenIfNotBegunPlay)
				{
					SquareDist = -0.01f; // This is a manual update before begin play and the creation of the pawn, so it should probably be high priority
					CustomizableObjectInstance->GetPrivate()->LastMinSquareDistFromComponentToPlayer = FMath::Min(SquareDist, CustomizableObjectInstance->GetPrivate()->MinSquareDistFromComponentToPlayer);
				}
				else
				{
					SquareDist = 0.f; // This a mutable tick before begin play and the creation of the pawn, so it should have a definite and high priority but less than a manual update
					CustomizableObjectInstance->GetPrivate()->LastMinSquareDistFromComponentToPlayer = FMath::Min(SquareDist, CustomizableObjectInstance->GetPrivate()->MinSquareDistFromComponentToPlayer);
				}

				CustomizableObjectInstance->GetPrivate()->MinSquareDistFromComponentToPlayer = FMath::Min(SquareDist, CustomizableObjectInstance->GetPrivate()->MinSquareDistFromComponentToPlayer);
				CustomizableObjectInstance->SetIsBeingUsedByComponentInPlay(true);

				if (CustomizableObjectInstance->GetPrivate()->MinSquareDistFromComponentToPlayer == SquareDist)
				{
					CustomizableObjectInstance->GetPrivate()->NearestToActor = GetPublic();
					CustomizableObjectInstance->GetPrivate()->NearestToViewCenter = ViewCenter;
				}
			}
		}

		const FName& ComponentName = GetPublic()->GetComponentName();

		if (ParentActor && GetAttachedSkeletalMesh() == nullptr && CustomizableObjectInstance->GetComponentMeshSkeletalMesh(ComponentName))
		{
			SetSkeletalMesh(CustomizableObjectInstance->GetComponentMeshSkeletalMesh(ComponentName));
		}
	}
}


void UCustomizableObjectInstanceUsagePrivate::Tick(float DeltaTime)
{
	if (!IsValid(this))
	{
		return;
	}

	UCustomizableObjectInstance* CustomizableObjectInstance = GetPublic()->GetCustomizableObjectInstance();

	if (!GetPendingSetSkeletalMesh() || GetPublic()->GetSkipSetSkeletalMeshOnAttach() || !CustomizableObjectInstance || !IsValid(CustomizableObjectInstance))
	{
		return;
	}
	
	UCustomizableObject* CustomizableObject = CustomizableObjectInstance->GetCustomizableObject();
	if (!CustomizableObject || !IsValid(CustomizableObject))
	{
		return;
	}	

	if (USkeletalMeshComponent* Parent = Cast<USkeletalMeshComponent>(GetPublic()->GetAttachParent()))
	{
		const FName& ComponentName = GetPublic()->GetComponentName();

		USkeletalMesh* SkeletalMesh = nullptr;

		const bool bInstanceGenerated = CustomizableObjectInstance->GetPrivate()->SkeletalMeshStatus == ESkeletalMeshStatus::Success;
		if (bInstanceGenerated)
		{
			// Generated SkeletalMesh to set, can be null if the component is empty
			SkeletalMesh = CustomizableObjectInstance->GetComponentMeshSkeletalMesh(ComponentName);
		}
		else
		{
			// If not generated yet, conditionally set the SkeletalMesh of reference
			if (!bInstanceGenerated && !GetPublic()->GetSkipSetReferenceSkeletalMesh() && 
				CustomizableObject->bEnableUseRefSkeletalMeshAsPlaceholder)
			{
				// Can be nullptr
				SkeletalMesh = CustomizableObject->GetComponentMeshReferenceSkeletalMesh(ComponentName);
			}
		}

		// Set SkeletalMesh
		if (bInstanceGenerated || SkeletalMesh)
		{
			SetSkeletalMeshAndOverrideMaterials(*Parent, SkeletalMesh, *CustomizableObjectInstance, nullptr, nullptr);
		}
	}
}


TStatId UCustomizableObjectInstanceUsagePrivate::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UCustomizableObjectInstanceUsage, STATGROUP_Tickables);
}


bool UCustomizableObjectInstanceUsagePrivate::IsTickableWhenPaused() const
{
	return true;
}


bool UCustomizableObjectInstanceUsagePrivate::IsTickableInEditor() const
{
	return true;
}


bool UCustomizableObjectInstanceUsagePrivate::IsTickable() const
{
	return !HasAnyFlags(RF_BeginDestroyed);
}


bool UCustomizableObjectInstanceUsagePrivate::IsNetMode(ENetMode InNetMode) const
{
	if (GetPublic()->CustomizableSkeletalComponent)
	{
		return GetPublic()->CustomizableSkeletalComponent->IsNetMode(InNetMode);
	}
	else if (GetPublic()->UsedSkeletalMeshComponent.IsValid())
	{
		return GetPublic()->UsedSkeletalMeshComponent->IsNetMode(InNetMode);
	}

	return false;
}
