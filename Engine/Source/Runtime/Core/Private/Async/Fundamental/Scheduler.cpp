// Copyright Epic Games, Inc. All Rights Reserved.

#include "Async/Fundamental/Scheduler.h"
#include "Async/Fundamental/Task.h"
#include "Async/TaskTrace.h"
#include "Logging/LogMacros.h"
#include "ProfilingDebugging/CsvProfiler.h"
#include "HAL/RunnableThread.h"
#include "Misc/ScopeLock.h"
#include "Misc/CommandLine.h"
#include "Misc/Fork.h"
#include "CoreGlobals.h"

extern CORE_API bool GTaskGraphUseDynamicPrioritization;
extern CORE_API float GTaskGraphOversubscriptionRatio;
extern CORE_API bool GTaskGraphUseDynamicThreadCreation;

CSV_DEFINE_CATEGORY(Scheduler, true);

namespace LowLevelTasks
{
	DEFINE_LOG_CATEGORY(LowLevelTasks);

	thread_local FSchedulerTls::FLocalQueueType* FSchedulerTls::LocalQueue = nullptr;
	thread_local FTask* FTask::ActiveTask = nullptr;
	thread_local FSchedulerTls* FSchedulerTls::ActiveScheduler = nullptr;
	thread_local FSchedulerTls::EWorkerType FSchedulerTls::WorkerType = FSchedulerTls::EWorkerType::None;
	thread_local bool FSchedulerTls::Impl::bIsStandbyWorker = false;
	thread_local bool Private::FOversubscriptionTls::bIsOversubscriptionAllowed = false;

	FScheduler FScheduler::Singleton;

	TUniquePtr<FThread> FScheduler::CreateWorker(uint32 WorkerId, const TCHAR* Name, bool bPermitBackgroundWork, FThread::EForkable IsForkable, Private::FWaitEvent* ExternalWorkerEvent, FSchedulerTls::FLocalQueueType* ExternalWorkerLocalQueue, EThreadPriority Priority, uint64 InAffinity)
	{
		const uint32 WaitTimes[8] = { 719, 991, 1361, 1237, 1597, 953, 587, 1439 };
		uint32 WaitTime = WaitTimes[WorkerId % 8];
		uint64 ThreadAffinityMask = FPlatformAffinity::GetTaskGraphThreadMask();
		if (bPermitBackgroundWork && FPlatformAffinity::GetTaskGraphBackgroundTaskMask() != 0xFFFFFFFFFFFFFFFF)
		{
			ThreadAffinityMask = FPlatformAffinity::GetTaskGraphBackgroundTaskMask();
		}
		if (InAffinity)
		{
			// we can override the affinity!
			ThreadAffinityMask = InAffinity;
		}

		const FProcessorGroupDesc& ProcessorGroups = FPlatformMisc::GetProcessorGroupDesc();
		int32 CpuGroupCount = ProcessorGroups.NumProcessorGroups;
		uint16 CpuGroup = 0;

		//offset the first set of workers to leave space for Game, RHI and Renderthread.
		uint64 GroupWorkerId = WorkerId + 2;
		for (uint16 GroupIndex = 0; GroupIndex < CpuGroupCount; GroupIndex++)
		{
			CpuGroup = GroupIndex;

			uint32 CpusInGroup = FMath::CountBits(ProcessorGroups.ThreadAffinities[GroupIndex]);
			if (GroupWorkerId < CpusInGroup)
			{
				if (CpuGroup != 0) // don't pin larger groups workers to a core and leave first group as is for legacy reasons
				{
					ThreadAffinityMask = MAX_uint64;
				}
				break;
			}
			GroupWorkerId -= CpusInGroup;
		}
		
		return MakeUnique<FThread>
		(
			Name,
			[this, ExternalWorkerEvent, ExternalWorkerLocalQueue, WaitTime, bPermitBackgroundWork]
			{ 
				WorkerMain(ExternalWorkerEvent, ExternalWorkerLocalQueue, WaitTime, bPermitBackgroundWork);
			}, 0, Priority, FThreadAffinity{ ThreadAffinityMask & ProcessorGroups.ThreadAffinities[CpuGroup], CpuGroup }, IsForkable
		);
	}

