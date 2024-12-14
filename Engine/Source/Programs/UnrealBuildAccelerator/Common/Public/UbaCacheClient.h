// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UbaHash.h"
#include "UbaLogger.h"
#include "UbaProcessHandle.h"

namespace uba
{
	class CompactCasKeyTable;
	class CompactPathTable;
	class Config;
	class NetworkClient;
	class RootPaths;
	class Session;
	class StorageImpl;
	struct CacheStats;
	struct CasKey;
	struct ProcessStartInfo;
	struct StringView;

	struct CacheClientCreateInfo
	{
		CacheClientCreateInfo(LogWriter& w, StorageImpl& st, NetworkClient& c, Session& se) : writer(w), storage(st), client(c), session(se) {}
		LogWriter& writer;
		StorageImpl& storage;
		NetworkClient& client;
		Session& session;

		void Apply(Config& config);

		bool reportMissReason = false; // Report the reason no matching cache entry was found
		bool useDirectoryPreparsing = true; // This is used to minimize syscalls. GetFileAttributes can be very expensive on cloud machines and we can enable this to minimize syscall count
		bool validateCacheWritesInput = false; // Set to true to validate cas of all input files before sent to cache
		bool validateCacheWritesOutput = false; // Set to true to validate cas of all output files before sent to cache
		bool useRoots = true; // Set this to false to allow paths that are not under roots and to not fix them up
		bool useCacheHit = true; // Set this to false to ignore found cache hits.. this is for debugging/testing only
	};

	struct CacheResult
	{
		bool hit = false;
		Vector<ProcessLogLine> logLines;
	};

	class CacheClient
	{
	public:
		CacheClient(const CacheClientCreateInfo& info);
		~CacheClient();

		bool WriteToCache(const RootPaths& rootPaths, u32 bucketId, const ProcessStartInfo& info, const u8* inputs, u64 inputsSize, const u8* outputs, u64 outputsSize, const u8* logLines, u64 logLinesSize, u32 processId = 0);
		bool FetchFromCache(CacheResult& outResult, const RootPaths& rootPaths, u32 bucketId, const ProcessStartInfo& info);
		bool RequestServerShutdown(const tchar* reason);

		bool ExecuteCommand(Logger& logger, const tchar* command, const tchar* destinationFile = nullptr, const tchar* additionalInfo = nullptr);

		inline MutableLogger& GetLogger() { return m_logger; }
		inline NetworkClient& GetClient() { return m_client; }
		inline StorageImpl& GetStorage() { return m_storage; }

	private:
		struct Bucket;
		u64 MakeId(u32 bucketId);

		bool SendPathTable(Bucket& bucket, u32 requiredPathTableSize);
		bool SendCasTable(Bucket& bucket, u32 requiredCasTableSize);
		bool SendCacheEntry(Bucket& bucket, const RootPaths& rootPaths, const CasKey& cmdKey, const Map<u32, u32>& inputsStringToCasKey, const Map<u32, u32>& outputsStringToCasKey, const u8* logLines, u64 logLinesSize, u64& outBytesSent);
		bool FetchCasTable(Bucket& bucket, CacheStats& stats, u32 requiredCasTableOffset);
		bool ReportUsedEntry(Vector<ProcessLogLine>& outLogLines, bool ownedLogLines, Bucket& bucket, const CasKey& cmdKey, u32 entryId);
		bool PopulateLogLines(Vector<ProcessLogLine>& outLogLines, const u8* mem, u64 memLen);

		CasKey GetCmdKey(const RootPaths& rootPaths, const ProcessStartInfo& info);
		bool ShouldNormalize(const StringBufferBase& path);

		bool GetLocalPathAndCasKey(Bucket& bucket, const RootPaths& rootPaths, StringBufferBase& outPath, CasKey& outKey, CompactCasKeyTable& casKeyTable, CompactPathTable& pathTable, u32 offset);
		bool IsFileCompressed(const ProcessStartInfo& info, const StringView& filename);
		void PreparseDirectory(const StringKey& fileNameKey, const StringBufferBase& filePath);

		MutableLogger m_logger;
		StorageImpl& m_storage;
		NetworkClient& m_client;
		Session& m_session;
		bool m_reportMissReason;
		bool m_useDirectoryPreParsing;
		bool m_validateCacheWritesInput;
		bool m_validateCacheWritesOutput;
		bool m_useRoots;
		bool m_useCacheHit;

		Atomic<bool> m_connected;

		ReaderWriterLock m_bucketsLock;
		UnorderedMap<u32, Bucket> m_buckets;

		ReaderWriterLock m_sendOneAtTheTimeLock;

		ReaderWriterLock m_directoryPreparserLock;
		struct PreparedDir { ReaderWriterLock lock; bool done = false; };
		UnorderedMap<StringKey, PreparedDir> m_directoryPreparser;

		CacheClient(const CacheClient&) = delete;
		CacheClient& operator=(const CacheClient&) = delete;
	};
}