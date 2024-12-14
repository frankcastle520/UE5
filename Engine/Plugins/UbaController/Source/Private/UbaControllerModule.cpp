// Copyright Epic Games, Inc. All Rights Reserved.

#include "UbaControllerModule.h"

#include "Misc/ConfigCacheIni.h"
#include "UbaJobProcessor.h"
#include "Features/IModularFeatures.h"
#include "HAL/FileManager.h"
#include "Misc/CommandLine.h"
#include "Misc/CoreDelegates.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "CoreMinimal.h"


DEFINE_LOG_CATEGORY(LogUbaController);

namespace UbaControllerModule
{
	static constexpr int32 SubFolderCount = 32;

	static bool bDumpTraceFiles = true;
	static FAutoConsoleVariableRef CVarDumpTraceFiles(
		TEXT("r.UbaController.DumpTraceFiles"),
		bDumpTraceFiles,
		TEXT("If true, UBA controller dumps trace files for later use with UBA visualizer in the Saved folder under UbaController (Enabled by default)"));

	static FString MakeAndGetDebugInfoPath()
	{
		// Build machines should dump to the AutomationTool/Saved/Logs directory and they will upload as build artifacts via the AutomationTool.
		FString BaseDebugInfoPath = FPaths::ProjectSavedDir();
		if (GIsBuildMachine)
		{
			BaseDebugInfoPath = FPaths::Combine(*FPaths::EngineDir(), TEXT("Programs"), TEXT("AutomationTool"), TEXT("Saved"), TEXT("Logs"));
		}

		FString AbsoluteDebugInfoDirectory = IFileManager::Get().ConvertToAbsolutePathForExternalAppForWrite(*(BaseDebugInfoPath / TEXT("UbaController")));
		FPaths::NormalizeDirectoryName(AbsoluteDebugInfoDirectory);

		// Create directory if it doesn't exit yet
		if (!IFileManager::Get().DirectoryExists(*AbsoluteDebugInfoDirectory))
		{
			IFileManager::Get().MakeDirectory(*AbsoluteDebugInfoDirectory, true);
		}

		return AbsoluteDebugInfoDirectory;
	}

	FString GetTempDir()
	{
		const FString HordeSharedDir = FPlatformMisc::GetEnvironmentVariable(TEXT("UE_HORDE_SHARED_DIR"));
		if (!HordeSharedDir.IsEmpty())
			return HordeSharedDir;
		return FPlatformProcess::UserTempDir();
	}

};

FUbaControllerModule::FUbaControllerModule()
	: bSupported(false)
	, bModuleInitialized(false)
	, bControllerInitialized(false)
	, RootWorkingDirectory(FPaths::Combine(*UbaControllerModule::GetTempDir(), TEXT("UbaControllerWorkingDir")))
	, WorkingDirectory(FPaths::Combine(RootWorkingDirectory, FGuid::NewGuid().ToString(EGuidFormats::Digits)))
	, NextFileID(0)
	, NextTaskID(0)
{
}

FUbaControllerModule::~FUbaControllerModule()
{	
	if (JobDispatcherThread)
	{
		JobDispatcherThread->Stop();
		// Wait until the thread is done
		FPlatformProcess::ConditionalSleep([&](){ return JobDispatcherThread && JobDispatcherThread->IsWorkDone(); },0.1f);
	}

	CleanWorkingDirectory();
}

