// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	ShaderCompilerThreadRunnable.cpp:
	Implements FShaderCompileThreadRunnableBase and FShaderCompileThreadRunnable.
=============================================================================*/

#include "ShaderCompilerPrivate.h"
#include "ShaderCompilerMemoryLimit.h"

#include "Async/ParallelFor.h"
#include "HAL/PlatformFile.h"
#include "HAL/PlatformFileManager.h"
#include "Logging/StructuredLog.h"
#include "Misc/ScopeTryLock.h"
#include "ProfilingDebugging/CookStats.h"
#include "ProfilingDebugging/DiagnosticTable.h"
#include "ProfilingDebugging/LoadTimeTracker.h"
#include "ProfilingDebugging/StallDetector.h"


float GShaderCompilerTooLongIOThresholdSeconds = 0.3;
static FAutoConsoleVariableRef CVarShaderCompilerTooLongIOThresholdSeconds(
	TEXT("r.ShaderCompiler.TooLongIOThresholdSeconds"),
	GShaderCompilerTooLongIOThresholdSeconds,
	TEXT("By default, task files for SCW will be read/written sequentially, but if we ever spend more than this time (0.3s by default) doing that, we'll switch to parallel.") \
	TEXT("We don't default to parallel writes as it increases the CPU overhead from the shader compiler."),
	ECVF_Default);

static TAutoConsoleVariable<bool> CVarCompileParallelInProcess(
	TEXT("r.ShaderCompiler.ParallelInProcess"),
	false,
	TEXT("EXPERIMENTAL- If true, shader compilation will be executed in-process in parallel. Note that this will serialize if the legacy preprocessor is enabled."),
	ECVF_ReadOnly);

int32 GShaderCompilerMemoryLimit = 0;
static FAutoConsoleVariableRef CVarShaderCompilerMemoryLimit(
	TEXT("r.ShaderCompiler.MemoryLimit"),
	GShaderCompilerMemoryLimit,
	TEXT("Specifies a memory limit (in MiB) for all ShaderCompileWorker (SCW) processes.") \
	TEXT("If the total memory consumption of all SCW processes exceeds this limit, the editor will start to suspend workers and reschedule compile jobs.") \
	TEXT("By default 0, effectively disabling the limitation. If this is non-zero, it must be greater than or equal to 1024 since shader compilation must be granted at least 1024 MiB of memory in total."),
	ECVF_ReadOnly);

// Configuration to retry shader compile through workers after a worker has been abandoned
static constexpr int32 GSingleThreadedRunsDisabled = -2;
static constexpr int32 GSingleThreadedRunsIncreaseFactor = 8;
static constexpr int32 GSingleThreadedRunsMaxCount = (1 << 24);

static const TCHAR* GWorkerInputFilename = TEXT("WorkerInputOnly.in");
static const TCHAR* GWorkerOutputFilename = TEXT("WorkerOutputOnly.out");


static FResourceRestrictedJobObject GSCWResourceRestrictedJobObject(TEXT("UE.ShaderCompileWorker.JobGroup"));

// Apply memory limits (see CVar r.ShaderCompiler.MemoryLimit) to by assigning input process to resource restricted job object
// and initialize this job object here, since we can't guarantee execution order of static global object (i.e. global cvar and the job object).
static void ApplyWorkerProcessMemoryLimits(const FProcHandle& Process)
{
	if (GShaderCompilerMemoryLimit > 0)
	{
		static bool bIsJobObjectLimitInitialized;
		if (!bIsJobObjectLimitInitialized)
		{
			GSCWResourceRestrictedJobObject.SetMemoryLimit(GShaderCompilerMemoryLimit);
			bIsJobObjectLimitInitialized = true;
		}
		GSCWResourceRestrictedJobObject.AssignProcess(Process);
	}
}


/** Information tracked for each shader compile worker process instance. */
struct FShaderCompileWorkerInfo
{
	/** Process handle of the worker app once launched.  Invalid handle means no process. */
	FProcHandle WorkerProcess;

	/** Tracks whether tasks have been issued to the worker. */
	bool bIssuedTasksToWorker;	

	/** Whether the worker has been launched for this set of tasks. */
	bool bLaunchedWorker;

	/** Tracks whether all tasks issued to the worker have been received. */
	bool bComplete;

	/** Whether this worker is available for new jobs. It will be false when shutting down the worker. */
	bool bAvailable;

	/** Time at which the worker started the most recent batch of tasks. */
	double StartTime;

	/** Time at which the worker ended the most recent batch of tasks. */
	double FinishTime;

	/** Jobs that this worker is responsible for compiling. */
	TArray<FShaderCommonCompileJobPtr> QueuedJobs;

	FShaderCompileWorkerInfo() :
		bIssuedTasksToWorker(false),
		bLaunchedWorker(false),
		bComplete(false),
		bAvailable(true),
		StartTime(0.0),
		FinishTime(0.0)
	{
	}

	// warning: not virtual
	~FShaderCompileWorkerInfo()
	{
		TerminateWorkerProcess();
	}

	void TerminateWorkerProcess(bool bAsynchronous = false)
	{
		if (WorkerProcess.IsValid())
		{
			FPlatformProcess::TerminateProc(WorkerProcess);
			if (!bAsynchronous)
			{
				while (FPlatformProcess::IsProcRunning(WorkerProcess))
				{
					FPlatformProcess::Sleep(0.01f);
				}
			}
			FPlatformProcess::CloseProc(WorkerProcess);
			WorkerProcess = FProcHandle();
		}
	}

	int32 CloseWorkerProcess()
	{
		int32 ReturnCode = 0;
		if (WorkerProcess.IsValid())
		{
			FPlatformProcess::GetProcReturnCode(WorkerProcess, &ReturnCode);
			FPlatformProcess::CloseProc(WorkerProcess);
			WorkerProcess = FProcHandle();
		}
		return ReturnCode;
	}
};

FShaderCompileThreadRunnableBase::FShaderCompileThreadRunnableBase(FShaderCompilingManager* InManager)
	: Manager(InManager)
	, Thread(nullptr)
	, MinPriorityIndex(0)
	, MaxPriorityIndex(NumShaderCompileJobPriorities - 1)
	, bForceFinish(false)
{
}
void FShaderCompileThreadRunnableBase::StartThread()
{
	if (Manager->bAllowAsynchronousShaderCompiling && !FPlatformProperties::RequiresCookedData())
	{
		Thread = FRunnableThread::Create(this, GetThreadName(), 0, TPri_Normal, FPlatformAffinity::GetPoolThreadMask());
	}
}

FShaderCompileThreadRunnable::FShaderCompileThreadRunnable(FShaderCompilingManager* InManager)
	: FShaderCompileThreadRunnableBase(InManager)
#if PLATFORM_WINDOWS
	, bEstimateCommittedMemory(FPlatformMisc::IsWine()) // Use alternative code path to estimate memory when we're running on POSIX/Wine instead of a real Windows host system
#else
	, bEstimateCommittedMemory(true) // Use alternative code path to estimate memory when we're running on POSIX
#endif
{
	for (uint32 WorkerIndex = 0; WorkerIndex < Manager->NumShaderCompilingThreads; WorkerIndex++)
	{
		WorkerInfos.Add(MakeUnique<FShaderCompileWorkerInfo>());
	}
}

FShaderCompileThreadRunnable::~FShaderCompileThreadRunnable()
{
	FScopeLock WorkerScopeLock(&WorkerInfosLock);
	WorkerInfos.Empty();
}

