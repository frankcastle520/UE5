// Copyright Epic Games, Inc. All Rights Reserved. 
#include "Scene/InterchangeLevelInstanceActorFactory.h"

#include "Engine/World.h"
#include "InterchangeEditorUtilitiesBase.h"
#include "InterchangeImportLog.h"
#include "InterchangeLevelFactoryNode.h"
#include "InterchangeLevelInstanceActorFactoryNode.h"
#include "InterchangeManager.h"
#include "LevelInstance/LevelInstanceActor.h"
#include "Nodes/InterchangeBaseNodeContainer.h"
#include "Scene/InterchangeActorHelper.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(InterchangeLevelInstanceActorFactory)

UClass* UInterchangeLevelInstanceActorFactory::GetFactoryClass() const
{
	return ALevelInstance::StaticClass();
}

UObject* UInterchangeLevelInstanceActorFactory::ProcessActor(AActor& SpawnedActor, const UInterchangeActorFactoryNode& FactoryNode, const UInterchangeBaseNodeContainer& NodeContainer, const FImportSceneObjectsParams& Params)
{
	auto SuperResult = [&]()
		{
			return Super::ProcessActor(SpawnedActor, FactoryNode, NodeContainer, Params);
		};

	const UInterchangeLevelInstanceActorFactoryNode* LevelInstanceActorFactoryNode = Cast<UInterchangeLevelInstanceActorFactoryNode>(&FactoryNode);
	if (!LevelInstanceActorFactoryNode)
	{
		UE_LOG(LogInterchangeImport, Warning, TEXT("UInterchangeLevelInstanceActorFactory::ProcessActor: Level instance actor factory node is null"));
		return SuperResult();
	}

	ALevelInstance* LevelInstanceActor = Cast<ALevelInstance>(&SpawnedActor);
	if (!LevelInstanceActor)
	{
		FString AssetName = LevelInstanceActorFactoryNode->GetDisplayLabel();
		UE_LOG(LogInterchangeImport, Warning, TEXT("UInterchangeLevelInstanceActorFactory::ProcessActor: Actor level instance was not created %s."), *AssetName);
		return SuperResult();
	}
	//Get the world reference by this level instance actor
	FString ReferenceLevelFactoryNodeUid;
	if (!LevelInstanceActorFactoryNode->GetCustomLevelReference(ReferenceLevelFactoryNodeUid))
	{
		UE_LOG(LogInterchangeImport, Warning, TEXT("UInterchangeLevelInstanceActorFactory::ProcessActor: Actor level instance do not reference any level factory node."));
		return SuperResult();
	}

	UInterchangeLevelFactoryNode* ReferenceLevelFactoryNode = Cast<UInterchangeLevelFactoryNode>(NodeContainer.GetFactoryNode(ReferenceLevelFactoryNodeUid));
	if (!ReferenceLevelFactoryNode)
	{
		UE_LOG(LogInterchangeImport, Warning, TEXT("UInterchangeLevelInstanceActorFactory::ProcessActor: Level factory node is invalid."));
		return SuperResult();
	}
	FSoftObjectPath ReferenceLevelSoftObjectPath;
	if (!ReferenceLevelFactoryNode->GetCustomReferenceObject(ReferenceLevelSoftObjectPath))
	{
		UE_LOG(LogInterchangeImport, Warning, TEXT("UInterchangeLevelInstanceActorFactory::ProcessActor: Level factory node do not reference any UWorld soft object path."));
		return SuperResult();
	}

	UWorld* ReferenceWorld = Cast<UWorld>(ReferenceLevelSoftObjectPath.TryLoad());
	if (!ReferenceWorld)
	{
		UE_LOG(LogInterchangeImport, Warning, TEXT("UInterchangeLevelInstanceActorFactory::ProcessActor: UWorld soft object path point on an invalid UWorld."));
		return SuperResult();
	}
#if WITH_EDITOR
	LevelInstanceActor->SetActorLabel(ReferenceLevelFactoryNode->GetDisplayLabel());
#endif

	return SuperResult();
}
