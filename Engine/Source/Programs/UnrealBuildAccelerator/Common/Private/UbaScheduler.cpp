// Copyright Epic Games, Inc. All Rights Reserved.

#include "UbaScheduler.h"
#include "UbaApplicationRules.h"
#include "UbaCacheClient.h"
#include "UbaConfig.h"
#include "UbaNetworkServer.h"
#include "UbaProcess.h"
#include "UbaProcessStartInfoHolder.h"
#include "UbaRootPaths.h"
#include "UbaSessionServer.h"
#include "UbaStringBuffer.h"

namespace uba
{
	struct Scheduler::ProcessStartInfo2 : ProcessStartInfoHolder
	{
		ProcessStartInfo2(const ProcessStartInfo& si, const u8* ki, u32 kic)
		:	ProcessStartInfoHolder(si)
		, knownInputs(ki)
		, knownInputsCount(kic)
		{
		}

		~ProcessStartInfo2()
		{
			delete[] knownInputs;
		}

		const u8* knownInputs;
		u32 knownInputsCount;
		float weight = 1.0f;
		u64 queryCacheTime = 0;
	};


	struct Scheduler::ExitProcessInfo
	{
		Scheduler* scheduler = nullptr;
		ProcessStartInfo2* startInfo;
		u32 processIndex = ~0u;
		bool wasReturned = false;
		bool isLocal = true;
	};


	class SkippedProcess : public Process
	{
	public:
		SkippedProcess(const ProcessStartInfo& i) : startInfo(i) {}
		virtual u32 GetExitCode() override { return ProcessCancelExitCode; }
		virtual bool HasExited() override { return true; }
		virtual bool WaitForExit(u32 millisecondsTimeout) override{ return true; }
		virtual const ProcessStartInfo& GetStartInfo() const override { return startInfo; }
		virtual const Vector<ProcessLogLine>& GetLogLines() const override { static Vector<ProcessLogLine> v{ProcessLogLine{TC("Skipped"), LogEntryType_Warning}}; return v; }
		virtual const Vector<u8>& GetTrackedInputs() const override { static Vector<u8> v; return v;}
		virtual const Vector<u8>& GetTrackedOutputs() const override { static Vector<u8> v; return v;}
		virtual bool IsRemote() const override { return false; }
		virtual ProcessExecutionType GetExecutionType() const override { return ProcessExecutionType_Native; }
		ProcessStartInfoHolder startInfo;
	};

	class CachedProcess : public Process
	{
	public:
		CachedProcess(const ProcessStartInfo& i) : startInfo(i) {}
		virtual u32 GetExitCode() override { return 0; }
		virtual bool HasExited() override { return true; }
		virtual bool WaitForExit(u32 millisecondsTimeout) override{ return true; }
		virtual const ProcessStartInfo& GetStartInfo() const override { return startInfo; }
		virtual const Vector<ProcessLogLine>& GetLogLines() const override { return logLines; }
		virtual const Vector<u8>& GetTrackedInputs() const override { static Vector<u8> v; return v;}
		virtual const Vector<u8>& GetTrackedOutputs() const override { static Vector<u8> v; return v;}
		virtual bool IsRemote() const override { return false; }
		virtual ProcessExecutionType GetExecutionType() const override { return ProcessExecutionType_FromCache; }
		ProcessStartInfoHolder startInfo;
		Vector<ProcessLogLine> logLines;
	};

	void SchedulerCreateInfo::Apply(Config& config)
	{
		if (const ConfigTable* table = config.GetTable(TC("Scheduler")))
		{
			table->GetValueAsBool(enableProcessReuse, TC("EnableProcessReuse"));
			table->GetValueAsBool(forceRemote, TC("ForceRemote"));
			table->GetValueAsBool(forceNative, TC("ForceNative"));
			table->GetValueAsU32(maxLocalProcessors, TC("MaxLocalProcessors"));
		}
	}


	Scheduler::Scheduler(const SchedulerCreateInfo& info)
	:	m_session(info.session)
	,	m_maxLocalProcessors(info.maxLocalProcessors != ~0u ? info.maxLocalProcessors : GetLogicalProcessorCount())
	,	m_updateThreadLoop(false)
	,	m_enableProcessReuse(info.enableProcessReuse)
	,	m_forceRemote(info.forceRemote)
	,	m_forceNative(info.forceNative)
	,	m_processConfigs(info.processConfigs)
	,	m_cacheClient(info.cacheClient)
	,	m_writeToCache(info.writeToCache && info.cacheClient)
	{
		m_session.RegisterGetNextProcess([this](Process& process, NextProcessInfo& outNextProcess, u32 prevExitCode)
			{
				return HandleReuseMessage(process, outNextProcess, prevExitCode);
			});
	}