void FShaderCompileThreadRunnable::OnMachineResourcesChanged()
{
	bool bWaitForWorkersToShutdown = false;
	{
		FScopeLock WorkerScopeLock(&WorkerInfosLock);
		// Set all bAvailable flags back to true
		for (TUniquePtr< FShaderCompileWorkerInfo>& WorkerInfo : WorkerInfos)
		{
			WorkerInfo->bAvailable = true;
		}

		if (Manager->NumShaderCompilingThreads >= static_cast<uint32>(WorkerInfos.Num()))
		{
			while (static_cast<uint32>(WorkerInfos.Num()) < Manager->NumShaderCompilingThreads)
			{
				WorkerInfos.Add(MakeUnique<FShaderCompileWorkerInfo>());
			}
		}
		else
		{
			for (int32 Index = 0; Index < WorkerInfos.Num(); ++Index)
			{
				FShaderCompileWorkerInfo& WorkerInfo = *WorkerInfos[Index];
				bool bReadyForShutdown = WorkerInfo.QueuedJobs.Num() == 0;
				if (bReadyForShutdown)
				{
					WorkerInfos.RemoveAtSwap(Index--);
					if (WorkerInfos.Num() == Manager->NumShaderCompilingThreads)
					{
						break;
					}
				}
			}
			bWaitForWorkersToShutdown = Manager->NumShaderCompilingThreads < static_cast<uint32>(WorkerInfos.Num());
			for (int32 Index = WorkerInfos.Num() - 1;
				static_cast<uint32>(Index) >= Manager->NumShaderCompilingThreads; --Index)
			{
				WorkerInfos[Index]->bAvailable = false;
			}
		}
	}
	const double StartTime = FPlatformTime::Seconds();
	constexpr float MaxDurationToWait = 60.f;
	const double MaxTimeToWait = StartTime + MaxDurationToWait;
	while (bWaitForWorkersToShutdown)
	{
		FPlatformProcess::Sleep(0.01f);
		const double CurrentTime = FPlatformTime::Seconds();
		if (CurrentTime > MaxTimeToWait)
		{
			UE_LOG(LogShaderCompilers, Warning, TEXT("OnMachineResourcesChanged timedout waiting %.0f seconds for WorkerInfos to complete. Workers will remain allocated."),
				(float)(CurrentTime - StartTime));
			break;
		}

		FScopeLock WorkerScopeLock(&WorkerInfosLock);
		for (int32 Index = WorkerInfos.Num() - 1;
			static_cast<uint32>(Index) >= Manager->NumShaderCompilingThreads; --Index)
		{
			FShaderCompileWorkerInfo& WorkerInfo = *WorkerInfos[Index];
			check(!WorkerInfos[Index]->bAvailable); // It should still be set to false from when we changed it above
			bool bReadyForShutdown = WorkerInfo.QueuedJobs.Num() == 0;
			if (bReadyForShutdown)
			{
				WorkerInfos.RemoveAtSwap(Index);
			}
		}
		bWaitForWorkersToShutdown = Manager->NumShaderCompilingThreads < static_cast<uint32>(WorkerInfos.Num());
	}
}

/** Entry point for the shader compiling thread. */
uint32 FShaderCompileThreadRunnableBase::Run()
{
	LLM_SCOPE_BYTAG(ShaderCompiler);
	check(Manager->bAllowAsynchronousShaderCompiling);
	while (!bForceFinish)
	{
		CompilingLoop();
	}
	UE_LOG(LogShaderCompilers, Display, TEXT("Shaders left to compile 0"));

	return 0;
}

int32 FShaderCompileThreadRunnable::PullTasksFromQueue()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FShaderCompileThreadRunnable::PullTasksFromQueue);

	auto SignalWorkerTasksToBeSubmitted = [this](FShaderCompileWorkerInfo& WorkerInfo, int32 WorkerIndex)
		{
			// Update the worker state as having new tasks that need to be issued					
			// don't reset worker app ID, because the shadercompileworkers don't shutdown immediately after finishing a single job queue.
			WorkerInfo.bIssuedTasksToWorker = false;
			WorkerInfo.bLaunchedWorker = false;
			WorkerInfo.StartTime = FPlatformTime::Seconds();

			if (WorkerInfo.FinishTime > 0.0)
			{
				const double WorkerIdleTime = WorkerInfo.StartTime - WorkerInfo.FinishTime;
				GShaderCompilerStats->RegisterLocalWorkerIdleTime(WorkerIdleTime);
				if (Manager->bLogJobCompletionTimes)
				{
					UE_LOG(LogShaderCompilers, Display, TEXT("  Worker (%d/%d) started working after being idle for %fs"), WorkerIndex + 1, WorkerInfos.Num(), WorkerIdleTime);
				}
			}
		};

	// Check if memory limitations has been violated and suspend workers as needed
	if (GShaderCompilerMemoryLimit > 0)
	{
		CheckMemoryLimitViolation();
	}

	FScopeLock WorkerScopeLock(&WorkerInfosLock); // Must be entered before CompileQueueSection

	int32 NumActiveThreads = 0;
	int32 NumJobsStarted[NumShaderCompileJobPriorities] = { 0 };
	{
		// Enter the critical section so we can access the input and output queues
		FScopeLock Lock(&Manager->CompileQueueSection);

		const int32 NumWorkersToFeed = Manager->bCompilingDuringGame ? Manager->NumShaderCompilingThreadsDuringGame : GetNumberOfAvailableWorkersUnsafe();

		// Pull tasks from backlogged queue first
		if (!BackloggedJobs.IsEmpty())
		{
			// Try to distribute the work evenly between the workers
			const int32 PriorityIndex = static_cast<int32>(EShaderCompileJobPriority::Normal);
			const int32 NumJobsPerWorker = FMath::DivideAndRoundUp(BackloggedJobs.Num(), NumWorkersToFeed);

			int32 NumWorkersToPickupBacklog = 0;
			int32 NumPickedupBackloggedJobs = 0;

			for (int32 WorkerIndex = 0; WorkerIndex < WorkerInfos.Num(); WorkerIndex++)
			{
				FShaderCompileWorkerInfo& CurrentWorkerInfo = *WorkerInfos[WorkerIndex];

				// If this worker doesn't have any queued jobs, look for more in the input queue
				if (CurrentWorkerInfo.QueuedJobs.Num() == 0 && CurrentWorkerInfo.bAvailable)
				{
					check(!CurrentWorkerInfo.bComplete);

					if (BackloggedJobs.Num() > 0)
					{
						const int32 MaxNumJobs = FMath::Min3(NumJobsPerWorker, BackloggedJobs.Num(), Manager->MaxShaderJobBatchSize);

						// Dequeue backlogged jobs and send them to worker
						CurrentWorkerInfo.QueuedJobs.Reserve(CurrentWorkerInfo.QueuedJobs.Num() + MaxNumJobs);
						for (int32 JobIndex = 0; JobIndex < MaxNumJobs; ++JobIndex)
						{
							CurrentWorkerInfo.QueuedJobs.Add(BackloggedJobs.Pop());
						}
						NumJobsStarted[PriorityIndex] += MaxNumJobs;

						NumPickedupBackloggedJobs += MaxNumJobs;
						NumWorkersToPickupBacklog += 1;

						SignalWorkerTasksToBeSubmitted(CurrentWorkerInfo, WorkerIndex);
					}
				}
			}

			if (NumPickedupBackloggedJobs > 0)
			{
				UE_LOG(
					LogShaderCompilers, Verbose, TEXT("Picked up %d backlogged compile %s and distributed them over %d %s"),
					NumPickedupBackloggedJobs,
					NumPickedupBackloggedJobs == 1 ? TEXT("job") : TEXT("jobs"),
					NumWorkersToPickupBacklog,
					NumWorkersToPickupBacklog == 1 ? TEXT("worker") : TEXT("workers")
				);
			}
		}

		// Pull tasks from compiling manager queue
		for (int32 PriorityIndex = MaxPriorityIndex; PriorityIndex >= MinPriorityIndex; --PriorityIndex)
		{
			int32 NumPendingJobs = Manager->AllJobs.GetNumPendingJobs((EShaderCompileJobPriority)PriorityIndex);

			// Try to distribute the work evenly between the workers
			const int32 NumJobsPerWorker = FMath::DivideAndRoundUp(NumPendingJobs, NumWorkersToFeed);

			for (int32 WorkerIndex = 0; WorkerIndex < WorkerInfos.Num(); WorkerIndex++)
			{
				FShaderCompileWorkerInfo& CurrentWorkerInfo = *WorkerInfos[WorkerIndex];

				// If this worker doesn't have any queued jobs, look for more in the input queue
				if (CurrentWorkerInfo.QueuedJobs.Num() == 0 && CurrentWorkerInfo.bAvailable)
				{
					check(!CurrentWorkerInfo.bComplete);

					NumPendingJobs = Manager->AllJobs.GetNumPendingJobs((EShaderCompileJobPriority)PriorityIndex);
					if (NumPendingJobs > 0)
					{
						UE_LOG(LogShaderCompilers, Verbose, TEXT("Worker (%d/%d): shaders left to compile %i"), WorkerIndex + 1, WorkerInfos.Num(), NumPendingJobs);

						int32 MaxNumJobs = 1;
						// high priority jobs go in 1 per "batch", unless the engine is still starting up
						if (PriorityIndex < (int32)EShaderCompileJobPriority::High || Manager->IgnoreAllThrottling())
						{
							MaxNumJobs = FMath::Min3(NumJobsPerWorker, NumPendingJobs, Manager->MaxShaderJobBatchSize);
						}

						NumJobsStarted[PriorityIndex] += Manager->AllJobs.GetPendingJobs(EShaderCompilerWorkerType::LocalThread, (EShaderCompileJobPriority)PriorityIndex, 1, MaxNumJobs, CurrentWorkerInfo.QueuedJobs);

						SignalWorkerTasksToBeSubmitted(CurrentWorkerInfo, WorkerIndex);
					}
				}
			}
		}
	}

	for (int32 WorkerIndex = 0; WorkerIndex < WorkerInfos.Num(); WorkerIndex++)
	{
		if (WorkerInfos[WorkerIndex]->QueuedJobs.Num() > 0)
		{
			NumActiveThreads++;
		}
	}

	for (int32 PriorityIndex = 0; PriorityIndex < NumShaderCompileJobPriorities; ++PriorityIndex)
	{
		if (NumJobsStarted[PriorityIndex] > 0)
		{
			UE_LOG(LogShaderCompilers, Verbose, TEXT("Started %d 'Local' shader compile jobs with '%s' priority"),
				NumJobsStarted[PriorityIndex],
				ShaderCompileJobPriorityToString((EShaderCompileJobPriority)PriorityIndex));
		}
	}

	return NumActiveThreads;
}

