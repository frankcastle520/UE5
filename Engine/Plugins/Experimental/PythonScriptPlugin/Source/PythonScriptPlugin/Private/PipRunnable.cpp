// Copyright Epic Games, Inc. All Rights Reserved.

#include "PipRunnable.h"

#include "PyUtil.h"

#include "Misc/AsyncTaskNotification.h"
#include "Misc/FeedbackContext.h"
#include "Misc/SlowTask.h"

#define LOCTEXT_NAMESPACE "PipInstall"

#if WITH_PYTHON

// FPipProgressParser Implementation
FPipProgressParser::FPipProgressParser(int GuessRequirementsCount, TSharedRef<ICmdProgressNotifier> InCmdNotifier)
	: RequirementsDone(0.0f)
	, RequirementsCount(FMath::Max(2.0f*GuessRequirementsCount,1.0f))
	, CmdNotifier(InCmdNotifier)
{}

float FPipProgressParser::GetTotalWork()
{
	return RequirementsCount;
}

float FPipProgressParser::GetWorkDone()
{
	return RequirementsDone;
}

bool FPipProgressParser::UpdateStatus(const FString& ChkLine)
{
	FString TrimLine = ChkLine.TrimStartAndEnd();
	// Just log if it's not a status update line
	if (!CheckUpdateMatch(TrimLine))
	{
		return false;
	}

	FText Status = FText::FromString(ReplaceUpdateStrs(TrimLine));
	CmdNotifier->UpdateProgress(RequirementsDone, RequirementsCount, Status);

	RequirementsDone += 1;
	RequirementsCount = FMath::Max(RequirementsCount, RequirementsDone + 1);

	// Note: Return true instead to not log status matched lines
	return false;
}

void FPipProgressParser::NotifyCompleted(bool bSuccess)
{
	CmdNotifier->Completed(bSuccess);
}


bool FPipProgressParser::CheckUpdateMatch(const FString& Line)
{
	for (const FString& ChkMatch : MatchStatusStrs)
	{
		if (Line.StartsWith(ChkMatch))
		{
			return true;
		}
	}

	return false;
}

FString FPipProgressParser::ReplaceUpdateStrs(const FString& Line)
{
	FString RepLine = Line;
	for (const TPair<FString, FString>& ReplaceMap : LogReplaceStrs)
	{
		RepLine = RepLine.Replace(*ReplaceMap.Key, *ReplaceMap.Value, ESearchCase::CaseSensitive);
	}

	return RepLine;
}


const TArray<FString> FPipProgressParser::MatchStatusStrs = {TEXT("Requirement"), TEXT("Downloading"), TEXT("Collecting"), TEXT("Using"), TEXT("Installing")};
const TMap<FString,FString> FPipProgressParser::LogReplaceStrs = {{TEXT("Installing collected packages:"), TEXT("Installing collected python package dependencies:")}};


// FSlowTaskNotifier implementation
FSlowTaskNotifier::FSlowTaskNotifier(float GuessSteps, const FText& Description, FFeedbackContext* Context)
	: SlowTask(nullptr)
	, TotalWork(FMath::Max(GuessSteps, 1.0f))
	, WorkDone(0.0f)
{
	SlowTask = MakeUnique<FSlowTask>(GuessSteps, Description, true, *Context);
	WorkDone = SlowTask->CompletedWork;
}

void FSlowTaskNotifier::UpdateProgress(float UpdateWorkDone, float UpdateTotalWork, const FText& Status)
{
	// Exponentially approach 100% if work goes above estimate
	float NextDone = TotalWork * UpdateWorkDone / UpdateTotalWork;
	float NextStep = FMath::Max(NextDone - WorkDone, 0.0f);
	float ProgLeft = FMath::Max(TotalWork - WorkDone, 0.0f);

	// Clamp to 90% of remaining progress
	float NextWork = FMath::Clamp(NextStep, 0.0f, 0.9*ProgLeft);

	// TODO: Pass in specific requirements to update status lines more accurately
	SlowTask->EnterProgressFrame(NextWork, Status);
	WorkDone += NextWork;
}

void FSlowTaskNotifier::Completed(bool bSuccess){}


// Implementation of a tickable class for running a subprocess with status updates
class FRunLoggedSubprocess
{
public:
	FRunLoggedSubprocess(const FString& URI, const FString& Params, FFeedbackContext* Context, TSharedPtr<ICmdProgressParser> CmdParser)
		: Context(Context), CmdParser(CmdParser)
	{
		bLogOutput = (Context != nullptr);

		// Create a stdout pipe for the child process
		StdOutPipeRead = nullptr;
		StdOutPipeWrite = nullptr;
		verify(FPlatformProcess::CreatePipe(StdOutPipeRead, StdOutPipeWrite));

		ProcExitCode = -1;
		ProcessHandle = FPlatformProcess::CreateProc(*URI, *Params, false, true, true, nullptr, 0, nullptr, StdOutPipeWrite, nullptr);
	}

	~FRunLoggedSubprocess()
	{
		FPlatformProcess::ClosePipe(StdOutPipeRead, StdOutPipeWrite);
	}

	bool IsValid()
	{
		return ProcessHandle.IsValid();
	}

	int32 GetExitCode()
	{
		return ProcExitCode;
	}

	bool TickSubprocess()
	{
		bool bProcessFinished = FPlatformProcess::GetProcReturnCode(ProcessHandle, &ProcExitCode);
		BufferedText += FPlatformProcess::ReadPipe(StdOutPipeRead);

		int32 EndOfLineIdx;
		while (BufferedText.FindChar(TEXT('\n'), EndOfLineIdx))
		{
			FString Line = BufferedText.Left(EndOfLineIdx);
			Line.RemoveFromEnd(TEXT("\r"), ESearchCase::CaseSensitive);

			// Always log if no output parser, also log if UpdateStatus returns false
			bool bOnlyStatusUpdate = CmdParser.IsValid() && CmdParser->UpdateStatus(Line);
			if ( bLogOutput && !bOnlyStatusUpdate)
			{
				Context->Log(LogPython.GetCategoryName(), ELogVerbosity::Log, Line);
			}

			BufferedText.MidInline(EndOfLineIdx + 1, MAX_int32, EAllowShrinking::No);
		}

		return !bProcessFinished;
	}

private:
	FFeedbackContext* Context;
	TSharedPtr<ICmdProgressParser> CmdParser;

	bool bLogOutput;
	int32 ProcExitCode;
	FProcHandle ProcessHandle;
	void* StdOutPipeRead = nullptr;
	void* StdOutPipeWrite = nullptr;

	FString BufferedText;
};


bool FLoggedSubprocessSync::Run(int32& OutExitCode, const FString& URI, const FString& Params, FFeedbackContext* Context, TSharedPtr<ICmdProgressParser> CmdParser)
{
	FRunLoggedSubprocess Subproc(URI, Params, Context, CmdParser);
	if (!Subproc.IsValid())
	{
		return false;
	}

	while (Subproc.TickSubprocess())
	{
		FPlatformProcess::Sleep(0.1f);
	}

	OutExitCode = Subproc.GetExitCode();
	return true;
}

#endif //WITH_PYTHON

#undef LOCTEXT_NAMESPACE