	void FScheduler::StartWorkers(uint32 NumForegroundWorkers, uint32 NumBackgroundWorkers, FThread::EForkable IsForkable, EThreadPriority InWorkerPriority,  EThreadPriority InBackgroundPriority, uint64 InWorkerAffinity, uint64 InBackgroundAffinity)
	{
		int32 Value = 0;
		if (FParse::Value(FCommandLine::Get(), TEXT("TaskGraphUseDynamicPrioritization="), Value))
		{
			GTaskGraphUseDynamicPrioritization = Value != 0;
		}

		if (FParse::Value(FCommandLine::Get(), TEXT("TaskGraphUseDynamicThreadCreation="), Value))
		{
			GTaskGraphUseDynamicThreadCreation = Value != 0;
		}

		if (NumForegroundWorkers == 0 && NumBackgroundWorkers == 0)
		{
			NumForegroundWorkers = FMath::Max<int32>(1, FMath::Min<int32>(2, FPlatformMisc::NumberOfWorkerThreadsToSpawn() - 1));
			NumBackgroundWorkers = FMath::Max<int32>(1, FPlatformMisc::NumberOfWorkerThreadsToSpawn() - NumForegroundWorkers);
		}

		WorkerPriority = InWorkerPriority;
		BackgroundPriority = InBackgroundPriority;

		if (InWorkerAffinity)
		{
			WorkerAffinity = InWorkerAffinity;
		}
		if (InBackgroundAffinity)
		{
			BackgroundAffinity = InBackgroundAffinity;
		}

		const bool bSupportsMultithreading = FPlatformProcess::SupportsMultithreading() || FForkProcessHelper::IsForkedMultithreadInstance();

		uint32 OldActiveWorkers = ActiveWorkers.load(std::memory_order_relaxed);
		if (OldActiveWorkers == 0 && bSupportsMultithreading && ActiveWorkers.compare_exchange_strong(OldActiveWorkers, NumForegroundWorkers + NumBackgroundWorkers, std::memory_order_relaxed))
		{
			FScopeLock Lock(&WorkerThreadsCS);
			check(!WorkerThreads.Num());
			check(!WorkerLocalQueues.Num());
			check(!WorkerEvents.Num());
			check(NextWorkerId == 0);
			ForegroundCreationIndex = 0;
			BackgroundCreationIndex = 0;

			const float OversubscriptionRatio = FMath::Max(1.0f, GTaskGraphOversubscriptionRatio);
			const int32 MaxForegroundWorkers = FMath::CeilToInt(float(NumForegroundWorkers) * OversubscriptionRatio);
			const int32 MaxBackgroundWorkers = FMath::CeilToInt(float(NumBackgroundWorkers) * OversubscriptionRatio);
			const int32 MaxWorkers = MaxForegroundWorkers + MaxBackgroundWorkers;
			const EThreadPriority ActualBackgroundPriority = GTaskGraphUseDynamicPrioritization ? WorkerPriority : BackgroundPriority;

			WorkerEvents.SetNum(MaxWorkers);
			WorkerLocalQueues.Reserve(MaxWorkers);
			WorkerThreads.Reserve(MaxWorkers);

			auto CreateThread =
				[this, IsForkable](Private::ELocalQueueType LocalQueueType, const TCHAR* ThreadGroup, const TCHAR* Prefix, std::atomic<int32>& CreationIndex, int32 NumWorkers, int32 NumMaxWorkers, EThreadPriority Priority, uint64 Affinity)
				{
					LLM_SCOPE_BYNAME(TEXT("EngineMisc/WorkerThreads"));

					// Thread creation can end up waiting, we don't want to recursively oversubscribe if that happens.
					Private::FOversubscriptionAllowedScope _(false); 

					const int32 LocalCreationIndex = CreationIndex++;
					check(LocalCreationIndex < NumMaxWorkers);
					const bool bIsStandbyWorker = LocalCreationIndex >= NumWorkers;
					FString WorkerName;
					if (bIsStandbyWorker)
					{
						WorkerName = FString::Printf(TEXT("%s Worker (Standby #%d)"), Prefix, LocalCreationIndex - NumWorkers);
					}
					else
					{
						WorkerName = FString::Printf(TEXT("%s Worker #%d"), Prefix, LocalCreationIndex);
					}

					uint32 WorkerId = NextWorkerId++;
					UE::Trace::ThreadGroupBegin(ThreadGroup);
					WorkerLocalQueues.Emplace(QueueRegistry, LocalQueueType);
					WorkerEvents[WorkerId].bIsStandby = bIsStandbyWorker;
					WorkerThreads.Add(
						CreateWorker(
							WorkerId,
							*WorkerName,
							LocalQueueType == Private::ELocalQueueType::EBackground, /* bPermitBackgroundWork */
							IsForkable,
							&WorkerEvents[WorkerId],
							&WorkerLocalQueues[WorkerId],
							Priority,
							Affinity)
					);
					UE::Trace::ThreadGroupEnd();
				};

			TFunction<void()> ForegroundCreateThread =
				[this, CreateThread, NumWorkers = NumForegroundWorkers, MaxWorkers = MaxForegroundWorkers]()
				{
					FScopeLock Lock(&WorkerThreadsCS);
					CreateThread(Private::ELocalQueueType::EForeground, TEXT("Foreground Workers"), TEXT("Foreground"), ForegroundCreationIndex, NumWorkers, MaxWorkers, WorkerPriority, WorkerAffinity);
				};

			TFunction<void()> BackgroundCreateThread =
				[this, CreateThread, NumWorkers = NumBackgroundWorkers, MaxWorkers = MaxBackgroundWorkers, ActualBackgroundPriority]()
				{
					FScopeLock Lock(&WorkerThreadsCS);
					CreateThread(Private::ELocalQueueType::EBackground, TEXT("Background Workers"), TEXT("Background"), BackgroundCreationIndex, NumWorkers, MaxWorkers, ActualBackgroundPriority, BackgroundAffinity);
				};

			WaitingQueue[0].Init(NumForegroundWorkers, MaxForegroundWorkers, ForegroundCreateThread, GTaskGraphUseDynamicThreadCreation ? 0 : MaxForegroundWorkers /* ActiveThreadCount */);
			WaitingQueue[1].Init(NumBackgroundWorkers, MaxBackgroundWorkers, BackgroundCreateThread, GTaskGraphUseDynamicThreadCreation ? 0 : MaxBackgroundWorkers /* ActiveThreadCount */);

			// Precreate all the threads if dynamic thread creation is not activated.
			if (!GTaskGraphUseDynamicThreadCreation)
			{
				for (int32 Index = 0; Index < MaxForegroundWorkers; Index++)
				{
					ForegroundCreateThread();
				}

				for (int32 Index = 0; Index < MaxBackgroundWorkers; Index++)
				{
					BackgroundCreateThread();
				}
			}
			else if (TemporaryShutdown)
			{
				// Since the global queue is not drained during temporary shutdown, kick threads
				// here so we can continue work if there was any tasks left when we stopped the workers.
				WaitingQueue[0].Notify();
				WaitingQueue[1].Notify();
			}
		}
	}
	
