// Copyright Epic Games, Inc. All Rights Reserved.

#include "ProcessWrapper.h"

#include "GenericPlatform/GenericPlatformTime.h"
#include "HAL/PlatformProcess.h"
#include "Logging/SubmitToolLog.h"
#include "Misc/DateTime.h"
#include "ProcessPipes.h"

FProcessWrapper::FProcessWrapper(const FString& InProcessName, const FString& InPath, const FString& InArgs, const FOnCompleted& InOnCompleted, const FOnOutputLine& InOnOutputLine, const FString& InWorkingDir, const bool InbLaunchHidden, const bool bLaunchReallyHidden, const bool InbLaunchDetached)
	: ProcessName(InProcessName)
	, Path(InPath)
	, Args(InArgs)
	, WorkingDir(InWorkingDir)
	, bLaunchesHidden(InbLaunchHidden)
	, bLaunchesReallyHidden(bLaunchReallyHidden)
	, bLaunchDetached(InbLaunchDetached)
	, Pipes(MakeUnique<FProcessPipes>())
	, OnCompleted(InOnCompleted)
	, OnOutputLine(InOnOutputLine)
{}

FProcessWrapper::~FProcessWrapper()
{
	ProcessHandle = nullptr;

	FTSTicker::GetCoreTicker().RemoveTicker(TickerHandle);
	TickerHandle = nullptr;
}

bool FProcessWrapper::Start(bool bWaitForExit)
{
	if(IsRunning())
	{
		OutputLine(FString::Format(TEXT("Process %s already running, ignored start request"), { *ProcessName }), EProcessOutputType::ProcessError);
		return false;
	}
	
	OutputRemainder = FString();
	ExecutingTime = 0;

	if (!Pipes->Create())
	{
		OutputLine(FString::Format(TEXT("Error creating pipes in process {0}"), { *ProcessName }), EProcessOutputType::ProcessError);
		return false;
	}

	OutputLine(FString::Format(TEXT("Running process {0}: {1} {2}"), { *ProcessName, *Path, *Args }), EProcessOutputType::ProcessInfo);

	ProcessHandle = MakeUnique<FProcHandle>(FPlatformProcess::CreateProc
	(
		*Path,
		*Args,
		bLaunchDetached,
		bLaunchesHidden,
		bLaunchesReallyHidden,
		nullptr,
		0,
		WorkingDir.Len() ? *WorkingDir : nullptr,
		Pipes->GetStdOutForProcess(),
		Pipes->GetStdInForProcess())
	);

	if (!ProcessHandle->IsValid())
	{
		OutputLine(FString::Format(TEXT("Error creating process {0}"), { *ProcessName }), EProcessOutputType::ProcessError);
		return false;
	}

	
	if(bWaitForExit)
	{
		FDateTime Before = FDateTime::UtcNow();
		FPlatformProcess::WaitForProc(*ProcessHandle);
		FTimespan Timespan = FDateTime::UtcNow() - Before;
		OnTick(Timespan.GetTotalSeconds());
		return true;
	}
	else
	{
		TickerHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateRaw(this, &FProcessWrapper::OnTick));
		return true;
	}
}

void FProcessWrapper::Stop()
{
	if(!IsRunning())
	{
		return;
	}

	OutputLine(FString::Format(TEXT("Process {0} was stopped"), { *ProcessName }), EProcessOutputType::ProcessInfo);

	FPlatformProcess::TerminateProc(*ProcessHandle, true);
	
	Cleanup();
}

bool FProcessWrapper::IsRunning() const
{
	return ProcessHandle.IsValid() && FPlatformProcess::IsProcRunning(*ProcessHandle);
}

bool FProcessWrapper::OnTick(float Delta)
{
	if(!ProcessHandle.IsValid())
	{
		return false;
	}

	if(FPlatformProcess::IsProcRunning(*ProcessHandle))
	{
		ReadOutput(false);
		ExecutingTime += Delta;
		return true;
	}

	ExecutingTime += Delta;

	// Flush output
	ReadOutput(true);	
	FPlatformProcess::GetProcReturnCode(*ProcessHandle, &ExitCode);

	OutputLine(FString::Printf(TEXT("Completed running process %s. Process took %s and exited with code %d"), *ProcessName, *FGenericPlatformTime::PrettyTime(ExecutingTime), ExitCode), EProcessOutputType::ProcessInfo);

	Cleanup();

	if(OnCompleted.IsBound())
	{
		OnCompleted.ExecuteIfBound(ExitCode);
	}

	return false;
}

void FProcessWrapper::Cleanup()
{
	Pipes->Reset();
	ProcessHandle = nullptr;

	FTSTicker::GetCoreTicker().RemoveTicker(TickerHandle);
	TickerHandle = nullptr;
}

void FProcessWrapper::ReadOutput(bool FlushOutput)
{
	if(!OnOutputLine.IsBound())
	{
		return;
	}
	
	FString NewOutput = OutputRemainder + FPlatformProcess::ReadPipe(Pipes->GetStdOutForReading());

	if(!NewOutput.IsEmpty() && (NewOutput.Contains(TEXT("\n")) || FlushOutput))
	{
		// Find the last line, which may be truncated
		if(!FlushOutput)
		{
			int32 Position;
			NewOutput.FindLastChar('\n', Position);
			OutputRemainder = NewOutput.Mid(Position + 1);
			NewOutput.RemoveFromEnd(OutputRemainder);
		}

		TArray<FString> Lines;
		const TCHAR* Separators[] = { TEXT("\n"), TEXT("\r") };
		NewOutput.ParseIntoArray(Lines, Separators, UE_ARRAY_COUNT(Separators));

		for(const FString& Line : Lines)
		{
			OutputLine(Line, EProcessOutputType::SDTOutput);
		}
	}
}

void FProcessWrapper::OutputLine(const FString OutputLine, const EProcessOutputType& OutputType)
{
	if(OnOutputLine.IsBound())
	{
		OnOutputLine.ExecuteIfBound(OutputLine, OutputType);
	}
}
