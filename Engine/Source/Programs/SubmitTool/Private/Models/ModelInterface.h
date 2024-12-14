// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Logic/ChangelistService.h"
#include "Logic/TasksService.h"
#include "Logic/TagService.h"
#include "Logic/Services/Interfaces/ISTSourceControlService.h"
#include "Logic/JiraService.h"
#include "Logic/IntegrationService.h"
#include "Logic/SwarmService.h"
#include "Logic/PreflightService.h"
#include "Logic/P4LockdownService.h"
#include "Logic/CredentialsService.h"
#include "Logic/UpdateService.h"
#include "Logic/Services/SubmitToolServiceProvider.h"
#include "Models/Tag.h"
#include "Models/PreflightData.h"

#include "Parameters/SubmitToolParameters.h"

#include "ModelInterface.generated.h"
class SDockTab;

UENUM()
enum class ESubmitToolAppState : uint8 {
	None = 0,
	Initializing = 1,
	WaitingUserInput = 2,
	Errored = 3,
	P4BlockingOperation = 4,
	Submitting = 5,
	SubmitLocked = 6,
	Finished = 7,
};

namespace SubmitToolAppState {

	using StateList = const TArray<ESubmitToolAppState>;

	// Allowed states to transition from the original state.
	const TMap<const ESubmitToolAppState, StateList> AllowedTransitions =
	{
		{ ESubmitToolAppState::Initializing, StateList {
				ESubmitToolAppState::WaitingUserInput,
				ESubmitToolAppState::P4BlockingOperation,
				ESubmitToolAppState::Errored,
				ESubmitToolAppState::SubmitLocked
		}},
		{ ESubmitToolAppState::WaitingUserInput, StateList {
				ESubmitToolAppState::Submitting,
				ESubmitToolAppState::Errored,
				ESubmitToolAppState::P4BlockingOperation,
				ESubmitToolAppState::SubmitLocked
		}},
		{ ESubmitToolAppState::Errored, StateList {
			ESubmitToolAppState::WaitingUserInput,
			ESubmitToolAppState::SubmitLocked
		}},
		{ ESubmitToolAppState::P4BlockingOperation, StateList {
				ESubmitToolAppState::WaitingUserInput,
				ESubmitToolAppState::Errored,
				ESubmitToolAppState::SubmitLocked,
				ESubmitToolAppState::Finished
		}},
		{ ESubmitToolAppState::Submitting, StateList {
				ESubmitToolAppState::WaitingUserInput,
				ESubmitToolAppState::Errored,
				ESubmitToolAppState::SubmitLocked,
				ESubmitToolAppState::Finished
		}},
		{ ESubmitToolAppState::SubmitLocked, StateList {
				ESubmitToolAppState::WaitingUserInput,
				ESubmitToolAppState::SubmitLocked,
				ESubmitToolAppState::Errored,
				ESubmitToolAppState::Finished
		}},
		{ ESubmitToolAppState::Finished, StateList {
			ESubmitToolAppState::Errored
		}}
	};
}

DECLARE_MULTICAST_DELEGATE(FPreSubmitCallBack)
DECLARE_MULTICAST_DELEGATE(FFilesRefresh)

class FModelInterface
{
public:
	FModelInterface(const FSubmitToolParameters& InParameters);
	~FModelInterface();
	void Dispose() const;
	void ParseValidators() const;
	void ParsePreSubmitOperations() const;

	void SetMainTab(TSharedPtr<SDockTab> InMainTab)									{ MainTab = InMainTab; }
	TWeakPtr<SDockTab> GetMainTab() const											{ return MainTab; }