	Scheduler::~Scheduler()
	{
		Stop();
		for (auto rt : m_rootPaths)
			delete rt;
	}

	void Scheduler::Start()
	{
		m_session.SetRemoteProcessReturnedEvent([this](Process& process) { RemoteProcessReturned(process); });
		m_session.SetRemoteProcessSlotAvailableEvent([this]() { RemoteSlotAvailable(); });

		m_loop = true;
		m_thread.Start([this]() { ThreadLoop(); return 0; });
	}

	void Scheduler::Stop()
	{
		m_loop = false;
		m_updateThreadLoop.Set();
		m_thread.Wait();
		m_session.WaitOnAllTasks();

		SCOPED_WRITE_LOCK(m_processEntriesLock, lock);
		for (auto& entry : m_processEntries)
		{
			UBA_ASSERTF(entry.status !=ProcessStatus_Running, TC("Found processes in running state when stopping scheduler."));
			delete[] entry.dependencies;
			delete entry.info;
		}
		m_processEntries.clear();
		m_processEntriesStart = 0;
	}

	void Scheduler::SetMaxLocalProcessors(u32 maxLocalProcessors)
	{
		m_maxLocalProcessors = maxLocalProcessors;
		m_updateThreadLoop.Set();
	}

	u32 Scheduler::EnqueueProcess(const EnqueueProcessInfo& info)
	{
		u8* ki = nullptr;
		if (info.knownInputsCount)
		{
			ki = new u8[info.knownInputsBytes];
			memcpy(ki, info.knownInputs, info.knownInputsBytes);
		}

		u32* dep = nullptr;
		if (info.dependencyCount)
		{
			dep = new u32[info.dependencyCount];
			memcpy(dep, info.dependencies, info.dependencyCount*sizeof(u32));
		}

		auto info2 = new ProcessStartInfo2(info.info, ki, info.knownInputsCount);
		info2->Expand();
		info2->weight = info.weight;

		const ApplicationRules* rules = m_session.GetRules(*info2);
		info2->rules = rules;

		bool useCache = m_cacheClient && !m_writeToCache && rules->IsCacheable();

		SCOPED_WRITE_LOCK(m_processEntriesLock, lock);
		u32 index = u32(m_processEntries.size());
		auto& entry = m_processEntries.emplace_back();
		entry.info = info2;
		entry.dependencies = dep;
		entry.dependencyCount = info.dependencyCount;
		entry.status = useCache ? ProcessStatus_QueuedForCache : ProcessStatus_QueuedForRun;
		entry.canDetour = info.canDetour;
		entry.canExecuteRemotely = info.canExecuteRemotely && info.canDetour;

		if (m_processConfigs)
		{
			auto name = info2->application;
			if (auto lastSeparator = TStrrchr(name, PathSeparator))
				name = lastSeparator + 1;
			StringBuffer<128> lower(name);
			lower.MakeLower();
			lower.Replace('.', '_');
			if (const ConfigTable* processConfig = m_processConfigs->GetTable(lower.data))
			{
				processConfig->GetValueAsBool(entry.canExecuteRemotely, TC("CanExecuteRemotely"));
				processConfig->GetValueAsBool(entry.canDetour, TC("CanDetour"));
			}
		}

		lock.Leave();

		UpdateQueueCounter(1);

		m_updateThreadLoop.Set();
		return index;
	}

	void Scheduler::GetStats(u32& outQueued, u32& outActiveLocal, u32& outActiveRemote, u32& outFinished)
	{
		outActiveLocal = m_activeLocalProcesses;
		outActiveRemote = m_activeRemoteProcesses;
		outFinished = m_finishedProcesses;
		outQueued = m_queuedProcesses;
	}

	void Scheduler::SetProcessFinishedCallback(const Function<void(const ProcessHandle&)>& processFinished)
	{
		m_processFinished = processFinished;
	}

