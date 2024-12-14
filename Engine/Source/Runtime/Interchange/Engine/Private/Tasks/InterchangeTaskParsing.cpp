// Copyright Epic Games, Inc. All Rights Reserved.
#include "InterchangeTaskParsing.h"

#include "Async/TaskGraphInterfaces.h"
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "InterchangeEngineLogPrivate.h"
#include "InterchangeFactoryBase.h"
#include "InterchangeManager.h"
#include "InterchangeSourceData.h"
#include "InterchangeTaskCompletion.h"
#include "InterchangeTaskCreateSceneObjects.h"
#include "InterchangeTaskImportObject.h"
#include "InterchangeTaskPipeline.h"
#include "InterchangeTranslatorBase.h"
#include "Misc/Paths.h"
#include "Nodes/InterchangeBaseNode.h"
#include "Nodes/InterchangeBaseNodeContainer.h"
#include "PackageUtils/PackageUtils.h"
#include "Stats/Stats.h"
#include "Templates/SharedPointer.h"
#include "UObject/GarbageCollection.h"
#include "UObject/Object.h"
#include "UObject/ObjectMacros.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/WeakObjectPtrTemplates.h"

struct FTaskData
{
	FString UniqueID;
	int32 SourceIndex = INDEX_NONE;
	bool bIsSceneNode = false;
	TArray<FString> Dependencies;
	uint64 GraphEventRef;
	TArray<uint64> Prerequisites;
	const UClass* FactoryClass;

	TArray<UInterchangeFactoryBaseNode*, TInlineAllocator<1>> Nodes; // For scenes, we can group multiple nodes into a single task as they are usually very light
};

/**
 * For the Dependency sort to work the predicate must be transitive ( A > B > C implying A > C).
 * That means we must take into account the whole dependency chain, not just the immediate dependencies.
 * 
 * This is a helper struct to quickly create the dependencies chain of a node using a cache.
 */
struct FNodeDependencyCache
{
	void CacheAccumulatedDependencies(const UInterchangeBaseNodeContainer* NodeContainer, const TArray<FTaskData>& SourceTaskDatas)
	{
		CachedDependencies.Reset();
		for (const FTaskData& TaskData : SourceTaskDatas)
		{
			TSet<FString> NodeStack;
			GetAccumulatedDependencies(NodeContainer, TaskData.UniqueID, NodeStack);
		}
	}

	const TSet<FString>& GetAccumulatedDependencies(const UInterchangeBaseNodeContainer* NodeContainer, const FString& NodeID)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(FNodeDependencyCache::GetAccumulatedDependencies)
		ensure(CachedDependencies.Find(NodeID));
		TSet<FString> NodeStack;
		return GetAccumulatedDependencies(NodeContainer, NodeID, NodeStack);
	}

private:

	const TSet<FString>& GetAccumulatedDependencies(const UInterchangeBaseNodeContainer* NodeContainer, const FString& NodeID, TSet<FString>& NodeStack)
	{
		if (const TSet<FString>* DependenciesPtr = CachedDependencies.Find(NodeID))
		{
			return *DependenciesPtr;
		}

		TSet<FString> Dependencies;
		AccumulateDependencies(NodeContainer, NodeID, Dependencies, NodeStack);
		return CachedDependencies.Add(NodeID, MoveTemp(Dependencies));
	}

	void AccumulateDependencies(const UInterchangeBaseNodeContainer* NodeContainer, const FString& NodeID, TSet<FString>& OutDependenciesSet, TSet<FString>& NodeStack)
	{
		const UInterchangeFactoryBaseNode* FactoryNode = NodeContainer->GetFactoryNode(NodeID);
		if (!FactoryNode)
		{
			return;
		}

		bool bAlreadyInSet = false;
		NodeStack.Add(NodeID, &bAlreadyInSet);
		if (ensureMsgf(!bAlreadyInSet, TEXT("FNodeDependencyCache::AccumulateDependencies - Node \"%s\" is in a circular dependency, assets may not be imported properly."), *NodeID))
		{
			TArray<FString> FactoryDependencies;
			FactoryNode->GetFactoryDependencies(FactoryDependencies);
			OutDependenciesSet.Reserve(OutDependenciesSet.Num() + FactoryDependencies.Num());
			for (const FString& DependencyID : FactoryDependencies)
			{
				bAlreadyInSet = false;
				OutDependenciesSet.Add(DependencyID, &bAlreadyInSet);
				// Avoid infinite recursion.
				if (!bAlreadyInSet)
				{
					OutDependenciesSet.Append(GetAccumulatedDependencies(NodeContainer, DependencyID, NodeStack));
				}
			}
			NodeStack.Remove(NodeID);
		}
	}

	TMap<FString, TSet<FString>> CachedDependencies;
};


