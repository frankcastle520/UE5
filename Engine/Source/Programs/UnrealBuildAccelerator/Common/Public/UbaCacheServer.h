// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UbaHash.h"
#include "UbaLogger.h"
#include "UbaStringBuffer.h"

namespace uba
{
	class NetworkServer;
	class StorageServer;
	struct BinaryReader;
	struct BinaryWriter;
	struct CacheEntry;
	struct CacheEntries;
	struct ConnectionInfo;

	struct CacheServerCreateInfo
	{
		CacheServerCreateInfo(StorageServer& s, const tchar* rd, LogWriter& w = g_consoleLogWriter) : storage(s), rootDir(rd), logWriter(w) {}

		// Storage server
		StorageServer& storage;

		// Root dir
		const tchar* rootDir = nullptr;

		// Log writer
		LogWriter& logWriter;

		// Will check cache entry inputs of they depend on cas files that have been deleted
		bool checkInputsForDeletedCas = true;

		// The time cache entries will stay around after they were last used in hours (defaults to two days)
		// Set to zero to never expire
		u64 expirationTimeSeconds = 2*24*60*60;

		// The amount of reserved memory used per core when doing maintenance
		u64 maintenanceReserveSize = 128ull * 1024 * 1024;

		// Max size of cas bucket. When within 2mb it will start decreasing expiry time by one hour
		u64 bucketCasTableMaxSize = 32ull * 1024 * 1024;
	};

	class CacheServer
	{
	public:
		CacheServer(const CacheServerCreateInfo& info);
		~CacheServer();

		bool Load();
		bool Save();

		bool RunMaintenance(bool force, const Function<bool()>& shouldExit);

		bool ShouldShutdown();

	private:
		struct Bucket;
		struct LoadStats;

		bool LoadBucket(Bucket& bucket, BinaryReader& reader, u32 databaseVersion, LoadStats& outStats);
		bool SaveBucket(u64 bucketId, Bucket& bucket);
		bool SaveNoLock();
		void OnDisconnected(u32 clientId);

		struct Connection;
		struct ConnectionBucket;

		ConnectionBucket& GetConnectionBucket(const ConnectionInfo& connectionInfo, BinaryReader& reader, u32* outClientVersion = nullptr);
		Bucket& GetBucket(BinaryReader& reader);
		Bucket& GetBucket(u64 id);
		u32 GetBucketWorkerCount();

		bool HandleMessage(const ConnectionInfo& connectionInfo, u8 messageType, BinaryReader& reader, BinaryWriter& writer);
		bool HandleStoreEntry(ConnectionBucket& bucket, BinaryReader& reader, BinaryWriter& writer, u32 clientVersion);
		bool HandleFetchPathTable(BinaryReader& reader, BinaryWriter& writer);
		bool HandleFetchCasTable(BinaryReader& reader, BinaryWriter& writer);
		bool HandleFetchEntries(BinaryReader& reader, BinaryWriter& writer, u32 clientVersion);
		bool HandleReportUsedEntry(BinaryReader& reader, BinaryWriter& writer, u32 clientVersion);
		bool HandleExecuteCommand(BinaryReader& reader, BinaryWriter& writer);

		MutableLogger m_logger;
		NetworkServer& m_server;
		StorageServer& m_storage;

		StringBuffer<MaxPath> m_rootDir;

		Atomic<u32> m_addsSinceMaintenance;
		Atomic<u64> m_cacheKeyFetchCount;
		Atomic<u64> m_cacheKeyHitCount;
		Atomic<bool> m_isRunningMaintenance;

		ReaderWriterLock m_bucketsLock;
		Map<u64, Bucket> m_buckets;

		ReaderWriterLock m_connectionsLock;
		Map<u32, Connection> m_connections;

		Atomic<bool> m_shutdownRequested = false;

		u64 m_maintenanceReserveSize = 0;
		u64 m_bucketCasTableMaxSize = 0;
		u64 m_creationTime = 0;
		u64 m_bootTime = 0;
		u64 m_lastMaintenance = 0;
		u64 m_longestMaintenance = 0;
		u64 m_expirationTimeSeconds = 0;
		bool m_dbfileDirty = false;

		bool m_checkInputsForDeletedCas = true;

		bool m_shouldWipe = false;
		bool m_forceAllSteps = false;

		CacheServer(const CacheServer&) = delete;
		CacheServer& operator=(const CacheServer&) = delete;
	};
}