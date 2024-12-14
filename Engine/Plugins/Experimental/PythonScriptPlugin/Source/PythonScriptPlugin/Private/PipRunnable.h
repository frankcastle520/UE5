// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if WITH_PYTHON

#include "Containers/UnrealString.h"
#include "HAL/Runnable.h"
#include "HAL/RunnableThread.h"
#include "Misc/AsyncTaskNotification.h"
#include "Templates/SharedPointer.h"

struct FSlowTask;

// Simple interface for parsing cmd output to update slowtask progress
// Similar to FFeedbackContextMarkup, but supports arbitrary line parsing
struct ICmdProgressParser
{
    virtual ~ICmdProgressParser(){};
	// Get a total work estimate
	virtual float GetTotalWork() = 0;
    virtual float GetWorkDone() = 0;
	// Parse line and update status/progress (return true to eat the output and not log)
	virtual bool UpdateStatus(const FString& ChkLine) = 0;
	virtual void NotifyCompleted(bool bSuccess) = 0;
};

// Interface to wrap notifications for progress updates (e.g. slowtask or asyncnotify)
struct ICmdProgressNotifier
{
    virtual ~ICmdProgressNotifier(){};
	virtual void UpdateProgress(float UpdateWorkDone, float UpdateTotalWork, const FText& Status) = 0;
	virtual void Completed(bool bSuccess) = 0;
};

// Pip progress parser implemenation of ICmdProgressParser
class FPipProgressParser : public ICmdProgressParser
{
public:
	FPipProgressParser(int GuessRequirementsCount, TSharedRef<ICmdProgressNotifier> InCmdNotifier);
    // ICmdProgressParser methods
	virtual float GetTotalWork() override;
    virtual float GetWorkDone() override;
	virtual bool UpdateStatus(const FString& ChkLine) override;
	virtual void NotifyCompleted(bool bSuccess) override;

private:
	static bool CheckUpdateMatch(const FString& Line);
	static FString ReplaceUpdateStrs(const FString& Line);

	float RequirementsDone;
	float RequirementsCount;
    TSharedRef<ICmdProgressNotifier> CmdNotifier;

	static const TArray<FString> MatchStatusStrs;
	static const TMap<FString,FString> LogReplaceStrs;
};

// Slowtask-based notifier for updating command progress (ICmdProgressNotifier)
class FSlowTaskNotifier : public ICmdProgressNotifier
{
public:
	FSlowTaskNotifier(float GuessSteps, const FText& Description, FFeedbackContext* Context);
    // ICmdProgressNotifier methods
	virtual void UpdateProgress(float UpdateWorkDone, float UpdateTotalWork, const FText& Status) override;
	virtual void Completed(bool bSuccess) override;

private:
	TUniquePtr<FSlowTask> SlowTask;

	float TotalWork;
	float WorkDone;
};

// Run a subprocess synchronously (assumes running in game thread)
struct FLoggedSubprocessSync
{
    static bool Run(int32& OutExitCode, const FString& URI, const FString& Params, FFeedbackContext* Context, TSharedPtr<ICmdProgressParser> CmdParser);
};

#endif //WITH_PYTHON
