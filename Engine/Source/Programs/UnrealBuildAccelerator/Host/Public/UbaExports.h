// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UbaLogWriter.h"
#include "UbaDefaultConstants.h"

namespace uba
{
	class CacheClient;
	class NetworkServer;
	class Process;
	class ProcessHandle;
	class RootPaths;
	class Session;
	class SessionServer;
	class Scheduler;
	class StorageServer;
	struct CacheResult;
	struct ProcessStartInfo;
	struct SessionServerCreateInfo;

	class CallbackLogWriter : public LogWriter
	{
	public:
		using BeginScopeCallback = void();
		using EndScopeCallback = void();
		using LogCallback = void(LogEntryType type, const uba::tchar* str, u32 strLen);

		UBA_API CallbackLogWriter(BeginScopeCallback* begin, EndScopeCallback* end, LogCallback* log);
		UBA_API virtual void BeginScope() override;
		UBA_API virtual void EndScope() override;
		UBA_API virtual void Log(LogEntryType type, const uba::tchar* str, u32 strLen, const uba::tchar* prefix = nullptr, u32 prefixLen = 0) override;

	private:
		BeginScopeCallback* m_beginScope;
		EndScopeCallback* m_endScope;
		LogCallback* m_logCallback;
	};
}

extern "C"
{
	// LogWriter
	UBA_API uba::LogWriter* GetDefaultLogWriter();
	UBA_API uba::LogWriter* CreateCallbackLogWriter(uba::CallbackLogWriter::BeginScopeCallback begin, uba::CallbackLogWriter::EndScopeCallback end, uba::CallbackLogWriter::LogCallback log);
	UBA_API void DestroyCallbackLogWriter(uba::LogWriter* writer);

	// Config
	UBA_API bool Config_Load(const uba::tchar* configFile);

	// NetworkServer
	UBA_API uba::NetworkServer* NetworkServer_Create(uba::LogWriter& writer = uba::g_consoleLogWriter, uba::u32 workerCount = 64, uba::u32 sendSize = uba::SendDefaultSize, uba::u32 receiveTimeoutSeconds = 60, bool useQuic = false);
	UBA_API void NetworkServer_Destroy(uba::NetworkServer* server);
	UBA_API bool NetworkServer_StartListen(uba::NetworkServer* server, int port = uba::DefaultPort, const uba::tchar* ip = nullptr, const uba::tchar* crypto = nullptr);
	UBA_API void NetworkServer_Stop(uba::NetworkServer* server);
	UBA_API bool NetworkServer_AddClient(uba::NetworkServer* server, const uba::tchar* ip, int port = uba::DefaultPort, const uba::tchar* crypto = nullptr);

	// StorageServer
	UBA_API uba::StorageServer* StorageServer_Create(uba::NetworkServer& server, const uba::tchar* rootDir, uba::u64 casCapacityBytes, bool storeCompressed, uba::LogWriter& writer = uba::g_consoleLogWriter, const uba::tchar* zone = TC(""));
	UBA_API void StorageServer_Destroy(uba::StorageServer* storageServer);
	UBA_API void StorageServer_SaveCasTable(uba::StorageServer* storageServer);
	UBA_API void StorageServer_RegisterDisallowedPath(uba::StorageServer* storageServer, const uba::tchar* path);
	UBA_API void StorageServer_DeleteFile(uba::StorageServer* storage, const uba::tchar* file);

	// RootPaths
	UBA_API uba::RootPaths* RootPaths_Create(uba::LogWriter& writer);
	UBA_API bool RootPaths_RegisterRoot(uba::RootPaths* rootPaths, const uba::tchar* path, bool includeInKey, uba::u8 id = 0);
	UBA_API bool RootPaths_RegisterSystemRoots(uba::RootPaths* rootPaths, uba::u8 startId = 0);
	UBA_API void RootPaths_Destroy(uba::RootPaths* rootPaths);

	// ProcessStartInfo
	using ProcessHandle_ExitCallback = void(void* userData, const uba::ProcessHandle&);
	UBA_API uba::ProcessStartInfo* ProcessStartInfo_Create(const uba::tchar* application, const uba::tchar* arguments, const uba::tchar* workingDir, const uba::tchar* description, uba::u32 priorityClass, uba::u64 outputStatsThresholdMs, bool trackInputs, const uba::tchar* logFile, ProcessHandle_ExitCallback* exit);
	UBA_API void ProcessStartInfo_Destroy(uba::ProcessStartInfo* info);

	// ProcessHandle
	UBA_API uba::u32 ProcessHandle_GetExitCode(const uba::ProcessHandle* handle);
	UBA_API const uba::tchar* ProcessHandle_GetExecutingHost(uba::ProcessHandle* handle);
	UBA_API const uba::tchar* ProcessHandle_GetLogLine(const uba::ProcessHandle* handle, uba::u32 index);
	UBA_API uba::u64 ProcessHandle_GetHash(uba::ProcessHandle* handle);
	UBA_API uba::u64 ProcessHandle_GetTotalProcessorTime(uba::ProcessHandle* handle);
	UBA_API uba::u64 ProcessHandle_GetTotalWallTime(uba::ProcessHandle* handle);
	UBA_API bool ProcessHandle_WaitForExit(uba::ProcessHandle* handle, uba::u32 millisecondsTimeout);
	UBA_API void ProcessHandle_Cancel(uba::ProcessHandle* handle, bool terminate);
	UBA_API void ProcessHandle_Destroy(uba::ProcessHandle* handle);
	UBA_API const uba::ProcessStartInfo* Process_GetStartInfo(uba::Process& process);

	// SessionServer
	using SessionServer_RemoteProcessAvailableCallback = void(void* userData);
	using SessionServer_RemoteProcessReturnedCallback = void(uba::Process& process, void* userData);
	using SessionServer_CustomServiceFunction = uba::u32(uba::ProcessHandle* handle, const void* recv, uba::u32 recvSize, void* send, uba::u32 sendCapacity, void* userData);

	UBA_API uba::SessionServerCreateInfo* SessionServerCreateInfo_Create(uba::StorageServer& storage, uba::NetworkServer& client, uba::LogWriter& writer, const uba::tchar* rootDir, const uba::tchar* traceOutputFile,
		bool disableCustomAllocator, bool launchVisualizer, bool resetCas, bool writeToDisk, bool detailedTrace, bool allowWaitOnMem = false, bool allowKillOnMem = false, bool storeObjFilesCompressed = false);
	UBA_API void SessionServerCreateInfo_Destroy(uba::SessionServerCreateInfo* info);

	UBA_API uba::SessionServer* SessionServer_Create(const uba::SessionServerCreateInfo& info, const uba::u8* environment = nullptr, uba::u32 environmentSize = 0);
	UBA_API void SessionServer_SetRemoteProcessAvailable(uba::SessionServer* server, SessionServer_RemoteProcessAvailableCallback* available, void* userData);
	UBA_API void SessionServer_SetRemoteProcessReturned(uba::SessionServer* server, SessionServer_RemoteProcessReturnedCallback* returned, void* userData);
	UBA_API void SessionServer_RefreshDirectory(uba::SessionServer* server, const uba::tchar* directory);
	UBA_API void SessionServer_RegisterNewFile(uba::SessionServer* server, const uba::tchar* filePath);
	UBA_API void SessionServer_RegisterDeleteFile(uba::SessionServer* server, const uba::tchar* filePath);
	UBA_API uba::ProcessHandle* SessionServer_RunProcess(uba::SessionServer* server, uba::ProcessStartInfo& info, bool async, bool enableDetour);
	UBA_API uba::ProcessHandle* SessionServer_RunProcessRemote(uba::SessionServer* server, uba::ProcessStartInfo& info, float weight, const void* knownInputs = nullptr, uba::u32 knownInputsCount = 0);
	UBA_API uba::ProcessHandle* SessionServer_RunProcessRacing(uba::SessionServer* server, uba::u32 raceAgainstRemoteProcessId);

	UBA_API void SessionServer_SetMaxRemoteProcessCount(uba::SessionServer* server, uba::u32 count);
	UBA_API void SessionServer_DisableRemoteExecution(uba::SessionServer* server);
	UBA_API void SessionServer_PrintSummary(uba::SessionServer* server);
	UBA_API void SessionServer_CancelAll(uba::SessionServer* server);
	UBA_API void SessionServer_SetCustomCasKeyFromTrackedInputs(uba::SessionServer* server, uba::ProcessHandle* handle, const uba::tchar* fileName, const uba::tchar* workingDir);
	UBA_API uba::u32 SessionServer_BeginExternalProcess(uba::SessionServer* server, const uba::tchar* description);
	UBA_API void SessionServer_EndExternalProcess(uba::SessionServer* server, uba::u32 id, uba::u32 exitCode);
	UBA_API void SessionServer_UpdateProgress(uba::SessionServer* server, uba::u32 processesTotal, uba::u32 processesDone, uba::u32 errorCount);
	UBA_API void SessionServer_UpdateStatus(uba::SessionServer* server, uba::u32 statusRow, uba::u32 statusColumn, const uba::tchar* statusText, uba::LogEntryType statusType, const uba::tchar* statusLink);
	UBA_API void SessionServer_RegisterCustomService(uba::SessionServer* server, SessionServer_CustomServiceFunction* function, void* userData = nullptr);
	UBA_API void SessionServer_Destroy(uba::SessionServer* server);

	// Scheduler
	UBA_API uba::Scheduler* Scheduler_Create(uba::SessionServer* session, uba::u32 maxLocalProcessors = ~0u, bool enableProcessReuse = false);
	UBA_API void Scheduler_Start(uba::Scheduler* scheduler);
	UBA_API uba::u32 Scheduler_EnqueueProcess(uba::Scheduler* scheduler, const uba::ProcessStartInfo& info, float weight = 1.0f, const void* knownInputs = nullptr, uba::u32 knownInputsBytes = 0, uba::u32 knownInputsCount = 0);
	UBA_API void Scheduler_SetMaxLocalProcessors(uba::Scheduler* scheduler, uba::u32 maxLocalProcessors);
	UBA_API void Scheduler_Stop(uba::Scheduler* scheduler);
	UBA_API void Scheduler_Destroy(uba::Scheduler* scheduler);
	UBA_API void Scheduler_GetStats(uba::Scheduler* scheduler, uba::u32& outQueued, uba::u32& outActiveLocal, uba::u32& outActiveRemote, uba::u32& outFinished);

	// Cache
	UBA_API uba::CacheClient* CacheClient_Create(uba::SessionServer* session, bool reportMissReason = false, const uba::tchar* crypto = nullptr);
	UBA_API bool CacheClient_Connect(uba::CacheClient* cacheClient, const uba::tchar* host, int port);
	UBA_API bool CacheClient_WriteToCache(uba::CacheClient* cacheClient, uba::RootPaths* rootPaths, uba::u32 bucket, const uba::ProcessHandle* process, const uba::u8* inputs, uba::u32 inputsSize, const uba::u8* outputs, uba::u32 outputsSize);
	UBA_API bool CacheClient_WriteToCache2(uba::CacheClient* cacheClient, uba::RootPaths* rootPaths, uba::u32 bucket, const uba::ProcessHandle* process, const uba::u8* inputs, uba::u32 inputsSize, const uba::u8* outputs, uba::u32 outputsSize, const uba::u8* logLines, uba::u32 logLinesSize);
	UBA_API uba::u32 CacheClient_FetchFromCache(uba::CacheClient* cacheClient, uba::RootPaths* rootPaths, uba::u32 bucket, const uba::ProcessStartInfo& info);
	UBA_API uba::CacheResult* CacheClient_FetchFromCache2(uba::CacheClient* cacheClient, uba::RootPaths* rootPaths, uba::u32 bucket, const uba::ProcessStartInfo& info);
	UBA_API void CacheClient_RequestServerShutdown(uba::CacheClient* cacheClient, const uba::tchar* reason);
	UBA_API void CacheClient_Destroy(uba::CacheClient* cacheClient);
	UBA_API const uba::tchar* CacheResult_GetLogLine(uba::CacheResult* result, uba::u32 index);
	UBA_API uba::u32 CacheResult_GetLogLineType(uba::CacheResult* result, uba::u32 index);
	UBA_API void CacheResult_Delete(uba::CacheResult* result);

	// Misc
	using Uba_CustomAssertHandler = void(const uba::tchar* text);
	UBA_API void Uba_SetCustomAssertHandler(Uba_CustomAssertHandler* handler);

	using ImportFunc = void(const uba::tchar* importName, void* userData);
	UBA_API void Uba_FindImports(const uba::tchar* binary, ImportFunc* func, void* userData);

	// High level interface using config file instead. Uses scheduler under the hood
	UBA_API void* Uba_Create(const uba::tchar* configFile);
	UBA_API uba::u32 Uba_RunProcess(void* uba, const uba::tchar* app, const uba::tchar* args, const uba::tchar* workDir, const uba::tchar* desc, void* userData, ProcessHandle_ExitCallback* exit);
	UBA_API void Uba_RegisterNewFile(void* uba, const uba::tchar* file);
	UBA_API void Uba_Destroy(void* uba);


	// DEPRECATED
	UBA_API void DestroyProcessHandle(uba::ProcessHandle* handle);
}
