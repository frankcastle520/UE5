// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Components/SceneComponent.h"

#include "SkeletalMeshComponentInstanceData.generated.h"

class USkeletalMeshComponent;
class USkeletalMesh;

/** Saves internal SkeletalMesh (transient) state that gets lost at reconstruction for Blueprint created components. */
USTRUCT()
struct ENGINE_API FSkeletalMeshComponentInstanceData : public FSceneComponentInstanceData
{
	GENERATED_BODY()
	
	FSkeletalMeshComponentInstanceData() = default;
	explicit FSkeletalMeshComponentInstanceData(const USkeletalMeshComponent* SourceComponent);
	
	virtual bool ContainsData() const override;
	virtual void ApplyToComponent(UActorComponent* Component, const ECacheApplyPhase CacheApplyPhase) override;

	USkeletalMesh* SkeletalMeshAsset;
	uint8 bUpdateAnimationInEditor : 1;
	uint8 bUpdateClothInEditor : 1;
};
