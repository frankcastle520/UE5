// Copyright Epic Games, Inc. All Rights Reserved.

#include "UbaJobProcessor.h"

#include "Interfaces/ITargetPlatform.h"
#include "Interfaces/ITargetPlatformManagerModule.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/CoreMisc.h"
#include "UbaControllerModule.h"
#include "UbaHordeAgentManager.h"
#include "UbaProcessStartInfo.h"
#include "UbaSessionServerCreateInfo.h"
#include "UbaStringConversion.h"

#if PLATFORM_WINDOWS
#include "Windows/AllowWindowsPlatformTypes.h"
#include "Windows/HideWindowsPlatformTypes.h"
#endif

#include "HAL/FileManager.h"
#include "HAL/PlatformFileManager.h"

#include "Misc/Paths.h"
#include "Misc/ScopeRWLock.h"

namespace UbaJobProcessorOptions
{
	static float SleepTimeBetweenActions = 0.01f;
	static FAutoConsoleVariableRef CVarSleepTimeBetweenActions(
        TEXT("r.UbaController.SleepTimeBetweenActions"),
        SleepTimeBetweenActions,
        TEXT("How much time the job processor thread should sleep between actions .\n"));

	static float MaxTimeWithoutTasks = 100.0f;
	static FAutoConsoleVariableRef CVarMaxTimeWithoutTasks(
        TEXT("r.UbaController.MaxTimeWithoutTasks"),
        MaxTimeWithoutTasks,
        TEXT("Time to wait (in seconds) before stop processing attempts if we don't have any pending task.\n"));

	static bool bAutoLaunchVisualizer = false;
	static FAutoConsoleVariableRef CVarAutoLaunchVisualizer(
		TEXT("r.UbaController.AutoLaunchVisualizer"),
		bAutoLaunchVisualizer,
		TEXT("If true, UBA visualizer will be launched automatically\n"));

	static bool bAllowProcessReuse = true;
	static FAutoConsoleVariableRef CVarAllowProcessReuse(
		TEXT("r.UbaController.AllowProcessReuse"),
		bAllowProcessReuse,
		TEXT("If true, remote process is allowed to fetch new processes from the queue (this requires the remote processes to have UbaRequestNextProcess implemented)\n"));

	static bool bDetailedTrace = false;
	static FAutoConsoleVariableRef CVarDetailedTrace(
		TEXT("r.UbaController.DetailedTrace"),
		bDetailedTrace,
		TEXT("If true, a UBA will output detailed trace\n"));

	enum EUbaLogVerbosity
	{
		UbaLogVerbosity_Default = 0, // foward erros and warnings only
		UbaLogVerbosity_High, // also forward infos
		UbaLogVerbosity_Max // forward all UBA logs to UE_LOG
	};

	static int32 UbaLogVerbosity = UbaLogVerbosity_Default;
	static FAutoConsoleVariableRef CVarShowUbaLog(
		TEXT("r.UbaController.LogVerbosity"),
		UbaLogVerbosity,
		TEXT("Specifies how much of UBA logs is forwarded to UE logs..\n")
		TEXT("0 - Default, only forward errrors and warnings.\n")
		TEXT("1 - Also forward regular information about UBA sessions.\n")
		TEXT("2 - Forward all UBA logs."));

	static bool bProcessLogEnabled = false;
	static FAutoConsoleVariableRef CVarProcessLogEnabled(
		TEXT("r.UbaController.ProcessLogEnabled"),
		bProcessLogEnabled,
		TEXT("If true, each detoured process will write a log file. Note this is only useful if UBA is compiled in debug\n"));

