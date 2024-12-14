// Copyright Epic Games, Inc. All Rights Reserved. 
#include "Scene/InterchangeActorFactory.h"

#include "InterchangeActorFactoryNode.h"
#include "InterchangeCameraFactoryNode.h"
#include "Nodes/InterchangeBaseNodeContainer.h"
#include "Nodes/InterchangeFactoryBaseNode.h"
#include "Scene/InterchangeActorHelper.h"

#include "GameFramework/Actor.h"
#include "Components/SceneComponent.h"

#if WITH_EDITORONLY_DATA
#include "Engine/World.h"
#include "Layers/LayersSubsystem.h"
#include "Layers/Layer.h"
#include "Editor/EditorEngine.h"

extern UNREALED_API UEditorEngine* GEditor;
#endif

#include UE_INLINE_GENERATED_CPP_BY_NAME(InterchangeActorFactory)

namespace UE::InterchangeActorFactory::Private
{
	// For the camera actor types we get two components each: The root component is a default scene component, and the actual camera
	// component is a child of the scene component. We want to place all scene component stuff (mostly transform) on the root
	// component, and all the camera stuff on the camera component. This agrees with how the actor/root component is bound on
	// LevelSequences, and is likely what users expect because when you place a camera actor on the level and move it around, you always
	// affect the root component transform

	template <class T>
	void ApplyAllCameraCustomAttributes(
		const UInterchangeFactoryBase::FImportSceneObjectsParams& CreateSceneObjectsParams,
		T* CameraFactoryNode,
		USceneComponent* RootSceneComponent,
		USceneComponent* ChildCameraComponent
	)
	{
		using namespace UE::Interchange;

		if (!RootSceneComponent || !ChildCameraComponent)
		{
			return;
		}

		UInterchangeBaseNodeContainer* const NodeContainer = const_cast<UInterchangeBaseNodeContainer* const>(CreateSceneObjectsParams.NodeContainer);

		// Create a temp factory node so we don't modify our existing nodes with our changes
		T* FactoryNodeCopy = NewObject<T>(NodeContainer, NAME_None);
		FactoryNodeCopy->InitializeNode(
			CameraFactoryNode->GetUniqueID(),
			CameraFactoryNode->GetDisplayLabel(),
			CameraFactoryNode->GetNodeContainerType()
		);

		NodeContainer->ReplaceNode(CameraFactoryNode->GetUniqueID(), FactoryNodeCopy);
		{
			UInterchangeFactoryBase::FImportSceneObjectsParams ParamsCopy{CreateSceneObjectsParams};
			ParamsCopy.FactoryNode = FactoryNodeCopy;

			// Apply exclusively camera stuff to the CineCameraComponent
			FactoryNodeCopy->CopyWithObject(CameraFactoryNode, ChildCameraComponent);
			FactoryNodeCopy->RemoveCustomAttributesForClass(USceneComponent::StaticClass());
			ActorHelper::ApplyAllCustomAttributes(ParamsCopy, *ChildCameraComponent);

			// Apply exclusively scene component stuff to the root SceneComponent
			FactoryNodeCopy->CopyWithObject(CameraFactoryNode, RootSceneComponent);
			FactoryNodeCopy->RemoveCustomAttributesForClass(UCineCameraComponent::StaticClass());
			ActorHelper::ApplyAllCustomAttributes(ParamsCopy, *RootSceneComponent);
		}
		NodeContainer->ReplaceNode(FactoryNodeCopy->GetUniqueID(), CameraFactoryNode);
	}
}

UClass* UInterchangeActorFactory::GetFactoryClass() const
{
	return AActor::StaticClass();
}

