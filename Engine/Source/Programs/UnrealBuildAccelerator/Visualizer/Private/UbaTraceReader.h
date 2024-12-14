// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UbaProcessHandle.h"
#include "UbaStats.h"
#include "UbaTrace.h"

namespace uba
{
	class NetworkClient;

	class TraceView
	{
	public:
		struct Process
		{
			u32 id = 0;
			u32 exitCode = ~0u;
			u64 start = 0;
			u64 stop = 0;
			TString description;
			TString returnedReason;
			TString breadcrumbs;
			HBITMAP bitmap = 0;
			u32 bitmapOffset = 0;
			bool bitmapDirty = true;
			bool cacheFetch = false;
			bool isRemote = false;
			u64 createFilesTime = 0;
			u64 writeFilesTime = 0;
			Vector<u8> stats;
			Vector<ProcessLogLine> logLines;
		};

		struct Processor
		{
			Vector<Process> processes;
		};

		struct SessionUpdate
		{
			u64 time;
			u64 send;
			u64 recv;
			u64 ping;
			u64 memAvail;
			float cpuLoad;
			u8 connectionCount;
		};

		struct WorkRecord
		{
			const tchar* description = nullptr;
			u64 start = 0;
			u64 stop = 0;
			HBITMAP bitmap = 0;
			u32 bitmapOffset = 0;
		};

		struct WorkTrack
		{
			Vector<WorkRecord> records;
		};


		struct FileTransfer
		{
			CasKey key;
			u64 size;
			TString hint;
			u64 start;
			u64 stop;
		};

		struct StatusUpdate
		{
			TString text;
			LogEntryType type;
			TString link;
		};

		struct Session
		{
			TString name;
			TString fullName;
			Guid clientUid;
			Vector<Processor> processors;
			Vector<SessionUpdate> updates;
			Vector<TString> summary;
			Vector<FileTransfer> fetchedFiles;
			Vector<FileTransfer> storedFiles;
			TString notification;
			u64 fetchedFilesBytes = 0;
			u64 storedFilesBytes = 0;
			u32 maxVisibleFiles = 0;

			float highestSendPerS = 0;
			float highestRecvPerS = 0;

			bool isReset = true;
			u64 disconnectTime = ~u64(0);
			u64 prevUpdateTime = 0;
			u64 prevSend = 0;
			u64 prevRecv = 0;
			u64 memTotal = 0;
			u32 processActiveCount = 0;
			u32 processExitedCount = 0;

			TString proxyName;
			bool proxyCreated = false;
		};

		struct ProcessLocation
		{
			u32 sessionIndex = 0;
			u32 processorIndex = 0;
			u32 processIndex = 0;
			bool operator==(const ProcessLocation& o) const { return sessionIndex == o.sessionIndex && processorIndex == o.processorIndex && processIndex == o.processIndex; }
		};

		struct CacheWrite
		{
			u64 start = 0;
			u64 end = 0;
			u64 bytesSent = 0;
			bool success = false;
		};

		const Process& GetProcess(const ProcessLocation& loc);
		const Session& GetSession(const ProcessLocation& loc);
		void Clear();

		Vector<Session> sessions;
		Vector<WorkTrack> workTracks;
		Vector<TString> strings;
		Map<u64, StatusUpdate> statusMap;
		Map<u32, CacheWrite> cacheWrites;
		u64 realStartTime = 0;
		u64 startTime = 0;
		u64 frequency = 0;
		u32 totalProcessActiveCount = 0;
		u32 totalProcessExitedCount = 0;
		u32 activeSessionCount = 0;
		u32 version = 0;
		u32 progressProcessesTotal = 0;
		u32 progressProcessesDone = 0;
		u32 progressErrorCount = 0;
		bool remoteExecutionDisabled = false;
		bool finished = true;
	};


	class TraceReader
	{
	public:
		TraceReader(Logger& logger);
		~TraceReader();

		#if PLATFORM_WINDOWS

		// Use for file read
		bool ReadFile(TraceView& out, const tchar* fileName, bool replay);
		bool UpdateReadFile(TraceView& out, u64 maxTime, bool& outChanged);

		// Use for network
		bool StartReadClient(TraceView& out, NetworkClient& client);
		bool UpdateReadClient(TraceView& out, NetworkClient& client, bool& outChanged);

		// Use for local
		bool StartReadNamed(TraceView& out, const tchar* namedTrace, bool silentFail = false, bool replay = false);
		bool UpdateReadNamed(TraceView& out, u64 maxTime, bool& outChanged);

		bool ReadMemory(TraceView& out, bool trackHost, u64 maxTime);
		bool ReadTrace(TraceView& out, BinaryReader& reader, u64 maxTime);
		void StopAllActive(TraceView& out, u64 stopTime);
		void Reset(TraceView& out);
		void Unmap();

		bool SaveAs(const tchar* fileName);

		Guid ReadClientId(TraceView& out, BinaryReader& reader);
		TraceView::Session& GetSession(TraceView& out, u32 sessionIndex);
		TraceView::Session* GetSession(TraceView& out, const Guid& clientUid);

		TraceView::Process* ProcessBegin(TraceView& out, u32 sessionIndex, u32 id, u64 time, const tchar* description);
		TraceView::Process* ProcessEnd(TraceView& out, u32& outSessionIndex, u32 id, u64 time);

		UnorderedMap<u32, TraceView::ProcessLocation> m_activeProcesses;

		struct WorkRecordLocation { u32 track; u32 index; };
		UnorderedMap<u32, WorkRecordLocation> m_activeWorkRecords;

		Vector<u32> m_sessionIndexToSession;

		#endif

		Logger& m_logger;
		TraceChannel m_channel;
		ReaderWriterLock m_memoryLock;
		FileMappingHandle m_memoryHandle;
		TString m_namedTrace;
		u8* m_memoryBegin = nullptr;
		u8* m_memoryPos = nullptr;
		u8* m_memoryEnd = nullptr;
		HANDLE m_hostProcess = NULL;
	};
}