	FString ReplaceEnvironmentVariablesInPath(const FString& ExtraFilePartialPath) // Duplicated code with FAST build.. put it somewhere else?
	{
		FString ParsedPath;

		// Fast build cannot read environmental variables easily
		// Is better to resolve them here
		if (ExtraFilePartialPath.Contains(TEXT("%")))
		{
			TArray<FString> PathSections;
			ExtraFilePartialPath.ParseIntoArray(PathSections, TEXT("/"));

			for (FString& Section : PathSections)
			{
				if (Section.Contains(TEXT("%")))
				{
					Section.RemoveFromStart(TEXT("%"));
					Section.RemoveFromEnd(TEXT("%"));
					Section = FPlatformMisc::GetEnvironmentVariable(*Section);
				}
			}

			for (FString& Section : PathSections)
			{
				ParsedPath /= Section;
			}

			FPaths::NormalizeDirectoryName(ParsedPath);
		}

		if (ParsedPath.IsEmpty())
		{
			ParsedPath = ExtraFilePartialPath;
		}

		return ParsedPath;
	}
}

FUbaJobProcessor::FUbaJobProcessor(
	FUbaControllerModule& InControllerModule) :

	Thread(nullptr),
	ControllerModule(InControllerModule),
	bForceStop(false),
	LastTimeCheckedForTasks(0),
	bShouldProcessJobs(false),
	bIsWorkDone(false),
	LogWriter([]() {}, []() {}, [](uba::LogEntryType type, const uba::tchar* str, uba::u32 /*strlen*/)
	{
			switch (type)
			{
			case uba::LogEntryType_Error:
				UE_LOG(LogUbaController, Error, TEXT("%s"), UBASTRING_TO_TCHAR(str));
				break;
			case uba::LogEntryType_Warning:
				UE_LOG(LogUbaController, Warning, TEXT("%s"), UBASTRING_TO_TCHAR(str));
				break;
			case uba::LogEntryType_Info:
				if (UbaJobProcessorOptions::UbaLogVerbosity >= UbaJobProcessorOptions::UbaLogVerbosity_High)
				{
					UE_LOG(LogUbaController, Display, TEXT("%s"), UBASTRING_TO_TCHAR(str));
				}
				break;
			default:
				if (UbaJobProcessorOptions::UbaLogVerbosity >= UbaJobProcessorOptions::UbaLogVerbosity_Max)
				{
					UE_LOG(LogUbaController, Display, TEXT("%s"), UBASTRING_TO_TCHAR(str));
				}
				break;
			}
	})
{
	Uba_SetCustomAssertHandler([](const uba::tchar* text)
		{
			checkf(false, TEXT("%s"), UBASTRING_TO_TCHAR(text));
		});

	if (!GConfig->GetInt(TEXT("UbaController"), TEXT("MaxLocalParallelJobs"), MaxLocalParallelJobs, GEngineIni))
	{
		MaxLocalParallelJobs = -1;
	}

	if (MaxLocalParallelJobs == -1)
	{
		MaxLocalParallelJobs = FPlatformMisc::NumberOfCoresIncludingHyperthreads();
	}
}

FUbaJobProcessor::~FUbaJobProcessor()
{
	delete Thread;
}

