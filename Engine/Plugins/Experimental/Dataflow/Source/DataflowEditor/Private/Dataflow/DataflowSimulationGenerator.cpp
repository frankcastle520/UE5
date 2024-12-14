// Copyright Epic Games, Inc. All Rights Reserved.

#include "Dataflow/DataflowSimulationGenerator.h"
#include "Dataflow/DataflowSimulationManager.h"
#include "Dataflow/DataflowSimulationControls.h"
#include "Dataflow/DataflowSimulationUtils.h"
#include "Chaos/CacheManagerActor.h"
#include "Chaos/CacheCollection.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "Misc/AsyncTaskNotification.h" 

DEFINE_LOG_CATEGORY(LogDataflowSimulationGenerator);

#define LOCTEXT_NAMESPACE "DataflowSimulationGenerator"

namespace UE::Dataflow
{
void FDataflowSimulationTask::DoWork()
{
	const int32 StartFrame = bAsyncCaching ? 1 : 0;
	const int32 NumFrames = (MaxTime-MinTime) / DeltaTime;
	for (int32 FrameIndex = StartFrame; FrameIndex < NumFrames; ++FrameIndex)
	{
		if (!TaskManager->bCancelled.load())
		{
			// Compute the simulation time that will be sent to the graph
			const float SimulationTime = MinTime + (FrameIndex+1) * DeltaTime;

			if(bAsyncCaching)
			{
				// Compute all the skelmesh animations at the simulation time
				UE::Dataflow::ComputeSkeletonAnimation(TaskManager->PreviewActor, SimulationTime);
				
				// Background task : run directly the advance simulation data without coming back to the game thread
				UDataflowSimulationManager* DataflowManager = SimulationWorld->GetSubsystem<UDataflowSimulationManager>();

				// Pre advance the proxies
				DataflowManager->ReadSimulationInterfaces(DeltaTime, true);

				// Advance the simulation proxies
                DataflowManager->AdvanceSimulationProxies(DeltaTime, SimulationTime);

				// Post advance the simulation proxies
				DataflowManager->WriteSimulationInterfaces(DeltaTime, true);
			}
			else
			{
				// Update all the skelmesh animations at the simulation time
				UE::Dataflow::UpdateSkeletonAnimation(TaskManager->PreviewActor, SimulationTime);
				
				// Foreground task : Run the world ticking 
				SimulationWorld->Tick(ELevelTick::LEVELTICK_All, DeltaTime);
			}

			// Finish the frame
			TaskManager->SimulationResource->FinishFrame();
		}
		else
		{
			break;
		}
	}
}

bool FDataflowTaskManager::AllocateSimulationResource(const FVector2f& TimeRange, const int32 FrameRate,
		const TObjectPtr<UChaosCacheCollection>& CacheAsset, const TSubclassOf<AActor>& ActorClass,
		const TObjectPtr<UDataflowBaseContent>& DataflowContent, const FTransform& BlueprintTransform, const bool bSkeletalMeshVisibility)
{
	SimulationWorld = UWorld::CreateWorld(EWorldType::Editor, false);
	SimulationWorld->bPostTickComponentUpdate = false;

	FWorldContext& WorldContext = GEngine->CreateNewWorldContext( SimulationWorld->WorldType );
	WorldContext.SetCurrentWorld(SimulationWorld);
	
	CacheManager = SimulationWorld->SpawnActor<AChaosCacheManager>();

	PreviewActor = UE::Dataflow::SpawnSimulatedActor(ActorClass, CacheManager, CacheAsset, true, DataflowContent, BlueprintTransform);
	UE::Dataflow::SetupSkeletonAnimation(PreviewActor, bSkeletalMeshVisibility);

	// Init the cache manager
	CacheManager->SetObservedComponentProperties(CacheManager->CacheMode);
	CacheManager->BeginEvaluate();
	
	SimulationResource = MakeShared<FDataflowSimulationResource>();
	SimulationResource->NumSimulatedFrames = &NumSimulatedFrames;
	SimulationResource->bCancelled = &bCancelled;
	
	NumFrames = FMath::Floor((TimeRange[1] - TimeRange[0]) * FrameRate);
	if(SimulationTask.IsValid())
	{
		SimulationTask->GetTask().SimulationWorld = SimulationWorld;
		SimulationTask->GetTask().MinTime = TimeRange[0];
		SimulationTask->GetTask().MaxTime = TimeRange[1];
		SimulationTask->GetTask().DeltaTime = (TimeRange[1]-TimeRange[0]) / NumFrames;

		UDataflowSimulationManager* DataflowManager = SimulationWorld->GetSubsystem<UDataflowSimulationManager>();
		if(SimulationTask->GetTask().bAsyncCaching)
		{
			// Update all the skelmesh animations at the simulation time
			UE::Dataflow::UpdateSkeletonAnimation(PreviewActor, SimulationTask->GetTask().MinTime+SimulationTask->GetTask().DeltaTime);
				
			// Foreground task : Run the world ticking 
			SimulationWorld->Tick(ELevelTick::LEVELTICK_All, SimulationTask->GetTask().DeltaTime);

			// Init simulation proxies from interface
			DataflowManager->InitSimulationInterfaces();
		}
		DataflowManager->SetSimulationEnabled(!SimulationTask->GetTask().bAsyncCaching);
	}

	return true;
}

void FDataflowTaskManager::FreeSimulationResource()
{
	if (SimulationTask.IsValid())
	{
		SimulationTask->EnsureCompletion();

		if(SimulationTask->GetTask().bAsyncCaching)
		{
			UDataflowSimulationManager* DataflowManager = SimulationWorld->GetSubsystem<UDataflowSimulationManager>();
			DataflowManager->ResetSimulationInterfaces();
		}
	}

	if(CacheManager)
	{
		CacheManager->EndEvaluate();

		//  Clear the observed components
        CacheManager->ClearObservedComponents();
		SimulationWorld->DestroyActor(CacheManager);
	}
	
	SimulationResource.Reset();

	GEngine->DestroyWorldContext(SimulationWorld);
	SimulationWorld->DestroyWorld(false);
}

void FDataflowTaskManager::CancelSimulationGeneration()
{
	bCancelled.store(true);
	SimulationTask->TryAbandonTask();
}

FDataflowSimulationGenerator::FDataflowSimulationGenerator()
{}

FDataflowSimulationGenerator::~FDataflowSimulationGenerator()
{
	if (TaskManager != nullptr)
	{
		TaskManager->FreeSimulationResource();
	}
}

void FDataflowSimulationGenerator::Tick(float DeltaTime)
{
	if (PendingAction == EDataflowGeneratorActions::StartGenerate)
	{
		StartGenerateSimulation();
	}
	else if (PendingAction == EDataflowGeneratorActions::TickGenerate)
	{
		TickGenerateSimulation();
	}
}

TStatId FDataflowSimulationGenerator::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(FDataflowSimulationGenerator, STATGROUP_Tickables);
}