	void ApplyTag(const FString& tagID) const										{ TagService->ApplyTag(tagID);}
	void ApplyTag(const FTag& tag) const											{ TagService->ApplyTag(const_cast<FTag&>(tag)); ValidationService->InvalidateForChanges(ETaskArea::Changelist); }
	void RemoveTag(const FString& tagID) const										{ TagService->RemoveTag(tagID); }
	void RemoveTag(const FTag& tag) const											{ TagService->RemoveTag(const_cast<FTag&>(tag)); ValidationService->InvalidateForChanges(ETaskArea::Changelist); }
	void SetTagValues(const FString& tagID, const FString& values) const			{ TagService->SetTagValues(tagID, values); }
	void SetTagValues(const FTag& tag, const FString& values) const					{ TagService->SetTagValues(const_cast<FTag&>(tag), values); ValidationService->InvalidateForChanges(ETaskArea::Changelist); }
	void SetTagValues(const FTag& tag, const TArray<FString>& values) const			{ TagService->SetTagValues(const_cast<FTag&>(tag), values); ValidationService->InvalidateForChanges(ETaskArea::Changelist); }
	void UpdateTagsInCL() const														{ TagService->UpdateTagsInCL(); ValidationService->InvalidateForChanges(ETaskArea::Changelist); }
	const FTag* GetTag(const FString& tagID) const									{ return TagService->GetTag(tagID); };
	const TArray<const FTag*>& GetTagsArray() const									{ return TagService->GetTagsArray(); }
	void RegisterTagUpdatedCallback(const FTagUpdated::FDelegate Callback)			{ TagService->OnTagUpdated.Add(Callback); }

	void SetCLDescription(const FText& newDescription, bool DoNotInvalidate = false) const;
	void SendDescriptionToP4() const;
	void UpdateCLFromP4Async() const;
	const FString& GetCLDescription() const											{ return ChangelistService->GetCLDescription(); }
	const FString GetCLID() const													{ return ChangelistService->GetCLID(); }

	FOnCLDescriptionUpdated& GetCLDescriptionUpdatedDelegate()						{ return ChangelistService->OnCLDescriptionUpdated; }
	
	const TArray<FSourceControlStateRef>& GetFilesInCL() const						{ return ChangelistService->GetFilesInCL(); }
	const TArray<FString>& GetDepotFilesInCL() const								{ return ChangelistService->GetFilesDepotPaths(); }
	bool HasShelvedFiles() const													{ return ChangelistService->HasShelvedFiles(); }

	bool IsP4OperationRunning(FName OperationName = FName()) const					{ return SubmitToolState == ESubmitToolAppState::P4BlockingOperation || ChangelistService->IsP4OperationRunning(OperationName); }
	bool IsBlockingOperationRunning() const											{ return SubmitToolState == ESubmitToolAppState::P4BlockingOperation || SwarmService->IsRequestRunning() || JiraService->IsBlockingRequestRunning() || P4LockdownService->IsBlockingOperationRunning(); }
	void CancelP4Operations(FName OperationName = FName()) const					{ ChangelistService->CancelP4Operations(OperationName); SwarmService->CancelOperations(); }

	void ValidateChangelist() const													{ ValidationService->QueueAll(); }
	void ValidateSingle(const FName& ValidatorId) const								{ ValidationService->QueueSingle(ValidatorId, true); }
	void ValidateCLDescription() const												{ ValidationService->QueueByArea(ETaskArea::Changelist); }
	bool IsCLValid() const															{ return ValidationService->GetIsRunSuccessful(); }
	bool CanLaunchPreflight() const;
	void ReevaluateSubmitToolTag();
	void UpdateSubmitToolTag(bool InbAdd);
	bool HasSubmitToolTag() const;
	bool IsValidationRunning() const												{ return ValidationService->GetIsAnyTaskRunning(); }
	const TArray<TWeakPtr<const FValidatorBase>>& GetValidators() const				{ return ValidationService->GetTasks(); }
	void CancelValidations(const FName& InValidatorId = FName()) const				{ ValidationService->StopTasks(InValidatorId); }
	void CheckForFileEdits() const													{ ValidationService->CheckForLocalFileEdit(); }

	FDelegateHandle AddSingleValidatorFinishedCallback
		(const FOnSingleTaskFinished::FDelegate& Callback) const					{ return ValidationService->OnSingleTaskFinished.Add(Callback); }

	void RemoveSingleValidatorFinishedCallback(const FDelegateHandle& Handle) const { ValidationService->OnSingleTaskFinished.Remove(Handle); }

	FDelegateHandle AddValidationFinishedCallback
		(const FOnTaskFinished::FDelegate& Callback) const							{ return ValidationService->OnTasksQueueFinished.Add(Callback); }

	void RemoveValidationFinishedCallback(const FDelegateHandle& Handle) const		{ ValidationService->OnTasksQueueFinished.Remove(Handle); }
	