void FShaderCompileThreadRunnable::PushCompletedJobsToManager()
{
	FScopeLock WorkerScopeLock(&WorkerInfosLock); // Must be entered before CompileQueueSection

	for (int32 WorkerIndex = 0; WorkerIndex < WorkerInfos.Num(); WorkerIndex++)
	{
		FShaderCompileWorkerInfo& CurrentWorkerInfo = *WorkerInfos[WorkerIndex];

		// Add completed jobs to the output queue, which is ShaderMapJobs
		if (CurrentWorkerInfo.bComplete)
		{
			// Enter the critical section so we can access the input and output queues
			FScopeLock Lock(&Manager->CompileQueueSection);

			for (int32 JobIndex = 0; JobIndex < CurrentWorkerInfo.QueuedJobs.Num(); JobIndex++)
			{
				auto& Job = CurrentWorkerInfo.QueuedJobs[JobIndex];

				Manager->ProcessFinishedJob(Job.GetReference());
			}

			const double ElapsedTime = FPlatformTime::Seconds() - CurrentWorkerInfo.StartTime;

			Manager->WorkersBusyTime += ElapsedTime;
			COOK_STAT(ShaderCompilerCookStats::AsyncCompileTimeSec += ElapsedTime);


			CurrentWorkerInfo.FinishTime = FPlatformTime::Seconds();
			CurrentWorkerInfo.bComplete = false;
			CurrentWorkerInfo.QueuedJobs.Empty();
		}
	}
}