void FDataflowSimulationGenerator::StartGenerateSimulation()
{
	check(PendingAction == EDataflowGeneratorActions::StartGenerate);
	
	if (TaskManager != nullptr)
	{
		UE_LOG(LogDataflowSimulationGenerator, Error, TEXT("Previous generation is still running."));
		PendingAction = EDataflowGeneratorActions::NoAction;
		return;
	}
	TaskManager = TSharedPtr<FDataflowTaskManager>(new FDataflowTaskManager);

	TaskManager->SimulationTask = MakeUnique<FAsyncTask<FDataflowSimulationTask>>();
	TaskManager->SimulationTask->GetTask().TaskManager = TaskManager;
	TaskManager->SimulationTask->GetTask().bAsyncCaching = CacheParams.bAsyncCaching;
	
	TaskManager->AllocateSimulationResource(CacheParams.TimeRange, CacheParams.FrameRate, CacheAsset, BlueprintClass, DataflowContent, BlueprintTransform, bSkeletalMeshVisibility);

	if(CacheParams.bAsyncCaching)
	{
		TaskManager->SimulationTask->StartBackgroundTask();
	}
	else
	{
		TaskManager->SimulationTask->StartSynchronousTask();
	}

	FAsyncTaskNotificationConfig NotificationConfig;
	NotificationConfig.TitleText = LOCTEXT("SimulateDataflow", "Simulating Dataflow Content");
	NotificationConfig.ProgressText = FText::FromString(TEXT("0%"));
	NotificationConfig.bCanCancel = true;
	NotificationConfig.bKeepOpenOnSuccess = true;
	NotificationConfig.bKeepOpenOnFailure = true;
	TaskManager->AsyncNotification = MakeUnique<FAsyncTaskNotification>(NotificationConfig);
	TaskManager->StartTime = FDateTime::UtcNow();
	TaskManager->LastUpdateTime = TaskManager->StartTime;

	PendingAction = EDataflowGeneratorActions::TickGenerate;
}