	bool FScheduler::IsOversubscriptionLimitReached(ETaskPriority TaskPriority) const
	{
		const bool bIsBackgroundTask = TaskPriority >= ETaskPriority::ForegroundCount;
		if (bIsBackgroundTask)
		{
			return 
				WaitingQueue[1].IsOversubscriptionLimitReached();
		}
		else
		{
			// Since we are allowing background thread to run foreground task we need both waiting queue
			// to reach their limit to consider that priority's limit reached.
			return 
				WaitingQueue[0].IsOversubscriptionLimitReached() &&
				WaitingQueue[1].IsOversubscriptionLimitReached();
		}
	}
	
	FOversubscriptionLimitReached& FScheduler::GetOversubscriptionLimitReachedEvent()
	{
		return OversubscriptionLimitReachedEvent;
	}

	inline FTask* FScheduler::ExecuteTask(FTask* InTask)
	{
		FTask* ParentTask = FTask::ActiveTask;
		FTask::ActiveTask = InTask;
		FTask* OutTask;

		if (!InTask->IsBackgroundTask())
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(ExecuteForegroundTask);
			OutTask = InTask->ExecuteTask();
		}
		else
		{
			// Dynamic priority only enables for root task when we're not inside a named thread (i.e. GT, RT)
			const bool bSkipPriorityChange = ParentTask || !GTaskGraphUseDynamicPrioritization || !FSchedulerTls::IsWorkerThread() || InTask->WasCanceledOrIsExpediting();

			FRunnableThread* RunnableThread = nullptr;
			if (!bSkipPriorityChange)
			{
				// We assume all threads executing tasks are RunnableThread and this can't be null or it will crash. 
				// Which is fine since we want to know about it sooner rather than later.
				RunnableThread = FRunnableThread::GetRunnableThread();

				checkSlow(RunnableThread && RunnableThread->GetThreadPriority() == WorkerPriority);

				TRACE_CPUPROFILER_EVENT_SCOPE(LowerThreadPriority);
				RunnableThread->SetThreadPriority(BackgroundPriority);
			}

			{
				TRACE_CPUPROFILER_EVENT_SCOPE(ExecuteBackgroundTask);
				OutTask = InTask->ExecuteTask();
			}

			if (!bSkipPriorityChange)
			{
				TRACE_CPUPROFILER_EVENT_SCOPE(RaiseThreadPriority);
				RunnableThread->SetThreadPriority(WorkerPriority);
			}
		}

