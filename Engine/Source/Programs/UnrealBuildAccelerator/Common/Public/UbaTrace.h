// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UbaFileMapping.h"
#include "UbaLogger.h"
#include "UbaProcessStats.h"
#include "UbaWorkManager.h"

namespace uba
{
	struct ProcessLogLine;
	struct ProcessStats;

	class TraceChannel
	{
	public:
		TraceChannel(Logger& logger);
		~TraceChannel();

		bool Init(const tchar* channelName = TC("Default"));
		bool Write(const tchar* traceName, const tchar* ifMatching = nullptr);
		bool Read(StringBufferBase& outTraceName);

		Logger& m_logger;
		MutexHandle m_mutex = InvalidMutexHandle;
		FileMappingHandle m_memHandle;
		void* m_mem = nullptr;
	};

	#define UBA_TRACE_TYPES \
		UBA_TRACE_TYPE(SessionAdded) \
		UBA_TRACE_TYPE(SessionUpdate) \
		UBA_TRACE_TYPE(ProcessAdded) \
		UBA_TRACE_TYPE(ProcessExited) \
		UBA_TRACE_TYPE(ProcessReturned) \
		UBA_TRACE_TYPE(FileBeginFetch) \
		UBA_TRACE_TYPE(FileEndFetch) \
		UBA_TRACE_TYPE(FileBeginStore) \
		UBA_TRACE_TYPE(FileEndStore) \
		UBA_TRACE_TYPE(Summary) \
		UBA_TRACE_TYPE(BeginWork) \
		UBA_TRACE_TYPE(EndWork) \
		UBA_TRACE_TYPE(String) \
		UBA_TRACE_TYPE(SessionSummary) \
		UBA_TRACE_TYPE(ProcessEnvironmentUpdated) \
		UBA_TRACE_TYPE(SessionDisconnect) \
		UBA_TRACE_TYPE(ProxyCreated) \
		UBA_TRACE_TYPE(ProxyUsed) \
		UBA_TRACE_TYPE(FileFetchLight) \
		UBA_TRACE_TYPE(FileStoreLight) \
		UBA_TRACE_TYPE(StatusUpdate) \
		UBA_TRACE_TYPE(SessionNotification) \
		UBA_TRACE_TYPE(CacheBeginFetch) \
		UBA_TRACE_TYPE(CacheEndFetch) \
		UBA_TRACE_TYPE(CacheBeginWrite) \
		UBA_TRACE_TYPE(CacheEndWrite) \
		UBA_TRACE_TYPE(ProgressUpdate) \
		UBA_TRACE_TYPE(RemoteExecutionDisabled) \

	enum TraceType : u8
	{
		#define UBA_TRACE_TYPE(name) TraceType_##name,
		UBA_TRACE_TYPES
		#undef UBA_TRACE_TYPE
	};

	using Color = u32;
	inline Color toColor(u8 r, u8 g, u8 b) { return (r << 16) + (g << 8) + b; }

	static constexpr u32 TraceVersion = 34;
	static constexpr u32 TraceReadCompatibilityVersion = 6;

	class Trace : public WorkTracker
	{
	public:
		Trace(LogWriter& logWriter);
		~Trace();

		bool IsWriting() const { return m_memoryBegin != nullptr; }

		bool StartWrite(const tchar* namedTrace, u64 traceMemCapacity = 64*1024*1024);
		void SessionAdded(u32 sessionId, u32 clientId, const tchar* name, const tchar* info);
		void SessionUpdate(u32 sessionId, u32 connectionCount, u64 send, u64 recv, u64 lastPing, u64 memAvail, u64 memTotal, float cpuLoad);
		void SessionNotification(u32 sessionId, const tchar* text);
		void SessionSummary(u32 sessionId, const u8* data, u64 dataSize);
		void SessionDisconnect(u32 sessionId);
		void ProcessAdded(u32 sessionId, u32 processId, const tchar* description);
		void ProcessEnvironmentUpdated(u32 processId, const tchar* reason, const u8* data, u64 dataSize);
		void ProcessExited(u32 processId, u32 exitCode, const u8* data, u64 dataSize, const Vector<ProcessLogLine>& logLines, const tchar* breadcrumbs);
		void ProcessReturned(u32 processId, const StringView& reason);
		void ProxyCreated(u32 clientId, const tchar* proxyName);
		void ProxyUsed(u32 clientId, const tchar* proxyName);
		void FileBeginFetch(u32 clientId, const CasKey& key, u64 size, const tchar* hint, bool detailed);
		void FileEndFetch(u32 clientId, const CasKey& key);
		void FileBeginStore(u32 clientId, const CasKey& key, u64 size, const tchar* hint, bool detailed);
		void FileEndStore(u32 clientId, const CasKey& key);
		void BeginWork(u32 workIndex, const tchar* desc);
		void EndWork(u32 workIndex);
		void ProgressUpdate(u32 processesTotal, u32 processesDone, u32 errorCount);
		void StatusUpdate(u32 statusRow, u32 statusColumn, const tchar* statusText, LogEntryType statusType, const tchar* statusLink);
		void RemoteExecutionDisabled();

		void CacheBeginFetch(u32 fetchId, const tchar* description);
		void CacheEndFetch(u32 fetchId, bool success, const u8* data, u64 dataSize);
		void CacheBeginWrite(u32 processId);
		void CacheEndWrite(u32 processId, bool success, u64 bytesSent);

		bool StopWrite(const tchar* writeFileName);

		virtual u32 TrackWorkStart(const tchar* desc) final override;
		virtual void TrackWorkEnd(u32 id) final override;

	private:
		struct WriterScope;
		void FreeMemory();
		bool EnsureMemory(u64 size);
		u32 AddString(const tchar* string);

		LoggerWithWriter m_logger;
		TString m_namedTrace;
		TraceChannel m_channel;
		ReaderWriterLock m_memoryLock;
		FileMappingHandle m_memoryHandle;
		u8* m_memoryBegin = nullptr;
		u64 m_memoryPos = 0;
		u64 m_memoryCommitted = 0;
		u64 m_memoryCapacity = 0;
		u64 m_startTime = ~u64(0);

		ReaderWriterLock m_stringsLock;
		UnorderedMap<StringKey, u32> m_strings;

		Atomic<u32> m_workCounter;

		friend class SessionServer;
	};

	struct OwnerInfo
	{
		const tchar* id;
		u32 pid;
	};

	const OwnerInfo& GetOwnerInfo();
}