	FDelegateHandle AddValidationUpdatedCallback
		(const FOnTaskRunStateChanged::FDelegate& Callback) const					{ return ValidationService->OnTasksRunResultUpdated.Add(Callback); }
	void RemoveValidationUpdatedCallback(const FDelegateHandle& Handle) const		{ ValidationService->OnTasksRunResultUpdated.Remove(Handle); }

	void GetUsers(const FOnUsersGet::FDelegate& Callback) const						{ return SourceControlService->GetUsers(Callback); }
	const TArray<TSharedPtr<FUserData>>& GetRecentUsers() const						{ return SourceControlService->GetRecentUsers(); }
	void AddRecentUser(TSharedPtr<FUserData>& User)									{ SourceControlService->AddRecentUser(User); }

	void GetGroups(const FOnGroupsGet::FDelegate& Callback) const					{ return SourceControlService->GetGroups(Callback); }
	const TArray<TSharedPtr<FString>>& GetRecentGroups() const						{ return SourceControlService->GetRecentGroups(); }
	void AddRecentGroup(TSharedPtr<FString>& Group)									{ SourceControlService->AddRecentGroup(Group); }
	FString GetUsername() const														{ return CredentialsService->GetUsername(); }
	const FString GetRootStreamName() const											{ return SourceControlService->GetRootStreamName(); }
	const FString GetCurrentStream() const											{ return SourceControlService->GetCurrentStreamName(); }

	void SetLogin(const FString& InUsername, const FString& InPassword)				{ CredentialsService->SetLogin(InUsername, InPassword); }

	static bool GetInputEnabled();	
	static void SetErrorState()														{ ChangeState(ESubmitToolAppState::Errored);}
	const ESubmitToolAppState GetState() const										{ return SubmitToolState; }
	bool IsSubmitBlocked() const													{ return SubmitToolState == ESubmitToolAppState::SubmitLocked; }
	void DeleteShelvedFiles() const													{ ChangelistService->DeleteShelvedFiles(DeleteShelveCallback); }

	void RequestPreflight();
	bool IsPreflightRequestInProgress() const										{ return PreflightService->IsRequestInProgress(); }
	bool IsPreflightQueued() const													{ return bPreflightQueued; }
	void RefreshPreflightInformation() const										{ PreflightService->FetchPreflightInfo(); UE_LOG(LogSubmitTool, Log, TEXT("Requesting preflight information...")); }
	const TUniquePtr<FPreflightList>& GetPreflightData()							{ return PreflightService->GetPreflightData(); }


	void ShowSwarmReview();
	void RequestSwarmReview();
	FDelegateHandle AddPreflightUpdateCallback
	(const FOnPreflightDataUpdated::FDelegate& Callback) const						{ return PreflightService->OnPreflightDataUpdated.Add(Callback); }

	void RemovePreflightUpdateCallback(const FDelegateHandle& Handle) const			{ PreflightService->OnPreflightDataUpdated.Remove(Handle); }

	void StartSubmitProcess();

	TSharedRef<FSubmitToolServiceProvider> GetServiceProvider() { return ServiceProvider.ToSharedRef(); }

	TSharedPtr<FJiraService> GetJiraService() { return this->JiraService; }
	TSharedPtr<FSwarmService> GetSwarmService() { return this->SwarmService; }
	TSharedPtr<FPreflightService> GetPreflightService() { return this->PreflightService; }

	bool bIsUserInAllowlist = false;

	const FSubmitToolParameters& GetParameters() const { return Parameters; }

	bool IsIntegrationRequired() const { return GetState() == ESubmitToolAppState::SubmitLocked; };
	void RequestIntegration() const;
	const TMap<FString, TSharedPtr<FIntegrationOptionBase>>& GetIntegrationOptions() { return FNIntegrationService->GetIntegrationOptions(); }
	bool ValidateIntegrationOptions(bool bSilent) const	{ return FNIntegrationService->ValidateIntegrationOptions(bSilent); }


	FPreSubmitCallBack PrepareSubmitCallBack;
	FFilesRefresh FileRefreshedCallback;
	bool bSubmitOnSuccessfulValidation = false;

	const TUniquePtr<FSwarmReview>& GetSwarmReview() const		{ return SwarmService->GetReview(); }
	const void RefreshSwarmReview()								{ return SwarmService->FetchReview(OnGetReviewComplete::CreateRaw(this, &FModelInterface::OnGetUsersFromSwarmCompleted)); }

