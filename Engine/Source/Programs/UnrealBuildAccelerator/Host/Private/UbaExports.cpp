// Copyright Epic Games, Inc. All Rights Reserved.

#include "UbaExports.h"
#include "UbaAWS.h"
#include "UbaCacheClient.h"
#include "UbaConfig.h"
#include "UbaCoordinatorWrapper.h"
#include "UbaNetworkBackendQuic.h"
#include "UbaNetworkBackendTcp.h"
#include "UbaNetworkClient.h"
#include "UbaProcess.h"
#include "UbaRootPaths.h"
#include "UbaScheduler.h"
#include "UbaStorageServer.h"
#include "UbaSessionServer.h"

#if PLATFORM_WINDOWS
#include "UbaWinBinDependencyParser.h"
#endif

namespace uba
{
	CallbackLogWriter::CallbackLogWriter(BeginScopeCallback begin, EndScopeCallback end, LogCallback log) : m_beginScope(begin), m_endScope(end), m_logCallback(log)
	{
	}

	void CallbackLogWriter::BeginScope()
	{
		(*m_beginScope)();
	}
	void CallbackLogWriter::EndScope()
	{
		(*m_endScope)();
	}

	void CallbackLogWriter::Log(LogEntryType type, const uba::tchar* str, u32 strLen, const uba::tchar* prefix, u32 prefixLen)
	{
		StringBuffer<> strBuf;
		if (prefixLen && strLen + prefixLen + 3 < strBuf.capacity) // TODO: Send prefix and prefixLen through callback
		{
			strBuf.Append(prefix, prefixLen);
			strBuf.Append(TC(" - "), 3);
			strBuf.Append(str, strLen);
			strLen += prefixLen + 3;
			str = strBuf.data;
		}
		(*m_logCallback)(type, str, strLen);
	}

	class NetworkServerWithBackend : public NetworkServer
	{
	public:
		NetworkServerWithBackend(bool& outSuccess, const NetworkServerCreateInfo& info, NetworkBackend* nb)
		: NetworkServer(outSuccess, info), backend(nb)
		{
		}

		NetworkBackend* backend;
	};

	class NetworkClientWithBackend : public NetworkClient
	{
	public:
		NetworkClientWithBackend(bool& outSuccess, const NetworkClientCreateInfo& info, NetworkBackend* nb, const tchar* name)
		: NetworkClient(outSuccess, info, name), backend(nb)
		{
		}

		NetworkBackend* backend;
	};

	class RootPathsWithLogger : public RootPaths
	{
	public:
		RootPathsWithLogger(LogWriter& writer) : logger(writer) {}
		LoggerWithWriter logger;
	};

	#define UBA_USE_SIGNALHANDLER 0//PLATFORM_LINUX // It might be that we can't use signal handlers in c# processes.. so don't set this to 1

	#if UBA_USE_SIGNALHANDLER
	void SignalHandler(int sig, siginfo_t* si, void* unused)
	{
		StringBuffer<256> desc;
		desc.Append("Segmentation fault at +0x").AppendHex(u64(si->si_addr));
		UbaAssert(desc.data, "", 0, "", -1);
	}
	#endif

	Config& GetConfig(const tchar* fileName = nullptr)
	{
		static Config config;
		if (config.IsLoaded())
			return config;
		LoggerWithWriter logger(g_nullLogWriter);
		StringBuffer<> temp;
		if (!fileName)
		{
			GetDirectoryOfCurrentModule(logger, temp);
			temp.EnsureEndsWithSlash().Append(TC("UbaHost.toml"));
			fileName = temp.data;
		}

		config.LoadFromFile(logger, fileName);
		return config;
	}
}