void FShaderCompileThreadRunnable::WriteNewTasks()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(ShaderCompiler.WriteNewTasks);
	FScopeLock WorkerScopeLock(&WorkerInfosLock);

	// first, a quick check if anything is needed just to avoid hammering the task graph
	bool bHasTasksToWrite = false;
	for (int32 WorkerIndex = 0, NumWorkers = WorkerInfos.Num(); WorkerIndex < NumWorkers; ++WorkerIndex)
	{
		FShaderCompileWorkerInfo& CurrentWorkerInfo = *WorkerInfos[WorkerIndex];
		if (!CurrentWorkerInfo.bIssuedTasksToWorker && CurrentWorkerInfo.QueuedJobs.Num() > 0)
		{
			bHasTasksToWrite = true;
			break;
		}
	}

	if (!bHasTasksToWrite)
	{
		return;
	}


	auto LoopBody = [this](int32 WorkerIndex)
	{
		// The calling thread holds the WorkerInfosLock and will not modify WorkerInfos, 
		// so we can access it here without entering the lock
		FShaderCompileWorkerInfo& CurrentWorkerInfo = *WorkerInfos[WorkerIndex];

		// Only write tasks once
		if (!CurrentWorkerInfo.bIssuedTasksToWorker && CurrentWorkerInfo.QueuedJobs.Num() > 0)
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(ShaderCompiler.WriteNewTasksForWorker);
			CurrentWorkerInfo.bIssuedTasksToWorker = true;

			const FString WorkingDirectory = GetWorkingDirectoryForWorker(WorkerIndex);

			// To make sure that the process waiting for input file won't try to read it until it's ready
			// we use a temp file name during writing.
			FString TransferFileName;
			do
			{
				FGuid Guid;
				FPlatformMisc::CreateGuid(Guid);
				TransferFileName = FPaths::Combine(WorkingDirectory, Guid.ToString());
			} while (IFileManager::Get().FileSize(*TransferFileName) != INDEX_NONE);

			// Write out the file that the worker app is waiting for, which has all the information needed to compile the shader.
			// 'Only' indicates that the worker should keep checking for more tasks after this one
			FArchive* TransferFile = nullptr;

			int32 RetryCount = 0;
			// Retry over the next two seconds if we can't write out the input file
			// Anti-virus and indexing applications can interfere and cause this write to fail
			//@todo - switch to shared memory or some other method without these unpredictable hazards
			while (TransferFile == nullptr && RetryCount < 2000)
			{
				if (RetryCount > 0)
				{
					FPlatformProcess::Sleep(0.01f);
				}
				TransferFile = IFileManager::Get().CreateFileWriter(*TransferFileName, FILEWRITE_EvenIfReadOnly);
				RetryCount++;
				if (TransferFile == nullptr)
				{
					UE_LOG(LogShaderCompilers, Warning, TEXT("Could not create the shader compiler transfer file '%s', retrying..."), *TransferFileName);
				}
			}
			if (TransferFile == nullptr)
			{
				UE_LOG(LogShaderCompilers, Fatal, TEXT("Could not create the shader compiler transfer file '%s'."), *TransferFileName);
			}
			check(TransferFile);

			GShaderCompilerStats->RegisterJobBatch(CurrentWorkerInfo.QueuedJobs.Num(), FShaderCompilerStats::EExecutionType::Local);
			if (!FShaderCompileUtilities::DoWriteTasks(CurrentWorkerInfo.QueuedJobs, *TransferFile))
			{
				uint64 TotalDiskSpace = 0;
				uint64 FreeDiskSpace = 0;
				FPlatformMisc::GetDiskTotalAndFreeSpace(TransferFileName, TotalDiskSpace, FreeDiskSpace);
				UE_LOG(LogShaderCompilers, Error, TEXT("Could not write the shader compiler transfer filename to '%s' (Free Disk Space: %llu."), *TransferFileName, FreeDiskSpace);
			}
			delete TransferFile;

#if 0 // debugging code to dump the worker inputs
			static FCriticalSection ArchiveLock;
			{
				FScopeLock Locker(&ArchiveLock);
				static int ArchivedTransferFileNum = 0;
				FString JobCacheDir = ShaderCompiler::IsJobCacheEnabled() ? TEXT("JobCache") : TEXT("NoJobCache");
				FString ArchiveDir = FPaths::ProjectSavedDir() / TEXT("ArchivedWorkerInputs") / JobCacheDir;
				FString ArchiveName = FString::Printf(TEXT("Input-%d"), ArchivedTransferFileNum++);
				FString ArchivePath = ArchiveDir / ArchiveName;
				if (!IFileManager::Get().Copy(*ArchivePath, *TransferFileName))
				{
					UE_LOG(LogInit, Error, TEXT("Could not copy file %s to %s"), *TransferFileName, *ArchivePath);
					ensure(false);
				}
			}
#endif

			// Change the transfer file name to proper one
			FString ProperTransferFileName = FPaths::Combine(WorkingDirectory, GWorkerInputFilename);
			if (!IFileManager::Get().Move(*ProperTransferFileName, *TransferFileName))
			{
				uint64 TotalDiskSpace = 0;
				uint64 FreeDiskSpace = 0;
				FPlatformMisc::GetDiskTotalAndFreeSpace(TransferFileName, TotalDiskSpace, FreeDiskSpace);
				UE_LOG(LogShaderCompilers, Error, TEXT("Could not rename the shader compiler transfer filename to '%s' from '%s' (Free Disk Space: %llu)."), *ProperTransferFileName, *TransferFileName, FreeDiskSpace);
			}
		}
	};

	if (bParallelizeIO)
	{
		ParallelFor( TEXT("ShaderCompiler.WriteNewTasks.PF"), WorkerInfos.Num(),1, LoopBody, EParallelForFlags::Unbalanced);
	}
	else
	{
		double StartIOWork = FPlatformTime::Seconds();
		for (int32 WorkerIndex = 0; WorkerIndex < WorkerInfos.Num(); WorkerIndex++)
		{
			LoopBody(WorkerIndex);
		}

		double IODuration = FPlatformTime::Seconds() - StartIOWork;
		if (IODuration > GShaderCompilerTooLongIOThresholdSeconds)
		{
			UE_LOG(LogShaderCompilers, Display, TEXT("FShaderCompileThreadRunnable::WriteNewTasks() took too long (%.3f seconds, threshold is %.3f s), will parallelize next time."), IODuration, GShaderCompilerTooLongIOThresholdSeconds);
			bParallelizeIO = true;
		}
	}
}

bool FShaderCompileThreadRunnable::LaunchWorkersIfNeeded()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FShaderCompileThreadRunnable::LaunchWorkersIfNeeded);

	const double CurrentTime = FPlatformTime::Seconds();
	// Limit how often we check for workers running since IsApplicationRunning eats up some CPU time on Windows
	const bool bCheckForWorkerRunning = (CurrentTime - LastCheckForWorkersTime > .1f);
	bool bAbandonWorkers = false;
	uint32_t NumberLaunched = 0;

	if (bCheckForWorkerRunning)
	{
		LastCheckForWorkersTime = CurrentTime;
	}

	FScopeLock WorkerScopeLock(&WorkerInfosLock);
	for (int32 WorkerIndex = 0; WorkerIndex < WorkerInfos.Num(); WorkerIndex++)
	{
		FShaderCompileWorkerInfo& CurrentWorkerInfo = *WorkerInfos[WorkerIndex];
		if (CurrentWorkerInfo.QueuedJobs.Num() == 0)
		{
			// Skip if nothing to do
			// Also, use the opportunity to free OS resources by cleaning up handles of no more running processes
			if (CurrentWorkerInfo.WorkerProcess.IsValid() && !FShaderCompilingManager::IsShaderCompilerWorkerRunning(CurrentWorkerInfo.WorkerProcess))
			{
				CurrentWorkerInfo.CloseWorkerProcess();
			}
			continue;
		}

		if (!CurrentWorkerInfo.WorkerProcess.IsValid() || (bCheckForWorkerRunning && !FShaderCompilingManager::IsShaderCompilerWorkerRunning(CurrentWorkerInfo.WorkerProcess)))
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(FShaderCompileThreadRunnable::LaunchingWorkers);

			// @TODO: dubious design - worker should not be launched unless we know there's more work to do.
			bool bLaunchAgain = true;

			// Detect when the worker has exited due to fatal error
			// bLaunchedWorker check here is necessary to distinguish between 'process isn't running because it crashed' and 'process isn't running because it exited cleanly and the outputfile was already consumed'
			if (CurrentWorkerInfo.WorkerProcess.IsValid())
			{
				// shader compiler exited one way or another, so clear out the stale PID.
				const int32 ReturnCode = CurrentWorkerInfo.CloseWorkerProcess();

				if (CurrentWorkerInfo.bLaunchedWorker)
				{
					const FString OutputFileNameAndPath = FPaths::Combine(GetWorkingDirectoryForWorker(WorkerIndex), GWorkerOutputFilename);

					if (FPlatformFileManager::Get().GetPlatformFile().FileExists(*OutputFileNameAndPath))
					{
						// If the worker is no longer running but it successfully wrote out the output, no need to assert
						bLaunchAgain = false;
					}
					else
					{
						UE_LOG(LogShaderCompilers, Error, TEXT("ShaderCompileWorker terminated unexpectedly, return code %d! Falling back to directly compiling which will be very slow.  Thread %u."), ReturnCode, WorkerIndex);
						LogQueuedCompileJobs(CurrentWorkerInfo.QueuedJobs, -1);

						bAbandonWorkers = true;
						break;
					}
				}
			}

			if (bLaunchAgain)
			{
				constexpr bool bRelativePath = true;
				const FString WorkingDirectory = GetWorkingDirectoryForWorker(WorkerIndex, bRelativePath);

				// Store the handle with this thread so that we will know not to launch it again
				CurrentWorkerInfo.WorkerProcess = Manager->LaunchWorker(WorkingDirectory, Manager->ProcessId, WorkerIndex, GWorkerInputFilename, GWorkerOutputFilename);
				CurrentWorkerInfo.bLaunchedWorker = true;

				// Assign process to job object to monitor the total memory consumption of all SCW processes. Ignore if we only estimate the memory due to partial support in POSIX/Wine.
				if (!bEstimateCommittedMemory)
				{
					ApplyWorkerProcessMemoryLimits(CurrentWorkerInfo.WorkerProcess);
				}

				NumberLaunched++;
			}
		}
	}

	const double FinishTime = FPlatformTime::Seconds();
	if (NumberLaunched > 0 && (FinishTime - CurrentTime) >= 10.0)
	{
		UE_LOG(LogShaderCompilers, Warning, TEXT("Performance Warning: It took %f seconds to launch %d ShaderCompileWorkers"), FinishTime - CurrentTime, NumberLaunched);
	}

	return bAbandonWorkers;
}

