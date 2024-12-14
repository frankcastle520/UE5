// Copyright Epic Games, Inc. All Rights Reserved.
#include "InterchangeTaskCreateSceneObjects.h"

#include "CoreMinimal.h"

#include "Async/TaskGraphInterfaces.h"
#include "Engine/Level.h"
#include "Engine/World.h"
#include "InterchangeEngineLogPrivate.h"
#include "InterchangeFactoryBase.h"
#include "InterchangeImportCommon.h"
#include "InterchangeManager.h"
#include "InterchangeSourceData.h"
#include "InterchangeTranslatorBase.h"
#include "Misc/Paths.h"
#include "Nodes/InterchangeFactoryBaseNode.h"
#include "PackageUtils/PackageUtils.h"
#include "Stats/Stats.h"
#include "Templates/SharedPointer.h"
#include "UObject/GarbageCollection.h"
#include "UObject/Object.h"
#include "UObject/ObjectMacros.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/WeakObjectPtrTemplates.h"

UE::Interchange::FTaskCreateSceneObjects_GameThread::FTaskCreateSceneObjects_GameThread(const FString& InPackageBasePath, const int32 InSourceIndex, TWeakPtr<FImportAsyncHelper> InAsyncHelper, TArrayView<UInterchangeFactoryBaseNode*> InFactoryNodes, const UClass* InFactoryClass)
	: PackageBasePath(InPackageBasePath)
	, SourceIndex(InSourceIndex)
	, WeakAsyncHelper(InAsyncHelper)
	, FactoryNodes(InFactoryNodes)
	, FactoryClass(InFactoryClass)
{
	check(FactoryClass);
}

void UE::Interchange::FTaskCreateSceneObjects_GameThread::Execute()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UE::Interchange::FTaskCreateSceneObjects_GameThread::DoTask)
#if INTERCHANGE_TRACE_ASYNCHRONOUS_TASK_ENABLED
	INTERCHANGE_TRACE_ASYNCHRONOUS_TASK(SpawnActor)
#endif
	using namespace UE::Interchange;

	check(IsInGameThread());
	TSharedPtr<FImportAsyncHelper> AsyncHelper = WeakAsyncHelper.Pin();
	check(WeakAsyncHelper.IsValid());

	//Verify if the task was canceled
	if (AsyncHelper->bCancel)
	{
		return;
	}

	TArray<TObjectPtr<UObject>> ImportObjects;
	AsyncHelper->IterateImportedAssets(SourceIndex, [&ImportObjects](const TArray<UE::Interchange::FImportAsyncHelper::FImportedObjectInfo>& ImportedObjectInfos)
		{
			for (const UE::Interchange::FImportAsyncHelper::FImportedObjectInfo& Info : ImportedObjectInfos)
			{
				ImportObjects.Add(Info.ImportedObject);
			}
		});

	UObject* ReimportObject = AsyncHelper->TaskData.ReimportObject;
	ULevel* ImportLevel = AsyncHelper->TaskData.ImportLevel ? AsyncHelper->TaskData.ImportLevel : GWorld->GetCurrentLevel();
	UWorld* ImportWorld = ImportLevel->GetWorld();

	for (UInterchangeFactoryBaseNode* FactoryNode : FactoryNodes)
	{
		// Don't create actors on a first import if they're disabled
		if (!FactoryNode || !FactoryNode->IsEnabled())
		{
			continue;
		}

		UInterchangeFactoryBase* Factory = NewObject<UInterchangeFactoryBase>(GetTransientPackage(), FactoryClass);
		Factory->SetResultsContainer(AsyncHelper->AssetImportResult->GetResults());
		AsyncHelper->AddCreatedFactory(FactoryNode->GetUniqueID(), Factory);

		FString SceneNodeName = FactoryNode->GetAssetName();
		UInterchangeManager::GetInterchangeManager().SanitizeNameInline(SceneNodeName, ESanitizeNameTypeFlags::ObjectName | ESanitizeNameTypeFlags::ObjectPath | ESanitizeNameTypeFlags::LongPackage);

		ULevel* FactoryImportLevel = ImportLevel;
		UWorld* FactoryImportWorld = ImportWorld;

		FString FactoryNodeLevelUid;
		if (FactoryNode->GetCustomLevelUid(FactoryNodeLevelUid))
		{
			if (UInterchangeFactoryBaseNode* LevelFactoryNode = AsyncHelper->BaseNodeContainers[SourceIndex]->GetFactoryNode(FactoryNodeLevelUid))
			{
				FSoftObjectPath LevelAssetPath;
				if (LevelFactoryNode->GetCustomReferenceObject(LevelAssetPath))
				{
					if (UWorld* FactoryNodeWorld = Cast<UWorld>(LevelAssetPath.TryLoad()))
					{
						if (FactoryNodeWorld->PersistentLevel)
						{
							FactoryImportWorld = FactoryNodeWorld;
							FactoryImportLevel = FactoryNodeWorld->PersistentLevel;
						}
					}
				}
			}
		}
		const FString FactoryNodeWorldPath = FactoryImportWorld->GetOutermost()->GetPathName();
		const FString FactoryNodeWorldName = FactoryImportWorld->GetName();
		const FString FactoryNodeNodePrefix = FactoryImportLevel->GetName() + TEXT(".");

		UInterchangeFactoryBase::FImportSceneObjectsParams CreateSceneObjectsParams;
		CreateSceneObjectsParams.ObjectName = SceneNodeName;
		CreateSceneObjectsParams.FactoryNode = FactoryNode;
		CreateSceneObjectsParams.Level = FactoryImportLevel;
		CreateSceneObjectsParams.ImportAssets = ImportObjects;
		CreateSceneObjectsParams.ReimportObject = FFactoryCommon::GetObjectToReimport(Factory, ReimportObject, *FactoryNode, FactoryNodeWorldPath, FactoryNodeWorldName, FactoryNodeNodePrefix + SceneNodeName);
		CreateSceneObjectsParams.ReimportFactoryNode = FFactoryCommon::GetFactoryNode(ReimportObject, FactoryNodeWorldPath, FactoryNodeWorldName, FactoryNodeNodePrefix + SceneNodeName);

		if (AsyncHelper->BaseNodeContainers.IsValidIndex(SourceIndex))
		{
			CreateSceneObjectsParams.NodeContainer = AsyncHelper->BaseNodeContainers[SourceIndex].Get();
			CreateSceneObjectsParams.SourceData = AsyncHelper->SourceDatas[SourceIndex];
		}

		UObject* SceneObject = Factory->ImportSceneObject_GameThread(CreateSceneObjectsParams);
		if (SceneObject)
		{
			const FImportAsyncHelper::FImportedObjectInfo* ImportedInfoPtr = AsyncHelper->FindImportedSceneObjects(SourceIndex, [SceneObject](const FImportAsyncHelper::FImportedObjectInfo& CurInfo)
				{
					return CurInfo.ImportedObject == SceneObject;
				});

			if (!ImportedInfoPtr)
			{
				UE::Interchange::FImportAsyncHelper::FImportedObjectInfo& ObjectInfo = AsyncHelper->AddDefaultImportedSceneObjectGetRef(SourceIndex);
				ObjectInfo.ImportedObject = SceneObject;
				ObjectInfo.Factory = Factory;
				ObjectInfo.FactoryNode = FactoryNode;
			}

			FactoryNode->SetCustomReferenceObject(FSoftObjectPath(SceneObject));
		}
	}
}
