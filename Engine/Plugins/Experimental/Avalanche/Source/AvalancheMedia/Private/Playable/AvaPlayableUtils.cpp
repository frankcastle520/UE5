// Copyright Epic Games, Inc. All Rights Reserved.

#include "AvaPlayableUtils.h"

#include "Components/PrimitiveComponent.h"

namespace UE::AvaMedia::PlayableUtils
{
	void AddPrimitiveComponentIds(const AActor* InActor, TSet<FPrimitiveComponentId>& InComponentIds)
	{
		TInlineComponentArray<UPrimitiveComponent*> Components;
		InActor->GetComponents(Components);
		for (int32 ComponentIndex = 0; ComponentIndex < Components.Num(); ComponentIndex++)
		{
			const UPrimitiveComponent* PrimitiveComponent = Components[ComponentIndex];
			if (PrimitiveComponent->IsRegistered())
			{
				InComponentIds.Add(PrimitiveComponent->GetPrimitiveSceneId());

				for (USceneComponent* AttachedChild : PrimitiveComponent->GetAttachChildren())
				{						
					const UPrimitiveComponent* AttachChildPC = Cast<UPrimitiveComponent>(AttachedChild);
					if (AttachChildPC && AttachChildPC->IsRegistered())
					{
						InComponentIds.Add(AttachChildPC->GetPrimitiveSceneId());
					}
				}
			}
		}
	}
}