	u32 Scheduler::GetProcessCountThatCanRunRemotelyNow()
	{
		u32 count = 0;
		SCOPED_READ_LOCK(m_processEntriesLock, lock);
		for (auto& entry : m_processEntries)
		{
			if (!entry.canExecuteRemotely)
				continue;
			if (entry.status != ProcessStatus_QueuedForRun)
				continue;
			++count;
		}

		count += m_activeRemoteProcesses;

		return count;
	}

	void Scheduler::ThreadLoop()
	{
		while (m_loop)
		{
			if (!m_updateThreadLoop.IsSet())
				break;

			while (RunQueuedProcess(true))
				;
		}
	}

	void Scheduler::RemoteProcessReturned(Process& process)
	{
		auto& ei = *(ExitProcessInfo*)process.GetStartInfo().userData;

		ei.wasReturned = true;
		u32 processIndex = ei.processIndex;

		process.Cancel(true); // Cancel will call ProcessExited
		
		if (processIndex == ~0u)
			return;

		SCOPED_WRITE_LOCK(m_processEntriesLock, lock);
		if (m_processEntries[processIndex].status != ProcessStatus_Running)
			return;
		m_processEntries[processIndex].status = ProcessStatus_QueuedForRun;
		m_processEntriesStart = Min(m_processEntriesStart, processIndex);
		lock.Leave();

		UpdateQueueCounter(1);
		UpdateActiveProcessCounter(false, -1);
		m_updateThreadLoop.Set();
	}

	void Scheduler::HandleCacheMissed(ExitProcessInfo* ei)
	{
		u32 processIndex = ei->processIndex;
		delete ei;

		if (processIndex == ~0u)
			return;

		SCOPED_WRITE_LOCK(m_processEntriesLock, lock);
		if (m_processEntries[processIndex].status != ProcessStatus_Running)
			return;
		m_processEntries[processIndex].status = ProcessStatus_QueuedForRun;
		m_processEntriesStart = Min(m_processEntriesStart, processIndex);
		--m_activeCacheQueries;
		lock.Leave();

		UpdateQueueCounter(1);
		UpdateActiveProcessCounter(false, -1);
		m_updateThreadLoop.Set();
	}

	void Scheduler::RemoteSlotAvailable()
	{
		RunQueuedProcess(false);
	}

	void Scheduler::ProcessExited(ExitProcessInfo* info, const ProcessHandle& handle)
	{
		auto ig = MakeGuard([info]() { delete info; });

		if (info->wasReturned)
			return;

		auto si = info->startInfo;
		if (!si) // Can be a process that was reused but didn't get a new process
		{
			UBA_ASSERT(info->processIndex == ~0u);
			return;
		}

		if (si->queryCacheTime) // A bit hacky but we know this is a local process
		{
			Timer& timer = ((ProcessImpl*)handle.m_process)->m_processStats.queryCache;
			timer.count = 1;
			timer.time = si->queryCacheTime;
		}

		ExitProcess(*info, *handle.m_process, handle.m_process->GetExitCode(), false);
	}

	u32 Scheduler::PopProcess(bool isLocal, ProcessStatus& outPrevStatus)
	{
		bool atMaxLocalWeight = m_activeLocalProcessWeight >= float(m_maxLocalProcessors);
		bool atMaxCacheQueries = m_activeCacheQueries >= 16;
		auto processEntries = m_processEntries.data();
		bool allFinished = true;

		for (u32 i=m_processEntriesStart, e=u32(m_processEntries.size()); i!=e; ++i)
		{
			auto& entry = processEntries[i];
			auto status = entry.status;
			if (status != ProcessStatus_QueuedForCache && status != ProcessStatus_QueuedForRun)
			{
				if (allFinished)
				{
					if (status != ProcessStatus_Running)
						m_processEntriesStart = i;
					else
						allFinished = false;
				}
				continue;
			}
			allFinished = false;

			if (isLocal)
			{
				if (m_forceRemote && entry.canExecuteRemotely)
					continue;
				if (status == ProcessStatus_QueuedForRun && atMaxLocalWeight)
					continue;
				if (status == ProcessStatus_QueuedForCache && atMaxCacheQueries)
					continue;
			}
			else
			{
				if (!entry.canExecuteRemotely)
					continue;
				if (status == ProcessStatus_QueuedForCache)
					continue;
			}

			bool canRun = true;
			for (u32 j=0, je=entry.dependencyCount; j!=je; ++j)
			{
				auto depIndex = entry.dependencies[j];
				if (depIndex >= m_processEntries.size())
				{
					m_session.GetLogger().Error(TC("Found dependency on index %u but there are only %u processes registered"), depIndex, u32(m_processEntries.size()));
					return ~0u;
				}
				auto depStatus = processEntries[depIndex].status;
				if (depStatus == ProcessStatus_Failed || depStatus == ProcessStatus_Skipped)
				{
					entry.status = ProcessStatus_Skipped;
					return i;
				}
				if (depStatus != ProcessStatus_Success)
				{
					canRun = false;
					break;
				}
			}

			if (!canRun)
				continue;

			if (isLocal)
			{
				if (status == ProcessStatus_QueuedForRun)
					m_activeLocalProcessWeight += entry.info->weight;
				else
					++m_activeCacheQueries;
			}

			outPrevStatus = entry.status;
			entry.status = ProcessStatus_Running;
			return i;
		}
		return ~0u;
	}