		FTask::ActiveTask = ParentTask;
		return OutTask;
	}

	void FScheduler::StopWorkers(bool bDrainGlobalQueue)
	{
		uint32 OldActiveWorkers = ActiveWorkers.load(std::memory_order_relaxed);
		if (OldActiveWorkers != 0 && ActiveWorkers.compare_exchange_strong(OldActiveWorkers, 0, std::memory_order_relaxed))
		{
			FScopeLock Lock(&WorkerThreadsCS);

			WaitingQueue[0].StartShutdown();
			WaitingQueue[1].StartShutdown();

			for (TUniquePtr<FThread>& Thread : WorkerThreads)
			{
				if (Thread.IsValid())
				{
					Thread->Join();
				}
			}

			WaitingQueue[0].FinishShutdown();
			WaitingQueue[1].FinishShutdown();

			NextWorkerId = 0;
			WorkerThreads.Reset();
			WorkerLocalQueues.Reset();
			WorkerEvents.Reset();

			if (bDrainGlobalQueue)
			{
				for (FTask* Task = QueueRegistry.DequeueGlobal(); Task != nullptr; Task = QueueRegistry.DequeueGlobal())
				{
					while(Task)
					{
						if ((Task = ExecuteTask(Task)) != nullptr)
						{
							verifySlow(Task->TryPrepareLaunch());
						}
					}
				}
			}

			QueueRegistry.Reset();
		}
	}

	void FScheduler::RestartWorkers(uint32 NumForegroundWorkers, uint32 NumBackgroundWorkers, FThread::EForkable IsForkable, EThreadPriority InWorkerPriority, EThreadPriority InBackgroundPriority, uint64 InWorkerAffinity, uint64 InBackgroundAffinity)
	{
		FScopeLock Lock(&WorkerThreadsCS);
		TemporaryShutdown.store(true, std::memory_order_release);
		const bool bDrainGlobalQueue = false;
		StopWorkers(bDrainGlobalQueue);
		StartWorkers(NumForegroundWorkers, NumBackgroundWorkers, IsForkable, InWorkerPriority, InBackgroundPriority, InWorkerAffinity, InBackgroundAffinity);
		TemporaryShutdown.store(false, std::memory_order_release);
	}

	void FScheduler::LaunchInternal(FTask& Task, EQueuePreference QueuePreference, bool bWakeUpWorker)
	{
		if (ActiveWorkers.load(std::memory_order_relaxed) || TemporaryShutdown.load(std::memory_order_acquire))
		{
			const bool bIsBackgroundTask = Task.IsBackgroundTask();
			const bool bIsBackgroundWorker = FSchedulerTls::IsBackgroundWorker();
			const bool bIsStandbyWorker = FSchedulerTls::IsStandbyWorker();

			// Standby workers always enqueue to the global queue and perform wakeup
			// as they can go to sleep whenever the oversubscription period is done
			// and we don't want that to happen without another thread picking up
			// this task.
			if ((bIsBackgroundTask && !bIsBackgroundWorker) || bIsStandbyWorker)
			{
				QueuePreference = EQueuePreference::GlobalQueuePreference;
			}

			bWakeUpWorker |= bIsStandbyWorker || FSchedulerTls::LocalQueue == nullptr;

			if (FSchedulerTls::LocalQueue && QueuePreference != EQueuePreference::GlobalQueuePreference)
			{
				FSchedulerTls::LocalQueue->Enqueue(&Task, uint32(Task.GetPriority()));
			}
			else
			{
				QueueRegistry.Enqueue(&Task, uint32(Task.GetPriority()));
			}

			if (bWakeUpWorker)
			{
				if (bWakeUpWorker && !WakeUpWorker(bIsBackgroundTask) && !bIsBackgroundTask)
				{
					WakeUpWorker(true);
				}
			}
		}
		else
		{
			FTask* TaskPtr = &Task;
			while (TaskPtr)
			{
				if ((TaskPtr = ExecuteTask(TaskPtr)) != nullptr)
				{
					verifySlow(TaskPtr->TryPrepareLaunch());
				}
			}
		}
	}

	void FScheduler::IncrementOversubscription()
	{
		FSchedulerTls::EWorkerType LocalWorkerType = WorkerType;

		if (LocalWorkerType != EWorkerType::None)
		{
			// The goal is to minimize the amount of wait in the worker tasks, this will help drive the
			// total number of oversubscription down and show any regressions.
			CSV_CUSTOM_STAT(Scheduler, Oversubscription, 1, ECsvCustomStatOp::Accumulate);

			const bool bPermitBackgroundWork = LocalWorkerType == FSchedulerTls::EWorkerType::Background;
			WaitingQueue[bPermitBackgroundWork].IncrementOversubscription();
		}
	}

	void FScheduler::DecrementOversubscription()
	{
		FSchedulerTls::EWorkerType LocalWorkerType = WorkerType;

		if (LocalWorkerType != EWorkerType::None)
		{
			const bool bPermitBackgroundWork = LocalWorkerType == FSchedulerTls::EWorkerType::Background;
			WaitingQueue[bPermitBackgroundWork].DecrementOversubscription();
		}
	}