extern "C"
{
	uba::LogWriter* GetDefaultLogWriter()
	{
		return &uba::g_consoleLogWriter;
	}

	uba::LogWriter* CreateCallbackLogWriter(uba::CallbackLogWriter::BeginScopeCallback begin, uba::CallbackLogWriter::EndScopeCallback end, uba::CallbackLogWriter::LogCallback log)
	{
		#if UBA_USE_SIGNALHANDLER
		struct sigaction action;
		memset(&action, 0, sizeof(action));
		sigfillset(&action.sa_mask);
		action.sa_flags = SA_SIGINFO | SA_RESTART | SA_ONSTACK;
		action.sa_sigaction = uba::SignalHandler;
		sigaction(SIGSEGV, &action, NULL);
		//sigaction(SIGABRT, &action, NULL);
		#endif

		return new uba::CallbackLogWriter(begin, end, log);
	}

	void DestroyCallbackLogWriter(uba::LogWriter* writer)
	{
		if (writer != &uba::g_consoleLogWriter)
			delete writer;
	}

	bool Config_Load(const uba::tchar* configFile)
	{
		uba::GetConfig(configFile);
		return true;
	}

	uba::NetworkServer* NetworkServer_Create(uba::LogWriter& writer, uba::u32 workerCount, uba::u32 sendSize, uba::u32 receiveTimeoutSeconds, bool useQuic)
	{
		using namespace uba;
		NetworkBackend* networkBackend;
		#if UBA_USE_QUIC
		if (useQuic)
			networkBackend = new NetworkBackendQuic(writer);
		else
		#endif
			networkBackend = new NetworkBackendTcp(writer);

		NetworkServerCreateInfo info(writer);

		info.Apply(GetConfig());

		info.workerCount = workerCount;
		info.sendSize = sendSize;
		info.receiveTimeoutSeconds = receiveTimeoutSeconds;

		bool success = true;
		auto server = new NetworkServerWithBackend(success, info, networkBackend);
		if (success)
			return server;
		delete server;
		return nullptr;
	}

	void NetworkServer_Destroy(uba::NetworkServer* server)
	{
		auto s = (uba::NetworkServerWithBackend*)server;
		auto networkBackend = s->backend;
		delete s;
		delete networkBackend;
	}

	bool NetworkServer_StartListen(uba::NetworkServer* server, int port, const uba::tchar* ip, const uba::tchar* crypto)
	{
		using namespace uba;

		auto s = (NetworkServerWithBackend*)server;

		bool requiresCrypto = false;
		if (crypto && *crypto)
		{
			//server->GetLogger().Error(TC("CRYPTO: %s"), crypto);
			u8 crypto128Data[16];
			if (!CryptoFromString(crypto128Data, 16, crypto))
				return server->GetLogger().Error(TC("Failed to parse crypto key %s"), crypto);
			s->RegisterCryptoKey(crypto128Data);
			requiresCrypto = true;
		}

		return s->StartListen(*s->backend, u16(port), ip, requiresCrypto);
	}

	void NetworkServer_Stop(uba::NetworkServer* server)
	{
		auto s = (uba::NetworkServerWithBackend*)server;
		auto networkBackend = s->backend;
		networkBackend->StopListen();
		server->DisconnectClients();
	}


	bool NetworkServer_AddClient(uba::NetworkServer* server, const uba::tchar* ip, int port, const uba::tchar* crypto)
	{
		using namespace uba;
		u8 crypto128Data[16];
		u8* crypto128 = nullptr;
		if (CryptoFromString(crypto128Data, 16, crypto))
			crypto128 = crypto128Data;

		auto s = (NetworkServerWithBackend*)server;
		return s->AddClient(*s->backend, ip, u16(port), crypto128);
	}

	uba::StorageServer* StorageServer_Create(uba::NetworkServer& server, const uba::tchar* rootDir, uba::u64 casCapacityBytes, bool storeCompressed, uba::LogWriter& writer, const uba::tchar* zone)
	{
		using namespace uba;

		StorageServerCreateInfo info(server, rootDir, writer);
		info.Apply(GetConfig());

		#if UBA_USE_AWS
		StringBuffer<> fixedRootDir;
		fixedRootDir.count = GetFullPathNameW(info.rootDir, fixedRootDir.capacity, fixedRootDir.data, NULL);
		fixedRootDir.Replace('/', PathSeparator).EnsureEndsWithSlash();
		info.rootDir = fixedRootDir.data;
		AWS aws;
		if (!zone || !*zone)
		{
			LoggerWithWriter logger(writer, TC(""));
			if (aws.QueryAvailabilityZone(logger, info.rootDir))
				zone = aws.GetAvailabilityZone();
		}
		#endif

		StringBuffer<256> zoneTemp;
		if (!zone || !*zone)
			if (GetZone(zoneTemp))
				zone = zoneTemp.data;

		info.casCapacityBytes = casCapacityBytes;
		info.storeCompressed = storeCompressed;
		info.zone = zone;
		return new StorageServer(info);
	}

	void StorageServer_Destroy(uba::StorageServer* storageServer)
	{
		delete storageServer;
	}

	void StorageServer_SaveCasTable(uba::StorageServer* storageServer)
	{
		storageServer->SaveCasTable(true);
	}

	void StorageServer_RegisterDisallowedPath(uba::StorageServer* storageServer, const uba::tchar* path)
	{
		storageServer->RegisterDisallowedPath(path);
	}

	void StorageServer_DeleteFile(uba::StorageServer* storageServer, const uba::tchar* file)
	{
		storageServer->DeleteCasForFile(file);
	}

	uba::u32 ProcessHandle_GetExitCode(const uba::ProcessHandle* handle)
	{
		return handle->GetExitCode();
	}

	const uba::tchar* ProcessHandle_GetExecutingHost(uba::ProcessHandle* handle)
	{
		return handle->GetExecutingHost();
	}

	const uba::tchar* ProcessHandle_GetLogLine(const uba::ProcessHandle* handle, uba::u32 index)
	{
		const auto& lines = handle->GetLogLines();
		if (index >= lines.size()) return nullptr;
		return lines[index].text.c_str();
	}

	uba::u64 ProcessHandle_GetHash(uba::ProcessHandle* handle)
	{
		return handle->GetHash();
	}

	// 100ns ticks
	uba::u64 ProcessHandle_GetTotalProcessorTime(uba::ProcessHandle* handle)
	{
		return uba::TimeToTick(handle->GetTotalProcessorTime());
	}

	// 100ns ticks
	uba::u64 ProcessHandle_GetTotalWallTime(uba::ProcessHandle* handle)
	{
		return uba::TimeToTick(handle->GetTotalWallTime());
	}

	bool ProcessHandle_WaitForExit(uba::ProcessHandle* handle, uba::u32 millisecondsTimeout)
	{
		return handle->WaitForExit(millisecondsTimeout);
	}

	void ProcessHandle_Cancel(uba::ProcessHandle* handle, bool terminate)
	{
		handle->Cancel(terminate);
	}

	void ProcessHandle_Destroy(uba::ProcessHandle* handle)
	{
		delete handle;
	}

	void DestroyProcessHandle(uba::ProcessHandle* handle)
	{
		delete handle;
	}

	const uba::ProcessStartInfo* Process_GetStartInfo(uba::Process& process)
	{
		return &process.GetStartInfo();
	}

	uba::SessionServerCreateInfo* SessionServerCreateInfo_Create(uba::StorageServer& storage, uba::NetworkServer& client, uba::LogWriter& writer, const uba::tchar* rootDir, const uba::tchar* traceOutputFile, bool disableCustomAllocator, bool launchVisualizer, bool resetCas, bool writeToDisk, bool detailedTrace, bool allowWaitOnMem, bool allowKillOnMem, bool storeObjFilesCompressed)
	{
		auto info = new uba::SessionServerCreateInfo(storage, client, writer);
		info->Apply(uba::GetConfig());
		info->rootDir = TStrdup(rootDir);
		info->traceOutputFile = TStrdup(traceOutputFile);
		info->disableCustomAllocator = disableCustomAllocator;
		info->launchVisualizer = launchVisualizer;
		info->resetCas = resetCas;
		info->shouldWriteToDisk = writeToDisk;
		info->detailedTrace = detailedTrace;
		info->allowWaitOnMem = allowWaitOnMem;
		info->allowKillOnMem = allowKillOnMem;
		info->storeObjFilesCompressed = storeObjFilesCompressed;
		//info->remoteTraceEnabled = true;
		//info->remoteLogEnabled = true;
		return info;
	}

	void SessionServerCreateInfo_Destroy(uba::SessionServerCreateInfo* info)
	{
		free((void*)info->traceOutputFile);
		free((void*)info->rootDir);
		delete info;
	}

	uba::SessionServer* SessionServer_Create(const uba::SessionServerCreateInfo& info, const uba::u8* environment, uba::u32 environmentSize)
	{
		return new uba::SessionServer(info, environment, environmentSize);
	}
	void SessionServer_SetRemoteProcessAvailable(uba::SessionServer* server, SessionServer_RemoteProcessAvailableCallback* available, void* userData)
	{
		server->SetRemoteProcessSlotAvailableEvent([available, userData]() { available(userData); });
	}
	void SessionServer_SetRemoteProcessReturned(uba::SessionServer* server, SessionServer_RemoteProcessReturnedCallback* returned, void* userData)
	{
		server->SetRemoteProcessReturnedEvent([returned, userData](uba::Process& process) { returned(process, userData); });
	}
	void SessionServer_RefreshDirectory(uba::SessionServer* server, const uba::tchar* directory)
	{
		server->RefreshDirectory(directory);
	}
	void SessionServer_RegisterNewFile(uba::SessionServer* server, const uba::tchar* filePath)
	{
		server->RegisterNewFile(filePath);
	}
	void SessionServer_RegisterDeleteFile(uba::SessionServer* server, const uba::tchar* filePath)
	{
		server->RegisterDeleteFile(filePath);
	}
	uba::ProcessHandle* SessionServer_RunProcess(uba::SessionServer* server, uba::ProcessStartInfo& info, bool async, bool enableDetour)
	{
		return new uba::ProcessHandle(server->RunProcess(info, async, enableDetour));
	}
	uba::ProcessHandle* SessionServer_RunProcessRemote(uba::SessionServer* server, uba::ProcessStartInfo& info, float weight, const void* knownInputs, uba::u32 knownInputsCount)
	{
		return new uba::ProcessHandle(server->RunProcessRemote(info, weight, knownInputs, knownInputsCount));
	}
	uba::ProcessHandle* SessionServer_RunProcessRacing(uba::SessionServer* server, uba::u32 raceAgainstRemoteProcessId)
	{
		return new uba::ProcessHandle(server->RunProcessRacing(raceAgainstRemoteProcessId));
	}
	void SessionServer_SetMaxRemoteProcessCount(uba::SessionServer* server, uba::u32 count)
	{
		return server->SetMaxRemoteProcessCount(count);
	}
	void SessionServer_DisableRemoteExecution(uba::SessionServer* server)
	{
		server->GetServer().DisallowNewClients();
		server->DisableRemoteExecution();
	}
	void SessionServer_PrintSummary(uba::SessionServer* server)
	{
		uba::LoggerWithWriter logger(server->GetLogWriter());
		server->PrintSummary(logger);
		server->GetStorage().PrintSummary(logger);
		server->GetServer().PrintSummary(logger);
		uba::KernelStats::GetGlobal().Print(logger, true);
		uba::PrintContentionSummary(logger);
	}
	void SessionServer_CancelAll(uba::SessionServer* server)
	{
		++server->GetServer().GetLogger().isMuted; // Mute forever
		++server->GetLogger().isMuted; // Mute forever
		server->CancelAllProcessesAndWait();
	}
	void SessionServer_SetCustomCasKeyFromTrackedInputs(uba::SessionServer* server, uba::ProcessHandle* handle, const uba::tchar* fileName, const uba::tchar* workingDir)
	{
		const auto& TrackedInputs = handle->GetTrackedInputs();
		server->SetCustomCasKeyFromTrackedInputs(fileName, workingDir, TrackedInputs.data(), (uba::u32)TrackedInputs.size());
	}
	uba::u32 SessionServer_BeginExternalProcess(uba::SessionServer* server, const uba::tchar* description)
	{
		return server->BeginExternalProcess(description);
	}
	void SessionServer_EndExternalProcess(uba::SessionServer* server, uba::u32 id, uba::u32 exitCode)
	{
		server->EndExternalProcess(id, exitCode);
	}

	void SessionServer_UpdateProgress(uba::SessionServer* server, uba::u32 processesTotal, uba::u32 processesDone, uba::u32 errorCount)
	{
		server->UpdateProgress(processesTotal, processesDone, errorCount);
	}

	void SessionServer_UpdateStatus(uba::SessionServer* server, uba::u32 statusRow, uba::u32 statusColumn, const uba::tchar* statusText, uba::LogEntryType statusType, const uba::tchar* statusLink)
	{
		server->UpdateStatus(statusRow, statusColumn, statusText, statusType, statusLink);
	}

	void SessionServer_RegisterCustomService(uba::SessionServer* server, SessionServer_CustomServiceFunction* function, void* userData)
	{
		server->RegisterCustomService([function, userData](uba::Process& process, const void* recv, uba::u32 recvSize, void* send, uba::u32 sendCapacity)
			{
				uba::ProcessHandle h(&process);
				return function(&h, recv, recvSize, send, sendCapacity, userData);
			});
	}

	void SessionServer_Destroy(uba::SessionServer* server)
	{
		if (server)
		{
			auto& s = (uba::NetworkServerWithBackend&)server->GetServer();
			s.backend->StopListen();
			s.DisconnectClients();
		}
		delete server;
	}

	uba::RootPaths* RootPaths_Create(uba::LogWriter& writer)
	{
		return new uba::RootPathsWithLogger(writer);
	}

	bool RootPaths_RegisterRoot(uba::RootPaths* rootPaths, const uba::tchar* path, bool includeInKey, uba::u8 id)
	{
		using namespace uba;
		auto rp = (uba::RootPathsWithLogger*)rootPaths;
		return rp->RegisterRoot(rp->logger, path, includeInKey, id);
	}

	bool RootPaths_RegisterSystemRoots(uba::RootPaths* rootPaths, uba::u8 startId)
	{
		using namespace uba;
		auto rp = (uba::RootPathsWithLogger*)rootPaths;
		return rp->RegisterSystemRoots(rp->logger, startId);
	}

	void RootPaths_Destroy(uba::RootPaths* rootPaths)
	{
		delete (uba::RootPathsWithLogger*)rootPaths;
	}


	uba::ProcessStartInfo* ProcessStartInfo_Create(const uba::tchar* application, const uba::tchar* arguments, const uba::tchar* workingDir, const uba::tchar* description, uba::u32 priorityClass, uba::u64 outputStatsThresholdMs, bool trackInputs, const uba::tchar* logFile, ProcessHandle_ExitCallback* exit)
	{
		auto info = new uba::ProcessStartInfo();
		info->application = TStrdup(application);
		info->arguments = TStrdup(arguments);
		info->workingDir = TStrdup(workingDir);
		info->description = TStrdup(description);
		info->priorityClass = priorityClass;
		info->outputStatsThresholdMs = outputStatsThresholdMs;
		info->trackInputs = trackInputs;
		info->logFile = TStrdup(logFile);
		info->exitedFunc = exit;
		return info;
	}

	void ProcessStartInfo_Destroy(uba::ProcessStartInfo* info)
	{
		free((void*)info->application);
		free((void*)info->arguments);
		free((void*)info->workingDir);
		free((void*)info->description);
		free((void*)info->logFile);
		delete info;
	}

	uba::Scheduler* Scheduler_Create(uba::SessionServer* session, uba::u32 maxLocalProcessors, bool enableProcessReuse)
	{
		uba::SchedulerCreateInfo info{*session};
		info.Apply(uba::GetConfig());
		info.maxLocalProcessors = maxLocalProcessors;
		info.enableProcessReuse = enableProcessReuse;
		info.processConfigs = &uba::GetConfig();
		return new uba::Scheduler(info);
	}

	void Scheduler_Start(uba::Scheduler* scheduler)
	{
		scheduler->Start();
	}

	uba::u32 Scheduler_EnqueueProcess(uba::Scheduler* scheduler, const uba::ProcessStartInfo& info, float weight, const void* knownInputs, uba::u32 knownInputsBytes, uba::u32 knownInputsCount)
	{
		uba::EnqueueProcessInfo epi(info);
		epi.weight = weight;
		epi.knownInputs = knownInputs;
		epi.knownInputsBytes = knownInputsBytes;
		epi.knownInputsCount = knownInputsCount;
		return scheduler->EnqueueProcess(epi);
	}

	void Scheduler_SetMaxLocalProcessors(uba::Scheduler* scheduler, uba::u32 maxLocalProcessors)
	{
		scheduler->SetMaxLocalProcessors(maxLocalProcessors);
	}

	void Scheduler_Stop(uba::Scheduler* scheduler)
	{
		scheduler->Stop();
	}

	void Scheduler_Destroy(uba::Scheduler* scheduler)
	{
		delete scheduler;
	}

	void Scheduler_GetStats(uba::Scheduler* scheduler, uba::u32& outQueued, uba::u32& outActiveLocal, uba::u32& outActiveRemote, uba::u32& outFinished)
	{
		scheduler->GetStats(outQueued, outActiveLocal, outActiveRemote, outFinished);
	}

	uba::CacheClient* CacheClient_Create(uba::SessionServer* session, bool reportMissReason, const uba::tchar* crypto)
	{
		using namespace uba;
		LogWriter& writer = session->GetLogWriter();
		StorageImpl& storage = (StorageImpl&)session->GetStorage();
		auto& server = (NetworkServerWithBackend&)session->GetServer();

		u8 crypto128Data[16];
		u8* crypto128 = nullptr;
		if (crypto && *crypto)
		{
			if (!CryptoFromString(crypto128Data, 16, crypto))
			{
				LoggerWithWriter(writer, TC("UbaCacheClient")).Error(TC("Failed to parse crypto key %s"), crypto);
				return nullptr;
			}
			crypto128 = crypto128Data;
		}

		NetworkClientCreateInfo ncci(writer);
		ncci.receiveTimeoutSeconds = 60;
		ncci.cryptoKey128 = crypto128;
		bool ctorSuccess = false;
		auto networkClient = new NetworkClientWithBackend(ctorSuccess, ncci, server.backend, TC("UbaCache"));
		if (!ctorSuccess)
		{
			delete networkClient;
			return nullptr;
		}
		CacheClientCreateInfo info{writer, storage, *networkClient, *session};
		info.Apply(GetConfig());

		info.reportMissReason = reportMissReason;
		return new CacheClient(info);
	}

	bool CacheClient_Connect(uba::CacheClient* cacheClient, const uba::tchar* host, int port)
	{
		using namespace uba;
		auto& networkClient = (NetworkClientWithBackend&)cacheClient->GetClient();

		if (!networkClient.Connect(*networkClient.backend, host, u16(port)))
			return false;
		cacheClient->GetStorage().LoadCasTable();
		return true;
	}

	bool CacheClient_WriteToCache(uba::CacheClient* cacheClient, uba::RootPaths* rootPaths, uba::u32 bucket, const uba::ProcessHandle* process, const uba::u8* inputs, uba::u32 inputsSize, const uba::u8* outputs, uba::u32 outputsSize)
	{
		using namespace uba;
		StackBinaryWriter<16*1024> logLinesWriter;

		auto& logLines = process->GetLogLines();
		for (auto& line : logLines)
		{
			if (logLinesWriter.GetCapacityLeft() < 1 + GetStringWriteSize(line.text.c_str(), line.text.size()))
				break;
			logLinesWriter.WriteString(line.text);
			logLinesWriter.WriteByte(line.type);
		}

		return cacheClient->WriteToCache(*rootPaths, bucket, process->GetStartInfo(), inputs, inputsSize, outputs, outputsSize, logLinesWriter.GetData(), logLinesWriter.GetPosition(), process->GetId());
	}

	bool CacheClient_WriteToCache2(uba::CacheClient* cacheClient, uba::RootPaths* rootPaths, uba::u32 bucket, const uba::ProcessHandle* process, const uba::u8* inputs, uba::u32 inputsSize, const uba::u8* outputs, uba::u32 outputsSize, const uba::u8* logLines, uba::u32 logLinesSize)
	{
		return cacheClient->WriteToCache(*rootPaths, bucket, process->GetStartInfo(), inputs, inputsSize, outputs, outputsSize, logLines, logLinesSize, process->GetId());
	}

	uba::u32 CacheClient_FetchFromCache(uba::CacheClient* cacheClient, uba::RootPaths* rootPaths, uba::u32 bucket, const uba::ProcessStartInfo& info)
	{
		using namespace uba;
		CacheResult cacheResult;
		bool res = cacheClient->FetchFromCache(cacheResult, *rootPaths, bucket, info);
		return (res && cacheResult.hit) ? 1 : 0;
	}

	uba::CacheResult* CacheClient_FetchFromCache2(uba::CacheClient* cacheClient, uba::RootPaths* rootPaths, uba::u32 bucket, const uba::ProcessStartInfo& info)
	{
		using namespace uba;
		auto cacheResult = new CacheResult();
		if (cacheClient->FetchFromCache(*cacheResult, *rootPaths, bucket, info))
			return cacheResult;
		delete cacheResult;
		return nullptr;
	}

	void CacheClient_RequestServerShutdown(uba::CacheClient* cacheClient, const uba::tchar* reason)
	{
		cacheClient->RequestServerShutdown(reason);
	}

	void CacheClient_Destroy(uba::CacheClient* cacheClient)
	{
		using namespace uba;
		auto& networkClient = (NetworkClientWithBackend&)cacheClient->GetClient();
		networkClient.Disconnect();
		delete cacheClient;
		delete &networkClient;
	}

	const uba::tchar* CacheResult_GetLogLine(uba::CacheResult* result, uba::u32 index)
	{
		auto& lines = result->logLines;
		if (index >= lines.size())
			return nullptr;
		return lines[index].text.c_str();
	}

	uba::u32 CacheResult_GetLogLineType(uba::CacheResult* result, uba::u32 index)
	{
		auto& lines = result->logLines;
		if (index >= lines.size())
			return 0;
		return uba::u32(lines[index].type);
	}

	void CacheResult_Delete(uba::CacheResult* result)
	{
		delete result;
	}

	void Uba_SetCustomAssertHandler(Uba_CustomAssertHandler* handler)
	{
		uba::SetCustomAssertHandler(handler);
	}

	void Uba_FindImports(const uba::tchar* binary, ImportFunc* func, void* userData)
	{
#if PLATFORM_WINDOWS
		uba::StringBuffer<> errors;
		uba::FindImports(binary, [&](const uba::tchar* importName, bool isKnown, const char* const* importLoaderPaths) { func(importName, userData); }, errors);
#endif
	}

	struct UbaInstance
	{
		uba::Scheduler* scheduler;
		uba::TString workDir;
		uba::CoordinatorWrapper coordinator;
	};

	void* Uba_Create(const uba::tchar* configFile)
	{
		using namespace uba;
		auto& config = GetConfig(configFile);
		auto networkServer = (uba::NetworkServerWithBackend*)NetworkServer_Create();
		auto storageServer = StorageServer_Create(*networkServer, nullptr, 0, true);

		SessionServerCreateInfo ssci((Storage&)*storageServer, *networkServer);
		ssci.Apply(config);
		auto sessionServer = SessionServer_Create(ssci);

		uba::SchedulerCreateInfo sci{*sessionServer};
		sci.Apply(config);
		sci.processConfigs = &config;
		auto scheduler = new uba::Scheduler(sci);
		scheduler->Start();

		bool networkListen = true;
		if (auto* ubaTable = config.GetTable(TC("Uba")))
			ubaTable->GetValueAsBool(networkListen, TC("NetworkListen"));

		if (networkListen)
			NetworkServer_StartListen(networkServer);

		auto ubaInstance = new UbaInstance();
		ubaInstance->scheduler = scheduler;
		
		StringBuffer<> temp;
		GetCurrentDirectoryW(temp);
		ubaInstance->workDir = temp.data;

		if (auto coordinatorTable = config.GetTable(TC("Coordinator")))
		{
			const tchar* coordinatorName;
			if (coordinatorTable->GetValueAsString(coordinatorName, TC("Name")))
			{
				auto& logger = sessionServer->GetLogger();
				const tchar* rootDir = nullptr;
				coordinatorTable->GetValueAsString(rootDir, TC("RootDir"));
				if (!rootDir)
					rootDir = sessionServer->GetRootDir();
				StringBuffer<512> coordinatorWorkDir(rootDir);
				coordinatorWorkDir.EnsureEndsWithSlash().Append(coordinatorName);
				StringBuffer<512> binariesDir;
				if (!GetDirectoryOfCurrentModule(logger, binariesDir))
					return nullptr;

				CoordinatorCreateInfo cinfo;
				cinfo.workDir = coordinatorWorkDir.data;
				cinfo.binariesDir = binariesDir.data;

				coordinatorTable->GetValueAsString(cinfo.pool, TC("Pool"));
				UBA_ASSERT(cinfo.pool);

				cinfo.maxCoreCount = 500;
				coordinatorTable->GetValueAsU32(cinfo.maxCoreCount, TC("MaxCoreCount"));

				cinfo.logging = false;
				coordinatorTable->GetValueAsBool(cinfo.logging, TC("Log"));

				const tchar* uri = nullptr;
				if (coordinatorTable->GetValueAsString(uri, TC("Uri")))
					uba::SetEnvironmentVariableW(TC("UE_HORDE_URL"), uri);

				if (!ubaInstance->coordinator.Create(logger, coordinatorName, cinfo, *networkServer->backend, *networkServer, scheduler))
					return nullptr;
			}
		}

		return ubaInstance;
	}

	uba::u32 Uba_RunProcess(void* uba, const uba::tchar* app, const uba::tchar* args, const uba::tchar* workDir, const uba::tchar* desc, void* userData, ProcessHandle_ExitCallback* exit)
	{
		using namespace uba;

		auto& ubaInstance = *(UbaInstance*)uba;

		if (!workDir)
			workDir = ubaInstance.workDir.data();

		auto scheduler = ubaInstance.scheduler;
		ProcessStartInfo info;
		info.application = app;
		info.arguments = args;
		info.workingDir = workDir;
		info.description = desc;
		info.userData = userData;
		info.exitedFunc = exit;
		return Scheduler_EnqueueProcess(scheduler, info, 1.0f, nullptr, 0, 0);
	}

	void Uba_RegisterNewFile(void* uba, const uba::tchar* file)
	{
		using namespace uba;
		auto& ubaInstance = *(UbaInstance*)uba;
		ubaInstance.scheduler->GetSession().RegisterNewFile(file);
	}

	void Uba_Destroy(void* uba)
	{
		using namespace uba;
		auto ubaInstance = (UbaInstance*)uba;
		auto scheduler = ubaInstance->scheduler;
		auto sessionServer = &scheduler->GetSession();
		auto storageServer = (StorageServer*)&sessionServer->GetStorage();
		auto networkServer = &sessionServer->GetServer();

		NetworkServer_Stop(networkServer);
		SessionServer_CancelAll(sessionServer);

		delete ubaInstance;

		Scheduler_Destroy(scheduler);
		SessionServer_Destroy(sessionServer);
		StorageServer_Destroy(storageServer);
		NetworkServer_Destroy(networkServer);
	}
}
