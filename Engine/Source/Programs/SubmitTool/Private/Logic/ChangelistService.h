// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ISourceControlProvider.h"
#include "ISourceControlChangelist.h"
#include "ISourceControlChangelistState.h"
#include "Containers/Ticker.h"
#include "Parameters/SubmitToolParameters.h"
#include "Services/Interfaces/ISubmitToolService.h"

struct FSourceControlResultInfo;
class ISTSourceControlService;
using FSourceControlOperationRef = TSharedRef<class ISourceControlOperation, ESPMode::ThreadSafe>;


DECLARE_DELEGATE(FOnP4OperationCompleteDelegate)
DECLARE_DELEGATE_OneParam(FOnChangeListReadyDelegate, bool /*bValidState*/)
DECLARE_DELEGATE_OneParam(FOnChangelistRefreshDelegate, ETaskArea /*ChangedValue*/)
DECLARE_MULTICAST_DELEGATE(FOnCLDescriptionUpdated)


class FChangelistService final : public ISubmitToolService
{
public:

	FChangelistService(const FGeneralParameters& InParameters, const TSharedPtr<ISTSourceControlService> SourceControlService, const FOnChangeListReadyDelegate& InCLReadyCallback, const FOnChangelistRefreshDelegate& InCLRefreshCallback);
	~FChangelistService();

	const FString GetCLID() const
	{
		if(ChangelistPtr.IsValid())
		{
			return ChangelistPtr->GetIdentifier();
		} 
		
		if(!CLID.IsEmpty())
		{
			return CLID;
		}

		return TEXT("Invalid");
	}

	const FString& GetCLDescription()
	{
		return CLDescription;
	}

	bool SetCLDescription(const FString& newDescription, bool bNotifyEvent = false)
	{
		FString lineEndReplaced = newDescription.Replace(TEXT("\r\n"), TEXT("\n"));

		if(CLDescription.Equals(lineEndReplaced, ESearchCase::IgnoreCase))
		{
			return false;
		}

		CLDescription = lineEndReplaced;

		if(bNotifyEvent)
		{
			OnCLDescriptionUpdated.Broadcast();
		}

		return true;
	}

	const TArray<FSourceControlStateRef>& GetFilesInCL() const
	{
		return FilesInCL;
	}

	const TArray<FSourceControlStateRef>& GetShelvedFilesInCL() const
	{
		return ShelvedFilesInCL;
	}

	bool HasShelvedFiles() const
	{
		return ShelvedFilesInCL.Num() != 0;
	}

	bool HasP4OperationsRunning() const
	{
		return ActiveP4Operations.Num() > 0;
	}


	void Init();
	void Submit(const FString& InDescriptionAddendum = TEXT(""), const FSourceControlOperationComplete& OnSubmitComplete = FSourceControlOperationComplete());
	void CreateCLFromDefaultCL();
	void FetchChangelistDataAsync();
	void RevertUnchangedFilesAsync(const FSourceControlOperationComplete& OnRevertComplete = FSourceControlOperationComplete());
	void SendCLDescriptionToP4(EConcurrency::Type Concurrency = EConcurrency::Asynchronous, FOnP4OperationCompleteDelegate InCallback = nullptr);
	void DeleteShelvedFiles(const FSourceControlOperationComplete& OnDeleteComplete);
	void CreateShelvedFiles(const FSourceControlOperationComplete& OnCreateComplete);
	bool P4Tick(float DeltaTime);

	bool IsP4OperationRunning(FName OperationName = FName());
	void CancelP4Operations(FName OperationName = FName());

	FOnCLDescriptionUpdated OnCLDescriptionUpdated;

	TArray<FSourceControlChangelistStatePtr> GetOtherChangelistsStates();

	const TArray<FString>& GetFilesDepotPaths(bool bForce = false);
	const TArray<FString>& GetShelvedFilesDepotPaths(bool bForce = false);

private:
	mutable FCriticalSection Mutex;

	FString CLID;
	FString CurrentStream;
	const FGeneralParameters& Parameters;
	const FOnChangeListReadyDelegate& CLReadyCallback;
	const FOnChangelistRefreshDelegate& CLRefreshCallback;

	const FTSTicker::FDelegateHandle TickHandle;

	TArray<TSharedRef<ISourceControlOperation>> ActiveP4Operations;

	FSourceControlChangelistPtr ChangelistPtr = nullptr;
	TArray<FSourceControlStateRef> FilesInCL;
	TArray<FSourceControlStateRef> ShelvedFilesInCL;
	TArray<FString> FilesDepotPaths;
	TArray<FString> ShelvedFilesDepotPaths;
	FSourceControlChangelistStatePtr ChangelistState = nullptr;
	FText OriginalDescription;
	FString CLDescription;
	ISourceControlProvider* CachedSCCProvider;
	const TSharedPtr<ISTSourceControlService> SourceControlService;

	void FindInitialChangelistsAsync();

	void FetchChangelistCallback(const FSourceControlOperationRef& UpdateOperation, ECommandResult::Type Result);
	void RehydrateDataFromP4State();
	bool AreCLDescriptionsIdentical();
	void PrintFilesAndShelvedFiles();

	void PrintMessages(const FSourceControlResultInfo& ResultInfo) const;
	void PrintErrorMessages(const FSourceControlResultInfo& ResultInfo) const;


};

Expose_TNameOf(FChangelistService);