	bool Scheduler::RunQueuedProcess(bool isLocal)
	{
		while (true)
		{
			ProcessStatus prevStatus;
			SCOPED_WRITE_LOCK(m_processEntriesLock, lock);
			u32 indexToRun = PopProcess(isLocal, prevStatus);
			if (indexToRun == ~0u)
				return false;

			auto& processEntry = m_processEntries[indexToRun];
			auto info = processEntry.info;
			bool canDetour = processEntry.canDetour && !m_forceNative;
			bool wasSkipped = processEntry.status == ProcessStatus_Skipped;
			lock.Leave();

			UpdateQueueCounter(-1);

			if (wasSkipped)
			{
				SkipProcess(*info);
				continue;
			}

			UpdateActiveProcessCounter(isLocal, 1);
	
			auto exitInfo = new ExitProcessInfo();
			exitInfo->scheduler = this;
			exitInfo->startInfo = info;
			exitInfo->isLocal = isLocal;
			exitInfo->processIndex = indexToRun;

			ProcessStartInfo si = *info;
			si.userData = exitInfo;
			si.exitedFunc = [](void* userData, const ProcessHandle& handle)
				{
					auto ei = (ExitProcessInfo*)userData;
					ei->scheduler->ProcessExited(ei, handle);
				};

			UBA_ASSERT(si.rules);
			if (m_writeToCache && si.rules->IsCacheable())
				si.trackInputs = true;
			else if (prevStatus == ProcessStatus_QueuedForCache)
			{
				// TODO: This should not use work manager since it is mostly waiting on network
				m_session.GetServer().AddWork([this, exitInfo]()
					{
						ProcessStartInfo& si = *exitInfo->startInfo;
						u64 startTime = GetTime();

						CacheResult cacheResult;
						if (m_cacheClient->FetchFromCache(cacheResult, *m_rootPaths[0], 0, si) && cacheResult.hit)
						{
							auto process = new CachedProcess(si);
							ProcessHandle ph(process);
							exitInfo->startInfo->queryCacheTime = GetTime() - startTime;
							ExitProcess(*exitInfo, *process, 0, true);
						}
						else
						{
							exitInfo->startInfo->queryCacheTime = GetTime() - startTime;
							HandleCacheMissed(exitInfo);
						}
					}, 1, TC("DownloadCache"));
				return true;
			}

			if (isLocal)
				m_session.RunProcess(si, true, canDetour);
			else
				m_session.RunProcessRemote(si, 1.0f, info->knownInputs, info->knownInputsCount);
			return true;
		}
	}

