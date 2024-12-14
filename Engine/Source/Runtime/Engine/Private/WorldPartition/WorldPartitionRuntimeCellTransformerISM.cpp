// Copyright Epic Games, Inc. All Rights Reserved.

#include "WorldPartition/WorldPartitionRuntimeCellTransformerISM.h"

#if WITH_EDITOR
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ISMPartition/ISMComponentBatcher.h"
#include "ISMPartition/ISMComponentDescriptor.h"
#include "Engine/Level.h"
#include "UObject/Package.h"
#endif

#if WITH_EDITORONLY_DATA
#include "ActorPartition/PartitionActor.h"
#include "Engine/StaticMeshActor.h"
#endif

#include "GameFramework/ActorPrimitiveColorHandler.h"

#define LOCTEXT_NAMESPACE "WorldPartition"

static const FName NAME_AutoInstancingActor(TEXT("AutoInstancingActor"));

UWorldPartitionRuntimeCellTransformerISM::UWorldPartitionRuntimeCellTransformerISM(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
#if WITH_EDITORONLY_DATA
	AllowedClasses.Add(APartitionActor::StaticClass());
	AllowedClasses.Add(AStaticMeshActor::StaticClass());
	MinNumInstances = 2;
#endif

#if ENABLE_ACTOR_PRIMITIVE_COLOR_HANDLER
	if (HasAnyFlags(RF_ClassDefaultObject) && ExactCast<UWorldPartitionRuntimeCellTransformerISM>(this))
	{
		FActorPrimitiveColorHandler::Get().RegisterPrimitiveColorHandler(TEXT("CellTransformerISM"), LOCTEXT("CellTransformerISM", "Cell Transformer ISM"), false, [](const UPrimitiveComponent* InPrimitiveComponent)
		{
			if (AActor* Actor = InPrimitiveComponent ? InPrimitiveComponent->GetOwner() : nullptr)
			{
				if (Actor->GetFName() == NAME_AutoInstancingActor)
				{
					return FLinearColor::MakeRandomSeededColor(GetTypeHash(InPrimitiveComponent->GetFName()));
				}
			}
			return FLinearColor::White;
		});
	}
#endif
}

#if WITH_EDITOR
void UWorldPartitionRuntimeCellTransformerISM::Transform(ULevel* InLevel)
{
	check(InLevel);

	struct FActorComponentBatcherDescriptor
	{
		TMap<TObjectPtr<AActor>*, TArray<UStaticMeshComponent*>> ActorComponents;
		FISMComponentBatcher ISMComponentBatcher;
	};

	TMap<FISMComponentDescriptor, FActorComponentBatcherDescriptor> ISMComponentBatchers;

	for (TObjectPtr<AActor>& Actor : InLevel->Actors)
	{
		if (IsValid(Actor) && CanAutoInstanceActor(Actor))
		{
			// Gather potential components that can be merged
			Actor->ForEachComponent<UStaticMeshComponent>(true, [&ISMComponentBatchers, &Actor](UStaticMeshComponent* StaticMeshComponent)
			{
				if (!StaticMeshComponent->IsEditorOnly() && StaticMeshComponent->IsVisible() && (StaticMeshComponent->Mobility == EComponentMobility::Static))
				{
					FISMComponentDescriptor ISMComponentDescriptor;
					ISMComponentDescriptor.InitFrom(StaticMeshComponent, false);
					ISMComponentBatchers.FindOrAdd(ISMComponentDescriptor).ActorComponents.FindOrAdd(&Actor).Add(StaticMeshComponent);
				}
			});
		}
	}

	int32 NumInstancedComponents = 0;
	for (auto& [ISMComponentDescriptor, ActorComponentBatcherDescriptor] : ISMComponentBatchers)
	{
		if ((uint32)ActorComponentBatcherDescriptor.ActorComponents.Num() >= MinNumInstances)
		{
			for (auto& [Actor, Components] : ActorComponentBatcherDescriptor.ActorComponents)
			{
				for (UStaticMeshComponent* StaticMeshComponent : Components)
				{
					// Register the component into the batcher
					StaticMeshComponent->UpdateComponentToWorld();
					ActorComponentBatcherDescriptor.ISMComponentBatcher.Add(StaticMeshComponent);

					// Remove the component from the actor
					(*Actor)->RemoveOwnedComponent(StaticMeshComponent);
					StaticMeshComponent->MarkAsGarbage();

					NumInstancedComponents++;
				}

				if (CanRemoveActor(*Actor))
				{
					*Actor = nullptr;
				}
			}
		}
	}

	InLevel->Actors.Remove(nullptr);

	if (NumInstancedComponents)
	{
		AActor* PackedActor = NewObject<AActor>(InLevel, NAME_AutoInstancingActor);

		for (auto& [ISMComponentDescriptor, ActorComponentBatcherDescriptor] : ISMComponentBatchers)
		{
			if (ActorComponentBatcherDescriptor.ISMComponentBatcher.GetNumInstances())
			{
				UInstancedStaticMeshComponent* ISMComponent = NewObject<UInstancedStaticMeshComponent>(PackedActor);
				ISMComponentDescriptor.InitComponent(ISMComponent);
				ActorComponentBatcherDescriptor.ISMComponentBatcher.InitComponent(ISMComponent);

				if (!PackedActor->GetRootComponent())
				{
					PackedActor->SetRootComponent(ISMComponent);
				}

				ISMComponent->SetMobility(EComponentMobility::Static);
				ISMComponent->SetWorldTransform(FTransform::Identity);

				PackedActor->AddInstanceComponent(ISMComponent);
			}
		}

		InLevel->Actors.Add(PackedActor);
	}
}

bool UWorldPartitionRuntimeCellTransformerISM::CanAutoInstanceActor(AActor* InActor) const
{
	if (InActor->GetIsReplicated())
	{
		return false;
	}

	if (!InActor->IsRootComponentStatic())
	{
		return false;
	}

	if (InActor->IsHidden())
	{
		return false;
	}

	if (InActor->IsEditorOnly())
	{
		return false;
	}

	if (InActor->Children.Num() || InActor->IsChildActor())
	{
		return false;
	}

	UClass* ActorClass = InActor->GetClass();
	for (TSubclassOf<AActor> DisallowedClass : DisallowedClasses)
	{
		if (ActorClass == DisallowedClass)
		{
			return false;
		}
	}

	for (TSubclassOf<AActor> AllowedClass : AllowedClasses)
	{
		if (ActorClass->IsChildOf(AllowedClass))
		{
			return true;
		}
	}

	return false;
}

bool UWorldPartitionRuntimeCellTransformerISM::CanRemoveActor(AActor* InActor) const
{
	uint32 NumRuntimeComponents = 0;
	for (UActorComponent* Component : InActor->GetComponents())
	{
		if (!Component->IsEditorOnly())
		{
			NumRuntimeComponents++;
		}
	}
		
	if (!NumRuntimeComponents)
	{
		return true;
	}

	if (NumRuntimeComponents == 1)
	{
		if (InActor->GetRootComponent() && ExactCast<USceneComponent>(InActor->GetRootComponent()))
		{
			return true;
		}
	}

	return false;
}
#endif

#undef LOCTEXT_NAMESPACE