UObject* UInterchangeActorFactory::ImportSceneObject_GameThread(const UInterchangeFactoryBase::FImportSceneObjectsParams& CreateSceneObjectsParams)
{
	using namespace UE::Interchange;
	using namespace UE::InterchangeActorFactory::Private;

	UInterchangeActorFactoryNode* FactoryNode = Cast<UInterchangeActorFactoryNode>(CreateSceneObjectsParams.FactoryNode);
	if (!ensure(FactoryNode) || !CreateSceneObjectsParams.NodeContainer)
	{
		return nullptr;
	}

	AActor* SpawnedActor = ActorHelper::SpawnFactoryActor(CreateSceneObjectsParams);

	if (SpawnedActor)
	{
		if (UObject* ObjectToUpdate = ProcessActor(*SpawnedActor, *FactoryNode, *CreateSceneObjectsParams.NodeContainer, CreateSceneObjectsParams))
		{
			if (USceneComponent* RootComponent = SpawnedActor->GetRootComponent())
			{
				// Cache mobility value to allow application of transform
				EComponentMobility::Type CachedMobility = RootComponent->Mobility;
				RootComponent->SetMobility(EComponentMobility::Type::Movable);

				// Apply factory node to object(s)
				if (FactoryNode->IsA<UInterchangePhysicalCameraFactoryNode>())
				{
					UCineCameraComponent* CameraComponent = Cast<UCineCameraComponent>(ObjectToUpdate);
					ApplyAllCameraCustomAttributes(CreateSceneObjectsParams, Cast<UInterchangePhysicalCameraFactoryNode>(FactoryNode), RootComponent, CameraComponent);
				}
				else if (FactoryNode->IsA<UInterchangeStandardCameraFactoryNode>())
				{
					UCameraComponent* CameraComponent = Cast<UCameraComponent>(ObjectToUpdate);
					ApplyAllCameraCustomAttributes(CreateSceneObjectsParams, Cast<UInterchangeStandardCameraFactoryNode>(FactoryNode), RootComponent, CameraComponent);
				}
				else
				{
					ActorHelper::ApplyAllCustomAttributes(CreateSceneObjectsParams, *ObjectToUpdate);
				}

				// Restore mobility value
				if (CachedMobility != EComponentMobility::Type::Movable)
				{
					RootComponent->SetMobility(CachedMobility);
				}
			}
		}

		ProcessTags(FactoryNode, SpawnedActor);

		ProcessLayerNames(FactoryNode, SpawnedActor);
	}

	return SpawnedActor;
}

UObject* UInterchangeActorFactory::ProcessActor(AActor& SpawnedActor, const UInterchangeActorFactoryNode& /*FactoryNode*/, const UInterchangeBaseNodeContainer& /*NodeContainer*/, const FImportSceneObjectsParams& Params)
{
	return SpawnedActor.GetRootComponent();
}

void UInterchangeActorFactory::ProcessTags(UInterchangeActorFactoryNode* FactoryNode, AActor* SpawnedActor)
{
	TArray<FString> TagsArray;
	FactoryNode->GetTags(TagsArray);

	TSet<FString> Tags(TagsArray);
	TSet<FName> AlreadySetTags(SpawnedActor->Tags);

	for (const FString& Tag : Tags)
	{
		FName TagName(Tag);
		if (!AlreadySetTags.Contains(TagName))
		{
			SpawnedActor->Tags.Add(TagName);
		}
	}
}

void UInterchangeActorFactory::ProcessLayerNames(UInterchangeActorFactoryNode* FactoryNode, AActor* SpawnedActor)
{
	TArray<FString> LayerNamesArray;
	FactoryNode->GetLayerNames(LayerNamesArray);

	TSet<FString> LayerNames(LayerNamesArray);
#if WITH_EDITORONLY_DATA
	AddUniqueLayersToWorld(SpawnedActor->GetWorld(), LayerNames);
#endif

	TSet<FName> AlreadySetLayerNames(SpawnedActor->Layers);

	for (const FString& LayerNameString : LayerNames)
	{
		FName LayerName(LayerNameString);
		if (!AlreadySetLayerNames.Contains(LayerName))
		{
			SpawnedActor->Layers.Add(FName(LayerName));
		}
	}
}

#if WITH_EDITORONLY_DATA
void UInterchangeActorFactory::AddUniqueLayersToWorld(UWorld* World, const TSet<FString>& LayerNames)
{
	if (!World || !IsValidChecked(World) || World->IsUnreachable() || LayerNames.Num() == 0)
	{
		return;
	}

	TSet< FName > ExistingLayers;
	for (ULayer* Layer : World->Layers)
	{
		ExistingLayers.Add(Layer->GetLayerName());
	}

	int32 NumberOfExistingLayers = World->Layers.Num();

	ULayersSubsystem* LayersSubsystem = GEditor ? GEditor->GetEditorSubsystem<ULayersSubsystem>() : nullptr;
	for (const FString& LayerNameString : LayerNames)
	{
		FName LayerName(LayerNameString);

		if (!ExistingLayers.Contains(LayerName))
		{
			// Use the ILayers if we are adding the layers to the currently edited world
			if (LayersSubsystem && GWorld && World == GWorld.GetReference())
			{
				LayersSubsystem->CreateLayer(LayerName);
			}
			else
			{
				ULayer* NewLayer = NewObject<ULayer>(World, NAME_None, RF_Transactional);
				if (!ensure(NewLayer != NULL))
				{
					continue;
				}

				World->Layers.Add(NewLayer);

				NewLayer->SetLayerName(LayerName);
				NewLayer->SetVisible(true);
			}
		}
	}

	if (NumberOfExistingLayers != World->Layers.Num())
	{
		World->Modify();
	}
}
#endif