int32 FShaderCompileThreadRunnable::ReadAvailableResults()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(ShaderCompiler.ReadAvailableResults);
	int32 NumProcessed = 0;
	FScopeLock WorkerScopeLock(&WorkerInfosLock);

	// first, a quick check if anything is needed just to avoid hammering the task graph
	bool bHasQueuedJobs = false;
	for (int32 WorkerIndex = 0, NumWorkers = WorkerInfos.Num(); WorkerIndex < NumWorkers; ++WorkerIndex)
	{
		if (WorkerInfos[WorkerIndex]->QueuedJobs.Num() > 0)
		{
			bHasQueuedJobs = true;
			break;
		}
	}

	if (!bHasQueuedJobs)
	{
		return NumProcessed;
	}

	auto LoopBody = [this, &NumProcessed](int32 WorkerIndex)
	{
		// The calling thread holds the WorkerInfosLock and will not modify WorkerInfos, 
		// so we can access it here without entering the lock
		FShaderCompileWorkerInfo& CurrentWorkerInfo = *WorkerInfos[WorkerIndex];

		// Check for available result files
		if (CurrentWorkerInfo.QueuedJobs.Num() > 0)
		{
			// Distributed compiles always use the same directory
			// 'Only' indicates to the worker that it should log and continue checking for the input file after the first one is processed
			const FString OutputFileNameAndPath = FPaths::Combine(GetWorkingDirectoryForWorker(WorkerIndex), GWorkerOutputFilename);

			// In the common case the output file will not exist, so check for existence before opening
			// This is only a win if FileExists is faster than CreateFileReader, which it is on Windows
			if (FPlatformFileManager::Get().GetPlatformFile().FileExists(*OutputFileNameAndPath))
			{
				TRACE_CPUPROFILER_EVENT_SCOPE(FShaderCompileThreadRunnable::ProcessOutputFile);

				if (TUniquePtr<FArchive> OutputFile = TUniquePtr<FArchive>(IFileManager::Get().CreateFileReader(*OutputFileNameAndPath, FILEREAD_Silent)))
				{
					check(!CurrentWorkerInfo.bComplete);
					FShaderCompileUtilities::DoReadTaskResults(CurrentWorkerInfo.QueuedJobs, *OutputFile);

					// Close the output file.
					OutputFile.Reset();

					// Delete the output file now that we have consumed it, to avoid reading stale data on the next compile loop.
					bool bDeletedOutput = IFileManager::Get().Delete(*OutputFileNameAndPath, true, true);
					int32 RetryCount = 0;
					// Retry over the next two seconds if we couldn't delete it
					while (!bDeletedOutput && RetryCount < 200)
					{
						TRACE_CPUPROFILER_EVENT_SCOPE(FShaderCompileThreadRunnable::DeleteOutputFile);

						FPlatformProcess::Sleep(0.01f);
						bDeletedOutput = IFileManager::Get().Delete(*OutputFileNameAndPath, true, true);
						RetryCount++;
					}
					checkf(bDeletedOutput, TEXT("Failed to delete %s!"), *OutputFileNameAndPath);

					CurrentWorkerInfo.bComplete = true;
				}

				FPlatformAtomics::InterlockedIncrement(&NumProcessed);
			}
		}
	};

	if (bParallelizeIO)
	{
		ParallelFor( TEXT("ShaderCompiler.ReadAvailableResults.PF"),WorkerInfos.Num(),1, LoopBody, EParallelForFlags::Unbalanced);
	}
	else 
	{
		double StartIOWork = FPlatformTime::Seconds();
		for (int32 WorkerIndex = 0; WorkerIndex < WorkerInfos.Num(); WorkerIndex++)
		{
			LoopBody(WorkerIndex);
		}

		double IODuration = FPlatformTime::Seconds() - StartIOWork;
		if (IODuration > 0.3)
		{
			UE_LOG(LogShaderCompilers, Display, TEXT("FShaderCompileThreadRunnable::WriteNewTasks() took too long (%.3f seconds, threshold is %.3f s), will parallelize next time."), IODuration, GShaderCompilerTooLongIOThresholdSeconds);
			bParallelizeIO = true;
		}
	}

	return NumProcessed;
}

void FShaderCompileThreadRunnable::CompileDirectlyThroughDll()
{
	// If we aren't compiling through workers, so we can just track the serial time here.
	COOK_STAT(FScopedDurationTimer CompileTimer (ShaderCompilerCookStats::AsyncCompileTimeSec));

	FScopeLock WorkerScopeLock(&WorkerInfosLock);
	for (int32 WorkerIndex = 0; WorkerIndex < WorkerInfos.Num(); WorkerIndex++)
	{
		FShaderCompileWorkerInfo& CurrentWorkerInfo = *WorkerInfos[WorkerIndex];

		if (CurrentWorkerInfo.QueuedJobs.Num() > 0)
		{
			DumpWorkerInputs(CurrentWorkerInfo.QueuedJobs);

			for (int32 JobIndex = 0; JobIndex < CurrentWorkerInfo.QueuedJobs.Num(); JobIndex++)
			{
				FShaderCommonCompileJob& CurrentJob = *CurrentWorkerInfo.QueuedJobs[JobIndex];
				FShaderCompileUtilities::ExecuteShaderCompileJob(CurrentJob);
			}

			CurrentWorkerInfo.bComplete = true;
		}
	}
}

void FShaderCompileThreadRunnable::PrintWorkerMemoryUsageWithLockTaken()
{
	FPlatformProcessMemoryStats TotalMemoryStats{};
	int32 NumValidWorkers = 0;
	constexpr int64 Gibibyte = 1024 * 1024 * 1024;
	for (int32 Iter = 0, End = WorkerInfos.Num(); Iter < End; Iter++)
	{
		const TUniquePtr<FShaderCompileWorkerInfo>& WorkerInfo = WorkerInfos[Iter];
		FProcHandle ProcHandle = WorkerInfo->WorkerProcess;
		if (!ProcHandle.IsValid())
		{
			continue;
		}
		FPlatformProcessMemoryStats MemoryStats;
		if (FPlatformProcess::TryGetMemoryUsage(ProcHandle, MemoryStats))
		{
			NumValidWorkers++;
			UE_LOG(LogShaderCompilers, Display,
				TEXT("ShaderCompileWorker [%d/%d] MemoryStats:")
				TEXT("\n\t     UsedPhysical %llu (%.2f GiB)")
				TEXT("\n\t PeakUsedPhysical %llu (%.2f GiB)")
				TEXT("\n\t      UsedVirtual %llu (%.2f GiB)")
				TEXT("\n\t  PeakUsedVirtual %llu (%.2f GiB)"),
				Iter + 1,
				End,
				MemoryStats.UsedPhysical, double(MemoryStats.UsedPhysical) / Gibibyte,
				MemoryStats.PeakUsedPhysical, double(MemoryStats.PeakUsedPhysical) / Gibibyte,
				MemoryStats.UsedVirtual, double(MemoryStats.UsedVirtual) / Gibibyte,
				MemoryStats.PeakUsedVirtual, double(MemoryStats.PeakUsedVirtual) / Gibibyte
			);
			TotalMemoryStats.UsedPhysical += MemoryStats.UsedPhysical;
			TotalMemoryStats.PeakUsedPhysical += MemoryStats.PeakUsedPhysical;
			TotalMemoryStats.UsedVirtual += MemoryStats.PeakUsedVirtual;
			TotalMemoryStats.PeakUsedVirtual += MemoryStats.PeakUsedVirtual;
		}
		LogQueuedCompileJobs(WorkerInfo->QueuedJobs, -1);
	}

	if (NumValidWorkers > 0)
	{
		UE_LOG(LogShaderCompilers, Display,
			TEXT("Sum of MemoryStats for %d ShaderCompileWorker(s):")
			TEXT("\n\t     UsedPhysical %llu (%.2f GiB)")
			TEXT("\n\t PeakUsedPhysical %llu (%.2f GiB)")
			TEXT("\n\t      UsedVirtual %llu (%.2f GiB)")
			TEXT("\n\t  PeakUsedVirtual %llu (%.2f GiB)"),
			NumValidWorkers,
			TotalMemoryStats.UsedPhysical, double(TotalMemoryStats.UsedPhysical) / Gibibyte,
			TotalMemoryStats.PeakUsedPhysical, double(TotalMemoryStats.PeakUsedPhysical) / Gibibyte,
			TotalMemoryStats.UsedVirtual, double(TotalMemoryStats.UsedVirtual) / Gibibyte,
			TotalMemoryStats.PeakUsedVirtual, double(TotalMemoryStats.PeakUsedVirtual) / Gibibyte
		);
	}
}