void FUbaJobProcessor::CalculateKnownInputs()
{
	// TODO: This is ShaderCompileWorker specific and this code is designed to handle all kinds of distributed workload.
	// Instead this information should be provided from the outside


	if (KnownInputsCount) // In order to improve startup we provide some of the input we know will be loaded by ShaderCompileWorker.exe
	{
		return;
	}

	auto AddKnownInput = [&](const FString& file)
		{
			#if PLATFORM_WINDOWS
			auto& fileData = file.GetCharArray();
			const uba::tchar* fileName = fileData.GetData();
			size_t fileNameLen = fileData.Num();
			#else
			FStringToUbaStringConversion conv(*file);
			const uba::tchar* fileName = conv.Get();
			size_t fileNameLen = strlen(fileName) + 1;
			#endif
			auto num = KnownInputsBuffer.Num();
			KnownInputsBuffer.SetNum(num + fileNameLen);
			memcpy(KnownInputsBuffer.GetData() + num, fileName, fileNameLen * sizeof(uba::tchar));
			++KnownInputsCount;
		};

	// Get the binaries
	TArray<FString> KnownFileNames;
	FString BinDir = FPaths::Combine(FPaths::EngineDir(), TEXT("Binaries"), FPlatformProcess::GetBinariesSubdirectory());

	#if PLATFORM_WINDOWS
	AddKnownInput(*FPaths::Combine(BinDir, TEXT("ShaderCompileWorker.exe")));
	#else
	AddKnownInput(*FPaths::Combine(BinDir, TEXT("ShaderCompileWorker")));
	#endif

	IFileManager::Get().FindFilesRecursive(KnownFileNames, *BinDir, TEXT("ShaderCompileWorker-*.*"), true, false);
	for (const FString& file : KnownFileNames)
	{
		if (file.EndsWith(FPlatformProcess::GetModuleExtension()))
		{
			AddKnownInput(file);
		}
	}

	// Get the compiler dependencies for all platforms)
	ITargetPlatformManagerModule* TargetPlatformManager = GetTargetPlatformManager();
	for (ITargetPlatform* TargetPlatform : GetTargetPlatformManager()->GetTargetPlatforms())
	{
		KnownFileNames.Empty();
		TargetPlatform->GetShaderCompilerDependencies(KnownFileNames);

		for (const FString& ExtraFilePartialPath : KnownFileNames)
		{
			if (!ExtraFilePartialPath.Contains(TEXT("*"))) // Seems like there are some *.x paths in there.. TODO: Do a find files
			{
				AddKnownInput(UbaJobProcessorOptions::ReplaceEnvironmentVariablesInPath(ExtraFilePartialPath));
			}
		}
	}

	// Get all the config files
	for (const FString& ConfigDir : FPaths::GetExtensionDirs(FPaths::EngineDir(), TEXT("Config")))
	{
		KnownFileNames.Empty();
		IFileManager::Get().FindFilesRecursive(KnownFileNames, *ConfigDir, TEXT("*.ini"), true, false);
		for (const FString& file : KnownFileNames)
		{
			AddKnownInput(file);
		}
	}

	KnownInputsBuffer.Add(0);
}

void FUbaJobProcessor::RunTaskWithUba(FTask* Task)
{
	FTaskCommandData& Data = Task->CommandData;
	SessionServer_RegisterNewFile(UbaSessionServer, TCHAR_TO_UBASTRING(*Data.InputFileName));

	FString InputFileName = FPaths::GetCleanFilename(Data.InputFileName);
	FString OutputFileName = FPaths::GetCleanFilename(Data.OutputFileName);
	FString Parameters = FString::Printf(TEXT("\"%s/\" %d 0 \"%s\" \"%s\" %s "), *Data.WorkingDirectory, Data.DispatcherPID, *InputFileName, *OutputFileName, *Data.ExtraCommandArgs);
	FString AppDir = FPaths::GetPath(Data.Command);
	
	FStringToUbaStringConversion UbaCommandStr(*Data.Command);
	FStringToUbaStringConversion UbaParametersStr(*Parameters);
	FStringToUbaStringConversion UbaInputFileNameStr(*InputFileName);
	FStringToUbaStringConversion UbaWorkingDirStr(*AppDir);
	FStringToUbaStringConversion UbaTaskDescription(*Data.Description);

	uba::ProcessStartInfo ProcessInfo;
	ProcessInfo.application = UbaCommandStr.Get();
	ProcessInfo.arguments = UbaParametersStr.Get();
	ProcessInfo.description = UbaInputFileNameStr.Get();
	ProcessInfo.workingDir = UbaWorkingDirStr.Get();
	ProcessInfo.writeOutputFilesOnFail = true;
	ProcessInfo.breadcrumbs = UbaTaskDescription.Get();

	if (UbaJobProcessorOptions::bProcessLogEnabled)
	{
		ProcessInfo.logFile = UbaInputFileNameStr.Get();
	}
	
	struct ExitedInfo
	{
		FUbaJobProcessor* Processor;
		FString InputFile;
		FString OutputFile;
		FTask* Task;
	};

	auto Info = new ExitedInfo;
	Info->Processor = this;
	Info->InputFile = Data.InputFileName;
	Info->OutputFile = Data.OutputFileName;
	Info->Task = Task;

	ProcessInfo.userData = Info;
	ProcessInfo.exitedFunc = [](void* userData, const uba::ProcessHandle& ph)
		{
			uint32 logLineIndex = 0;
			while (const uba::tchar* line = ProcessHandle_GetLogLine(&ph, logLineIndex++))
			{
				UE_LOG(LogUbaController, Display, TEXT("%s"), UBASTRING_TO_TCHAR(line));
			}

			if (auto Info = (ExitedInfo*)userData) // It can be null if custom message has already handled all of them
			{
				IFileManager::Get().Delete(*Info->InputFile);
				SessionServer_RegisterDeleteFile(Info->Processor->UbaSessionServer, TCHAR_TO_UBASTRING(*Info->InputFile));
				Info->Processor->HandleUbaJobFinished(Info->Task);

				StorageServer_DeleteFile(Info->Processor->UbaStorageServer, TCHAR_TO_UBASTRING(*Info->InputFile));
				StorageServer_DeleteFile(Info->Processor->UbaStorageServer, TCHAR_TO_UBASTRING(*Info->OutputFile));

				delete Info;
			}
		};


	Scheduler_EnqueueProcess(UbaScheduler, ProcessInfo, 1.0f, KnownInputsBuffer.GetData(), KnownInputsBuffer.Num()*sizeof(uba::tchar), KnownInputsCount);
}