static bool IsUbaControllerEnabled()
{
	if (FParse::Param(FCommandLine::Get(), TEXT("NoUbaController")))
	{
		return false;
	}

	// Check if UbaController is enabled via command line argument
	if (FParse::Param(FCommandLine::Get(), TEXT("Uba")))
	{
		return true;
	}

	// Check if UbaController is enabled via INI configuration in [UbaController] section.
	FString EnabledState;
	GConfig->GetString(TEXT("UbaController"), TEXT("Enabled"), EnabledState, GEngineIni);

	// This "Enabled" parameter is a tri-state so we have to parse it as a string.
	// Those strings can include the comments (starting with ';') from the INI file, so we have to trim that section from the string.
	const int32 EnabledStateCommentPosition = EnabledState.Find(TEXT(";"));
	if (EnabledStateCommentPosition != INDEX_NONE)
	{
		EnabledState.RemoveAt(EnabledStateCommentPosition, EnabledState.Len() - EnabledStateCommentPosition);
		EnabledState.RemoveSpacesInline();
	}

	if (EnabledState.Equals(TEXT("True"), ESearchCase::IgnoreCase) || (EnabledState.Equals(TEXT("BuildMachineOnly"), ESearchCase::IgnoreCase) && GIsBuildMachine))
	{
		return true;
	}

	return false;
}

bool FUbaControllerModule::IsSupported()
{
	if (bControllerInitialized)
	{
		return bSupported;
	}
	
	const bool bEnabled = IsUbaControllerEnabled();

	bSupported = FPlatformProcess::SupportsMultithreading() && bEnabled;
	return bSupported;
}

void FUbaControllerModule::CleanWorkingDirectory() const
{
	if (UE::GetMultiprocessId() != 0) // Only director is allowed to clean
	{
		return;
	}

	IFileManager& FileManager = IFileManager::Get();
	
	if (!RootWorkingDirectory.IsEmpty())
	{
		if (!FileManager.DeleteDirectory(*RootWorkingDirectory, false, true))
		{
			UE_LOG(LogUbaController, Log, TEXT("%s => Failed to delete current working Directory => %s"), ANSI_TO_TCHAR(__FUNCTION__), *RootWorkingDirectory);
		}	
	}
}

bool FUbaControllerModule::HasTasksDispatchedOrPending() const
{
	return !PendingRequestedCompilationTasks.IsEmpty() || (JobDispatcherThread.IsValid() && JobDispatcherThread->HasJobsInFlight());
}

FString GetUbaBinariesPath()
{
#if PLATFORM_WINDOWS
#if PLATFORM_CPU_ARM_FAMILY
	const TCHAR* BinariesArch = TEXT("arm64");
#else
	const TCHAR* BinariesArch = TEXT("x64");
#endif
	return FPaths::Combine(FPaths::EngineDir(), TEXT("Binaries"), TEXT("Win64"), TEXT("UnrealBuildAccelerator"), BinariesArch);
#elif PLATFORM_MAC
    return FPaths::Combine(FPaths::EngineDir(), TEXT("Binaries"), TEXT("Mac"), TEXT("UnrealBuildAccelerator"));
#elif PLATFORM_LINUX
    return FPaths::Combine(FPaths::EngineDir(), TEXT("Binaries"), TEXT("Linux"), TEXT("UnrealBuildAccelerator"));
#else
#error Unsupported platform to compile UbaController plugin. Only Win64, Mac, and Linux are supported!
#endif
}

void FUbaControllerModule::LoadDependencies()
{
	const FString UbaBinariesPath = GetUbaBinariesPath();
	FPlatformProcess::AddDllDirectory(*UbaBinariesPath);
	FPlatformProcess::GetDllHandle(*(FPaths::Combine(UbaBinariesPath, "UbaHost.dll")));
}

void FUbaControllerModule::StartupModule()
{
	check(!bModuleInitialized);

	LoadDependencies();

	IModularFeatures::Get().RegisterModularFeature(GetModularFeatureType(), this);

	bModuleInitialized = true;

	FCoreDelegates::OnEnginePreExit.AddLambda([&]()
	{
		if (bControllerInitialized && JobDispatcherThread)
		{
			JobDispatcherThread->Stop();
			FPlatformProcess::ConditionalSleep([&]() { return JobDispatcherThread && JobDispatcherThread->IsWorkDone(); }, 0.1f);
			JobDispatcherThread = nullptr;
		}
	});
}

