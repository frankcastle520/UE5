// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UbaProcessStartInfoHolder.h"
#include "UbaProtocol.h"
#include "UbaSession.h"
#include "UbaStats.h"

namespace uba
{
	class Process
	{
	public:
		virtual const ProcessStartInfo& GetStartInfo() const = 0;
		virtual u32 GetId() { UBA_ASSERT(false); return 0; }
		virtual u32 GetExitCode() { UBA_ASSERT(false); return ~0u; }
		virtual bool HasExited()  { UBA_ASSERT(false); return false; }
		virtual bool WaitForExit(u32 millisecondsTimeout) { UBA_ASSERT(false); return false; }
		virtual u64 GetTotalProcessorTime() const { UBA_ASSERT(false); return 0; }
		virtual u64 GetTotalWallTime() const { UBA_ASSERT(false); return 0; }
		virtual const Vector<ProcessLogLine>& GetLogLines() const = 0;
		virtual const Vector<u8>& GetTrackedInputs() const = 0;
		virtual const Vector<u8>& GetTrackedOutputs() const = 0;
		virtual void Cancel(bool terminate) { UBA_ASSERT(false); }
		virtual const tchar* GetExecutingHost() const { UBA_ASSERT(false); return nullptr; }
		virtual bool IsRemote() const { UBA_ASSERT(false); return false; }
		virtual ProcessExecutionType GetExecutionType() const { UBA_ASSERT(false); return ProcessExecutionType_Native; }
		virtual bool IsChild() { UBA_ASSERT(false); return false; }

	protected:
		void AddRef();
		void Release();
		virtual ~Process() {}
		Atomic<u32> m_refCount;
		friend ProcessHandle;
	};

	class ProcessImpl : public Process
	{
	public:

		virtual const ProcessStartInfo& GetStartInfo() const override { return m_startInfo; }
		virtual u32 GetId() override { return m_id; }
		virtual u32 GetExitCode() override { return m_exitCode; }
		virtual bool HasExited() override  { return m_hasExited; }
		virtual bool WaitForExit(u32 millisecondsTimeout) override;
		virtual u64 GetTotalProcessorTime() const override;
		virtual u64 GetTotalWallTime() const override;
		virtual const Vector<ProcessLogLine>& GetLogLines() const override { return m_logLines; }
		virtual const Vector<u8>& GetTrackedInputs() const override { return m_trackedInputs; }
		virtual const Vector<u8>& GetTrackedOutputs() const override { return m_trackedOutputs; }
		virtual void Cancel(bool terminate) override;
		virtual const tchar* GetExecutingHost() const override { return TC(""); }
		virtual bool IsRemote() const override { return false; }
		virtual ProcessExecutionType GetExecutionType() const override { return m_detourEnabled ? ProcessExecutionType_Detoured : ProcessExecutionType_Native; }
		virtual bool IsChild() override { return m_parentProcess != nullptr; }

		ProcessImpl(Session& session, u32 id, ProcessImpl* parent);
		~ProcessImpl();

		struct PipeReader;

		bool Start(const ProcessStartInfo& startInfo, bool runningRemote, void* environment, bool async, bool enableDetour);

		bool IsActive();
		bool IsCancelled();
		bool HasFailedMessage();

		bool WaitForRead(PipeReader& outReader, PipeReader& errReader);
		void SetWritten();

		void ThreadRun(bool runningRemote, void* environment);
		void ThreadExit();

		bool HandleMessage(BinaryReader& reader, BinaryWriter& writer);
		bool HandleSpecialApplication();

		#define UBA_PROCESS_MESSAGE(M) bool Handle##M(BinaryReader& reader, BinaryWriter& writer);
		UBA_PROCESS_MESSAGES
		#undef UBA_PROCESS_MESSAGE

		void LogLine(bool printInSession, TString&& line, LogEntryType logType);
		bool CreateTempFile(BinaryReader& reader, ProcHandle nativeProcessHandle, const tchar* application);
		bool OpenTempFile(BinaryReader& reader, BinaryWriter& writer, const tchar* application);
		bool WriteFilesToDisk();

		const tchar* InternalGetChildLogFile(StringBufferBase& temp);
		
		#if !PLATFORM_WINDOWS
		bool PollStdPipes(PipeReader& outReader, PipeReader& errReader, int timeoutMs = -1);
		#endif

		u32 InternalCreateProcess(bool runningRemote, void* environment, FileMappingHandle communicationHandle, u64 communicationOffset);
		u32	InternalExitProcess(bool cancel);
		void ClearTempFiles();
		void WaitForParent();

		ProcessStartInfoHolder m_startInfo;
		Session& m_session;
		ProcessImpl* m_parentProcess;
		ReaderWriterLock m_initLock;
		u32 m_id;
		FileMappingAllocator::Allocation m_comMemory;

	#if PLATFORM_WINDOWS
		Event m_cancelEvent;
		Event m_writeEvent;
		Event m_readEvent;
	#else
		ReaderWriterLock m_comMemoryLock;
		Atomic<bool> m_cancelled;
		Event& m_cancelEvent;
		Event& m_writeEvent;
		Event& m_readEvent;

		int m_stdOutPipe = -1;
		int m_stdErrPipe = -1;
		bool m_doOneExtraCheckForExitMessage = true;
	#endif

		ProcHandle m_nativeProcessHandle = InvalidProcHandle;

		#if PLATFORM_WINDOWS
		HANDLE m_nativeThreadHandle = 0;
		HANDLE m_accountingJobObject = NULL;
		#endif

		u32 m_nativeProcessId = 0;
		u32 m_nativeProcessExitCode = ~0u;
		u32 m_exitCode = ~0u;
		u32 m_messageCount = 0;
		Atomic<bool> m_hasExited;
		bool m_messageSuccess = true;
		bool m_echoOn = true;
		bool m_gotExitMessage = false;
		bool m_parentReportedExit = false;
		bool m_detourEnabled = true;
		TString m_realApplication;
		const tchar* m_realWorkingDir = nullptr;
		u64 m_startTime = 0;
		Event m_waitForParent;
		ReaderWriterLock m_logLinesLock;
		Vector<ProcessLogLine> m_logLines;
		Vector<u8> m_trackedInputs;
		Vector<u8> m_trackedOutputs;
		Vector<ProcessHandle> m_childProcesses;
		ReaderWriterLock& m_writtenFilesLock;
		UnorderedMap<TString, WrittenFile>& m_writtenFiles;
		ReaderWriterLock& m_tempFilesLock;
		UnorderedMap<StringKey, WrittenFile>& m_tempFiles;
		Atomic<u32> m_tempFilesModified;
		SessionStats m_sessionStats;
		StorageStats m_storageStats;
		ProcessStats m_processStats;
		KernelStats m_kernelStats;

		Thread m_messageThread;

		bool m_extractExports = false;

		ProcessImpl(const ProcessImpl&) = delete;
		void operator=(const ProcessImpl&) = delete;
	};
}