FString GetUbaBinariesPath();

void FUbaJobProcessor::StartUba()
{
	checkf(UbaServer == nullptr, TEXT("FUbaJobProcessor::StartUba() was called twice before FUbaJobProcessor::ShutDownUba()"));

	UbaServer = NetworkServer_Create(LogWriter);

	FString RootDir = FString::Printf(TEXT("%s/%s/%u"), FPlatformProcess::UserTempDir(), TEXT("UbaControllerStorageDir"), UE::GetMultiprocessId());
	IFileManager::Get().MakeDirectory(*RootDir, true);

	uba::u64 casCapacityBytes = 32llu * 1024 * 1024 * 1024;
	UbaStorageServer = StorageServer_Create(*UbaServer, TCHAR_TO_UBASTRING(*RootDir), casCapacityBytes, true, LogWriter);

	uba::SessionServerCreateInfo info(*(uba::Storage*)UbaStorageServer, *UbaServer, LogWriter);
	info.launchVisualizer = UbaJobProcessorOptions::bAutoLaunchVisualizer;
	const FStringToUbaStringConversion UbaRootDirStr(*RootDir);
	info.rootDir = UbaRootDirStr.Get();
	info.allowMemoryMaps = false; // Skip using memory maps
	info.remoteLogEnabled = UbaJobProcessorOptions::bProcessLogEnabled;

	info.traceEnabled = true;
	FString TraceOutputFile;
	if (!ControllerModule.GetDebugInfoPath().IsEmpty())
	{
		static uint32 UbaSessionCounter;
		TraceOutputFile = ControllerModule.GetDebugInfoPath() / FString::Printf(TEXT("UbaController.MultiprocessId-%u.Session-%u.uba"), UE::GetMultiprocessId(), UbaSessionCounter++);
	}
	const FStringToUbaStringConversion UbaTraceOutputFileStr(*TraceOutputFile);
	info.traceOutputFile = UbaTraceOutputFileStr.Get();
	info.detailedTrace = UbaJobProcessorOptions::bDetailedTrace;
	FString TraceName = FString::Printf(TEXT("UbaController_%s"), *FGuid::NewGuid().ToString(EGuidFormats::Digits));
	const FStringToUbaStringConversion UbaTraceNameStr(*TraceName);
	info.traceName = UbaTraceNameStr.Get();


	//info.remoteLogEnabled = true;
	UbaSessionServer = SessionServer_Create(info);

	CalculateKnownInputs();

	UbaScheduler = Scheduler_Create(UbaSessionServer, MaxLocalParallelJobs, UbaJobProcessorOptions::bAllowProcessReuse);
	Scheduler_Start(UbaScheduler);

	HandleTaskQueueUpdated(TEXT("")); // Flush tasks into uba scheduler

	if (UE::GetMultiprocessId() == 0)
	{
		NetworkServer_StartListen(UbaServer, uba::DefaultPort, nullptr); // Start listen so any helper on the LAN can join in
	}

	HordeAgentManager = MakeUnique<FUbaHordeAgentManager>(ControllerModule.GetWorkingDirectory(), GetUbaBinariesPath());

	auto AddClientCallback = [](void* userData, const uba::tchar* ip, uint16 port)
		{
			return NetworkServer_AddClient((uba::NetworkServer*)userData, ip, port, nullptr);
		};
	HordeAgentManager->SetAddClientCallback(AddClientCallback, UbaServer);

	FString HordeConfig;
	if (GConfig->GetString(TEXT("UbaController"), TEXT("Horde"), HordeConfig, GEngineIni))
	{
		HordeConfig.TrimStartInline();
		HordeConfig.TrimEndInline();
		HordeConfig.RemoveFromStart(TEXT("("));
		HordeConfig.RemoveFromEnd(TEXT(")"));

		FString Pool;
		bool FoundPool = false;

		#if PLATFORM_MAC
		FoundPool = FParse::Value(*HordeConfig, TEXT("MacPool="), Pool);
		#endif

		if (!FoundPool)
		{
			FoundPool = FParse::Value(*HordeConfig, TEXT("Pool="), Pool);
		}

		if (FoundPool)
		{
			UE_LOG(LogUbaController, Log, TEXT("Found UBA controller Pool: \"%s\""), *Pool);
			HordeAgentManager->SetPool(Pool);
		}

		uint32 MaxCores = 0;
		if (FParse::Value(*HordeConfig, TEXT("MaxCores="), MaxCores))
		{
			UE_LOG(LogUbaController, Log, TEXT("Found UBA controller MaxCores: \"%u\""), MaxCores);
			HordeAgentManager->SetMaxCoreCount(MaxCores);
		}
	}

	FString Host;
	if (GConfig->GetString(TEXT("UbaController"), TEXT("Host"), Host, GEngineIni))
	{
		UE_LOG(LogUbaController, Log, TEXT("Found UBA controller Host: \"%s\""), *Host);
		HordeAgentManager->SetUbaHost(StringCast<ANSICHAR>(*Host).Get());
	}

	int32 Port = 0;
	if (GConfig->GetInt(TEXT("UbaController"), TEXT("Port"), Port, GEngineIni))
	{
		UE_LOG(LogUbaController, Log, TEXT("Found UBA controller Port: \"%d\""), Port);
		HordeAgentManager->SetUbaPort(Port);
	}
	
	if (GConfig->GetBool(TEXT("UbaController"), TEXT("bForceRemote"), bForceRemote, GEngineIni))
	{
		UE_LOG(LogUbaController, Log, TEXT("Found UBA controller Force Remote: [%s]"), bForceRemote ? TEXT("True") : TEXT("False"));
	}

	UE_LOG(LogUbaController, Display, TEXT("Created UBA storage server: RootDir=%s"), *RootDir);
}

