// Copyright Epic Games, Inc. All Rights Reserved.

#include "UnrealVirtualizationTool.h"

#include "Modules/ModuleManager.h"
#include "ProjectUtilities.h"
#include "RequiredProgramMainCPPInclude.h"
#include "UnrealVirtualizationToolApp.h"

IMPLEMENT_APPLICATION(UnrealVirtualizationTool, "UnrealVirtualizationTool");

DEFINE_LOG_CATEGORY(LogVirtualizationTool);

int32 UnrealVirtualizationToolMain(int32 ArgC, TCHAR* ArgV[])
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UnrealVirtualizationToolMain);

	using namespace UE::Virtualization;

	// Allows this program to accept a project argument on the commandline and use project-specific config
	UE::ProjectUtilities::ParseProjectDirFromCommandline(ArgC, ArgV);

	GEngineLoop.PreInit(ArgC, ArgV);
	check(GConfig && GConfig->IsReadyForUse());

	const bool bReportFailures = FParse::Param(FCommandLine::Get(), TEXT("ReportFailures"));
	
#if 0
	while (!FPlatformMisc::IsDebuggerPresent())
	{
		FPlatformProcess::SleepNoStats(0.0f);
	}

	PLATFORM_BREAK();
#endif

	FModuleManager::Get().StartProcessingNewlyLoadedObjects();

	UE_LOG(LogVirtualizationTool, Display, TEXT("Running UnrealVirtualization Tool"));

	EProcessResult ProcessResult = EProcessResult::Success;

	FUnrealVirtualizationToolApp App;

	EInitResult InitResult = App.Initialize();
	if (InitResult == EInitResult::Success)
	{
		ProcessResult = App.Run();
		if (ProcessResult != EProcessResult::Success)
		{
			UE_LOG(LogVirtualizationTool, Error, TEXT("UnrealVirtualizationTool ran with errors"));
		}
	}	
	else if(InitResult == EInitResult::Error)
	{
		UE_LOG(LogVirtualizationTool, Error, TEXT("UnrealVirtualizationTool failed to initialize"));
		ProcessResult = EProcessResult::Error;
	}

	UE_CLOG(ProcessResult == EProcessResult::Success, LogVirtualizationTool, Display, TEXT("UnrealVirtualizationTool ran successfully"));

	// Don't report if the error was in a child process, they will raise their own ensures
	if (bReportFailures && ProcessResult == EProcessResult::Error)
	{
		ensure(false);
	}

	const uint8 ReturnCode = ProcessResult == EProcessResult::Success ? 0 : 1;

	if (FParse::Param(FCommandLine::Get(), TEXT("fastexit")))
	{
		FPlatformMisc::RequestExitWithStatus(true, ReturnCode);
	}
	else
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(Shutdown);

		GConfig->DisableFileOperations(); // We don't want to write out any config file changes!

		// Even though we are exiting anyway we need to request an engine exit in order to get a clean shutdown
		RequestEngineExit(TEXT("The process has finished"));

		FEngineLoop::AppPreExit();
		FModuleManager::Get().UnloadModulesAtShutdown();
		FEngineLoop::AppExit();
	}

	return ReturnCode;
}

INT32_MAIN_INT32_ARGC_TCHAR_ARGV()
{
	FTaskTagScope Scope(ETaskTag::EGameThread);
	return UnrealVirtualizationToolMain(ArgC, ArgV);
}