int32 FShaderCompileThreadRunnable::GetNumberOfWorkers() const
{
	FScopeLock WorkerScopeLock(&WorkerInfosLock);
	return WorkerInfos.Num();
}

int32 FShaderCompileThreadRunnable::GetNumberOfAvailableWorkersUnsafe() const
{
	// Don't lock WorkerScopeLock critical section here, since this function might be called inside an already locked scope, hence the "Unsafe" name
	int32 NumAvailableWorkers = 0;

	for (const TUniquePtr<FShaderCompileWorkerInfo>& WorkerInfo : this->WorkerInfos)
	{
		if (WorkerInfo->bAvailable)
		{
			++NumAvailableWorkers;
		}
	}

	return NumAvailableWorkers;
}

int32 FShaderCompileThreadRunnable::GetNumberOfAvailableWorkers() const
{
	FScopeLock WorkerScopeLock(&WorkerInfosLock);
	return GetNumberOfAvailableWorkersUnsafe();
}

int32 FShaderCompileThreadRunnable::GetNumberOfSuspendedWorkersUnsafe() const
{
	return WorkerInfos.Num() - GetNumberOfAvailableWorkersUnsafe();
}

int32 FShaderCompileThreadRunnable::SuspendWorkersAndBacklogJobs(int32 NumWorkersToSuspend, int32* OutNumBackloggedJobs)
{
	int32 NumSuspendedWorkers = 0;
	int32 NumBackloggedJobs = 0;

	// Before suspending workers, we need to know how many workers are available to ensure there is always at least one worker available
	if (NumWorkersToSuspend > 0)
	{
		FScopeLock WorkerScopeLock(&WorkerInfosLock);
		const int32 NumAvailableWorkers = GetNumberOfAvailableWorkersUnsafe();
		NumWorkersToSuspend = FMath::Min(NumWorkersToSuspend, NumAvailableWorkers - 1);

		if (NumWorkersToSuspend > 0)
		{
			for (int32 WorkerIndex = 0; WorkerIndex < WorkerInfos.Num(); ++WorkerIndex)
			{
				FShaderCompileWorkerInfo& CurrentWorkerInfo = *WorkerInfos[WorkerIndex];
				if (CurrentWorkerInfo.bAvailable)
				{
					// Suspend worker: Terminate its process immediately as we want to free up system resources.
					// Also discard its output file if it has already created one. Otherwise, this file will be linked to the wrong compile jobs.
					CurrentWorkerInfo.bAvailable = false;
					CurrentWorkerInfo.TerminateWorkerProcess();
					DiscardWorkerOutputFile(WorkerIndex);

					// Move its jobs into the backlog queue
					BackloggedJobs.Reserve(BackloggedJobs.Num() + CurrentWorkerInfo.QueuedJobs.Num());
					for (FShaderCommonCompileJobPtr& QueuedJob : CurrentWorkerInfo.QueuedJobs)
					{
						BackloggedJobs.Add(QueuedJob);
					}
					NumBackloggedJobs += CurrentWorkerInfo.QueuedJobs.Num();
					CurrentWorkerInfo.QueuedJobs.Empty();

					// No more workers to suspend? Early exit loop.
					++NumSuspendedWorkers;
					if (NumSuspendedWorkers == NumWorkersToSuspend)
					{
						break;
					}
				}
			}
		}
	}

	if (OutNumBackloggedJobs)
	{
		*OutNumBackloggedJobs = NumBackloggedJobs;
	}

	return NumSuspendedWorkers;
}

int32 FShaderCompileThreadRunnable::ResumeSuspendedWorkers(int32 NumWorkersToResume)
{
	int32 NumResumedWorkers = 0;

	if (NumWorkersToResume > 0)
	{
		FScopeLock WorkerScopeLock(&WorkerInfosLock);
		const int32 NumSuspendedWorkers = GetNumberOfSuspendedWorkersUnsafe();
		NumWorkersToResume = FMath::Min(NumWorkersToResume, NumSuspendedWorkers);

		if (NumWorkersToResume > 0)
		{
			for (int32 WorkerIndex = 0; WorkerIndex < WorkerInfos.Num(); ++WorkerIndex)
			{
				FShaderCompileWorkerInfo& CurrentWorkerInfo = *WorkerInfos[WorkerIndex];
				if (!CurrentWorkerInfo.bAvailable)
				{
					// Resume worker by making it available again. It will pick up jobs next time tasks are pulled from the queue.
					CurrentWorkerInfo.bAvailable = true;

					// No more workers to suspend? Early exit loop.
					++NumResumedWorkers;
					if (NumResumedWorkers == NumWorkersToResume)
					{
						break;
					}
				}
			}
		}
	}

	return NumResumedWorkers;
}

void FShaderCompileThreadRunnable::DiscardWorkerOutputFile(int32 WorkerIndex)
{
	// If the previously suspended worker left a stale output file, delete it now before it gets picked up and is linked to the wrong input jobs
	const FString OutputFileNameAndPath = FPaths::Combine(GetWorkingDirectoryForWorker(WorkerIndex), GWorkerOutputFilename);
	if (IFileManager::Get().FileExists(*OutputFileNameAndPath))
	{
		UE_LOG(LogShaderCompilers, Verbose, TEXT("Discard stale worker output file: %s"), *OutputFileNameAndPath);
		IFileManager::Get().Delete(*OutputFileNameAndPath);
	}
}

FString FShaderCompileThreadRunnable::GetWorkingDirectoryForWorker(int32 WorkerIndex, bool bRelativePath) const
{
	return FPaths::Combine(bRelativePath ? Manager->ShaderBaseWorkingDirectory : Manager->AbsoluteShaderBaseWorkingDirectory, FString::FromInt(WorkerIndex));
}

