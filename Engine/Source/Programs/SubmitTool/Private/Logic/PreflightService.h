// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "ChangelistService.h"
#include "Logic/ProcessWrapper.h"
#include "Logic/DialogFactory.h"
#include "Parameters/SubmitToolParameters.h"
#include "Models/PreflightData.h"
#include "Services/Interfaces/ISubmitToolService.h"

class FSubmitToolServiceProvider;
class FModelInterface;
class IHttpRequest;

enum class EPreflightServiceState
{
	Idle,
	RequestDeleteShelve,
	WaitingForDeleteShelve,
	RequestCreateShelve,
	WaitingForCreateShelve,
	StartPreflight,
	Error,
};


class FPreflightService final : public ISubmitToolService
{
public:
	FPreflightService() = delete;
	FPreflightService(
		const FHordeParameters& InSettings,
		FModelInterface* InModelInterface,
		TSharedRef<FSubmitToolServiceProvider> InServiceProvider);
	~FPreflightService();

	void RequestPreflight();
	void FetchPreflightInfo(bool bRequeue = false, const FString& InOAuthToken = TEXT(""));

	EPreflightServiceState GetState() const
	{
		return State;
	}
	bool IsRequestInProgress() const
	{
		return State != EPreflightServiceState::Idle;
	}
	const TUniquePtr<FPreflightList>& GetPreflightData() const
	{
		return HordePreflights;
	}
	const TMap<FString, FPreflightData>& GetUnlinkedPreflights() const
	{
		return UnlinkedHordePreflights;
	}
	const FString& GetHordeServerAddress() const
	{
		return Definition.HordeServerAddress;
	}

	FOnPreflightDataUpdated OnPreflightDataUpdated;

private:
	void QueueFetch(bool bRequeue, float InSeconds);

	void Requeue();

	void StartPreflight();
	bool SelectPreflightTemplate(FPreflightTemplateDefinition& OutTemplate) const;
	FString GetAdditionalTasksString(const FPreflightTemplateDefinition& InTemplate) const;

	void FetchUnlinkedPreflight(const FString& InPreflightId, bool bRequeue, const FString& InOAuthToken);

	EDialogFactoryResult ShowRecreateShelveDialog() const;
	EDialogFactoryResult ShowUpdatePreflightTagDialog() const;

	void OnDeleteShelveOperationComplete(const FSourceControlOperationRef& Operation, ECommandResult::Type Result);
	void OnCreateShelveOperationComplete(const FSourceControlOperationRef& Operation, ECommandResult::Type Result);

	bool Tick(float DeltaTime);

	// Definitions from the ini
	const FHordeParameters Definition;

	// services we depend on
	TSharedRef<FSubmitToolServiceProvider> ServiceProvider;
	FTag* PreflightTag;
	FModelInterface* ModelInterface;

	// local data
	bool bCheckShelveInstead = false;
	bool bStopAskingTagUpdate = false;
	FDateTime LastRequest = FDateTime::MinValue();
	EPreflightServiceState State;
	FTSTicker::FDelegateHandle TickHandle;
	FString LastErrorMessage;
	TUniquePtr<FPreflightList> HordePreflights;
	TMap<FString, FPreflightData> UnlinkedHordePreflights;

	FString StreamName;

	TMap<FString, FStringFormatArg> GetFormatParameters() const;

	// delete/create shelve callbacks
	FSourceControlOperationComplete DeleteShelveCallback;
	FSourceControlOperationComplete CreateShelveCallback;

	// get stream data process
	TUniquePtr<FProcessWrapper> GetStreamDataProcess;
	FOnCompleted OnGetStreamDataCompletedCallback;
	FOnOutputLine OnGetStreamDataOutputLineCallback;

	// Fetch Preflight
	TSharedPtr<IHttpRequest> LinkedPFRequest = nullptr;
	TMap<FString, TSharedPtr<IHttpRequest>> UnlinkedPFRequests;
	int8 ActiveUnlinkedRequests = 0;
};

Expose_TNameOf(FPreflightService);