#if PLATFORM_DESKTOP || !IS_MONOLITHIC
	const FTask* FTask::GetActiveTask()
	{
		return ActiveTask;
	}
#endif

	bool FSchedulerTls::IsWorkerThread() const
	{
		return WorkerType != FSchedulerTls::EWorkerType::None && ActiveScheduler == this;
	}

	bool FSchedulerTls::IsBusyWaiting()
	{
		return false;
	}

	template<typename QueueType, FTask* (QueueType::*DequeueFunction)(bool), bool bIsStandbyWorker>
	bool FScheduler::TryExecuteTaskFrom(Private::FWaitEvent* WaitEvent, QueueType* Queue, Private::FOutOfWork& OutOfWork, bool bPermitBackgroundWork)
	{
		bool AnyExecuted = false;

		FTask* Task = (Queue->*DequeueFunction)(bPermitBackgroundWork);
		while (Task)
		{
			checkSlow(FTask::ActiveTask == nullptr);

			if (OutOfWork.Stop())
			{
				// Standby workers don't need cancellation, this logic doesn't apply to them.
				if constexpr (bIsStandbyWorker == false)
				{
					// CancelWait will tell us if we need to start a new worker to replace
					// a potential wakeup we might have consumed during the cancellation.
					if (WaitingQueue[bPermitBackgroundWork].CancelWait(WaitEvent))
					{
						if (!WakeUpWorker(bPermitBackgroundWork) && !FSchedulerTls::IsBackgroundWorker())
						{
							WakeUpWorker(!bPermitBackgroundWork);
						}
					}
				}
			}

			AnyExecuted = true;

			// Executing a task can return a continuation.
			if ((Task = ExecuteTask(Task)) != nullptr)
			{
				verifySlow(Task->TryPrepareLaunch());
			}
		}
		return AnyExecuted;
	}

	void FScheduler::StandbyLoop(Private::FWaitEvent* WorkerEvent, FSchedulerTls::FLocalQueueType* WorkerLocalQueue, uint32 WaitCycles, bool bPermitBackgroundWork)
	{
		bool bPreparingStandby = false;
		Private::FOutOfWork OutOfWork;
		while (true)
		{
			bool bExecutedSomething = false;
			while (TryExecuteTaskFrom<FSchedulerTls::FLocalQueueType, &FSchedulerTls::FLocalQueueType::Dequeue, true>(WorkerEvent, WorkerLocalQueue, OutOfWork, bPermitBackgroundWork)
				|| TryExecuteTaskFrom<FSchedulerTls::FLocalQueueType, &FSchedulerTls::FLocalQueueType::DequeueSteal, true>(WorkerEvent, WorkerLocalQueue, OutOfWork, bPermitBackgroundWork))
			{
				bPreparingStandby = false;
				bExecutedSomething = true;

				// If we're currently oversubscribed... we might be selected for standby even when there is work left.
				WaitingQueue[bPermitBackgroundWork].ConditionalStandby(WorkerEvent);
			}

			// Check if we're shutting down
			if (ActiveWorkers.load(std::memory_order_relaxed) == 0)
			{
				OutOfWork.Stop();
				break;
			}

			if (bExecutedSomething == false)
			{
				if (!bPreparingStandby)
				{
					OutOfWork.Start();
					WaitingQueue[bPermitBackgroundWork].PrepareStandby(WorkerEvent);
					bPreparingStandby = true;
				}
				else if (WaitingQueue[bPermitBackgroundWork].CommitStandby(WorkerEvent, OutOfWork))
				{
					// Only reset this when the commit succeeded, otherwise we're backing off the commit and looking at the queue again
					bPreparingStandby = false;
				}
			}
		}
	}

	void FScheduler::WorkerLoop(Private::FWaitEvent* WorkerEvent, FSchedulerTls::FLocalQueueType* WorkerLocalQueue, uint32 WaitCycles, bool bPermitBackgroundWork)
	{
		bool bPreparingWait = false;
		Private::FOutOfWork OutOfWork;
		while (true)
		{
			bool bExecutedSomething = false;
			while (TryExecuteTaskFrom<FSchedulerTls::FLocalQueueType, &FSchedulerTls::FLocalQueueType::Dequeue, false>(WorkerEvent, WorkerLocalQueue, OutOfWork, bPermitBackgroundWork)
				|| TryExecuteTaskFrom<FSchedulerTls::FLocalQueueType, &FSchedulerTls::FLocalQueueType::DequeueSteal, false>(WorkerEvent, WorkerLocalQueue, OutOfWork, bPermitBackgroundWork))
			{
				bPreparingWait = false;
				bExecutedSomething = true;
			}

			// Check if we're shutting down
			if (ActiveWorkers.load(std::memory_order_relaxed) == 0)
			{
				// Don't leave the waiting queue in a bad state
				if (OutOfWork.Stop())
				{
					WaitingQueue[bPermitBackgroundWork].CancelWait(WorkerEvent);
				}
				break;
			}

			if (bExecutedSomething == false)
			{
				if (!bPreparingWait)
				{
					OutOfWork.Start();
					WaitingQueue[bPermitBackgroundWork].PrepareWait(WorkerEvent);
					bPreparingWait = true;
				}
				else if (WaitingQueue[bPermitBackgroundWork].CommitWait(WorkerEvent, OutOfWork, WorkerSpinCycles, WaitCycles))
				{
					// Only reset this when the commit succeeded, otherwise we're backing off the commit and looking at the queue again
					bPreparingWait = false;
				}
			}
		}
	}

	void FScheduler::WorkerMain(Private::FWaitEvent* WorkerEvent, FSchedulerTls::FLocalQueueType* WorkerLocalQueue, uint32 WaitCycles, bool bPermitBackgroundWork)
	{
		checkSlow(FSchedulerTls::LocalQueue == nullptr);
		checkSlow(WorkerLocalQueue != nullptr);
		checkSlow(WorkerEvent != nullptr);

		FTaskTagScope WorkerScope(ETaskTag::EWorkerThread);
		FSchedulerTls::ActiveScheduler = this;

		FMemory::SetupTLSCachesOnCurrentThread();
		FSchedulerTls::WorkerType = bPermitBackgroundWork ? FSchedulerTls::EWorkerType::Background : FSchedulerTls::EWorkerType::Foreground;
		FSchedulerTls::SetStandbyWorker(WorkerEvent->bIsStandby);
		FSchedulerTls::LocalQueue = WorkerLocalQueue;

		{
			Private::FOversubscriptionAllowedScope _(true);

			if (WorkerEvent->bIsStandby)
			{
				StandbyLoop(WorkerEvent, WorkerLocalQueue, WaitCycles, bPermitBackgroundWork);
			}
			else
			{
				WorkerLoop(WorkerEvent, WorkerLocalQueue, WaitCycles, bPermitBackgroundWork);
			}
		}

		FSchedulerTls::LocalQueue = nullptr;
		FSchedulerTls::ActiveScheduler = nullptr;
		FSchedulerTls::SetStandbyWorker(false);
		FSchedulerTls::WorkerType = FSchedulerTls::EWorkerType::None;
		FMemory::ClearAndDisableTLSCachesOnCurrentThread();
	}

	void FScheduler::BusyWaitInternal(const FConditional& Conditional, bool ForceAllowBackgroundWork)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(FScheduler::BusyWaitInternal);
		CSV_SCOPED_TIMING_STAT(Scheduler, BusyWait);

		FOversubscriptionScope _;
		while (!Conditional())
		{
			FPlatformProcess::YieldThread();
		}
	}
}