	bool Scheduler::HandleReuseMessage(Process& process, NextProcessInfo& outNextProcess, u32 prevExitCode)
	{
		if (!m_enableProcessReuse)
			return false;

		auto& currentStartInfo = process.GetStartInfo();
		auto ei = (ExitProcessInfo*)currentStartInfo.userData;
		if (!ei) // If null, process has already exited from some other thread
			return false;

		ExitProcess(*ei, process, prevExitCode, false);

		ei->startInfo = nullptr;
		ei->processIndex = ~0u;
		if (ei->wasReturned)
			return false;

		bool isLocal = !process.IsRemote();

		while (true)
		{
			ProcessStatus prevStatus;
			SCOPED_WRITE_LOCK(m_processEntriesLock, lock);
			u32 indexToRun = PopProcess(isLocal, prevStatus);
			if (indexToRun == ~0u)
				return false;
			UBA_ASSERT(prevStatus != ProcessStatus_QueuedForCache);
			auto& processEntry = m_processEntries[indexToRun];
			auto newInfo = processEntry.info;
			bool wasSkipped = processEntry.status == ProcessStatus_Skipped;
			lock.Leave();

			UpdateQueueCounter(-1);

			if (wasSkipped)
			{
				SkipProcess(*newInfo);
				continue;
			}

			UpdateActiveProcessCounter(isLocal, 1);

			ei->startInfo = newInfo;
			ei->processIndex = indexToRun;

			auto& si = *newInfo;
			outNextProcess.arguments = si.arguments;
			outNextProcess.workingDir = si.workingDir;
			outNextProcess.description = si.description;
			outNextProcess.logFile = si.logFile;

			#if UBA_DEBUG
			auto PrepPath = [this](StringBufferBase& out, const ProcessStartInfo& psi)
				{
					if (IsAbsolutePath(psi.application))
						FixPath(psi.application, nullptr, 0, out);
					else
						SearchPathForFile(m_session.GetLogger(), out, psi.application, psi.workingDir);
				};
			StringBuffer<> temp1;
			StringBuffer<> temp2;
			PrepPath(temp1, currentStartInfo);
			PrepPath(temp2, si);
			UBA_ASSERTF(temp1.Equals(temp2.data), TC("%s vs %s"), temp1.data, temp2.data);
			#endif
			
			return true;
		}
	}

	void Scheduler::ExitProcess(ExitProcessInfo& info, Process& process, u32 exitCode, bool fromCache)
	{
		ProcessHandle ph;
		ph.m_process = &process;

		auto si = info.startInfo;
		if (auto func = si->exitedFunc)
			func(si->userData, ph);

		SCOPED_WRITE_LOCK(m_processEntriesLock, lock);
		auto& entry = m_processEntries[info.processIndex];
		u32* dependencies = entry.dependencies;
		entry.status = exitCode == 0 ? ProcessStatus_Success : ProcessStatus_Failed;
		entry.info = nullptr;
		entry.dependencies = nullptr;
		if (info.isLocal)
		{
			if (fromCache)
				--m_activeCacheQueries;
			else
				m_activeLocalProcessWeight -= si->weight;
		}
		lock.Leave();

		UpdateActiveProcessCounter(info.isLocal, -1);
		FinishProcess(ph);
		m_updateThreadLoop.Set();
		delete[] dependencies;
		delete si;

		if (m_writeToCache && exitCode == 0)
		{
			// TODO: Read dep.json file
			UBA_ASSERTF(false, TC("Not implemented"));
		}

		ph.m_process = nullptr;
	}

	void Scheduler::SkipProcess(ProcessStartInfo2& info)
	{
		ProcessHandle ph(new SkippedProcess(info));
		if (auto func = info.exitedFunc)
			func(info.userData, ph);
		FinishProcess(ph);
	}

	void Scheduler::UpdateQueueCounter(int offset)
	{
		m_queuedProcesses += u32(offset);
		m_session.UpdateProgress(m_queuedProcesses + m_activeLocalProcesses + m_activeRemoteProcesses + m_finishedProcesses, m_finishedProcesses, 0);
	}

	void Scheduler::UpdateActiveProcessCounter(bool isLocal, int offset)
	{
		if (isLocal)
			m_activeLocalProcesses += u32(offset);
		else
			m_activeRemoteProcesses += u32(offset);
	}

	void Scheduler::FinishProcess(const ProcessHandle& handle)
	{
		++m_finishedProcesses;
		if (m_processFinished)
			m_processFinished(handle);
		m_session.UpdateProgress(m_queuedProcesses + m_activeLocalProcesses + m_activeRemoteProcesses + m_finishedProcesses, m_finishedProcesses, 0);
	}