void FUbaJobProcessor::ShutDownUba()
{
	UE_LOG(LogUbaController, Display, TEXT("Shutting down UBA/Horde connection"));

	HordeAgentManager = nullptr;

	if (UbaSessionServer == nullptr)
	{
		return;
	}

	NetworkServer_Stop(UbaServer);

	Scheduler_Destroy(UbaScheduler);
	SessionServer_Destroy(UbaSessionServer);
	StorageServer_Destroy(UbaStorageServer);
	NetworkServer_Destroy(UbaServer);

	UbaScheduler = nullptr;
	UbaSessionServer = nullptr;
	UbaStorageServer = nullptr;
	UbaServer = nullptr;
}

uint32 FUbaJobProcessor::Run()
{
	bIsWorkDone = false;
	
	uint32 LastTimeSinceHadJobs = FPlatformTime::Cycles();	

	while (!bForceStop)
	{
		const float ElapsedSeconds = (FPlatformTime::Cycles() - LastTimeSinceHadJobs) * FPlatformTime::GetSecondsPerCycle();

		uint32 queued = 0;
		uint32 activeLocal = 0;
		uint32 activeRemote = 0;
		uint32 finished = 0;

		FScopeLock lock(&bShouldProcessJobsLock);

		if (UbaScheduler)
		{
			Scheduler_GetStats(UbaScheduler, queued, activeLocal, activeRemote, finished);
		}
		uint32 active = activeLocal + activeRemote;

		// We don't want to hog up Horde resources.
		if (bShouldProcessJobs && ElapsedSeconds > UbaJobProcessorOptions::MaxTimeWithoutTasks && (queued + active) == 0)
		{
			// If we're optimizing job starting, we only want to shutdown UBA if all the processes have terminated
			bShouldProcessJobs = false;
			ShutDownUba();
		}

		// Check if we have new tasks to process
		if ((ControllerModule.HasTasksDispatchedOrPending() || (queued + active) != 0))
		{
			if (!bShouldProcessJobs)
			{
				// We have new tasks. Start processing again
				StartUba();

				bShouldProcessJobs = true;
			}

			LastTimeSinceHadJobs = FPlatformTime::Cycles();
		}

		if (bShouldProcessJobs)
		{
			int32 MaxPossible = FPlatformMisc::NumberOfCoresIncludingHyperthreads();
			int32 LocalCoresToNotUse = 1 + activeRemote / 30; // Use one core per 30 remote ones
			int32 MaxLocal = FMath::Max(0, MaxPossible - LocalCoresToNotUse);
			MaxLocal = FMath::Min(MaxLocal, MaxLocalParallelJobs);
			Scheduler_SetMaxLocalProcessors(UbaScheduler, bForceRemote ? 0 : MaxLocal);

			int32 TargetCoreCount = FMath::Max(0, int32(queued + active) - MaxLocal);

			HordeAgentManager->SetTargetCoreCount(TargetCoreCount);
			
			// TODO: Not sure this is a good idea in a cooking scenario where number of queued processes are going up and down
			SessionServer_SetMaxRemoteProcessCount(UbaSessionServer, TargetCoreCount);

			UpdateStats();
		}

		lock.Unlock();

		FPlatformProcess::Sleep(UbaJobProcessorOptions::SleepTimeBetweenActions);
	}

	FScopeLock lock(&bShouldProcessJobsLock);

	ShutDownUba();

	bIsWorkDone = true;
	return 0;
}