void UE::Interchange::FTaskParsing::Execute()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UE::Interchange::FTaskParsing::DoTask)
#if INTERCHANGE_TRACE_ASYNCHRONOUS_TASK_ENABLED
	INTERCHANGE_TRACE_ASYNCHRONOUS_TASK(ParsingGraph)
#endif
	LLM_SCOPE_BYNAME(TEXT("Interchange"));

	TOptional<FGCScopeGuard> GCScopeGuard;
	if (!IsInGameThread())
	{
		GCScopeGuard.Emplace();
	}

	TSharedPtr<FImportAsyncHelper, ESPMode::ThreadSafe> AsyncHelper = WeakAsyncHelper.Pin();
	check(AsyncHelper.IsValid());

	//Parse each graph and prepare import task data, we will then be able to create all the task with the correct dependencies
	TArray<FTaskData> TaskDatas;

	//Avoid creating asset if the asynchronous import is canceled, just create the completion task
	if (!AsyncHelper->bCancel)
	{
		for (int32 SourceIndex = 0; SourceIndex < AsyncHelper->SourceDatas.Num(); ++SourceIndex)
		{
			TArray<FTaskData> SourceTaskDatas;

			if (!AsyncHelper->BaseNodeContainers.IsValidIndex(SourceIndex))
			{
				continue;
			}

			UInterchangeBaseNodeContainer* BaseNodeContainer = AsyncHelper->BaseNodeContainers[SourceIndex].Get();
			if (!BaseNodeContainer)
			{
				continue;
			}

			//Translation and pipelines are not executed, compute the children cache for translated and factory nodes
			BaseNodeContainer->ComputeChildrenCache();

			const bool bCanImportSceneNode = AsyncHelper->TaskData.ImportType == EImportType::ImportType_Scene;
			BaseNodeContainer->IterateNodesOfType<UInterchangeFactoryBaseNode>([&](const FString& NodeUID, UInterchangeFactoryBaseNode* FactoryNode)
			{
				UClass* ObjectClass = FactoryNode->GetObjectClass();
				if (ObjectClass != nullptr)
				{
					const UClass* RegisteredFactoryClass = InterchangeManager->GetRegisteredFactoryClass(ObjectClass);

					const bool bIsSceneNode = FactoryNode->GetObjectClass()->IsChildOf<AActor>() || FactoryNode->GetObjectClass()->IsChildOf<UActorComponent>();

					if (!RegisteredFactoryClass || (bIsSceneNode && !bCanImportSceneNode))
					{
						//nothing we can import from this element
						return;
					}

					FTaskData& NodeTaskData = SourceTaskDatas.AddDefaulted_GetRef();
					NodeTaskData.UniqueID = FactoryNode->GetUniqueID();
					NodeTaskData.SourceIndex = SourceIndex;
					NodeTaskData.bIsSceneNode = bIsSceneNode;
					NodeTaskData.Nodes.Add(FactoryNode);
					FactoryNode->GetFactoryDependencies(NodeTaskData.Dependencies);
					NodeTaskData.FactoryClass = RegisteredFactoryClass;
				}
			});

			{
				FNodeDependencyCache DependencyCache;
				//Reserving the cache will allow us to use reference when sorting the dependencies.
				DependencyCache.CacheAccumulatedDependencies(BaseNodeContainer, SourceTaskDatas);

				//Sort per dependencies
				auto SortByDependencies =
					[&BaseNodeContainer, &DependencyCache](const FTaskData& A, const FTaskData& B)
				{
					const TSet<FString>& BDependencies = DependencyCache.GetAccumulatedDependencies(BaseNodeContainer, B.UniqueID);
					const TSet<FString>& ADependencies = DependencyCache.GetAccumulatedDependencies(BaseNodeContainer, A.UniqueID);

					if (ADependencies.Num() == BDependencies.Num())
					{
						return A.UniqueID < B.UniqueID;
					}

					return ADependencies.Num() < BDependencies.Num();
				};

				// Nodes cannot depend on a node from another source, so it's faster to sort the dependencies per-source and then append those to the TaskData arrays.
				SourceTaskDatas.Sort(SortByDependencies);
			}

			TaskDatas.Append(MoveTemp(SourceTaskDatas));
		}
	}

	auto CreateTasksForEachTaskData = [](TArray<FTaskData>& TaskDatas, TFunction<uint64(FTaskData&)> CreateTasksFunc) -> TArray<uint64>
	{
		TArray<uint64> GraphEvents;

		for (int32 TaskIndex = 0; TaskIndex < TaskDatas.Num(); ++TaskIndex)
		{
			FTaskData& TaskData = TaskDatas[TaskIndex];

			if (TaskData.Dependencies.Num() > 0)
			{
				//Search the previous node to find the dependence
				for (int32 DepTaskIndex = 0; DepTaskIndex < TaskIndex; ++DepTaskIndex)
				{
					if (TaskData.Dependencies.Contains(TaskDatas[DepTaskIndex].UniqueID))
					{
						//Add has prerequisite
						TaskData.Prerequisites.Add(TaskDatas[DepTaskIndex].GraphEventRef);
					}
				}
			}

			TaskData.GraphEventRef = CreateTasksFunc(TaskData);
			GraphEvents.Add(TaskData.GraphEventRef);
		}

		return GraphEvents;
	};

	struct FTaskParsingRenameInfo
	{
		UInterchangeFactoryBaseNode* FactoryNode = nullptr;
		int32 SourceIndex = INDEX_NONE;
		FString OriginalName = FString();
		FString NewName = FString();
	};

	TMap<FString, FTaskParsingRenameInfo> RenameAssets;
	TSet<FString> CreatedTasksAssetNames; // Tracks for which asset name we have created a task so that we don't have 2 tasks for the same asset name
	TFunction<uint64(FTaskData&)> CreateTasksFromData = [this, &AsyncHelper, &RenameAssets, &CreatedTasksAssetNames](FTaskData& TaskData)
	{
		LLM_SCOPE_BYNAME(TEXT("Interchange"));
		check(TaskData.Nodes.Num() == 1); //We expect 1 node per asset task

		const int32 SourceIndex = TaskData.SourceIndex;
		const UClass* const FactoryClass = TaskData.FactoryClass;
		UInterchangeFactoryBaseNode* FactoryNode = TaskData.Nodes[0];

		if (TaskData.bIsSceneNode)
		{
			TSharedPtr<UE::Interchange::FTaskCreateSceneObjects_GameThread, ESPMode::ThreadSafe> TaskCreateSceneObjects = MakeShared<UE::Interchange::FTaskCreateSceneObjects_GameThread, ESPMode::ThreadSafe>(AsyncHelper->ContentBasePath
				, SourceIndex
				, WeakAsyncHelper
				, TaskData.Nodes
				, FactoryClass);

			return AsyncHelper->SceneTasks.Add_GetRef(UE::Interchange::FInterchangeTaskSystem::Get().AddTask(TaskCreateSceneObjects, TaskData.Prerequisites));
		}
		else
		{
			FString PackageSubPath;
			FactoryNode->GetCustomSubPath(PackageSubPath);

			FString AssetFullPath = FPaths::Combine(AsyncHelper->ContentBasePath, PackageSubPath, FactoryNode->GetAssetName());

			//Make sure there is no duplicate name full path
			uint32 NameIndex = 1;
			FString NewName = AssetFullPath;
			bool NameClash = FactoryNode->IsEnabled();
			
			while (NameClash)
			{
				if (CreatedTasksAssetNames.Contains(NewName))
				{
					const FString NameIndexString = FString::FromInt(NameIndex++);
					NewName = AssetFullPath + NameIndexString;
					FTaskParsingRenameInfo& RenameInfo = RenameAssets.FindOrAdd(AssetFullPath);
					RenameInfo.FactoryNode = FactoryNode;
					RenameInfo.OriginalName = AssetFullPath;
					RenameInfo.NewName = NewName;
					RenameInfo.SourceIndex = SourceIndex;
					FactoryNode->SetDisplayLabel(FactoryNode->GetDisplayLabel() + NameIndexString);
					if (FactoryNode->HasAttribute(UE::Interchange::FBaseNodeStaticData::AssetNameKey()))
					{
						//TextureFactorNodes automatically set up the AssetNames which then are used for asset creation:
						FactoryNode->SetAssetName(FactoryNode->GetDisplayLabel());
					}
				}
				else
				{
					AssetFullPath = NewName;
					NameClash = false;
				}
			}

			if (!FactoryNode->IsEnabled() || ensureMsgf(!CreatedTasksAssetNames.Contains(AssetFullPath),
				TEXT("Found multiple task data with the same asset name (%s). Only one will be executed."), *AssetFullPath))
			{
				LLM_SCOPE_BYNAME(TEXT("Interchange"));
				
				// Create factory
				UInterchangeFactoryBase* Factory = NewObject<UInterchangeFactoryBase>(GetTransientPackage(), FactoryClass);
				Factory->SetResultsContainer(AsyncHelper->AssetImportResult->GetResults());
				AsyncHelper->AddCreatedFactory(FactoryNode->GetUniqueID(), Factory);

				//If the factory wants to get async payloads it can add the task here so the payloads are in the dependency graph
				TArray<uint64> ImportObjectQueryPayloadsTasksPrerequistes;
				{
					TArray<TSharedPtr<FInterchangeTaskBase>> PayloadTasks;
					UInterchangeFactoryBase::FImportAssetObjectParams ImportAssetObjectParams;
					ImportAssetObjectParams.AssetName = FactoryNode->GetDisplayLabel();
					ImportAssetObjectParams.AssetNode = FactoryNode;
					ImportAssetObjectParams.NodeContainer = AsyncHelper->BaseNodeContainers[SourceIndex].Get();
					ImportAssetObjectParams.SourceData = AsyncHelper->SourceDatas[SourceIndex];
					ImportAssetObjectParams.Translator = AsyncHelper->Translators[SourceIndex];
					bool bAsyncPayloads = !AsyncHelper->bRunSynchronous;
					if (!ImportAssetObjectParams.Translator || !ImportAssetObjectParams.Translator->IsThreadSafe())
					{
						//Translator not thread safe cannot get payloads asynchronously
						bAsyncPayloads = false;
					}

					if (!AsyncHelper->bRuntimeOrPIE || FactoryNode->IsRuntimeImportAllowed())
					{
						//Only create payloads if it's allowed.
						Factory->CreatePayloadTasks(ImportAssetObjectParams, bAsyncPayloads, PayloadTasks);
					}
					
					if (!PayloadTasks.IsEmpty())
					{
						for (TSharedPtr<FInterchangeTaskBase> PayloadTask : PayloadTasks)
						{
							int32 ImportObjectQueryPayloadsTaskIndex = AsyncHelper->ImportObjectQueryPayloadsTasks.Add(FInterchangeTaskSystem::Get().AddTask(PayloadTask, TaskData.Prerequisites));
							ImportObjectQueryPayloadsTasksPrerequistes.Add(AsyncHelper->ImportObjectQueryPayloadsTasks[ImportObjectQueryPayloadsTaskIndex]);
						}
					}
					else
					{
						ImportObjectQueryPayloadsTasksPrerequistes = TaskData.Prerequisites;
					}
				}

				TArray<uint64> ImportObjectTasksPrerequistes;
				TSharedPtr<FTaskImportObject_GameThread, ESPMode::ThreadSafe> TaskImportObject_GameThread = MakeShared<FTaskImportObject_GameThread, ESPMode::ThreadSafe>(AsyncHelper->ContentBasePath
					, SourceIndex
					, WeakAsyncHelper
					, FactoryNode
					, FactoryClass);
				int32 BeginImportObjectTaskIndex = AsyncHelper->BeginImportObjectTasks.Add(FInterchangeTaskSystem::Get().AddTask(TaskImportObject_GameThread, ImportObjectQueryPayloadsTasksPrerequistes));
				ImportObjectTasksPrerequistes.Add(AsyncHelper->BeginImportObjectTasks[BeginImportObjectTaskIndex]);

				TSharedPtr<FTaskImportObject_Async, ESPMode::ThreadSafe> TaskImportObject_Async = MakeShared<FTaskImportObject_Async, ESPMode::ThreadSafe>(AsyncHelper->ContentBasePath
					, SourceIndex
					, WeakAsyncHelper
					, FactoryNode);
				int32 ImportObjectTaskIndex = AsyncHelper->ImportObjectTasks.Add(FInterchangeTaskSystem::Get().AddTask(TaskImportObject_Async, ImportObjectTasksPrerequistes));
				
				TArray<uint64> FinalizeImportObjectTasksPrerequistes;
				FinalizeImportObjectTasksPrerequistes.Add(AsyncHelper->ImportObjectTasks[ImportObjectTaskIndex]);

				
				
				TSharedPtr<FTaskImportObjectFinalize_GameThread, ESPMode::ThreadSafe> TaskImportObjectFinalize_GameThread = MakeShared<FTaskImportObjectFinalize_GameThread, ESPMode::ThreadSafe>(AsyncHelper->ContentBasePath
					, SourceIndex
					, WeakAsyncHelper
					, FactoryNode);
				int32 FinalizeCreateTaskIndex = AsyncHelper->FinalizeImportObjectTasks.Add(FInterchangeTaskSystem::Get().AddTask(TaskImportObjectFinalize_GameThread, FinalizeImportObjectTasksPrerequistes));

				//Only add the name if the factory node is enabled
				if (FactoryNode->IsEnabled())
				{
					CreatedTasksAssetNames.Add(AssetFullPath);
				}

				return AsyncHelper->FinalizeImportObjectTasks[FinalizeCreateTaskIndex];
			}
			else
			{
				constexpr uint64 EmptyGraphEvent = 0;
				return EmptyGraphEvent;
			}
		}
	};

	//If we find nothing to import, we notify the user
	if (TaskDatas.Num() == 0)
	{
		for(TObjectPtr<UInterchangeTranslatorBase> Translator : AsyncHelper->Translators)
		{
			//We need a valid translator to add a result message
			if (!Translator || AsyncHelper->bCancel)
			{
				continue;
			}
			UInterchangeResultError_Generic* ErrorResult = NewObject<UInterchangeResultError_Generic>(GetTransientPackage(), UInterchangeResultError_Generic::StaticClass());
			ErrorResult->Text = NSLOCTEXT("InterchangeTaskParsing", "TranslatorNoAssetImported", "There was no data to import in the provided source data.");
			ErrorResult->SourceAssetName = Translator->GetSourceData()->GetFilename();
			Translator->AddMessage(ErrorResult);
		}
	}

	TArray<uint64> CompletionPrerequistes;
	const int32 PoolWorkerThreadCount = FTaskGraphInterface::Get().GetNumWorkerThreads() / 2;
	const int32 MaxNumWorker = FMath::Max(PoolWorkerThreadCount, 1);
	TArray<uint64> GroupPrerequistes;
	for (int32 TaskIndex = 0; TaskIndex < TaskDatas.Num(); ++TaskIndex)
	{
		FTaskData& TaskData = TaskDatas[TaskIndex];

		if (TaskData.Dependencies.Num() > 0)
		{
			//Search the previous node to find the dependence
			for (int32 DepTaskIndex = 0; DepTaskIndex < TaskIndex; ++DepTaskIndex)
			{
				if (TaskData.Dependencies.Contains(TaskDatas[DepTaskIndex].UniqueID))
				{
					//Add has prerequisite
					TaskData.Prerequisites.Add(TaskDatas[DepTaskIndex].GraphEventRef);
				}
			}
		}

		//Append the group prerequisite to the task data prerequisite if the group is full
		//This allow to chain the group dependencies to control the number of task
		if (GroupPrerequistes.Num() >= MaxNumWorker)
		{
			TaskData.Prerequisites.Append(GroupPrerequistes);
			GroupPrerequistes.Reset();
		}
		TaskData.GraphEventRef = CreateTasksFromData(TaskData);
		GroupPrerequistes.Add(TaskData.GraphEventRef);
	}
	CompletionPrerequistes.Append(GroupPrerequistes);

	if (!RenameAssets.IsEmpty())
	{
		TMap<TWeakObjectPtr<UInterchangeTranslatorBase>, FString> TranslatorMessageMap;
		for (const TPair<FString, FTaskParsingRenameInfo>& RenameAssetKvp : RenameAssets)
		{
			const FTaskParsingRenameInfo& RenameInfo = RenameAssetKvp.Value;
			FString& Message = TranslatorMessageMap.FindOrAdd(AsyncHelper->Translators[RenameInfo.SourceIndex]);
			Message += FText::Format(NSLOCTEXT("InterchangeTaskParsingDoTask", "RenamedAssetMessagePerAsset", "\n OriginalName:[{0}] NewName:[{1}]")
				, FText::FromString(RenameInfo.OriginalName)
				, FText::FromString(RenameInfo.NewName)).ToString();
		}
		for (const TPair<TWeakObjectPtr<UInterchangeTranslatorBase>, FString>& MessagePerTranslator : TranslatorMessageMap)
		{

			FText ResultText = FText::Format(NSLOCTEXT("InterchangeTaskParsingDoTask", "RenamedAssetsMessageHeader", "Renamed Assets: {0}")
				, FText::FromString(MessagePerTranslator.Value));

			if (GIsAutomationTesting)
			{
				UInterchangeResultDisplay_Generic* DisplayResult = NewObject<UInterchangeResultDisplay_Generic>(GetTransientPackage(), UInterchangeResultDisplay_Generic::StaticClass());
				DisplayResult->Text = ResultText;
				MessagePerTranslator.Key->AddMessage(DisplayResult);
			}
			else
			{
				UInterchangeResultWarning_Generic* WarningResult = NewObject<UInterchangeResultWarning_Generic>(GetTransientPackage(), UInterchangeResultWarning_Generic::StaticClass());
				WarningResult->Text = ResultText;
				MessagePerTranslator.Key->AddMessage(WarningResult);
			}
		}
	}

	//Add an async task for pre completion
	TArray<uint64> PreCompletionPrerequistes;
	TSharedPtr<FTaskPreCompletion_GameThread, ESPMode::ThreadSafe> TaskPreCompletion = MakeShared<FTaskPreCompletion_GameThread, ESPMode::ThreadSafe>(InterchangeManager, WeakAsyncHelper);
	AsyncHelper->PreCompletionTask = FInterchangeTaskSystem::Get().AddTask(TaskPreCompletion, CompletionPrerequistes);
	PreCompletionPrerequistes.Add(AsyncHelper->PreCompletionTask);

	TArray<uint64> AssetCompilationPrerequistes;
	//Start the wait for asset compilation task on a async task
	for (int32 SourceIndex = 0; SourceIndex < AsyncHelper->SourceDatas.Num(); ++SourceIndex)
	{
		TSharedPtr<FTaskWaitAssetCompilation_GameThread, ESPMode::ThreadSafe> TaskWaitAssetCompilation = MakeShared<FTaskWaitAssetCompilation_GameThread, ESPMode::ThreadSafe>(SourceIndex, WeakAsyncHelper);
		int32 WaitAssetCompilationTaskIndex = AsyncHelper->WaitAssetCompilationTasks.Add(FInterchangeTaskSystem::Get().AddTask(TaskWaitAssetCompilation, PreCompletionPrerequistes));
		AssetCompilationPrerequistes.Add(AsyncHelper->WaitAssetCompilationTasks[WaitAssetCompilationTaskIndex]);
	}

	//Start the Post pipeline task
	for (int32 SourceIndex = 0; SourceIndex < AsyncHelper->SourceDatas.Num(); ++SourceIndex)
	{

		TSharedPtr<FTaskPostImport_GameThread, ESPMode::ThreadSafe> TaskPostImport = MakeShared<FTaskPostImport_GameThread, ESPMode::ThreadSafe>(SourceIndex, WeakAsyncHelper);
		int32 PostImportTaskIndex = AsyncHelper->PostImportTasks.Add(FInterchangeTaskSystem::Get().AddTask(TaskPostImport, AssetCompilationPrerequistes));

		//Ensure we run the pipeline in the same order we create the task, since the pipeline modifies the node container, its important that its not processed in parallel, Adding the one we start to the prerequisites
		//is the way to go here
		AssetCompilationPrerequistes.Add(AsyncHelper->PostImportTasks[PostImportTaskIndex]);
	}

	TSharedPtr<FTaskCompletion_GameThread, ESPMode::ThreadSafe> TaskCompletion = MakeShared<FTaskCompletion_GameThread, ESPMode::ThreadSafe>(InterchangeManager, WeakAsyncHelper);
	AsyncHelper->CompletionTask = FInterchangeTaskSystem::Get().AddTask(TaskCompletion, AssetCompilationPrerequistes);
}
