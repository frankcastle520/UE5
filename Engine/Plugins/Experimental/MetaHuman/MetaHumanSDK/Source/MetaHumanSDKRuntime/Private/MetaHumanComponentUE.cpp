// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetaHumanComponentUE.h"
#include "Components/SkeletalMeshComponent.h"

#include "Animation/AnimInstance.h"
#include "ControlRig.h"
#include "Engine/AssetManager.h"
#include "Engine/SkeletalMesh.h"
#include "PhysicsEngine/PhysicsAsset.h"

void UMetaHumanComponentUE::OnRegister()
{
	Super::OnRegister();
}

void UMetaHumanComponentUE::BeginPlay()
{
	Super::BeginPlay();

	UpdateComponentLinks();

	SetupCustomizableBodyPart(Torso);
	SetupCustomizableBodyPart(Legs);
	SetupCustomizableBodyPart(Feet);

	if (Face)
	{
		PostInitAnimBP(Face.Get(), Face->GetPostProcessInstance());
	}

	if (Body)
	{
		UAnimInstance* AnimInstance = Body->GetPostProcessInstance();
		if (AnimInstance)
		{
			MetaHumanComponentHelpers::ConnectVariable<FBoolProperty, bool>(AnimInstance, TEXT("Enable Body Correctives"), bEnableBodyCorrectives);
		}
	}
}

void UMetaHumanComponentUE::OnUnregister()
{
	Super::OnUnregister();
}

void UMetaHumanComponentUE::SetupCustomizableBodyPart(FMetaHumanCustomizableBodyPart& BodyPart)
{
	if (!BodyPart.SkeletalMeshComponent)
	{
		return;
	}

	BodyPart.SkeletalMeshComponent->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::OnlyTickPoseWhenRendered;

	// Retrieve the physics asset as well as the control rig set by the skeletal mesh asset.
	UPhysicsAsset* SkelMeshPhysicsAsset = nullptr;
	TSubclassOf<UControlRig> SkelMeshControlRigClass = nullptr;
	if (USkeletalMesh* SkeletalMeshAsset = BodyPart.SkeletalMeshComponent->GetSkeletalMeshAsset())
	{
		if (TSubclassOf<UAnimInstance> PostProcessAnimBPClass = SkeletalMeshAsset->GetPostProcessAnimBlueprint())
		{
			if (UAnimInstance* DefaultAnimBP = PostProcessAnimBPClass.GetDefaultObject())
			{
				static constexpr FStringView OverridePhysicsAssetPropertyName = TEXTVIEW("Override Physics Asset");
				MetaHumanComponentHelpers::GetPropertyValue(DefaultAnimBP, OverridePhysicsAssetPropertyName, SkelMeshPhysicsAsset);

				static constexpr FStringView ControlRigClassPropertyName = TEXTVIEW("Control Rig Class");
				MetaHumanComponentHelpers::GetPropertyValue(DefaultAnimBP, ControlRigClassPropertyName, SkelMeshControlRigClass);
			}
		}
	}

	bool ShouldEvalInstancePostProcessAnimBP = (PostProcessAnimBP && (BodyPart.ControlRigClass || BodyPart.PhysicsAsset) && (BodyPart.PhysicsAsset != SkelMeshPhysicsAsset || BodyPart.ControlRigClass != SkelMeshControlRigClass));
	if (ShouldEvalInstancePostProcessAnimBP)
	{
		// Run post-processing AnimBP on the skeletal mesh component (instance) and overwrite the post-processing AnimBP that might be possibly set on the skeletal mesh asset.
		LoadAndRunAnimBP(PostProcessAnimBP, BodyPart.SkeletalMeshComponent, /*IsPostProcessingAnimBP*/true, /*RunAsOverridePostAnimBP*/true);

		// Force nulling the leader pose component to disable following another skel mesh component's pose.
		// When using a post-processing AnimBP we use a copy pose from mesh anim graph node to sync the skeletons.
		BodyPart.SkeletalMeshComponent->SetLeaderPoseComponent(nullptr);
	}
	else
	{
		if (SkelMeshPhysicsAsset || SkelMeshControlRigClass)
		{
			// Keep running the post-processing AnimBP from the skeletal mesh asset, hook into the variables so we can control its performance and LOD thresholds on the instance.
			PostConnectAnimBPVariables(BodyPart, BodyPart.SkeletalMeshComponent, BodyPart.SkeletalMeshComponent->GetPostProcessInstance());
		}

		if (USkeletalMesh* SkeletalMesh = BodyPart.SkeletalMeshComponent->GetSkeletalMeshAsset(); IsValid(SkeletalMesh))
		{
			if (!SkeletalMesh->GetPostProcessAnimBlueprint() && !BodyPart.SkeletalMeshComponent->GetAnimInstance())
			{
				// Didn't have a post-processing AnimBP and AnimBP running, use leader-follower pose.
				SetFollowBody(BodyPart.SkeletalMeshComponent);
			}
		}
	}
}

void UMetaHumanComponentUE::PostInitAnimBP(USkeletalMeshComponent* SkeletalMeshComponent, UAnimInstance* AnimInstance) const
{
	if (!AnimInstance)
	{
		return;
	}

	UMetaHumanComponentBase::PostInitAnimBP(SkeletalMeshComponent, AnimInstance);

	PostConnectAnimBPVariables(Torso, SkeletalMeshComponent, AnimInstance);
	PostConnectAnimBPVariables(Legs, SkeletalMeshComponent, AnimInstance);
	PostConnectAnimBPVariables(Feet, SkeletalMeshComponent, AnimInstance);

	// Refresh the given skeletal mesh component and update the pose. This is needed to see an updated and correct pose
	// in the editor in case it is not ticking or in the game before the first tick. Otherwise any post-processing of the override AnimBPs won't be visible.
	SkeletalMeshComponent->TickAnimation(0.0f, false /*bNeedsValidRootMotion*/);
	SkeletalMeshComponent->TickComponent(0.0f, ELevelTick::LEVELTICK_All, nullptr);
	SkeletalMeshComponent->RefreshBoneTransforms(nullptr /*TickFunction*/);
	SkeletalMeshComponent->RefreshFollowerComponents();
}
