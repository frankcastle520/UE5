// Copyright Epic Games, Inc. All Rights Reserved.

#include "EpicRtcThread.h"

#include "HAL/Event.h"
#include "HAL/Runnable.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "HAL/RunnableThread.h"
#include "EpicRtcTickableTask.h"
#include "Misc/SingleThreadRunnable.h"

namespace UE::PixelStreaming2
{
	static TWeakPtr<FEpicRtcRunnable> EpicRtcRunnable;

	/**
	 * The runnable. Handles ticking of all tasks
	 */
	class FEpicRtcRunnable : public FRunnable, public FSingleThreadRunnable
	{
	public:
		// Begin FRunnable
		virtual bool Init() override
		{
			return true;
		}

		virtual uint32 Run() override
		{
			bIsRunning = true;

			while (bIsRunning)
			{
				Tick();

				// Sleep 1ms
				FPlatformProcess::Sleep(0.001f);
			}

			return 0;
		}

		virtual void Stop() override
		{
			bIsRunning = false;

			TaskEvent->Trigger();
		}

		virtual void Exit() override
		{
			bIsRunning = false;

			TaskEvent->Trigger();
		}

		virtual FSingleThreadRunnable* GetSingleThreadInterface() override
		{
			return this;
		}
		// End FRunnable

		// Begin FSingleThreadRunnable
		virtual void Tick() override
		{
			FScopeLock TaskLock(&TasksMutex);

			const uint64 NowCycles = FPlatformTime::Cycles64();
			const double DeltaMs = FPlatformTime::ToMilliseconds64(NowCycles - LastTickCycles);

			StartTicking();

			for (auto& Task : Tasks)
			{
				// A task may be nulled out due to deletion during our loop. Check for safety
				if (Task)
				{
					Task->Tick(DeltaMs);
				}
			}

			FinishTicking();

			LastTickCycles = NowCycles;
		}
		// End FSingleThreadRunnable

		void StartTicking()
		{
			FScopeLock NewTaskLock(&NewTasksMutex);
			for (auto& NewTask : NewTasks)
			{
				Tasks.Add(NewTask);
			}

			NewTasks.Empty();

			bIsTicking = true;
		}

		void FinishTicking()
		{
			if (bNeedsCleanup)
			{
				Tasks.RemoveAll([](const FEpicRtcTickableTask* Entry) { return Entry == nullptr; });
				bNeedsCleanup = false;
			}

			bIsTicking = false;

			if (Tasks.Num() == 0)
			{
				// Sleep the thread indefinitely because there are no tasks to tick.
				// Adding a new task will wake the thread
				TaskEvent->Wait();
			}
		}

		FEpicRtcRunnable()
			: bIsTicking(false)
			, bNeedsCleanup(false)
			, bIsRunning(false)
			, LastTickCycles(FPlatformTime::Cycles64())
		{
		}

		virtual ~FEpicRtcRunnable() = default;

	private:
		void AddTask(FEpicRtcTickableTask* Task)
		{
			FScopeLock NewTaskLock(&NewTasksMutex);
			NewTasks.Add(Task);
			// We've added a new task. Wake the thread (if it was sleeping)
			TaskEvent->Trigger();
		}

		void RemoveTask(FEpicRtcTickableTask* Task)
		{
			// Lock TaskLock before NewTaskLock to ensure deadlock does not happen when Tick and StartTicking lock.
			// Locking matches FTickableObjectBase locking in Tickable.cpp
			FScopeLock TaskLock(&TasksMutex);
			FScopeLock NewTaskLock(&NewTasksMutex);

			// Remove from pending list if it hasn't been registered
			NewTasks.Remove(Task);

			const int32 Pos = Tasks.IndexOfByKey(Task);
			if (Pos != INDEX_NONE)
			{
				if (bIsTicking)
				{
					// During ticking it is not safe to modify the set so null and mark for later
					Tasks[Pos] = nullptr;
					bNeedsCleanup = true;
				}
				else
				{
					Tasks.RemoveAt(Pos);
				}
			}
		}

	private:
		// Allow the FEpicRtcTickableTask to access the private add and remove tasks
		friend FEpicRtcTickableTask;

		// New tasks that have not yet been added to the Tasks list*/
		TArray<FEpicRtcTickableTask*> NewTasks;
		// Lock for modifying new list */
		FCriticalSection NewTasksMutex;

		// Tasks to execute every tick
		TArray<FEpicRtcTickableTask*> Tasks;
		// This critical section should be locked during entire tick process
		FCriticalSection TasksMutex;

		// Use this event to signal when we should wake.
		FEventRef TaskEvent;

		// Tasks can removed from any thread so this needs to be thread safe
		std::atomic<bool> bIsTicking;
		// Tasks can removed from any thread so this needs to be thread safe
		std::atomic<bool> bNeedsCleanup;
		// This thread can be stopped from another thread during shutdown so this needs to be thread safe
		std::atomic<bool> bIsRunning;
		uint64			  LastTickCycles;
	};

	FEpicRtcThread::FEpicRtcThread()
	{
		Runnable = MakeShared<FEpicRtcRunnable>();
		EpicRtcRunnable = Runnable;
		Thread = TSharedPtr<FRunnableThread>(FRunnableThread::Create(Runnable.Get(), TEXT("Pixel Streaming EpicRtc Thread")));
	}

	FEpicRtcThread::~FEpicRtcThread()
	{
		if (Thread)
		{
			Thread->Kill();
			Thread.Reset();
		}

		if (Runnable)
		{
			Runnable->Stop();
			Runnable.Reset();
		}
	}

	/**
	 * ---------- FEpicRtcTickableTask ---------------
	 */

	void FEpicRtcTickableTask::Register()
	{
		TSharedPtr<FEpicRtcRunnable> Runnable = EpicRtcRunnable.Pin();
		if (Runnable)
		{
			Runnable->AddTask(this);
		}
	}

	void FEpicRtcTickableTask::Unregister()
	{
		TSharedPtr<FEpicRtcRunnable> Runnable = EpicRtcRunnable.Pin();
		if (Runnable)
		{
			Runnable->RemoveTask(this);
		}
	}

} // namespace UE::PixelStreaming2