// Copyright Epic Games, Inc. All Rights Reserved.

#include "Component/SkinnedMeshComponentExtensions.h"
#include "GenerationTools.h"
#include "Components/SkinnedMeshComponent.h"

namespace UE::Anim
{

void FSkinnedMeshComponentExtensions::CompleteAndDispatch(
	USkinnedMeshComponent* InComponent,
	TConstArrayView<FBoneIndexType> InParentIndices,
	TConstArrayView<FBoneIndexType> InRequiredBoneIndices,
	TConstArrayView<FTransform> InLocalSpaceTransforms)
{
	// Fill the component space transform buffer
	TArrayView<FTransform> ComponentSpaceTransforms = InComponent->GetEditableComponentSpaceTransforms();
	if (ComponentSpaceTransforms.Num() > 0)
	{
		UE::AnimNext::FGenerationTools::ConvertLocalSpaceToComponentSpace(InParentIndices, InLocalSpaceTransforms, InRequiredBoneIndices, ComponentSpaceTransforms);

		// Flag buffer for flip
		InComponent->bNeedToFlipSpaceBaseBuffers = true;

		InComponent->FlipEditableSpaceBases();
		InComponent->bHasValidBoneTransform = true;

		InComponent->InvalidateCachedBounds();
		InComponent->UpdateBounds();

		// Send updated transforms to the renderer
		InComponent->SendRenderDynamicData_Concurrent();
	}
}

}
