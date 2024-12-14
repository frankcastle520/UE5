// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Modules/ModuleInterface.h"
#include "Features/IModularFeatures.h"
#include "Async/Async.h"

struct FDistributedBuildTaskResult
{
	int32 ReturnCode;
	bool bCompleted;
};

struct FDistributedBuildStats
{
	uint32 MaxRemoteAgents = 0;
	uint32 MaxActiveAgentCores = 0;
};

struct FTaskCommandData
{	
	FString Command;
	FString WorkingDirectory;
	FString InputFileName;
	FString OutputFileName;
	FString ExtraCommandArgs;
	FString Description; // Optional string describing the task. Shows up in UBA trace files for each job.
	uint32 DispatcherPID = 0;
	TArray<FString> Dependencies;
};

struct FTask
{
	uint32 ID;
	FTaskCommandData CommandData;
	TPromise<FDistributedBuildTaskResult> Promise;

	FTask(uint32 ID, const FTaskCommandData& CommandData, TPromise<FDistributedBuildTaskResult>&& Promise)
        : ID(ID)
        , CommandData(CommandData)
        , Promise(MoveTemp(Promise))
	{}
};

struct FTaskResponse
{
	uint32 ID;
	int32 ReturnCode;
};

class IDistributedBuildController : public IModuleInterface, public IModularFeature
{
public:
	virtual bool SupportsDynamicReloading() override { return false; }
	
	virtual bool RequiresRelativePaths() { return false; }

	virtual void InitializeController() = 0;
	
	// Returns true if the controller may be used.
	virtual bool IsSupported() = 0;

	// Returns the name of the controller. Used for logging purposes.
	virtual const FString GetName() = 0;

	virtual FString RemapPath(const FString& SourcePath) const { return SourcePath; }

	virtual void Tick(float DeltaSeconds){}

	// Returns a new file path to be used for writing input data to.
	virtual FString CreateUniqueFilePath() = 0;

	// Returns the distributed build statistics since the last call and resets its internal values. Returns false if there are no statistics provided.
	virtual bool PollStats(FDistributedBuildStats& OutStats) { return false; }

	// Launches a task. Returns a future which can be waited on for the results.
	virtual TFuture<FDistributedBuildTaskResult> EnqueueTask(const FTaskCommandData& CommandData) = 0;
	
	static const FName& GetModularFeatureType()
	{
		static FName FeatureTypeName = FName(TEXT("DistributedBuildController"));
		return FeatureTypeName;
	}
};