	bool GetSwarmReviewUrl(FString& OutUrl) const	{ return SwarmService->GetCurrentReviewUrl(OutUrl); }

	// AUTO-UPDATE
	bool IsAutoUpdateOn() const { return Parameters.AutoUpdateParameters.bIsAutoUpdateOn; }
	bool CheckForNewVersion() { return UpdateService->CheckForNewVersion(); }
	FString GetDeployId() const { return UpdateService->GetDeployId(); }
	FString GetLocalVersion() const { return UpdateService->GetLocalVersion(); }
	FString GetLatestVersion() const { return UpdateService->GetLatestVersion(); }
	void InstallLatestVersion() { UpdateService->InstallLatestVersion(); }
	void CancelInstallLatestVersion() { UpdateService->Cancel(); }
	const FString GetDownloadMessage() { return UpdateService->GetDownloadMessage(); }

	const bool IsSwarmServiceValid() { return SwarmService.IsValid(); }
	const bool HasSwarmReview()
	{
		if (SwarmService.IsValid())
		{
			const TUniquePtr<FSwarmReview>& Review = SwarmService->GetReview();
			if(Review.IsValid())
			{
				return Review->Id != 0;
			}
		}
		return false;
	}

private:	
	const FSubmitToolParameters& Parameters;
	void OnChangelistRefresh(ETaskArea InChangeType)
	{
		if((InChangeType & ETaskArea::Changelist) == ETaskArea::Changelist)
		{
			TagService->ParseCLDescription();
		}

		if((InChangeType & ETaskArea::LocalFiles) == ETaskArea::LocalFiles)
		{
			FileRefreshedCallback.Broadcast();		
		}

		if((InChangeType & (ETaskArea::LocalFiles | ETaskArea::ShelvedFiles)) != ETaskArea::None)
		{
			RefreshStateBasedOnFiles();
		}


		ValidationService->InvalidateForChanges(InChangeType);
	}
	void RefreshStateBasedOnFiles();
	void OnChangelistReady(bool bIsValid);
	void RevertUnchangedAndSubmit();
	void Submit();
	void OnDeleteShelveOperationComplete(const FSourceControlOperationRef& Operation, ECommandResult::Type Result);
	void OnRevertUnchangedOperationComplete(const FSourceControlOperationRef& Operation, ECommandResult::Type Result);
	void OnPresubmitOperationsComplete(bool bInSuccess);
	void OnSubmitOperationComplete(const FSourceControlOperationRef& Operation, ECommandResult::Type Result);
	void OnGetUsersFromSwarmCompleted(const TUniquePtr<FSwarmReview>& InReview, const FString& InErrorMessage);
	void OnSwarmCreateCompleted(bool InResult, const FString& InErrorMessage);

	FOnChangeListReadyDelegate CLReadyCallback;
	FOnChangelistRefreshDelegate CLRefreshCallback;
	FSourceControlOperationComplete SubmitFinishedCallback;
	FSourceControlOperationComplete DeleteShelveCallback;
	FSourceControlOperationComplete RevertUnchangedCallback;
	FDelegateHandle OnValidationStateUpdatedHandle;
	FDelegateHandle OnValidationFinishedHandle;
	FDelegateHandle OnPresubmitFinishedHandle;
	FDelegateHandle OnSingleValidationFinishedHandle;

	TSharedPtr<ISTSourceControlService> SourceControlService;
	TSharedPtr<FChangelistService> ChangelistService;
	TSharedPtr<FP4LockdownService> P4LockdownService;
	TSharedPtr<FTagService> TagService;
	TSharedPtr<FTasksService> ValidationService;
	TSharedPtr<FJiraService> JiraService;
	TSharedPtr<FPreflightService> PreflightService;
	TWeakPtr<SDockTab> MainTab;
	TSharedPtr<FTasksService> PresubmitOperationsService;
	TSharedPtr<FIntegrationService> FNIntegrationService;
	TSharedPtr<FSwarmService> SwarmService;
	TSharedPtr<FCredentialsService> CredentialsService;
	TSharedPtr<FUpdateService> UpdateService;
	TSharedPtr<FSubmitToolServiceProvider> ServiceProvider;
	bool bPreflightQueued = false;
	bool Tick(float InDeltaTime);

	static ESubmitToolAppState SubmitToolState;

	static void ChangeState(ESubmitToolAppState newState, bool bForce = false);
};