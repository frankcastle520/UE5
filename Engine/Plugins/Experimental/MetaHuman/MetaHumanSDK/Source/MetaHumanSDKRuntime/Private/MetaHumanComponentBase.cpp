// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetaHumanComponentBase.h"
#include "Animation/AnimInstance.h"
#include "Components/SkeletalMeshComponent.h"
#include "ControlRig.h"
#include "Engine/AssetManager.h"
#include "Engine/SkeletalMesh.h"
#include "PhysicsEngine/PhysicsAsset.h"


UMetaHumanComponentBase::UMetaHumanComponentBase()
	: UActorComponent()
{
	Torso.ComponentName = "Torso";
	Legs.ComponentName = "Legs";
	Feet.ComponentName = "Feet";
}

void UMetaHumanComponentBase::OnRegister()
{
	Super::OnRegister();

	UpdateComponentLinks();
}

void UMetaHumanComponentBase::UpdateComponentLinks()
{
	AActor* Owner = GetOwner();
	const TInlineComponentArray<USkeletalMeshComponent*, 5> SkelMeshComponents(Owner);
	const TMap<FName, int32, TInlineSetAllocator<16>> ComponentNameToIndexMap = CreateComponentNameToIndexMap(SkelMeshComponents);

	AssignSkelMeshComponentByName(SkelMeshComponents, ComponentNameToIndexMap, Face, *FaceComponentName);
	AssignSkelMeshComponentByName(SkelMeshComponents, ComponentNameToIndexMap, Torso.SkeletalMeshComponent, *Torso.ComponentName);
	AssignSkelMeshComponentByName(SkelMeshComponents, ComponentNameToIndexMap, Legs.SkeletalMeshComponent, *Legs.ComponentName);
	AssignSkelMeshComponentByName(SkelMeshComponents, ComponentNameToIndexMap, Feet.SkeletalMeshComponent, *Feet.ComponentName);
	AssignBodySkelMeshComponentByName(SkelMeshComponents, ComponentNameToIndexMap);
}

TMap<FName, int32, TInlineSetAllocator<16>> UMetaHumanComponentBase::CreateComponentNameToIndexMap(const TInlineComponentArray<USkeletalMeshComponent*, 5>& SkelMeshComponents) const
{
	TMap<FName, int32, TInlineSetAllocator<16>> Result;

	const int32 NumSkelMeshComponents = SkelMeshComponents.Num();
	for (int32 i = 0; i < NumSkelMeshComponents; ++i)
	{
		FName ComponentName = SkelMeshComponents[i]->GetFName();
		Result.Add(ComponentName, i);
	}

	return Result;
}

void UMetaHumanComponentBase::AssignSkelMeshComponentByName(const TInlineComponentArray<USkeletalMeshComponent*, 5>& SkeletalMeshComponents,
	const TMap<FName, int32, TInlineSetAllocator<16>>& ComponentNameToIndexMap,
	TObjectPtr<USkeletalMeshComponent>& OutComponent,
	const TCHAR* ComponentName) const
{
	if (const int32* ComponentIndex = ComponentNameToIndexMap.Find(FName(ComponentName)))
	{
		USkeletalMeshComponent* PrimComponent = SkeletalMeshComponents[*ComponentIndex];
		if (PrimComponent)
		{
			OutComponent = PrimComponent;
		}
	}
};

void UMetaHumanComponentBase::AssignBodySkelMeshComponentByName(const TInlineComponentArray<USkeletalMeshComponent*, 5>& SkeletalMeshComponents,
	const TMap<FName, int32, TInlineSetAllocator<16>>& ComponentNameToIndexMap)
{
	AssignSkelMeshComponentByName(SkeletalMeshComponents, ComponentNameToIndexMap, Body, *BodyComponentName);

	// In case the body cannot be found by name, take the parent component of the face.
	// This happens on characters and pawns which have their own pre-integrated skeletal mesh component
	// used for the driving skeleton that we have to hook into.
	if (!Body && Face)
	{
		if (USkeletalMeshComponent* ParentComponent = Cast<USkeletalMeshComponent>(Face->GetAttachParent()))
		{
			Body = ParentComponent;
		}
	}
}

void UMetaHumanComponentBase::SetFollowBody(USkeletalMeshComponent* SkelMeshComponent) const
{
	if (SkelMeshComponent)
	{
		SkelMeshComponent->SetLeaderPoseComponent(Body);
	}
}

void UMetaHumanComponentBase::RunAndInitPostAnimBP(USkeletalMeshComponent* SkelMeshComponent, TSubclassOf<UAnimInstance> AnimInstance, bool bRunAsOverridePostAnimBP, bool bReinitAnimInstances) const
{
	if (USkeletalMesh* SkeletalMesh = SkelMeshComponent->GetSkeletalMeshAsset(); IsValid(SkeletalMesh))
	{
		if (bRunAsOverridePostAnimBP)
		{
			// Check if the Skeletal Mesh set in the component is valid as SetOverridePostProcessAnimBP
			// calls InitializeAnimScriptInstance and check if the mesh is valid
			SkelMeshComponent->SetOverridePostProcessAnimBP(AnimInstance, bReinitAnimInstances);
		}
		else
		{
			SkeletalMesh->SetPostProcessAnimBlueprint(AnimInstance);

			// In case the skeletal mesh component was pre-existing, we need to re-initialize the AnimBPs,
			// as the post-processing AnimBP on the skeletal mesh changed without informing the component.
			SkelMeshComponent->InitializeAnimScriptInstance();
		}
	}
}