void FShaderCompileThreadRunnable::CheckMemoryLimitViolation()
{
	constexpr double kMemoryLimitPollInterval = 0.1; // Check every 0.1s if the memory limit has been exceeded
	constexpr double kResumingWorkersPollInterval = 1.0; // Check every second since the last time workers have been suspending if we can resume some workers again

	const double CurrentTime = FPlatformTime::Seconds();

	// Check memory limit violations periodically
	if (CurrentTime - MemoryMonitoringState.LastTimeOfMemoryLimitPoll > kMemoryLimitPollInterval)
	{
		MemoryMonitoringState.LastTimeOfMemoryLimitPoll = CurrentTime;

		// Check if memory limit has been exceeded
		FJobObjectLimitationInfo LimitInfo;
		if (QueryMemoryLimitViolationStatus(LimitInfo))
		{
			MemoryMonitoringState.LastTimeOfSuspeningOrResumingWorkers = CurrentTime;

			// Try to halve the number of workers
			const int32 NumWorkersToSuspend = GetNumberOfAvailableWorkers() / 2;

			int32 NumBackloggedJobs = 0;
			const int32 NumSuspendedWorkers = SuspendWorkersAndBacklogJobs(NumWorkersToSuspend, &NumBackloggedJobs);
			if (NumSuspendedWorkers > 0)
			{
				UE_LOGFMT_NSLOC(
					LogShaderCompilers, Display, "ShaderCompilers", "SuspendingWorkers",
					"Shader compiler memory usage of {MemoryUsed} MiB exceeded limit of {MemoryLimit} MiB: " \
					"Backlogged {BackloggedJobs} compile {BackloggedJobsName} from {SuspendedWorkers} suspended {SuspendedWorkersName} ({ActiveWorkerCount}/{TotalWorkerCount} active)",
					("MemoryUsed", static_cast<int32>(LimitInfo.MemoryUsed / 1024 / 1024)),
					("MemoryLimit", static_cast<int32>(LimitInfo.MemoryLimit / 1024 / 1024)),
					("BackloggedJobs", NumBackloggedJobs),
					("BackloggedJobsName", NumBackloggedJobs == 1 ? TEXT("job") : TEXT("jobs")),
					("SuspendedWorkers", NumSuspendedWorkers),
					("SuspendedWorkersName", NumSuspendedWorkers == 1 ? TEXT("worker") : TEXT("workers")),
					("ActiveWorkerCount", GetNumberOfAvailableWorkers()),
					("TotalWorkerCount", GetNumberOfWorkers())
				);
				MemoryMonitoringState.bHasSuspendedWorkers = true;
				MemoryMonitoringState.bHasFailedToSuspendWorkers = false;
			}
			else if (!MemoryMonitoringState.bHasFailedToSuspendWorkers)
			{
				UE_LOGFMT_NSLOC(
					LogShaderCompilers, Warning, "ShaderCompilers", "SuspendingWorkersFailed",
					"Shader compiler memory usage of {MemoryUsed} MiB exceeded limit of {MemoryLimit} MiB, but cannot suspend any more workers",
					("MemoryUsed", static_cast<int32>(LimitInfo.MemoryUsed / 1024 / 1024)),
					("MemoryLimit", static_cast<int32>(LimitInfo.MemoryLimit / 1024 / 1024))
				);
				MemoryMonitoringState.bHasFailedToSuspendWorkers = true; // Don't show this warning again unless we were able to suspend workers again
			}
		}
	}

	// Check if we can resume previously suspended workers periodically
	if (MemoryMonitoringState.bHasSuspendedWorkers && CurrentTime - MemoryMonitoringState.LastTimeOfSuspeningOrResumingWorkers > kResumingWorkersPollInterval)
	{
		MemoryMonitoringState.LastTimeOfSuspeningOrResumingWorkers = CurrentTime;

		FJobObjectLimitationInfo LimitInfo;
		if (QueryMemoryStatus(LimitInfo))
		{
			// If we are below half of our memory limit, resume 50% of available workers.
			// This approach suspends workers from 100% to 50% and then resumes them back up to 75%.
			if (LimitInfo.MemoryUsed < LimitInfo.MemoryLimit / 2)
			{
				// Number of workers to resume should be a lower bound (i.e. division by 2 can result in zero),
				// in which case we accept that we cannot allow the last suspended worker to be resumed.
				const int32 NumWorkersToResume = GetNumberOfAvailableWorkers() / 2;
				const int32 NumResumedWorkers = ResumeSuspendedWorkers(NumWorkersToResume);
				if (NumResumedWorkers > 0)
				{
					UE_LOGFMT_NSLOC(
						LogShaderCompilers, Display, "ShaderCompilers", "ResumingWorkers",
						"Resumed {ResumedWorkers} suspended {ResumedWorkersName} since memory usage of {MemoryUsed} MiB is below half the limit of {MemoryLimit} MiB ({ActiveWorkerCount}/{TotalWorkerCount} active)",
						("ResumedWorkers", NumResumedWorkers),
						("ResumedWorkersName", NumResumedWorkers == 1 ? TEXT("worker") : TEXT("workers")),
						("MemoryUsed", static_cast<int32>(LimitInfo.MemoryUsed / 1024 / 1024)),
						("MemoryLimit", static_cast<int32>(LimitInfo.MemoryLimit / 1024 / 1024)),
						("ActiveWorkerCount", GetNumberOfAvailableWorkers()),
						("TotalWorkerCount", GetNumberOfWorkers())
					);
				}
				else
				{
					// No more workers that could be resumed
					MemoryMonitoringState.bHasSuspendedWorkers = false;
				}
			}
		}
	}
}

bool FShaderCompileThreadRunnable::QueryMemoryStatus(FJobObjectLimitationInfo& OutInfo)
{
	if (bEstimateCommittedMemory)
	{
		const FShaderCompileMemoryUsage MemoryUsage = GetExternalWorkerMemoryUsage();
		if (MemoryUsage.VirtualMemory > 0)
		{
			OutInfo.MemoryLimit = static_cast<int64>(GShaderCompilerMemoryLimit) * 1024 * 1024;
			OutInfo.MemoryUsed = MemoryUsage.VirtualMemory;
			return true;
		}
		return false;
	}
	else
	{
		return GSCWResourceRestrictedJobObject.QueryStatus(OutInfo);
	}
}

bool FShaderCompileThreadRunnable::QueryMemoryLimitViolationStatus(FJobObjectLimitationInfo& OutInfo)
{
	if (bEstimateCommittedMemory)
	{
		const FShaderCompileMemoryUsage MemoryUsage = GetExternalWorkerMemoryUsage();
		const int64 MemoryLimitInBytes = static_cast<int64>(GShaderCompilerMemoryLimit) * 1024 * 1024;
		if (MemoryUsage.VirtualMemory >= static_cast<uint64>(MemoryLimitInBytes))
		{
			OutInfo.MemoryLimit = MemoryLimitInBytes;
			OutInfo.MemoryUsed = MemoryUsage.VirtualMemory;
			return true;
		}
		return false;
	}
	else
	{
		return GSCWResourceRestrictedJobObject.QueryLimitViolationStatus(OutInfo);
	}
}

bool FShaderCompileThreadRunnable::PrintWorkerMemoryUsage(bool bAllowToWaitForLock)
{
	if (bAllowToWaitForLock)
	{
		FScopeLock WorkerScopeLock(&WorkerInfosLock);
		PrintWorkerMemoryUsageWithLockTaken();
		return true;
	}
	else
	{
		FScopeTryLock WorkerScopeLock(&WorkerInfosLock);
		if (WorkerScopeLock.IsLocked())
		{
			PrintWorkerMemoryUsageWithLockTaken();
			return true;
		}
		return false;
	}
}

