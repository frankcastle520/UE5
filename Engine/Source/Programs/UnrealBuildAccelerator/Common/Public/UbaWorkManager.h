// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UbaEvent.h"
#include "UbaMemory.h"

namespace uba
{
	class WorkTracker
	{
	public:
		virtual u32 TrackWorkStart(const tchar* desc) { return 0; }
		virtual void TrackWorkEnd(u32 id) {}
	};

	class WorkManager
	{
	public:
		virtual void AddWork(const Function<void()>& work, u32 count, const tchar* desc, bool highPriority = false) = 0;
		virtual u32 GetWorkerCount() = 0;
		virtual void DoWork(u32 count = 1) = 0;

		u32 TrackWorkStart(const tchar* desc);
		void TrackWorkEnd(u32 id);

		void SetWorkTracker(WorkTracker* workTracker) { m_workTracker = workTracker; }
		WorkTracker* GetWorkTracker() { return m_workTracker; }

		template<typename TContainer, typename TFunc>
		void ParallelFor(u32 workCount, TContainer& container, const TFunc& func, const tchar* description = TC(""), bool highPriority = false);

	protected:
		Atomic<WorkTracker*> m_workTracker;
	};


	class WorkManagerImpl : public WorkManager
	{
	public:
		WorkManagerImpl(u32 workerCount);
		virtual ~WorkManagerImpl();
		virtual void AddWork(const Function<void()>& work, u32 count, const tchar* desc, bool highPriority = false) override;
		virtual u32 GetWorkerCount() override;
		virtual void DoWork(u32 count = 1) override;
		void FlushWork();

	private:
		struct Worker;
		void PushWorker(Worker* worker);
		void PushWorkerNoLock(Worker* worker);
		Worker* PopWorkerNoLock();

		Vector<Worker*> m_workers;
		struct Work { Function<void()> func; TString desc; };
		ReaderWriterLock m_workLock;
		List<Work> m_work;
		Atomic<u32> m_activeWorkerCount;
		Atomic<u32> m_workCounter;

		ReaderWriterLock m_availableWorkersLock;
		Worker* m_firstAvailableWorker = nullptr;
	};

	struct TrackWorkScope
	{
		TrackWorkScope(WorkManager& wm, const tchar* desc) : workManager(wm), workIndex(wm.TrackWorkStart(desc)) {}
		~TrackWorkScope() { workManager.TrackWorkEnd(workIndex); }
		WorkManager& workManager;
		u32 workIndex;
	};



	template<typename TContainer, typename TFunc>
	void WorkManager::ParallelFor(u32 workCount, TContainer& container, const TFunc& func, const tchar* description, bool highPriority)
	{
		#if !defined( __clang_analyzer__ ) // Static analyzer claims context can leak but it can only leak when process terminates (and then it doesn't matter)

		struct Context
		{
			typename TContainer::iterator it;
			typename TContainer::iterator end;
			u32 refCount;
			u32 activeCount;
			bool isDone;
			ReaderWriterLock lock;
			Event* doneEvent;
		};

		Event doneEvent(true);

		auto context = new Context();
		context->it = container.begin();
		context->end = container.end();
		context->refCount = workCount + 1;
		context->activeCount = 0;
		context->isDone = false;
		context->doneEvent = &doneEvent;

		auto work = [context, funcCopy = func]() mutable
			{
				u32 active = 0;
				while (true)
				{
					SCOPED_WRITE_LOCK(context->lock, l);
					context->activeCount -= active;
					context->isDone = context->it == context->end;
					if (context->isDone)
					{
						if (context->activeCount == 0 && context->doneEvent)
						{
							context->doneEvent->Set();
							context->doneEvent = nullptr;
						}
						if (--context->refCount)
							return;
						l.Leave();
						delete context;
						return;
					}

					auto it2 = context->it++;
					active = 1;
					context->activeCount += active;
					l.Leave();

					funcCopy(it2);
				}
			};
		
		AddWork(work, workCount, description, highPriority);
		work();
		doneEvent.IsSet();

		#endif
	}
}