void FDataflowSimulationGenerator::TickGenerateSimulation()
{
	check(PendingAction == EDataflowGeneratorActions::TickGenerate && TaskManager != nullptr);
		
	bool bFinished = false;
	const bool bCancelled = TaskManager->AsyncNotification->GetPromptAction() == EAsyncTaskNotificationPromptAction::Cancel;
	if (TaskManager->SimulationTask->IsDone())
	{
		bFinished = true;
	}
	else if (bCancelled)
	{
		TaskManager->CancelSimulationGeneration();
		bFinished = true;
	}
		
	if (!bFinished)
	{
		const FDateTime CurrentTime = FDateTime::UtcNow();
		const double SinceLastUpdate = (CurrentTime - TaskManager->LastUpdateTime).GetTotalSeconds();
		if (SinceLastUpdate < 0.2)
		{
			return;
		}
		
		const int32 NumSimulatedFrames = TaskManager->NumSimulatedFrames.load();
		const int32 NumTotalFrames = TaskManager->NumFrames;
		const FText ProgressMessage = FText::FromString(FString::Printf(TEXT("Finished %d/%d, %.1f%%"),
			NumSimulatedFrames, NumTotalFrames, 100.0 * NumSimulatedFrames / NumTotalFrames));
		TaskManager->AsyncNotification->SetProgressText(ProgressMessage);
		TaskManager->LastUpdateTime = CurrentTime;
	}
	else
	{
		FreeTaskResource(bCancelled);
		PendingAction = EDataflowGeneratorActions::NoAction;
	}
}
	
void FDataflowSimulationGenerator::SetCacheParams(const FDataflowPreviewCacheParams& InCacheParams)
{
	CacheParams = InCacheParams;
}

void FDataflowSimulationGenerator::SetCacheAsset(const TObjectPtr<UChaosCacheCollection>& InCacheAsset) 
{
	CacheAsset = InCacheAsset;
}

void FDataflowSimulationGenerator::SetBlueprintClass(const TSubclassOf<AActor>& InBlueprintClass)
{
	BlueprintClass = InBlueprintClass;
}

void FDataflowSimulationGenerator::SetBlueprintTransform(const FTransform& InBlueprintTransform)
{
	BlueprintTransform = InBlueprintTransform;
}

void FDataflowSimulationGenerator::SetDataflowContent(const TObjectPtr<UDataflowBaseContent>& InDataflowContent)
{
	DataflowContent = InDataflowContent;
}

void FDataflowSimulationGenerator::SetSkeletalMeshVisibility(const bool bInSkeletalMeshVisibility)
{
	bSkeletalMeshVisibility = bInSkeletalMeshVisibility;
}

void FDataflowSimulationGenerator::RequestGeneratorAction(EDataflowGeneratorActions ActionType)
{
	if (PendingAction != EDataflowGeneratorActions::NoAction)
	{
		return;
	}
	PendingAction = ActionType;
}

void FDataflowSimulationGenerator::FreeTaskResource(bool bCancelled)
{
	TaskManager->AsyncNotification->SetProgressText(LOCTEXT("Finishing", "Finishing, please wait"));
	TaskManager->FreeSimulationResource();
	const FDateTime CurrentTime = FDateTime::UtcNow();
	UE_LOG(LogDataflowSimulationGenerator, Log, TEXT("Simulation finished in %f seconds"), (CurrentTime - TaskManager->StartTime).GetTotalSeconds());

	{
		// TODO : save the result to the chaos cache collection
	}
	if (bCancelled)
	{
		TaskManager->AsyncNotification->SetProgressText(LOCTEXT("Cancelled", "Cancelled"));
		TaskManager->AsyncNotification->SetComplete(false);
	}
	else
	{
		TaskManager->AsyncNotification->SetProgressText(LOCTEXT("Finished", "Finished"));
		TaskManager->AsyncNotification->SetComplete(true);
	}
	TaskManager.Reset();
	CacheAsset->MarkPackageDirty();
}
};

#undef LOCTEXT_NAMESPACE