FShaderCompileMemoryUsage FShaderCompileThreadRunnable::GetExternalWorkerMemoryUsage()
{
	FScopeLock WorkerScopeLock(&WorkerInfosLock);
	FShaderCompileMemoryUsage MemoryUsage{};
	for (const TUniquePtr<FShaderCompileWorkerInfo>& WorkerInfo : WorkerInfos)
	{
		FProcHandle ProcHandle = WorkerInfo->WorkerProcess;
		if (!ProcHandle.IsValid())
		{
			continue;
		}
		FPlatformProcessMemoryStats MemoryStats;
		if (FPlatformProcess::TryGetMemoryUsage(ProcHandle, MemoryStats))
		{
			// Virtual memory is committed memory on Windows.
			MemoryUsage.VirtualMemory += MemoryStats.UsedVirtual;
			MemoryUsage.PhysicalMemory += MemoryStats.UsedPhysical;
		}
	}
	return MemoryUsage;
}

int32 FShaderCompileThreadRunnable::CompilingLoop()
{
	if (!Manager->bAllowCompilingThroughWorkers && CVarCompileParallelInProcess.GetValueOnAnyThread())
	{
		int32 NumJobs = Manager->GetNumPendingJobs();
		if (NumJobs == 0)
		{
			return 0;
		}

		// if -noshaderworker is specified and the experimental in-process parallel compile is enabled, submit tasks for a batch 
		// of pending jobs to run wide in-process (and a tailing task to mark those jobs as complete in the manager)
		TUniquePtr<TArray<FShaderCommonCompileJobPtr>> Jobs = TUniquePtr<TArray<FShaderCommonCompileJobPtr>>(new TArray<FShaderCommonCompileJobPtr>());
		TUniquePtr<TArray<float>> JobTimes = TUniquePtr<TArray<float>>(new TArray<float>());
		{
			FScopeLock Lock(&Manager->CompileQueueSection);
			for (int32 PriorityIndex = MaxPriorityIndex; PriorityIndex >= MinPriorityIndex; --PriorityIndex)
			{
				// Throttle how many jobs we kick per tick so we get more frequent progress updates in the UI;
				// this doesn't seem to have much effect on overall throughput.
				const int32 MaxNumJobs = 64;
				Manager->AllJobs.GetPendingJobs(EShaderCompilerWorkerType::LocalThread, (EShaderCompileJobPriority)PriorityIndex, 1, MaxNumJobs, *Jobs);
			}
		}

		JobTimes->SetNum(Jobs->Num());

		TArray<UE::Tasks::FTask> CompileTasks;
		CompileTasks.Reserve(Jobs->Num());
		for (TArray<FShaderCommonCompileJobPtr>::SizeType JobIndex = 0; JobIndex < Jobs->Num(); ++JobIndex)
		{
			FShaderCommonCompileJob* Job = (*Jobs)[JobIndex];
			float* Time = &(*JobTimes)[JobIndex];
			CompileTasks.Add(UE::Tasks::Launch(UE_SOURCE_LOCATION, [Job, Time]()
				{
					const float StartTime = FPlatformTime::Seconds();
					FShaderCompileUtilities::ExecuteShaderCompileJob(*Job);
					*Time = FPlatformTime::Seconds() - StartTime;
				}));
		}

		if (!Jobs->IsEmpty())
		{
			UE::Tasks::Launch(UE_SOURCE_LOCATION, [Jobs = MoveTemp(Jobs), JobTimes = MoveTemp(JobTimes), this]()
				{
					FScopeLock Lock(&Manager->CompileQueueSection);
					for (FShaderCommonCompileJobPtr Job : *Jobs)
					{
						Manager->ProcessFinishedJob(Job.GetReference());
					}

					float ElapsedTime = 0.0f;
					for (const float& JobTime : *JobTimes)
					{
						ElapsedTime += JobTime;
					}

					Manager->WorkersBusyTime += ElapsedTime;
					COOK_STAT(ShaderCompilerCookStats::AsyncCompileTimeSec += ElapsedTime);
				}, CompileTasks);
			// num active threads is up to the task system; as long as we return non-zero
			// this will indicate to the caller that pending jobs remain
			return 1;
		}
		return 0;
	}
	else // compile either through worker processes or single-threaded in-process (depending on Manager->bAllowCompilingThroughWorkers)
	{
		// push completed jobs to Manager->ShaderMapJobs before asking for new ones, so we can free the workers now and avoid them waiting a cycle
		PushCompletedJobsToManager();

		// Grab more shader compile jobs from the input queue
		const int32 NumActiveThreads = PullTasksFromQueue();

		if (NumActiveThreads == 0 && Manager->bAllowAsynchronousShaderCompiling)
		{
			// Yield while there's nothing to do
			// Note: sleep-looping is bad threading practice, wait on an event instead!
			// The shader worker thread does it because it needs to communicate with other processes through the file system
			FPlatformProcess::Sleep(.010f);
		}

		if (Manager->bAllowCompilingThroughWorkers)
		{
			// Write out the files which are input to the shader compile workers
			WriteNewTasks();

			// Launch shader compile workers if they are not already running
			// Workers can time out when idle so they may need to be relaunched
			bool bAbandonWorkers = LaunchWorkersIfNeeded();

			if (bAbandonWorkers)
			{
				// Immediately terminate all worker processes and delete any output files they may have generated;
				// we will re-run all these jobs locally instead.
				for (int32 WorkerIndex = 0; WorkerIndex < WorkerInfos.Num(); ++WorkerIndex)
				{
					WorkerInfos[WorkerIndex]->TerminateWorkerProcess();
					DiscardWorkerOutputFile(WorkerIndex);
				}

				// Fall back to local compiles if the SCW crashed.
				// This is nasty but needed to work around issues where message passing through files to SCW is unreliable on random PCs
				Manager->bAllowCompilingThroughWorkers = false;

				// Try to recover from abandoned workers after a certain amount of single-threaded compilations
				if (Manager->NumSingleThreadedRunsBeforeRetry == GSingleThreadedRunsIdle)
				{
					// First try to recover, only run single-threaded approach once
					Manager->NumSingleThreadedRunsBeforeRetry = 1;
				}
				else if (Manager->NumSingleThreadedRunsBeforeRetry > GSingleThreadedRunsMaxCount)
				{
					// Stop retry approach after too many retries have failed
					Manager->NumSingleThreadedRunsBeforeRetry = GSingleThreadedRunsDisabled;
				}
				else
				{
					// Next time increase runs by factor X
					Manager->NumSingleThreadedRunsBeforeRetry *= GSingleThreadedRunsIncreaseFactor;
				}
			}
			else
			{
				// Read files which are outputs from the shader compile workers
				int32 NumProcessedResults = ReadAvailableResults();
				if (NumProcessedResults == 0)
				{
					// Reduce filesystem query rate while actively waiting for results.
					FPlatformProcess::Sleep(0.1f);
				}
			}
		}
		else
		{
			// Execute all pending worker tasks single-threaded
			CompileDirectlyThroughDll();

			// If single-threaded mode was enabled by an abandoned worker, try to recover after the given amount of runs
			if (Manager->NumSingleThreadedRunsBeforeRetry > 0)
			{
				Manager->NumSingleThreadedRunsBeforeRetry--;
				if (Manager->NumSingleThreadedRunsBeforeRetry == 0)
				{
					UE_LOG(LogShaderCompilers, Display, TEXT("Retry shader compiling through workers."));
					Manager->bAllowCompilingThroughWorkers = true;
				}
			}
		}

		return NumActiveThreads;
	}
}