	bool Scheduler::EnqueueFromFile(const tchar* yamlFilename)
	{
		auto& logger = m_session.GetLogger();

		TString app;
		TString arg;
		TString dir;
		TString desc;
		bool allowDetour = true;
		bool allowRemote = true;
		float weight = 1.0f;
		Vector<u32> deps;

		ProcessStartInfo si;

		auto enqueueProcess = [&]()
			{
				si.application = app.c_str();
				si.arguments = arg.c_str();
				si.workingDir = dir.c_str();
				si.description = desc.c_str();

				#if UBA_DEBUG
				StringBuffer<> logFile;
				if (true)
				{
					static u32 processId = 1; // TODO: This should be done in a better way.. or not at all?
					GenerateNameForProcess(logFile, si.arguments, ++processId);
					logFile.Append(TC(".log"));
					si.logFile = logFile.data;
				};
				#endif

				EnqueueProcessInfo info { si };
				info.dependencies = deps.data();
				info.dependencyCount = u32(deps.size());
				info.canDetour = allowDetour;
				info.canExecuteRemotely = allowRemote;
				info.weight = weight;
				EnqueueProcess(info);
				app.clear();
				arg.clear();
				dir.clear();
				desc.clear();
				deps.clear();
				allowDetour = true;
				allowRemote = true;
				weight = 1.0f;
			};

		enum InsideArray
		{
			InsideArray_None,
			InsideArray_CacheRoots,
			InsideArray_Processes,
		};

		InsideArray insideArray = InsideArray_None;

		auto readLine = [&](const TString& line)
			{
				const tchar* keyStart = line.c_str();
				while (*keyStart && *keyStart == ' ')
					++keyStart;
				if (!*keyStart)
					return true;
				u32 indentation = u32(keyStart - line.c_str());

				if (insideArray != InsideArray_None && !indentation)
					insideArray = InsideArray_None;

				StringBuffer<32> key;
				const tchar* valueStart = nullptr;

				if (*keyStart == '-')
				{
					UBA_ASSERT(insideArray != InsideArray_None);
					valueStart = keyStart + 2;
				}
				else
				{
					const tchar* colon = TStrchr(keyStart, ':');
					if (!colon)
						return false;
					key.Append(keyStart, colon - keyStart);
					valueStart = colon + 1;
					while (*valueStart && *valueStart == ' ')
						++valueStart;
				}

				switch (insideArray)
				{
				case InsideArray_None:
				{
					if (key.Equals(TC("environment")))
					{
						#if PLATFORM_WINDOWS
						SetEnvironmentVariable(TC("PATH"), valueStart);
						#endif
						return true;
					}
					if (key.Equals(TC("cacheroots")))
					{
						insideArray = InsideArray_CacheRoots;
						return true;
					}
					if (key.Equals(TC("processes")))
					{
						insideArray = InsideArray_Processes;
						return true;
					}
					return true;
				}
				case InsideArray_CacheRoots:
				{
					auto& rootPaths = *m_rootPaths.emplace_back(new RootPaths());
					if (Equals(valueStart, TC("SystemRoots")))
						rootPaths.RegisterSystemRoots(logger);
					else
						rootPaths.RegisterRoot(logger, valueStart);
					return true;
				}
				case InsideArray_Processes:
				{
					if (*keyStart == '-')
					{
						keyStart += 2;
						if (!app.empty())
							enqueueProcess();
					}

					if (key.Equals(TC("app")))
						app = valueStart;
					else if (key.Equals(TC("arg")))
						arg = valueStart;
					else if (key.Equals(TC("dir")))
						dir = valueStart;
					else if (key.Equals(TC("desc")))
						desc = valueStart;
					else if (key.Equals(TC("detour")))
						allowDetour = !Equals(valueStart, TC("false"));
					else if (key.Equals(TC("remote")))
						allowRemote = !Equals(valueStart, TC("false"));
					else if (key.Equals(TC("weight")))
						StringBuffer<32>(valueStart).Parse(weight);
					else if (key.Equals(TC("dep")))
					{
						const tchar* depStart = TStrchr(valueStart, '[');
						if (!depStart)
							return false;
						++depStart;
						StringBuffer<32> depStr;
						for (const tchar* it = depStart; *it; ++it)
						{
							if (*it != ']' && *it != ',')
							{
								if (*it != ' ')
									depStr.Append(*it);
								continue;
							}
							u32 depIndex;
							if (!depStr.Parse(depIndex))
								return false;
							depStr.Clear();
							deps.push_back(depIndex);

							if (!*it)
								break;
							depStart = it + 1;
						}
					}
					return true;
				}
				}
				return true;
			};

		if (!ReadLines(logger, yamlFilename, readLine))
			return false;

		if (!app.empty())
			enqueueProcess();

		return true;
	}

}