void FUbaControllerModule::ShutdownModule()
{
	check(bModuleInitialized);
	
	IModularFeatures::Get().UnregisterModularFeature(GetModularFeatureType(), this);
	
	if (bControllerInitialized)
	{
		// Stop the jobs thread
		if (JobDispatcherThread)
		{
			JobDispatcherThread->Stop();	
			// Wait until the thread is done
			FPlatformProcess::ConditionalSleep([&](){ return JobDispatcherThread && JobDispatcherThread->IsWorkDone(); },0.1f);
		}

		FTask* Task;
		while (PendingRequestedCompilationTasks.Dequeue(Task))
		{
			FDistributedBuildTaskResult Result;
			Result.ReturnCode = 0;
			Result.bCompleted = false;
			Task->Promise.SetValue(Result);
			delete Task;
		}

		PendingRequestedCompilationTasks.Empty();
	}

	CleanWorkingDirectory();
	bModuleInitialized = false;
	bControllerInitialized = false;
}

void FUbaControllerModule::InitializeController()
{
	// We should never Initialize the controller twice
	if (ensureAlwaysMsgf(!bControllerInitialized, TEXT("Multiple initialization of UBA controller!")))
	{
		CleanWorkingDirectory();

		if (IsSupported())
		{
			IFileManager::Get().MakeDirectory(*WorkingDirectory, true);

			// Pre-create the directories so we don't have to explicitly register them to uba later
			for (int32 It = 0; It != UbaControllerModule::SubFolderCount; ++It)
			{
				IFileManager::Get().MakeDirectory(*FString::Printf(TEXT("%s/%d"), *WorkingDirectory, It));
			}

			if (UbaControllerModule::bDumpTraceFiles)
			{
				DebugInfoPath = UbaControllerModule::MakeAndGetDebugInfoPath();
			}

			JobDispatcherThread = MakeShared<FUbaJobProcessor>(*this);
			JobDispatcherThread->StartThread();
		}

		bControllerInitialized = true;
	}
}

FString FUbaControllerModule::CreateUniqueFilePath()
{
	check(bSupported);
	int32 ID = NextFileID++;
	int32 FolderID = ID % UbaControllerModule::SubFolderCount; // We use sub folders to be nicer to file system (we can end up with 20000 files in one folder otherwise)
	return FString::Printf(TEXT("%s/%d/%d.uba"), *WorkingDirectory, FolderID, ID);
}

TFuture<FDistributedBuildTaskResult> FUbaControllerModule::EnqueueTask(const FTaskCommandData& CommandData)
{
	check(bSupported);

	TPromise<FDistributedBuildTaskResult> Promise;
	TFuture<FDistributedBuildTaskResult> Future = Promise.GetFuture();

	// Enqueue the new task
	FTask* Task = new FTask(NextTaskID++, CommandData, MoveTemp(Promise));
	{
		PendingRequestedCompilationTasks.Enqueue(Task);
	}

	JobDispatcherThread->HandleTaskQueueUpdated(CommandData.InputFileName);

	return MoveTemp(Future);
}

bool FUbaControllerModule::PollStats(FDistributedBuildStats& OutStats)
{
	return JobDispatcherThread != nullptr && JobDispatcherThread->PollStats(OutStats);
}

void FUbaControllerModule::ReportJobProcessed(const FTaskResponse& InTaskResponse, FTask* CompileTask)
{
	if (CompileTask)
	{
		FDistributedBuildTaskResult Result;
		Result.ReturnCode = InTaskResponse.ReturnCode;
		Result.bCompleted = true;
		CompileTask->Promise.SetValue(Result);
		delete CompileTask;
	}
}

UBACONTROLLER_API FUbaControllerModule& FUbaControllerModule::Get()
{
	return FModuleManager::LoadModuleChecked<FUbaControllerModule>(TEXT("UbaController"));
}

IMPLEMENT_MODULE(FUbaControllerModule, UbaController);
