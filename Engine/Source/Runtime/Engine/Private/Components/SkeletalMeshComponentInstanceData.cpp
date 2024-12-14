// Copyright Epic Games, Inc. All Rights Reserved.

#include "SkeletalMeshComponentInstanceData.h"

#include "Components/SkeletalMeshComponent.h"


FSkeletalMeshComponentInstanceData::FSkeletalMeshComponentInstanceData(const USkeletalMeshComponent* SourceComponent)
	: FSceneComponentInstanceData(SourceComponent)
	, SkeletalMeshAsset(nullptr)
	, bUpdateAnimationInEditor(0)
	, bUpdateClothInEditor(0)
{
#if WITH_EDITOR
	// Only Blueprint components would reset transient values when the construction script is re-run.
	// Hence, we only need to apply instance cache for Blueprint created components.
	const bool bIsBlueprintCreatedComponent = SourceComponent->CreationMethod == EComponentCreationMethod::SimpleConstructionScript
		|| SourceComponent->CreationMethod == EComponentCreationMethod::UserConstructionScript;
	if (bIsBlueprintCreatedComponent)
	{
		SkeletalMeshAsset = SourceComponent->GetSkeletalMeshAsset();
		bUpdateAnimationInEditor = SourceComponent->GetUpdateAnimationInEditor();
		bUpdateClothInEditor = SourceComponent->GetUpdateClothInEditor();
	}
#endif // WITH_EDITOR
}

bool FSkeletalMeshComponentInstanceData::ContainsData() const
{
	return bUpdateAnimationInEditor || bUpdateClothInEditor || SkeletalMeshAsset;
}

void FSkeletalMeshComponentInstanceData::ApplyToComponent(UActorComponent* Component, const ECacheApplyPhase CacheApplyPhase)
{
	Super::ApplyToComponent(Component, CacheApplyPhase);

#if WITH_EDITOR
	const bool bIsBlueprintCreatedComponent = Component->CreationMethod == EComponentCreationMethod::SimpleConstructionScript
		|| Component->CreationMethod == EComponentCreationMethod::UserConstructionScript;
	USkeletalMeshComponent* SkeletalMesh = Cast<USkeletalMeshComponent>(Component);

	if (SkeletalMesh && bIsBlueprintCreatedComponent)
	{
		if (SkeletalMeshAsset)
		{
			SkeletalMesh->SetSkeletalMeshAsset(SkeletalMeshAsset);
		}
		SkeletalMesh->SetUpdateAnimationInEditor(bUpdateAnimationInEditor);
		SkeletalMesh->SetUpdateClothInEditor(bUpdateClothInEditor);
	}
#endif // WITH_EDITOR
}
