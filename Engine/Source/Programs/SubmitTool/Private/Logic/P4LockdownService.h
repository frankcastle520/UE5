// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Parameters/SubmitToolParameters.h"
#include "Logic/ProcessWrapper.h"
#include "Tasks/Task.h"
#include "Services/Interfaces/ISubmitToolService.h"
#include "Memory/SharedBuffer.h"

DECLARE_DELEGATE_OneParam(FOnConfigFileRetrieved, bool /*bValid*/)

class FSubmitToolServiceProvider;

class FP4LockdownService : public ISubmitToolService
{	
public:
	FP4LockdownService() = delete;
	FP4LockdownService(const FP4LockdownParameters& InParameters, TSharedPtr<FSubmitToolServiceProvider> InServiceProvider);
	bool ArePathsInLockdown(const TArray<FString>& InPaths, bool& bOutAllowlisted);
	bool IsBlockingOperationRunning() const
	{
		return ConfigFilesTask.IsValid() && !ConfigFilesTask.IsCompleted();
	}

private:

	struct FAllowListData
	{
		FString GroupName;
		TSet<FString> AllowListers;
		TArray<TPair<bool, FString>> Views;
	};

	struct FOverrideData : public FAllowListData
	{
		TSet<FString> Sections;
	};

	struct FPathLockdownResult
	{
		bool bIsLocked;
		bool bAllowlisted;
	};

	FPathLockdownResult IsPathInLockdown(const FString& InPath) const;
	bool GetConfigFile(const FString& InConfigId, const FString& InDepotPath);
	void ParseAllowListData();
	void GetAdditionalHardlockedPaths();
	FString GetFilePath(const FString& InConfigId);

	TMap<FString, FSharedBuffer> DownloadedFiles;
	UE::Tasks::TTask<bool> ConfigFilesTask;
	TSharedPtr<FSubmitToolServiceProvider> ServiceProvider;
	const FP4LockdownParameters& Parameters;
	TArray<FAllowListData> AllowListData;
	TArray<FOverrideData> OverrideData;
	TArray<FString> AdditionalHardlocks;
	FCriticalSection FileMutex;
};

Expose_TNameOf(FP4LockdownService);