void UMetaHumanComponentBase::LoadAndRunAnimBP(TSoftClassPtr<UAnimInstance> AnimBlueprint, TWeakObjectPtr<USkeletalMeshComponent> SkelMeshComponent, bool bIsPostProcessingAnimBP, bool bRunAsOverridePostAnimBP)
{
	if (!SkelMeshComponent.IsValid())
	{
		return;
	}

	// Skip attempting a load if the AnimBP is null
	if (AnimBlueprint.IsNull())
	{
		if (bIsPostProcessingAnimBP)
		{
			RunAndInitPostAnimBP(SkelMeshComponent.Get(), nullptr, bRunAsOverridePostAnimBP);
		}
		else
		{
			SkelMeshComponent->SetAnimInstanceClass(nullptr);
		}
		return;
	}

	// Try to load the AnimBP asynchronously.
	const FSoftObjectPath& AssetPath = AnimBlueprint.ToSoftObjectPath();
	TWeakObjectPtr<UMetaHumanComponentBase> WeakThis(this);
	UAssetManager::GetStreamableManager().RequestAsyncLoad(
		AssetPath,
		[WeakThis, AnimBlueprint, SkelMeshComponent, bIsPostProcessingAnimBP, bRunAsOverridePostAnimBP]()
		{
			UMetaHumanComponentBase* MetaHumanComponent = WeakThis.Get();
			if (MetaHumanComponent && SkelMeshComponent.IsValid())
			{
				if (USkeletalMesh* SkeletalMesh = SkelMeshComponent->GetSkeletalMeshAsset(); IsValid(SkeletalMesh))
				{
					if (USkeleton* Skeleton = SkeletalMesh->GetSkeleton())
					{
						if (bIsPostProcessingAnimBP)
						{
							MetaHumanComponent->RunAndInitPostAnimBP(SkelMeshComponent.Get(), AnimBlueprint.Get(), bRunAsOverridePostAnimBP);
							MetaHumanComponent->PostInitAnimBP(SkelMeshComponent.Get(), SkelMeshComponent->GetPostProcessInstance());
						}
						else
						{
							SkelMeshComponent->SetAnimInstanceClass(AnimBlueprint.Get());

							// Feed the right values to the AnimBP variables.
							MetaHumanComponent->PostInitAnimBP(SkelMeshComponent.Get(), SkelMeshComponent->GetAnimInstance());
						}
					}
				}
			}
		},
		FStreamableManager::DefaultAsyncLoadPriority);
}

void UMetaHumanComponentBase::PostInitAnimBP(USkeletalMeshComponent* SkeletalMeshComponent, UAnimInstance* AnimInstance) const
{
	if (!AnimInstance)
	{
		return;
	}

	// Face
	if (Face.Get() == SkeletalMeshComponent)
	{
		MetaHumanComponentHelpers::ConnectVariable<FIntProperty, int32>(AnimInstance, TEXT("BodyTypeIndex"), static_cast<int32>(BodyType));
		MetaHumanComponentHelpers::ConnectVariable<FIntProperty, int32>(AnimInstance, TEXT("Rig Logic LOD Threshold"), static_cast<int32>(RigLogicLODThreshold));
		MetaHumanComponentHelpers::ConnectVariable<FBoolProperty, bool>(AnimInstance, TEXT("Enable Neck Correctives"), bEnableNeckCorrectives);
		MetaHumanComponentHelpers::ConnectVariable<FIntProperty, int32>(AnimInstance, TEXT("Neck Correctives LOD Threshold"), static_cast<int32>(NeckCorrectivesLODThreshold));
		MetaHumanComponentHelpers::ConnectVariable<FBoolProperty, bool>(AnimInstance, TEXT("Enable Neck Procedural Control Rig"), bEnableNeckProcControlRig);
		MetaHumanComponentHelpers::ConnectVariable<FIntProperty, int32>(AnimInstance, TEXT("Neck Procedural Control Rig LOD Threshold"), static_cast<int32>(NeckProcControlRigLODThreshold));
	}
}

void UMetaHumanComponentBase::PostConnectAnimBPVariables(const FMetaHumanCustomizableBodyPart& BodyPart, USkeletalMeshComponent* SkeletalMeshComponent, UAnimInstance* AnimInstance) const
{
	if (!AnimInstance)
	{
		return;
	}

	if (BodyPart.SkeletalMeshComponent == SkeletalMeshComponent)
	{
		MetaHumanComponentHelpers::ConnectVariable<FBoolProperty, bool>(AnimInstance, TEXT("Enable Control Rig"), (BodyPart.ControlRigClass.Get() != nullptr));
		if (BodyPart.ControlRigClass)
		{
			MetaHumanComponentHelpers::ConnectVariable<FObjectProperty, TSubclassOf<UControlRig>>(AnimInstance, TEXT("Control Rig Class"), BodyPart.ControlRigClass);
			MetaHumanComponentHelpers::ConnectVariable<FIntProperty, int32>(AnimInstance, TEXT("Control Rig LOD Threshold"), BodyPart.ControlRigLODThreshold);
		}

		MetaHumanComponentHelpers::ConnectVariable<FBoolProperty, bool>(AnimInstance, TEXT("Enable Rigid Body Simulation"), (BodyPart.PhysicsAsset.Get() != nullptr));
		if (BodyPart.PhysicsAsset)
		{
			MetaHumanComponentHelpers::ConnectVariable<FObjectProperty, TObjectPtr<UPhysicsAsset>>(AnimInstance, TEXT("Override Physics Asset"), BodyPart.PhysicsAsset);
			MetaHumanComponentHelpers::ConnectVariable<FIntProperty, int32>(AnimInstance, TEXT("Rigid Body LOD Threshold"), BodyPart.RigidBodyLODThreshold);
		}
	}
}