void FUbaJobProcessor::Stop()
{
	bForceStop = true;
};

void FUbaJobProcessor::StartThread()
{
	Thread = FRunnableThread::Create(this, TEXT("UbaJobProcessor"), 0, TPri_SlightlyBelowNormal, FPlatformAffinity::GetPoolThreadMask());
}

bool FUbaJobProcessor::ProcessOutputFile(FTask* CompileTask)
{
	//TODO: This method is mostly taken from the other Distribution controllers
	// As we get an explicit callback when the process ends, we should be able to simplify this to just check if the file exists
	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	IFileManager& FileManager = IFileManager::Get();

	constexpr uint64 VersionAndFileSizeSize = sizeof(uint32) + sizeof(uint64);	
	if (ensure(CompileTask) && 
		ensureMsgf(PlatformFile.FileExists(*CompileTask->CommandData.OutputFileName), TEXT("Filename=%s"), *CompileTask->CommandData.OutputFileName) &&
		ensureMsgf(FileManager.FileSize(*CompileTask->CommandData.OutputFileName) > VersionAndFileSizeSize,
			TEXT("Filename=%s, FileSize=%d"), *CompileTask->CommandData.OutputFileName, (int32)FileManager.FileSize(*CompileTask->CommandData.OutputFileName)))
	{
		const TUniquePtr<FArchive> OutputFilePtr(FileManager.CreateFileReader(*CompileTask->CommandData.OutputFileName, FILEREAD_Silent));
		if (ensure(OutputFilePtr))
		{
			FArchive& OutputFile = *OutputFilePtr;
			int32 OutputVersion;
			OutputFile << OutputVersion; // NOTE (SB): Do not care right now about the version.
			int64 FileSize = 0;
			OutputFile << FileSize;

			// NOTE (SB): Check if we received the full file yet.
			if (ensure(OutputFile.TotalSize() >= FileSize))
			{
				FTaskResponse TaskCompleted;
				TaskCompleted.ID = CompileTask->ID;
				TaskCompleted.ReturnCode = 0;
						
				ControllerModule.ReportJobProcessed(TaskCompleted, CompileTask);
			}
			else
			{
				UE_LOG(LogUbaController, Error, TEXT("Output file size is not correct [%s] | Expected Size [%lld] : => Actual Size : [%lld]"), *CompileTask->CommandData.OutputFileName, OutputFile.TotalSize(), FileSize);
				return false;
			}
		}
		else
		{
			UE_LOG(LogUbaController, Error, TEXT("Failed open for read Output File [%s]"), *CompileTask->CommandData.OutputFileName);
			return false;
		}
	}
	else
	{
		const FString OutputFileName = CompileTask != nullptr ? *CompileTask->CommandData.OutputFileName : TEXT("Invalid CompileTask, cannot retrieve name");
		UE_LOG(LogUbaController, Error, TEXT("Output File [%s] is invalid or does not exist"), *OutputFileName);
		return false;
	}

	return true;
}

