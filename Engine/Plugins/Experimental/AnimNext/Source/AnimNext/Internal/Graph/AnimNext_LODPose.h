// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ReferencePose.h"
#include "LODPose.h"
#include "Animation/AnimCurveTypes.h"
#include "Animation/AttributesRuntime.h"

#include "AnimNext_LODPose.generated.h"

USTRUCT(BlueprintType, meta = (DisplayName = "ReferencePose"))
struct FAnimNextGraphReferencePose
{
	GENERATED_BODY()

	FAnimNextGraphReferencePose() = default;

	explicit FAnimNextGraphReferencePose(UE::AnimNext::FDataHandle& InReferencePose)
		: ReferencePose(InReferencePose)
	{
	}

	UE::AnimNext::FDataHandle ReferencePose;
};

USTRUCT(BlueprintType, meta = (DisplayName = "LODPose"))
struct FAnimNextGraphLODPose
{
	GENERATED_BODY()

	// Joint transforms
	UE::AnimNext::FLODPoseHeap LODPose;

	// Float curves
	FBlendedHeapCurve Curves;

	// Attributes
	// Note that attribute bone indices are LOD bone indices matching the LOD pose
	UE::Anim::FHeapAttributeContainer Attributes;
};