void FUbaJobProcessor::HandleUbaJobFinished(FTask* CompileTask)
{
	const bool bWasSuccessful = ProcessOutputFile(CompileTask);
	if (!bWasSuccessful)
	{
		// If it failed running locally, lets try Run it locally but outside Uba
		// Signaling a jobs as complete when it wasn't done, will cause a rerun on local worker as fallback
		// because there is not output file for this job

		FTaskResponse TaskCompleted;
		TaskCompleted.ID = CompileTask->ID;
		TaskCompleted.ReturnCode = 0;
		ControllerModule.ReportJobProcessed(TaskCompleted, CompileTask);
	}
}

void FUbaJobProcessor::HandleTaskQueueUpdated(const FString& InputFileName)
{
	FScopeLock lock(&bShouldProcessJobsLock);

	if (!UbaScheduler)
	{
		return;
	}

	while (true)
	{
		FTask* Task = nullptr;
		if (!ControllerModule.PendingRequestedCompilationTasks.Dequeue(Task) || !Task)
			break;
		RunTaskWithUba(Task);
	}
}

bool FUbaJobProcessor::HasJobsInFlight() const
{
	if (!UbaScheduler)
	{
		return false;
	}
	uint32 queued = 0;
	uint32 activeLocal = 0;
	uint32 activeRemote = 0;
	uint32 finished = 0;
	Scheduler_GetStats(UbaScheduler, queued, activeLocal, activeRemote, finished);	
	return (queued + activeLocal + activeRemote) != 0;
}

bool FUbaJobProcessor::PollStats(FDistributedBuildStats& OutStats)
{
	// Return current stats and reset internal data
	FScopeLock StatsLockGuard(&StatsLock);
	OutStats = Stats;
	Stats = FDistributedBuildStats();
	return true;
}

void FUbaJobProcessor::UpdateStats()
{
	FScopeLock StatsLockGuard(&StatsLock);

	// Update maximum
	Stats.MaxRemoteAgents = FMath::Max(Stats.MaxRemoteAgents, (uint32)HordeAgentManager->GetAgentCount());
	Stats.MaxActiveAgentCores = FMath::Max(Stats.MaxActiveAgentCores, HordeAgentManager->GetActiveCoreCount());
}
