// Copyright Epic Games, Inc. All Rights Reserved.

#include "UbaCacheServer.h"
#include "UbaCacheEntry.h"
#include "UbaCompactTables.h"
#include "UbaBinaryReaderWriter.h"
#include "UbaDirectoryIterator.h"
#include "UbaFileAccessor.h"
#include "UbaHashMap.h"
#include "UbaNetworkServer.h"
#include "UbaStorageServer.h"
//#include <oodle2.h>

// TODO
// - Fix so expiration time is set to oldest if overflowing and decreasing time didn't cause any deletes. That way we can make sure next maintenance will delete entries
// - Sort buckets by last maintenance time to make sure the long ones always get a slot first
// - Change so save happens when bucket is done in same work to minimize latency for the long ones


#if PLATFORM_WINDOWS
#define UBA_FORCEINLINE __forceinline
#else
#define UBA_FORCEINLINE inline __attribute__ ((always_inline))
#endif

namespace uba
{
	static constexpr u32 CacheFileVersion = 6;
	static constexpr u32 CacheFileCompatibilityVersion = 3;

	bool IsCaseInsensitive(u64 id) { return (id & (1ull << 32)) == 0; }

	struct BitArray
	{
		void Init(MemoryBlock& memoryBlock, u32 bitCount)
		{
			u32 bytes = AlignUp((bitCount+7) / 8, 8u); // Align up to 64 bits
			data = (u64*)memoryBlock.Allocate(bytes, 8, TC(""));
			memset(data, 0, bytes);
			count = bytes / 8;
		}

		UBA_FORCEINLINE void Set(u32 bitIndex)
		{
			u32 index = bitIndex / 64;
			UBA_ASSERTF(index < count, TC("Out of bounds (%u/%u). Bit index : %u"), index, count, bitIndex);
			u32 bitOffset = bitIndex - index * 64;
			data[index] |= 1ull << bitOffset;
		}

		UBA_FORCEINLINE bool IsSet(u32 bitIndex)
		{
			u32 index = bitIndex / 64;
			UBA_ASSERTF(index < count, TC("Out of bounds (%u/%u). Bit index : %u"), index, count, bitIndex);
			u32 bitOffset = bitIndex - index * 64;
			return (data[index] & (1ull << bitOffset)) != 0;
		}

		UBA_FORCEINLINE u32 CountSetBits()
		{
			u64 bits = 0;
			for (u64 i=0,e=count; i!=e; ++i)
				bits += CountBits(data[i]);
			return u32(bits);
		}

		template<typename Func>
		void Traverse(const Func& func)
		{
			u32 index = 0;
			for (u64 i=0,e=count; i!=e; ++i)
			{
				u64 v = data[i];
				while (v)
				{
					u64 bitIndex = FindFirstBit(v);
					func(index + u32(bitIndex));
					v &= ~(1ull << bitIndex);
				}
				index += 64;
			}
		}

		static constexpr UBA_FORCEINLINE u64 CountBits(u64 bits)
		{
			// https://en.wikipedia.org/wiki/Hamming_weight
			bits -= (bits >> 1) & 0x5555555555555555ull;
			bits = (bits & 0x3333333333333333ull) + ((bits >> 2) & 0x3333333333333333ull);
			bits = (bits + (bits >> 4)) & 0x0f0f0f0f0f0f0f0full;
			return (bits * 0x0101010101010101) >> 56;
		}

		static UBA_FORCEINLINE u64 FindFirstBit(u64 v)
		{
			#if PLATFORM_WINDOWS && (defined(_M_X64) || defined(_M_IX86))
			// Use TZCNT intrinsic on Windows x86/x64
			return _tzcnt_u64(v);
			#elif PLATFORM_WINDOWS && defined(_M_ARM64)
			// Use the ARM64 equivalent
			return _CountTrailingZeros64(v);
			#elif PLATFORM_LINUX && (defined(__x86_64__) || defined(__i386__))
			// Use GCC's built-in TZCNT equivalent for x86/x64
			return __builtin_ia32_tzcnt_u64(v);
			#elif PLATFORM_LINUX && defined(__aarch64__)
			// Use the ARM64 equivalent
			return __builtin_ctzll(v);
			#else
			u64 pos = 0;
			if (v >= 1ull<<32) { v >>= 32; pos += 32; }
			if (v >= 1ull<<16) { v >>= 16; pos += 16; }
			if (v >= 1ull<< 8) { v >>=  8; pos +=  8; }
			if (v >= 1ull<< 4) { v >>=  4; pos +=  4; }
			if (v >= 1ull<< 2) { v >>=  2; pos +=  2; }
			if (v >= 1ull<< 1) {           pos +=  1; }
			return pos;
			#endif
		}

		u64* data = nullptr;
		u32 count = 0;
	};

	struct CacheServer::ConnectionBucket
	{
		ConnectionBucket(u64 i) : pathTable(CachePathTableMaxSize, CompactPathTable::V1, IsCaseInsensitive(i)), casKeyTable(CacheCasKeyTableMaxSize), id(i) {}
		CompactPathTable pathTable;
		CompactCasKeyTable casKeyTable;

		ReaderWriterLock cacheEntryLookupLock;
		UnorderedMap<CasKey, CacheEntry> cacheEntryLookup;
		u64 id;
	};

	struct CacheServer::Connection
	{
		u32 clientVersion;
		UnorderedMap<u64, ConnectionBucket> buckets;
	};

	struct CacheServer::Bucket
	{
		Bucket(u64 id) : m_pathTable(CachePathTableMaxSize, CompactPathTable::V1, IsCaseInsensitive(id)), m_casKeyTable(CacheCasKeyTableMaxSize) {}
		ReaderWriterLock m_cacheEntryLookupLock;
		UnorderedMap<CasKey, CacheEntries> m_cacheEntryLookup;

		CompactPathTable m_pathTable;
		CompactCasKeyTable m_casKeyTable;

		Atomic<u64> totalEntryCount;
		Atomic<u64> totalEntrySize;
		Atomic<bool> hasDeletedEntries;
		Atomic<bool> needsSave;

		Atomic<u64> lastSavedTime;
		Atomic<u64> lastUsedTime;

		u64 expirationTimeSeconds = 0;
		u32 index = ~0u;

		struct MaintenanceContext
		{
			MemoryBlock memoryBlock;
			BitArray deletedOffsets;
			bool isInitialized = false;
			bool shouldTest = false;
		}* m_maintenanceContext = nullptr;
	};

	const tchar* ToString(CacheMessageType type)
	{
		switch (type)
		{
			#define UBA_CACHE_MESSAGE(x) case CacheMessageType_##x: return TC("")#x;
			UBA_CACHE_MESSAGES
			#undef UBA_CACHE_MESSAGE
		default:
			return TC("Unknown"); // Should never happen
		}
	}

	CacheServer::CacheServer(const CacheServerCreateInfo& info)
	:	m_logger(info.logWriter, TC("UbaCacheServer"))
	,	m_server(info.storage.GetServer())
	,	m_storage(info.storage)
	{
		m_checkInputsForDeletedCas = info.checkInputsForDeletedCas;
		m_bootTime = GetTime();

		m_maintenanceReserveSize = info.maintenanceReserveSize;
		m_expirationTimeSeconds = info.expirationTimeSeconds;
		m_bucketCasTableMaxSize = info.bucketCasTableMaxSize;

		m_rootDir.count = GetFullPathNameW(info.rootDir, m_rootDir.capacity, m_rootDir.data, NULL);
		m_rootDir.Replace('/', PathSeparator).EnsureEndsWithSlash();

		m_server.RegisterService(CacheServiceId,
			[this](const ConnectionInfo& connectionInfo, MessageInfo& messageInfo, BinaryReader& reader, BinaryWriter& writer)
			{
				return HandleMessage(connectionInfo, messageInfo.type, reader, writer);
			},
			[](u8 messageType)
			{
				return ToString(CacheMessageType(messageType));
			}
		);

		m_server.RegisterOnClientDisconnected(CacheServiceId, [this](const Guid& clientUid, u32 clientId)
			{
				OnDisconnected(clientId);
			});
	}

	CacheServer::~CacheServer()
	{
	}

	struct CacheServer::LoadStats
	{
		Atomic<u32> totalPathTableSize;
		Atomic<u32> totalCasKeyTableSize;
		Atomic<u64> totalCacheEntryCount;
	};

	bool CacheServer::Load()
	{
		u64 startTime = GetTime();

		StringBuffer<> fileName(m_rootDir);
		fileName.EnsureEndsWithSlash().Append(TC("cachedb"));

		FileAccessor file(m_logger, fileName.data);
		if (!file.OpenMemoryRead(0, false))
		{
			m_logger.Detail(TC("No database found. Starting a new one at %s"), fileName.data);
			m_creationTime = GetSystemTimeAsFileTime();
			m_dbfileDirty = true;
			return true;
		}
		BinaryReader reader(file.GetData(), 0, file.GetSize());

		u32 databaseVersion = reader.ReadU32();
		if (databaseVersion < CacheFileCompatibilityVersion || databaseVersion > CacheFileVersion)
		{
			m_logger.Detail(TC("Can't load database of version %u. Starting a new one at %s"), databaseVersion, fileName.data);
			return true;
		}
		if (databaseVersion == 3)
			m_creationTime = GetSystemTimeAsFileTime() - 1;
		else
			m_creationTime = reader.ReadU64();

		if (databaseVersion != CacheFileVersion)
			m_dbfileDirty = true;

		LoadStats stats;

		if (databaseVersion == 4)
		{
			u32 bucketCount = reader.ReadU32();
			while (bucketCount--)
			{
				Bucket& bucket = GetBucket(reader.ReadU64());
				LoadBucket(bucket, reader, databaseVersion, stats);
			}
		}
		else
		{
			StringBuffer<MaxPath> bucketsDir(m_rootDir);
			bucketsDir.EnsureEndsWithSlash().Append(TC("buckets"));
			TraverseDir(m_logger, bucketsDir.data, [&](const DirectoryEntry& e)
				{
					StringBuffer<128> keyName;
					keyName.Append(e.name, e.nameLen);
					u64 id;
					if (!keyName.Parse(id))
						return;
					GetBucket(id);
				});

			m_server.ParallelFor(GetBucketWorkerCount(), m_buckets, [&](auto& it)
				{
					u64 key = it->first;

					StringBuffer<MaxPath> bucketFilename(bucketsDir);
					bucketFilename.EnsureEndsWithSlash().AppendValue(key);
					FileAccessor bucketFile(m_logger, bucketFilename.data);
					if (!bucketFile.OpenMemoryRead(0, false))
					{
						m_logger.Detail(TC("Failed to open bucket file %s"), bucketFilename.data);
						return;
					}
					BinaryReader reader(bucketFile.GetData(), 0, bucketFile.GetSize());
					u32 bucketVersion = reader.ReadU32();
					LoadBucket(it->second, reader, bucketVersion, stats);
				});
		}

		u64 duration = GetTime() - startTime;
		m_logger.Detail(TC("Database loaded from %s (v%u)  in %s (%llu bucket(s) containing %s paths, %s keys, %s cache entries)"), fileName.data, databaseVersion, TimeToText(duration).str, m_buckets.size(), BytesToText(stats.totalPathTableSize).str, BytesToText(stats.totalCasKeyTableSize).str, CountToText(stats.totalCacheEntryCount.load()).str);
		return true;
	}

	bool CacheServer::LoadBucket(Bucket& bucket, BinaryReader& reader, u32 databaseVersion, LoadStats& outStats)
	{
		if (databaseVersion != CacheFileVersion)
			bucket.needsSave = true;

		bucket.expirationTimeSeconds = m_expirationTimeSeconds;

		u32 pathTableSize = reader.ReadU32();
		if (pathTableSize)
		{
			BinaryReader pathTableReader(reader.GetPositionData(), 0, pathTableSize);
			bucket.m_pathTable.ReadMem(pathTableReader, true);
			reader.Skip(pathTableSize);
		}
		outStats.totalPathTableSize += pathTableSize;

		u32 casKeyTableSize = reader.ReadU32();
		if (casKeyTableSize)
		{
			BinaryReader casKeyTableReader(reader.GetPositionData(), 0, casKeyTableSize);
			bucket.m_casKeyTable.ReadMem(casKeyTableReader, true);
			reader.Skip(casKeyTableSize);
		}
		outStats.totalCasKeyTableSize += casKeyTableSize;

		u32 entryLookupCount = reader.ReadU32();
		bucket.m_cacheEntryLookup.reserve(entryLookupCount);

		while (entryLookupCount--)
		{
			auto insres = bucket.m_cacheEntryLookup.try_emplace(reader.ReadCasKey());
			UBA_ASSERT(insres.second);
			auto& cacheEntries = insres.first->second;
			cacheEntries.Read(m_logger, reader, databaseVersion);
			outStats.totalCacheEntryCount += cacheEntries.entries.size();
		}
		return true;
	}

	bool CacheServer::Save()
	{
		for (auto& kv : m_buckets)
		{
			Bucket& bucket = kv.second;
			if (bucket.lastSavedTime < bucket.lastUsedTime)
				bucket.needsSave = true;
		}

		return SaveNoLock();
	}

	struct FileWriter
	{
		static constexpr u64 TempBufferSize = 1024*1024;

		FileWriter(Logger& l, const tchar* fn)
		:	logger(l)
		,	fileName(fn)
		,	tempFileName(StringBuffer<MaxPath>(fn).Append(TC(".tmp")).data)
		,	file(logger, tempFileName.c_str())
		{
			tempBuffer = (u8*)malloc(TempBufferSize);
		}

		~FileWriter()
		{
			free(tempBuffer);
		}

		void WriteBytes(const void* data, u64 size)
		{
			u8* readPos = (u8*)data;
			u64 left = size;
			while (left)
			{
				if (tempBufferPos != TempBufferSize)
				{
					u64 toWrite = Min(TempBufferSize - tempBufferPos, left);
					memcpy(tempBuffer+tempBufferPos, readPos, toWrite);
					tempBufferPos += toWrite;
					left -= toWrite;
					readPos += toWrite;
				}
				else
				{
					written += tempBufferPos;
					success &= file.Write(tempBuffer, tempBufferPos);
					tempBufferPos = 0;
				}
			}
		}

		template<typename T>
		void Write(const T& v)
		{
			WriteBytes(&v, sizeof(v));
		}

		bool Create() { return file.CreateWrite(); }

		bool Close()
		{
			success &= file.Write(tempBuffer, tempBufferPos);
			written += tempBufferPos;

			if (!success)
				return false;

			if (!file.Close())
				return false;

			if (!MoveFileExW(tempFileName.data(), fileName.data(), MOVEFILE_REPLACE_EXISTING))
				return logger.Error(TC("Can't move file from %s to %s (%s)"), tempFileName.data(), fileName.data(), LastErrorToText().data);

			return true;
		}

		Logger& logger;
		bool success = true;
		u8* tempBuffer = nullptr;
		u64 tempBufferPos = 0;
		u64 written = 0;
		TString fileName;
		TString tempFileName;
		FileAccessor file;
	};

	bool CacheServer::SaveBucket(u64 bucketId, Bucket& bucket)
	{
		u64 saveStart = GetTime();

		StringBuffer<MaxPath> bucketsDir(m_rootDir);
		bucketsDir.EnsureEndsWithSlash().Append(TC("buckets"));
		if (!m_storage.CreateDirectory(bucketsDir.data))
			return false;
		bucketsDir.EnsureEndsWithSlash();
		StringBuffer<MaxPath> bucketsFile(bucketsDir);
		bucketsFile.AppendValue(bucketId);

		FileWriter file(m_logger, bucketsFile.data);
		
		if (!file.Create())
			return false;

		file.Write(CacheFileVersion);

		u32 pathTableSize = bucket.m_pathTable.GetSize();
		file.Write(pathTableSize);
		file.WriteBytes(bucket.m_pathTable.GetMemory(), pathTableSize);

		u32 casKeyTableSize = bucket.m_casKeyTable.GetSize();
		file.Write(casKeyTableSize);
		file.WriteBytes(bucket.m_casKeyTable.GetMemory(), casKeyTableSize);

		u32 entryLookupCount = u32(bucket.m_cacheEntryLookup.size());
		file.Write(entryLookupCount);

		Vector<u8> temp;

		for (auto& kv2 : bucket.m_cacheEntryLookup)
		{
			file.Write(kv2.first);

			temp.resize(kv2.second.GetTotalSize(CacheNetworkVersion, true));
			BinaryWriter writer(temp.data(), 0, temp.size());
			kv2.second.Write(writer, CacheNetworkVersion, true);
			UBA_ASSERT(writer.GetPosition() == temp.size());
			file.WriteBytes(temp.data(), temp.size());
		}

		if (!file.Close())
			return false;

		bucket.lastSavedTime = GetSystemTimeAsFileTime() - m_creationTime;

		m_logger.Detail(TC("    Bucket %u saved - %s (%s)"), bucket.index, BytesToText(file.written).str, TimeToText(GetTime() - saveStart).str);
		return true;
	}

	bool CacheServer::SaveNoLock()
	{
		if (m_dbfileDirty)
		{
			StringBuffer<MaxPath> fileName(m_rootDir);
			fileName.EnsureEndsWithSlash().Append(TC("cachedb"));

			FileWriter file(m_logger, fileName.data);
		
			if (!file.Create())
				return false;

			file.Write(CacheFileVersion);

			file.Write(m_creationTime);

			if (!file.Close())
				return false;
			m_dbfileDirty = false;
		}

		StringBuffer<MaxPath> bucketsDir(m_rootDir);
		bucketsDir.EnsureEndsWithSlash().Append(TC("buckets"));
		if (!m_storage.CreateDirectory(bucketsDir.data))
			return false;
		bucketsDir.EnsureEndsWithSlash();

		Atomic<bool> success = true;

		for (auto it=m_buckets.begin(); it!=m_buckets.end();)
		{
			Bucket& bucket = it->second;
			if (!bucket.m_cacheEntryLookup.empty())
			{
				++it;
				continue;
			}

			StringBuffer<MaxPath> bucketsFile(m_rootDir);
			bucketsFile.EnsureEndsWithSlash().Append(TC("buckets")).EnsureEndsWithSlash().AppendValue(it->first);
			DeleteFileW(bucketsFile.data);
			m_logger.Detail(TC("    Bucket %u was empty. Deleted"), bucket.index);
			it = m_buckets.erase(it);
		}

		m_server.ParallelFor(GetBucketWorkerCount(), m_buckets, [&, temp = Vector<u8>()](auto& it) mutable
			{
				Bucket& bucket = it->second;
				if (!bucket.needsSave)
					return;
				if (SaveBucket(it->first, bucket))
					bucket.needsSave = false;
				else
					success = false;
			});

		return success;
	}

	bool CacheServer::RunMaintenance(bool force, const Function<bool()>& shouldExit)
	{
		if (m_addsSinceMaintenance == 0 && !force)
			return true;

		SCOPED_WRITE_LOCK(m_connectionsLock, lock2);
		if (!m_connections.empty())
			return true;
		m_isRunningMaintenance = true;
		lock2.Leave();

		auto g = MakeGuard([&]()
			{
				SCOPED_WRITE_LOCK(m_connectionsLock, lock2);
				m_isRunningMaintenance = false;
			});


		//m_forceAllSteps = true;
		bool forceAllSteps = m_forceAllSteps;
		m_forceAllSteps = false;

		u32 addsSinceMaintenance = m_addsSinceMaintenance;
		bool entriesAdded = addsSinceMaintenance != 0;
		m_addsSinceMaintenance = 0;

		u64 startTime = GetTime();

		if (entriesAdded)
		{
			auto& storageStats = m_storage.Stats();
			u64 hits = m_cacheKeyHitCount;
			u64 miss = m_cacheKeyFetchCount - hits;
			m_logger.Info(TC("Stats since boot (%s ago)"), TimeToText(startTime - m_bootTime, true).str);
			m_logger.Info(TC("  CacheServer %s hits, %s misses"), CountToText(hits).str, CountToText(miss).str);
			u64 recvCount = storageStats.sendCas.count.load();
			u64 sendCount = storageStats.recvCas.count.load();
			m_logger.Info(TC("  StorageServer cas %s (%s) sent, %s (%s) received"), CountToText(recvCount).str, BytesToText(storageStats.sendCasBytesComp).str, CountToText(sendCount).str, BytesToText(storageStats.recvCasBytesComp).str);
		}

		if (m_shouldWipe)
		{
			m_shouldWipe = false;
			m_logger.Info(TC("Obliterating database"));
			m_longestMaintenance = 0;
			m_buckets.clear();
			forceAllSteps = true;
			m_creationTime = GetSystemTimeAsFileTime();
		}
		else
		{
			TimeToText lastTime(startTime - m_lastMaintenance, true);
			m_logger.Info(TC("Maintenance started after %u added cache entries (Ran last time %s ago)"), addsSinceMaintenance, (m_lastMaintenance ? lastTime.str : TC("<never>")));
		}

		m_lastMaintenance = startTime;
		
		UnorderedSet<CasKey> deletedCasFiles;
		m_storage.HandleOverflow(&deletedCasFiles);
		u64 deletedCasCount = deletedCasFiles.size();

		u64 totalCasSize = 0;

		struct CasFileInfo { CasFileInfo(u32 s = 0) : size(s) {} u32 size; Atomic<bool> isUsed; }; // These are compressed cas, should never be over 4gb

		// Existing cas entries can be more than 2 million entries.. which uses a lot of memory
		constexpr u64 existingCasMemoryReserveSize = 192*1024*1024;
		MemoryBlock existingCasMemoryBlock;
		if (!existingCasMemoryBlock.Init(existingCasMemoryReserveSize, nullptr, true))
			existingCasMemoryBlock.Init(existingCasMemoryReserveSize);

		HashMap<CasKey, CasFileInfo> existingCas;


		m_storage.WaitForActiveWork();

		u64 totalCasCount;
		{
			u64 collectCasStartTime = GetTime();

			u32 removedNonExisting = 0;

			// TODO: Make this cleaner... (inside UbaStorage instead)
			SCOPED_WRITE_LOCK(m_storage.m_casLookupLock, lookupLock);
			
			totalCasCount = m_storage.m_casLookup.size();
			existingCas.Init(existingCasMemoryBlock, totalCasCount);

			for (auto i=m_storage.m_casLookup.begin(), e=m_storage.m_casLookup.end(); i!=e;)
			{
				if (i->second.verified && !i->second.exists)
				{
					m_storage.DetachEntry(i->second);
					++removedNonExisting;
					i = m_storage.m_casLookup.erase(i);
					e = m_storage.m_casLookup.end();
					continue;
				}
				totalCasSize += i->second.size;
				UBA_ASSERT(i->second.size < ~0u);
				existingCas.Insert(i->first).size = u32(i->second.size);
				++i;
			}
			lookupLock.Leave();

			if (removedNonExisting)
				m_logger.Detail(TC("  Removed %s cas entries (marked as not existing)"), CountToText(removedNonExisting).str);

			m_logger.Detail(TC("  Found %s (%s) cas files and %s deleted by overflow (%s)"), CountToText(existingCas.Size()).str, BytesToText(totalCasSize).str, CountToText(deletedCasFiles.size()).str, TimeToText(GetTime() - collectCasStartTime).str);
		}

		if (shouldExit())
			return true;

		ReaderWriterLock globalStatsLock;
		u64 now = GetSystemTimeAsFileTime();
		u64 oldest = 0;
		u64 longestUnused = 0;

		u32 workerCount = m_server.GetWorkerCount();
		u32 workerCountToUse = workerCount > 0 ? workerCount - 1 : 0;
		u32 workerCountToUseForBuckets = Min(workerCountToUse, u32(m_buckets.size()));

		Atomic<u64> totalEntryCount;
		Atomic<u64> deleteEntryCount;
		Atomic<u64> expiredEntryCount;
		Atomic<u64> overflowedEntryCount;
		Atomic<u64> missingOutputEntryCount;
		Atomic<u64> missingInputEntryCount;

		Atomic<u64> activeDropCount;
		auto dropCasGuard = MakeGuard([&]() { while (activeDropCount != 0) Sleep(1); });

		auto EnsureBucketContextInitialized = [&](Bucket& bucket)
			{
				auto& context = *bucket.m_maintenanceContext;
				if (!context.isInitialized)
				{
					if (!context.memoryBlock.Init(m_maintenanceReserveSize, nullptr, true)) // Try to use large blocks
						context.memoryBlock.Init(m_maintenanceReserveSize, nullptr, false);
					context.deletedOffsets.Init(context.memoryBlock, bucket.m_casKeyTable.GetSize());
					context.isInitialized = true;
				}
			};

		u32 deleteIteration = 0;
		u64 deleteCacheEntriesStartTime = GetTime();
		do
		{
			bool checkInputsForDeletes = m_checkInputsForDeletedCas && !deletedCasFiles.empty();

			oldest = 0;
			longestUnused = 0;
			totalEntryCount = 0;

			m_server.ParallelFor(workerCountToUseForBuckets, m_buckets, [&](auto& it)
			{
				Bucket& bucket = it->second;
				auto context = bucket.m_maintenanceContext;
				if (!context)
					context = bucket.m_maintenanceContext = new Bucket::MaintenanceContext;

				bool foundDeletedCasKey = false;
				for (auto& cas : deletedCasFiles)
					bucket.m_casKeyTable.TraverseOffsets(cas, [&](u32 casKeyOffset)
						{
							EnsureBucketContextInitialized(bucket);
							foundDeletedCasKey = true;
							context->deletedOffsets.Set(casKeyOffset);
						});

				if (!foundDeletedCasKey)
					checkInputsForDeletes = false;

				auto& deletedOffsets = context->deletedOffsets;

				bucket.totalEntryCount = 0;
				bucket.totalEntrySize = 0;


				ReaderWriterLock keysToEraseLock;
				Vector<CasKey> keysToErase;

				u64 lastUseTimeLimit = 0; // This is the time relative to server startup time
				if (bucket.expirationTimeSeconds && m_bucketCasTableMaxSize)
				{
					if (deleteIteration == 0)
					{
						u64 bucketCasTableSize = bucket.m_casKeyTable.GetSize();
						u64 oldExpirationTime = bucket.expirationTimeSeconds;
						if (bucketCasTableSize >= m_bucketCasTableMaxSize)
							bucket.expirationTimeSeconds -= 60*60; // Decreased by one hour
						else if (bucket.expirationTimeSeconds < m_expirationTimeSeconds && (bucketCasTableSize + 1ull*1024*1024) < m_bucketCasTableMaxSize)
							bucket.expirationTimeSeconds += 60*60; // Increased by one hour
						if (oldExpirationTime != bucket.expirationTimeSeconds)
							m_logger.Info(TC("%s expiration time for bucket %u to %s"), oldExpirationTime < bucket.expirationTimeSeconds ? TC("Increased") : TC("Decreased"), bucket.index, TimeToText(MsToTime(bucket.expirationTimeSeconds*1000), true).str);
					}

					u64 secondsRunning = GetFileTimeAsSeconds(now - m_creationTime);
					if (secondsRunning > bucket.expirationTimeSeconds)
						lastUseTimeLimit = (now - m_creationTime) - GetSecondsAsFileTime(bucket.expirationTimeSeconds);
				}

				m_server.ParallelFor(workerCountToUse, bucket.m_cacheEntryLookup, [&, touchedCas = Vector<Atomic<bool>*>()](auto& li) mutable
				{
					CacheEntries& entries = li->second;

					// There is currently no idea saving more than 256kb worth of entries per lookup key (because that is what fetch max returns).. so let's wipe out
					// all the entries that overflow that number
					u64 capacityLeft = SendMaxSize - 32 - entries.GetSharedSize();

					// Check if any offset has been deleted in shared offsets..
					bool offsetDeletedInShared = false;
					auto& sharedOffsets = entries.sharedInputCasKeyOffsets;
					if (checkInputsForDeletes)
					{
						BinaryReader reader2(sharedOffsets.data(), 0, sharedOffsets.size());
						while (reader2.GetLeft())
						{
							if (!deletedOffsets.IsSet(u32(reader2.Read7BitEncoded())))
								continue;
							offsetDeletedInShared = true;
							break;
						}
					}

					for (auto i=entries.entries.begin(), e=entries.entries.end(); i!=e;)
					{
						auto& entry = *i;
						bool deleteEntry = false;

						u64 neededSize = entries.GetEntrySize(entry, CacheNetworkVersion, false);
						if (neededSize > capacityLeft)
						{
							deleteEntry = true;
							capacityLeft = 0;
							++overflowedEntryCount;
						}

						if (!deleteEntry && entry.creationTime < lastUseTimeLimit && entry.lastUsedTime < lastUseTimeLimit)
						{
							deleteEntry = true;
							++expiredEntryCount;
						}

						// This is an attempt at removing entries that has inputs that depends on other entries outputs.
						// and that there is no point keeping them if the other entry is removed
						// Example would be that there is no idea keeping entries that uses a pch if the entry producing the pch is gone
						if (checkInputsForDeletes)
						{
							if (!deleteEntry && offsetDeletedInShared)
							{
								BinaryReader rangeReader(entry.sharedInputCasKeyOffsetRanges.data(), 0, entry.sharedInputCasKeyOffsetRanges.size());
								while (!deleteEntry && rangeReader.GetLeft())
								{
									u64 begin = rangeReader.Read7BitEncoded();
									u64 end = rangeReader.Read7BitEncoded();
									BinaryReader inputReader(sharedOffsets.data() + begin, 0, end - begin);
									while (inputReader.GetLeft())
									{
										if (!deletedOffsets.IsSet(u32(inputReader.Read7BitEncoded())))
											continue;
										deleteEntry = true;
										++missingInputEntryCount;
										break;
									}
								}
							}

							if (!deleteEntry)
							{
								auto& extraInputs = entry.extraInputCasKeyOffsets;
								BinaryReader extraReader(extraInputs.data(), 0, extraInputs.size());
								while (extraReader.GetLeft())
								{
									if (!deletedOffsets.IsSet(u32(extraReader.Read7BitEncoded())))
										continue;
									deleteEntry = true;
									++missingInputEntryCount;
									break;
								}
							}
						}

						if (!deleteEntry)
						{
							// Traverse outputs and check if cas files exists for each output, if not, delete entry.
							touchedCas.clear();

							auto& outputs = entry.outputCasKeyOffsets;
							BinaryReader outputsReader(outputs.data(), 0, outputs.size());
							while (outputsReader.GetLeft())
							{
								u64 offset = outputsReader.Read7BitEncoded();
								CasKey casKey;
								bucket.m_casKeyTable.GetKey(casKey, offset);
								UBA_ASSERT(IsCompressed(casKey));
								if (auto value = existingCas.Find(casKey))
								{
									touchedCas.push_back(&value->isUsed);
									continue;
								}
								deleteEntry = true;
								++missingOutputEntryCount;
								break;
							}
						}

						// Remove entry from entries list and skip increasing ref count of cas files
						if (deleteEntry)
						{
							if (i->id == entries.primaryId)
								entries.primaryId = ~0u;
							bucket.hasDeletedEntries = true;
							++deleteEntryCount;
							i = entries.entries.erase(i);
							e = entries.entries.end();
							continue;
						}

						++bucket.totalEntryCount;

						capacityLeft -= neededSize;

						{
							SCOPED_WRITE_LOCK(globalStatsLock, l);
							if (!oldest || entry.creationTime < oldest)
								oldest = entry.creationTime;
							if (!longestUnused || entry.lastUsedTime < longestUnused)
								longestUnused = entry.lastUsedTime;
						}

						for (auto v : touchedCas)
							*v = true;

						++i;
					}


					if (entries.entries.empty())
					{
						SCOPED_WRITE_LOCK(keysToEraseLock, lock2);
						keysToErase.push_back(li->first);
					}
					else
						bucket.totalEntrySize += entries.GetTotalSize(CacheNetworkVersion, false);
				});

				for (auto& key : keysToErase)
					bucket.m_cacheEntryLookup.erase(key);

				totalEntryCount += bucket.totalEntryCount;
			}, TC(""), true);

			// Reset deleted cas files and update it again..
			deletedCasFiles.clear();

			for (auto i=existingCas.ValuesBegin(), e=existingCas.ValuesEnd(); i!=e; ++i)
			{
				if (i->isUsed)
				{
					i->isUsed = false;
					continue;
				}
				const CasKey* key = existingCas.GetKey(i);
				if (!key)
					continue;
				deletedCasFiles.insert(*key);
				++deletedCasCount;
				totalCasSize -= i->size;
				existingCas.Erase(*key);
			}

			// Add drop cas as work so it can run in the background
			for (auto& casKey : deletedCasFiles)
			{
				++activeDropCount;
				m_server.AddWork([&, key = casKey]() { m_storage.DropCasFile(key, true, TC("")); --activeDropCount; }, 1, TC(""));
			}
			++deleteIteration;
		}
		while (!deletedCasFiles.empty()); // if cas files are deleted we need to do another loop and check cache entry inputs to see if files were inputs

		existingCasMemoryBlock.Deinit();

		if (overflowedEntryCount)
			m_logger.Detail(TC("  Found %llu overflowed cache entries"), overflowedEntryCount.load());
		if (expiredEntryCount)
			m_logger.Detail(TC("  Found %llu expired cache entries"), expiredEntryCount.load());
		if (missingOutputEntryCount)
			m_logger.Detail(TC("  Found %llu cache entries with missing output cas"), missingOutputEntryCount.load());
		if (missingInputEntryCount)
			m_logger.Detail(TC("  Found %llu cache entries with missing input cas"), missingInputEntryCount.load());

		m_logger.Detail(TC("  Deleted %llu cas files and %llu cache entries over %u buckets (%s)"), deletedCasCount, deleteEntryCount.load(), u32(m_buckets.size()), TimeToText(GetTime() - deleteCacheEntriesStartTime).str);

		if (shouldExit())
			return true;

		u64 maxCommittedMemory = 0;

		m_server.ParallelFor(workerCountToUseForBuckets, m_buckets, [&](auto& it)
		{
			u64 bucketStartTime = GetTime();

			Bucket& bucket = it->second;
			auto deleteContext = MakeGuard([&]() { delete bucket.m_maintenanceContext; bucket.m_maintenanceContext = nullptr; });

			if (!bucket.hasDeletedEntries && !forceAllSteps)
			{
				m_logger.Detail(TC("    Bucket %u skipped updating. (%s entries)"), bucket.index, CountToText(bucket.totalEntryCount.load()).str);
				return;
			}
			bucket.hasDeletedEntries = false;

			EnsureBucketContextInitialized(bucket);
			MemoryBlock& memoryBlock = bucket.m_maintenanceContext->memoryBlock;

			BitArray usedCasKeyOffsets;
			usedCasKeyOffsets.Init(memoryBlock, bucket.m_casKeyTable.GetSize());

			u64 collectUsedCasKeysStart = GetTime();

			// Collect all caskeys that are used by cache entries.
			for (auto& kv2 : bucket.m_cacheEntryLookup)
			{
				auto collectUsedCasKeyOffsets = [&](const Vector<u8>& offsets)
					{
						BinaryReader reader2(offsets.data(), 0, offsets.size());
						while (reader2.GetLeft())
						{
							u32 offset = u32(reader2.Read7BitEncoded());
							usedCasKeyOffsets.Set(offset);
						}
					};

				collectUsedCasKeyOffsets(kv2.second.sharedInputCasKeyOffsets);
				for (auto& entry : kv2.second.entries)
				{
					collectUsedCasKeyOffsets(entry.extraInputCasKeyOffsets);
					collectUsedCasKeyOffsets(entry.outputCasKeyOffsets);
				}
			}
			u64 usedCasKeyOffsetsCount = usedCasKeyOffsets.CountSetBits();

			m_logger.Detail(TC("    Bucket %u Collected %s used caskeys. (%s)"), bucket.index, CountToText(usedCasKeyOffsetsCount).str, TimeToText(GetTime() - collectUsedCasKeysStart).str);

			u64 recreatePathTableStart = GetTime();

			// Traverse all caskeys in caskey table and figure out which ones we can delete
			BitArray usedPathOffsets;
			usedPathOffsets.Init(memoryBlock, bucket.m_pathTable.GetSize());

			BinaryReader casKeyTableReader(bucket.m_casKeyTable.GetMemory(), 0, bucket.m_casKeyTable.GetSize());
			usedCasKeyOffsets.Traverse([&](u32 casKeyOffset)
				{
					casKeyTableReader.SetPosition(casKeyOffset);
					u32 pathOffset = u32(casKeyTableReader.Read7BitEncoded());
					usedPathOffsets.Set(pathOffset);
				});

			// Build new path table based on used offsets
			HashMap2<u32, u32> oldToNewPathOffset;
			u32 oldSize = bucket.m_pathTable.GetSize();
			{
				CompactPathTable newPathTable(CachePathTableMaxSize, CompactPathTable::V1, bucket.m_pathTable.GetPathCount(), bucket.m_pathTable.GetSegmentCount());
				oldToNewPathOffset.Init(memoryBlock, usedPathOffsets.CountSetBits());

				StringBuffer<> temp;
				usedPathOffsets.Traverse([&](u32 pathOffset)
				{
					bucket.m_pathTable.GetString(temp.Clear(), pathOffset);
					u32 newPathOffset = newPathTable.AddNoLock(temp.data, temp.count);

					#if 0
					StringBuffer<> test;
					newPathTable.GetString(test, newPathOffset);
					UBA_ASSERT(test.Equals(temp.data));
					#endif

					if (pathOffset != newPathOffset)
						oldToNewPathOffset.Insert(pathOffset) = newPathOffset;
				});
				bucket.m_pathTable.Swap(newPathTable);
			}
			m_logger.Detail(TC("    Bucket %u Recreated path table. %s -> %s (%s)"), bucket.index, BytesToText(oldSize).str, BytesToText(bucket.m_pathTable.GetSize()).str, TimeToText(GetTime() - recreatePathTableStart).str);


			// Build new caskey table based on used offsets
			u64 recreateCasKeyTableStart = GetTime();
			HashMap2<u32, u32> oldToNewCasKeyOffset;
			oldSize = bucket.m_casKeyTable.GetSize();
			{
				oldToNewCasKeyOffset.Init(memoryBlock, usedCasKeyOffsetsCount);
				CompactCasKeyTable newCasKeyTable(CacheCasKeyTableMaxSize, usedCasKeyOffsetsCount);
				BinaryReader reader2(bucket.m_casKeyTable.GetMemory(), 0, bucket.m_casKeyTable.GetSize());
				usedCasKeyOffsets.Traverse([&](u32 casKeyOffset)
				{
					reader2.SetPosition(casKeyOffset);
					u32 oldPathOffset = u32(reader2.Read7BitEncoded());
					CasKey casKey = reader2.ReadCasKey();
					u32 newPathOffset = oldPathOffset;
					if (auto value = oldToNewPathOffset.Find(oldPathOffset))
						newPathOffset = *value;
					u32 newCasKeyOffset = newCasKeyTable.Add(casKey, newPathOffset);
					if (casKeyOffset != newCasKeyOffset)
						oldToNewCasKeyOffset.Insert(casKeyOffset) = newCasKeyOffset;
				});
				bucket.m_casKeyTable.Swap(newCasKeyTable);
			}
			m_logger.Detail(TC("    Bucket %u Recreated caskey table. %s -> %s (%s)"), bucket.index, BytesToText(oldSize).str, BytesToText(bucket.m_casKeyTable.GetSize()).str, TimeToText(GetTime() - recreateCasKeyTableStart).str);


			if (oldToNewCasKeyOffset.Size() > 0)
			{
				// Update all casKeyOffsets
				u64 updateEntriesStart = GetTime();

				m_server.ParallelFor(workerCountToUse, bucket.m_cacheEntryLookup, [&, temp = Vector<u32>(), temp2 = Vector<u8>()](auto& it) mutable
					{
						it->second.UpdateEntries(m_logger, oldToNewCasKeyOffset, temp, temp2);
					});

				#if 0
				u8* mem = bucket.m_pathTable.GetMemory();
				u64 memLeft = bucket.m_pathTable.GetSize();
				while (memLeft)
				{
					u8 buffer[256*1024];
					auto compressor = OodleLZ_Compressor_Kraken;
					auto compressionLevel = OodleLZ_CompressionLevel_SuperFast;
					u64 toCompress = Min(memLeft, u64(256*1024 - 128));
					auto compressedBlockSize = OodleLZ_Compress(compressor, mem, (OO_SINTa)toCompress, buffer, compressionLevel);
					(void)compressedBlockSize;
					memLeft -= toCompress;
				}
				#endif


				m_logger.Detail(TC("    Bucket %u Updated cache entries with new tables (%s)"), bucket.index, TimeToText(GetTime() - updateEntriesStart).str);
			}

			bucket.needsSave = true;

			m_logger.Info(TC("    Bucket %u Done (%s). CacheEntries: %s (%s) PathTable: %s CasTable: %s Expiration: %s"), bucket.index, TimeToText(GetTime() - bucketStartTime).str, CountToText(bucket.totalEntryCount.load()).str, BytesToText(bucket.totalEntrySize.load()).str, BytesToText(bucket.m_pathTable.GetSize()).str, BytesToText(bucket.m_casKeyTable.GetSize()).str, TimeToText(MsToTime(bucket.expirationTimeSeconds*1000), true).str);

			SCOPED_WRITE_LOCK(globalStatsLock, l);
			maxCommittedMemory = Max(maxCommittedMemory, memoryBlock.writtenSize);

		}, TC(""), true);

		// Need to make sure all cas entries are dropped before saving cas table
		u64 dropStartTime = GetTime();
		dropCasGuard.Execute();
		u64 dropCasDuration = GetTime() - dropStartTime;
		if (TimeToMs(dropCasDuration) > 10)
			m_logger.Detail(TC("  Done deleting cas files (%s)"), TimeToText(dropCasDuration).str);

		if (entriesAdded || deletedCasCount || deleteEntryCount || forceAllSteps)
		{
			u64 saveStart = GetTime();
			m_logger.Detail(TC("  Saving to disk"));
			Event saveCasEvent(true);
			m_server.AddWork([&]() { m_storage.SaveCasTable(false, false); saveCasEvent.Set(); }, 1, TC("SaveCas"));
			SaveNoLock();
			saveCasEvent.IsSet();
			m_logger.Detail(TC("  Save Done (%s)"), TimeToText(GetTime() - saveStart).str);
		}

		u64 oldestTime = oldest ? GetFileTimeAsTime(now - (m_creationTime + oldest)) : 0;
		u64 longestUnusedTime = longestUnused ? GetFileTimeAsTime(now - (m_creationTime + longestUnused)) : 0;
		u64 duration = GetTime() - startTime;
		m_logger.Info(TC("Maintenance done! (%s) CasFiles: %s (%s) Entries: %s Oldest: %s LongestUnused: %s MaintenanceMem: %s/%s"), TimeToText(duration).str, CountToText(totalCasCount - deletedCasCount).str, BytesToText(totalCasSize).str, CountToText(totalEntryCount.load()).str, TimeToText(oldestTime, true).str, TimeToText(longestUnusedTime, true).str, BytesToText(maxCommittedMemory).str, BytesToText(m_maintenanceReserveSize).str);
		
		m_longestMaintenance = Max(m_longestMaintenance, duration);

		return true;
	}

	bool CacheServer::ShouldShutdown()
	{
		if (!m_shutdownRequested)
			return false;
		SCOPED_READ_LOCK(m_connectionsLock, lock2);
		if (!m_connections.empty() || m_addsSinceMaintenance)
			return false;
		return true;
	}

	void CacheServer::OnDisconnected(u32 clientId)
	{
		SCOPED_WRITE_LOCK(m_connectionsLock, lock);
		m_connections.erase(clientId);
		lock.Leave();
	}

	CacheServer::ConnectionBucket& CacheServer::GetConnectionBucket(const ConnectionInfo& connectionInfo, BinaryReader& reader, u32* outClientVersion)
	{
		u64 id = reader.Read7BitEncoded();
		SCOPED_WRITE_LOCK(m_connectionsLock, lock);
		auto& connection = m_connections[connectionInfo.GetId()];
		if (outClientVersion)
			*outClientVersion = connection.clientVersion;
		return connection.buckets.try_emplace(id, id).first->second;
	}

	CacheServer::Bucket& CacheServer::GetBucket(BinaryReader& reader)
	{
		u64 id = reader.Read7BitEncoded();
		return GetBucket(id);
		
	}

	CacheServer::Bucket& CacheServer::GetBucket(u64 id)
	{
		SCOPED_WRITE_LOCK(m_bucketsLock, bucketsLock);
		auto insres = m_buckets.try_emplace(id, id);
		auto& bucket = insres.first->second;
		if (!insres.second)
			return bucket;
		bucket.index = u32(m_buckets.size() - 1);
		bucket.expirationTimeSeconds = m_expirationTimeSeconds;
		return bucket;
	}

	u32 CacheServer::GetBucketWorkerCount()
	{
		u32 workerCount = m_server.GetWorkerCount();
		u32 workerCountToUse = workerCount > 0 ? workerCount - 1 : 0;
		return Min(workerCountToUse, u32(m_buckets.size()));
	}

	bool CacheServer::HandleMessage(const ConnectionInfo& connectionInfo, u8 messageType, BinaryReader& reader, BinaryWriter& writer)
	{
		if (messageType != CacheMessageType_Connect && m_isRunningMaintenance)
			return m_logger.Error(TC("Can't handle network message %s while running maintenance mode"), ToString(CacheMessageType(messageType)));

		switch (messageType)
		{
		case CacheMessageType_Connect:
		{
			u32 clientVersion = reader.ReadU32();
			if (clientVersion < 3 || clientVersion > CacheNetworkVersion)
				return m_logger.Error(TC("Different network versions. Client: %u, Server: %u. Disconnecting"), clientVersion, CacheNetworkVersion);

			SCOPED_WRITE_LOCK(m_connectionsLock, lock);
			if (m_isRunningMaintenance)
			{
				writer.WriteBool(false);
				writer.WriteString(TC("Running maintenance..."));
			}

			writer.WriteBool(true);
			auto insres = m_connections.try_emplace(connectionInfo.GetId());
			auto& connection = insres.first->second;
			connection.clientVersion = clientVersion;
			return true;
		}
		case CacheMessageType_StorePathTable:
		{
			GetConnectionBucket(connectionInfo, reader).pathTable.ReadMem(reader, false);
			return true;
		}
		case CacheMessageType_StoreCasTable:
		{
			GetConnectionBucket(connectionInfo, reader).casKeyTable.ReadMem(reader, false);
			return true;
		}
		case CacheMessageType_StoreEntry:
		{
			u32 clientVersion;
			auto& bucket = GetConnectionBucket(connectionInfo, reader, &clientVersion);
			return HandleStoreEntry(bucket, reader, writer, clientVersion);
		}
		case CacheMessageType_StoreEntryDone:
		{
			auto& connectionBucket = GetConnectionBucket(connectionInfo, reader);
			CasKey cmdKey = reader.ReadCasKey();

			bool success = true;
			if (reader.GetLeft())
				success = reader.ReadBool();

			SCOPED_WRITE_LOCK(connectionBucket.cacheEntryLookupLock, lock2);
			auto findIt = connectionBucket.cacheEntryLookup.find(cmdKey);
			if (findIt == connectionBucket.cacheEntryLookup.end())
				return true;
			auto g = MakeGuard([&]() { connectionBucket.cacheEntryLookup.erase(findIt); });
			if (!success)
				return true;

			u64 id = connectionBucket.id;
			Bucket& bucket = GetBucket(id);

			SCOPED_WRITE_LOCK(bucket.m_cacheEntryLookupLock, lock3);
			auto insres = bucket.m_cacheEntryLookup.try_emplace(cmdKey);
			auto& cacheEntries = insres.first->second;
			lock3.Leave();
			
			SCOPED_WRITE_LOCK(cacheEntries.lock, lock4);
			cacheEntries.entries.emplace_front(std::move(findIt->second));
			return true;
		}
		case CacheMessageType_FetchPathTable:
			return HandleFetchPathTable(reader, writer);

		case CacheMessageType_FetchCasTable:
			return HandleFetchCasTable(reader, writer);

		case CacheMessageType_FetchEntries:
		{
			SCOPED_READ_LOCK(m_connectionsLock, lock);
			u32 clientVersion = m_connections[connectionInfo.GetId()].clientVersion;
			lock.Leave();
			return HandleFetchEntries(reader, writer, clientVersion);
		}
		case CacheMessageType_ExecuteCommand:
			return HandleExecuteCommand(reader, writer);

		case CacheMessageType_ReportUsedEntry:
		{
			SCOPED_READ_LOCK(m_connectionsLock, lock);
			u32 clientVersion = m_connections[connectionInfo.GetId()].clientVersion;
			lock.Leave();
			return HandleReportUsedEntry(reader, writer, clientVersion);
		}
		case CacheMessageType_RequestShutdown:
		{
			TString reason = reader.ReadString();
			m_logger.Info(TC("Shutdown requested. Reason: %s"), reason.empty() ? TC("Unknown") : reason.c_str());
			m_shutdownRequested = true;
			writer.WriteBool(true);
			return true;
		}

		default:
			return false;
		}
	}

	bool CacheServer::HandleStoreEntry(ConnectionBucket& connectionBucket, BinaryReader& reader, BinaryWriter& writer, u32 clientVersion)
	{
		CasKey cmdKey = reader.ReadCasKey();

		u64 inputCount = ~0u;
		if (clientVersion >= 5)
			inputCount = reader.Read7BitEncoded();


		u64 outputCount = reader.Read7BitEncoded();
		u64 index = 0;

		Set<u32> inputs;
		u64 bytesForInput = 0;

		u64 outputStartOffset = reader.GetPosition();
		u64 id = connectionBucket.id;
		Bucket& bucket = GetBucket(id);

		while (reader.GetLeft())
		{
			bool isInput = index++ >= outputCount;
			if (isInput && !inputCount--) // For client versions under 5 we will hit reader.GetLeft() == false first.
				break;

			u32 offset = u32(reader.Read7BitEncoded());
			if (!isInput)
				continue;

			CasKey casKey;
			StringBuffer<> path;
			connectionBucket.casKeyTable.GetPathAndKey(path, casKey, connectionBucket.pathTable, offset);
			UBA_ASSERT(path.count);

			u32 pathOffset = bucket.m_pathTable.Add(path.data, path.count);

			#if 0
			StringBuffer<> test;
			bucket.m_pathTable.GetString(test, pathOffset);
			UBA_ASSERT(test.Equals(path.data));
			#endif

			u32 casKeyOffset = bucket.m_casKeyTable.Add(casKey, pathOffset);
			auto insres = inputs.insert(casKeyOffset);
			if (!insres.second)
			{
				m_logger.Warning(TC("Input file %s exists more than once in cache entry"), path.data);
				continue;
			}
			bytesForInput += Get7BitEncodedCount(casKeyOffset);

			//m_logger.Info(TC("%s - %s"), path.data, CasKeyString(casKey).str);
		}

		// For client versions 5 and over we have log entries after the inputs
		Vector<u8> logLines;
		if (u64 logLinesSize = reader.GetLeft())
		{
			logLines.resize(logLinesSize);
			reader.ReadBytes(logLines.data(), logLinesSize);
		}

		Vector<u8> inputCasKeyOffsets;
		{
			inputCasKeyOffsets.resize(bytesForInput);
			BinaryWriter w2(inputCasKeyOffsets.data(), 0, inputCasKeyOffsets.size());
			for (u32 input : inputs)
				w2.Write7BitEncoded(input);
		}

		SCOPED_WRITE_LOCK(bucket.m_cacheEntryLookupLock, lock);
		auto insres = bucket.m_cacheEntryLookup.try_emplace(cmdKey);
		auto& cacheEntries = insres.first->second;
		lock.Leave();

		SCOPED_WRITE_LOCK(cacheEntries.lock, lock2);
		

		// Create entry based on existing entry
		CacheEntry newEntry;
		cacheEntries.BuildInputs(newEntry, inputs);

		List<CacheEntry>::iterator matchingEntry = cacheEntries.entries.end();
		for (auto i=cacheEntries.entries.begin(), e=cacheEntries.entries.end(); i!=e; ++i)
		{
			if (i->sharedInputCasKeyOffsetRanges != newEntry.sharedInputCasKeyOffsetRanges || i->extraInputCasKeyOffsets != newEntry.extraInputCasKeyOffsets)
				continue;
			matchingEntry = i;
			break;
		}

		// Already exists
		if (matchingEntry != cacheEntries.entries.end())
		{
			bool shouldOverwrite = false;
			Map<TString, CasKey> existing;

			BinaryReader r2(matchingEntry->outputCasKeyOffsets.data(), 0, matchingEntry->outputCasKeyOffsets.size());
			while (r2.GetLeft())
			{
				u32 existingOffset = u32(r2.Read7BitEncoded());
				CasKey casKey;
				StringBuffer<> path;
				bucket.m_casKeyTable.GetPathAndKey(path, casKey, bucket.m_pathTable, existingOffset);
				existing.try_emplace(path.data, casKey);
			}

			reader.SetPosition(outputStartOffset);
			u64 left = outputCount;
			while (left--)
			{
				u32 outputOffset = u32(reader.Read7BitEncoded());
				CasKey casKey;
				StringBuffer<> path;
				connectionBucket.casKeyTable.GetPathAndKey(path, casKey, connectionBucket.pathTable, outputOffset);

				auto findIt = existing.find(path.data);
				if (findIt == existing.end())
				{
					m_logger.Warning(TC("Existing cache entry matches input but does not match output (output file %s did not exist in existing cache entry)"), path.data);
					cacheEntries.entries.erase(matchingEntry);
					shouldOverwrite = true;
					break;
				}
				if (findIt->second != casKey)
				{
					//m_logger.Warning(TC("Existing cache entry matches input but does not match output (%s has different caskey)"), path.data);
					cacheEntries.entries.erase(matchingEntry);
					shouldOverwrite = true;
					break;
				}
			}
			if (!shouldOverwrite)
				return true;
		}

		Set<u32> outputs;
		u64 bytesForOutput = 0;

		bool hasAllContent = true;
		reader.SetPosition(outputStartOffset);
		u64 left = outputCount;
		while (left--)
		{
			u32 outputOffset = u32(reader.Read7BitEncoded());
			CasKey casKey;
			StringBuffer<> path;
			connectionBucket.casKeyTable.GetPathAndKey(path, casKey, connectionBucket.pathTable, outputOffset);
			u32 pathOffset = bucket.m_pathTable.Add(path.data, path.count);

			#if 0
			StringBuffer<> test;
			bucket.m_pathTable.GetString(test, pathOffset);
			UBA_ASSERT(test.Equals(path.data));
			#endif

			u32 casKeyOffset = bucket.m_casKeyTable.Add(casKey, pathOffset);
			outputs.insert(casKeyOffset);
			bytesForOutput += Get7BitEncodedCount(casKeyOffset);

			if (!m_storage.EnsureCasFile(casKey, nullptr))
			{
				writer.Write7BitEncoded(outputOffset);
				hasAllContent = false;
			}
		}

		newEntry.outputCasKeyOffsets.resize(bytesForOutput);
		BinaryWriter w2(newEntry.outputCasKeyOffsets.data(), 0, newEntry.outputCasKeyOffsets.size());
		for (u32 output : outputs)
			w2.Write7BitEncoded(output);


		newEntry.creationTime = GetSystemTimeAsFileTime() - m_creationTime;
		newEntry.id = cacheEntries.idCounter++;

		if (logLines.empty())
		{
			newEntry.logLinesType = LogLinesType_Empty;
		}
		else if (cacheEntries.sharedLogLines.empty() && logLines.size() < 150) // If log line is very long it is most likely a warning that will be fixed
		{
			cacheEntries.sharedLogLines = std::move(logLines);
			newEntry.logLinesType = LogLinesType_Shared;
		}
		else
		{
			if (cacheEntries.sharedLogLines == logLines)
			{
				newEntry.logLinesType = LogLinesType_Shared;
			}
			else
			{
				newEntry.logLinesType = LogLinesType_Owned;
				newEntry.logLines = std::move(logLines);
			}
		}

		// If cache server has all content we can put the new cache entry directly in the lookup.. otherwise we'll have to wait until client has uploaded content
		if (hasAllContent)
		{
			cacheEntries.entries.emplace_front(std::move(newEntry));
		}
		else
		{
			SCOPED_WRITE_LOCK(connectionBucket.cacheEntryLookupLock, lock3);
			bool res = connectionBucket.cacheEntryLookup.try_emplace(cmdKey, std::move(newEntry)).second;
			UBA_ASSERT(res);(void)res;
		}

		//m_logger.Info(TC("Added new cache entry (%u inputs and %u outputs)"), u32(inputs.size()), outputCount);
		bucket.needsSave = true;

		++m_addsSinceMaintenance;

		return true;
	}

	bool CacheServer::HandleFetchPathTable(BinaryReader& reader, BinaryWriter& writer)
	{
		Bucket& bucket = GetBucket(reader);
		u32 haveSize = reader.ReadU32();
		u32 size = bucket.m_pathTable.GetSize();
		writer.WriteU32(size);
		u32 toSend = Min(u32(writer.GetCapacityLeft()), size - haveSize);
		writer.WriteBytes(bucket.m_pathTable.GetMemory() + haveSize, toSend);
		return true;
	}

	bool CacheServer::HandleFetchCasTable(BinaryReader& reader, BinaryWriter& writer)
	{
		Bucket& bucket = GetBucket(reader);
		u32 haveSize = reader.ReadU32();
		u32 size = bucket.m_casKeyTable.GetSize();
		writer.WriteU32(size);
		u32 toSend = Min(u32(writer.GetCapacityLeft()), size - haveSize);
		writer.WriteBytes(bucket.m_casKeyTable.GetMemory() + haveSize, toSend);
		return true;
	}

	bool CacheServer::HandleFetchEntries(BinaryReader& reader, BinaryWriter& writer, u32 clientVersion)
	{
		Bucket& bucket = GetBucket(reader);
		CasKey cmdKey = reader.ReadCasKey();

		++m_cacheKeyFetchCount;

		SCOPED_READ_LOCK(bucket.m_cacheEntryLookupLock, lock);
		auto findIt = bucket.m_cacheEntryLookup.find(cmdKey);
		if (findIt == bucket.m_cacheEntryLookup.end())
		{
			writer.WriteU16(0);
			return true;
		}
		auto& cacheEntries = findIt->second;
		lock.Leave();

		SCOPED_READ_LOCK(cacheEntries.lock, lock2);
		return cacheEntries.Write(writer, clientVersion, false);
	}

	bool CacheServer::HandleReportUsedEntry(BinaryReader& reader, BinaryWriter& writer, u32 clientVersion)
	{
		Bucket& bucket = GetBucket(reader);
		CasKey cmdKey = reader.ReadCasKey();
		u64 entryId = reader.Read7BitEncoded();

		++m_cacheKeyHitCount;

		SCOPED_READ_LOCK(bucket.m_cacheEntryLookupLock, lock);
		auto findIt = bucket.m_cacheEntryLookup.find(cmdKey);
		if (findIt == bucket.m_cacheEntryLookup.end())
			return true;
		auto& cacheEntries = findIt->second;
		lock.Leave();

		SCOPED_WRITE_LOCK(cacheEntries.lock, lock2);
		for (auto& entry : cacheEntries.entries)
		{
			if (entryId != entry.id)
				continue;
			u64 fileTime = GetSystemTimeAsFileTime() - m_creationTime;
			entry.lastUsedTime = fileTime;
			bucket.lastUsedTime = fileTime;

			if (clientVersion >= 5 && entry.logLinesType == LogLinesType_Owned)
				if (entry.logLines.size() <= writer.GetCapacityLeft())
					writer.WriteBytes(entry.logLines.data(), entry.logLines.size());
			break;
		}
		return true;
	}

	bool CacheServer::HandleExecuteCommand(BinaryReader& reader, BinaryWriter& writer)
	{
		StringBuffer<> command;
		reader.ReadString(command);

		StringBuffer<> additionalInfo;
		reader.ReadString(additionalInfo);

		StringBuffer<> tempFile(m_storage.GetTempPath());
		Guid guid;
		CreateGuid(guid);
		tempFile.Append(GuidToString(guid).str);

		FileAccessor file(m_logger, tempFile.data);
		if (!file.CreateWrite())
			return false;

		bool writeSuccess = true;
		auto Write = [&](const void* data, u64 size) { writeSuccess &= file.Write(data, size); };

		u8 bom[] = {0xEF,0xBB,0xBF}; 
		Write(bom, sizeof(bom));

		auto writeLine = [&](const tchar* text)
			{
				u8 buffer[1024];
				BinaryWriter w(buffer, 0, sizeof(buffer));
				w.WriteUtf8String(text, TStrlen(text));
				w.WriteUtf8String(TC("\n"), 1);
				Write(buffer, w.GetPosition());
			};

		StringBuffer<> line;

		if (command.Equals(TC("content")))
		{
			writeLine(TC("UbaCache server summary"));

			StringBufferBase& filterString = additionalInfo;

			u64 now = GetSystemTimeAsFileTime();

			Vector<u8> temp;

			SCOPED_READ_LOCK(m_bucketsLock, bucketsLock);
			for (auto& kv : m_buckets)
			{
				Bucket& bucket = kv.second;
				SCOPED_READ_LOCK(bucket.m_cacheEntryLookupLock, lock2);

				for (auto& kv2 : bucket.m_cacheEntryLookup)
				{
					CacheEntries& entries = kv2.second;
					SCOPED_READ_LOCK(entries.lock, lock3);

					Set<u32> visibleIndices;
					if (filterString.count)
					{
						u32 index = 0;
						auto findString = [&](const Vector<u8>& offsets)
							{
								BinaryReader reader2(offsets.data(), 0, offsets.size());
								while (reader2.GetLeft())
								{
									u64 offset = reader2.Read7BitEncoded();
									CasKey casKey;
									StringBuffer<> path;
									bucket.m_casKeyTable.GetPathAndKey(path, casKey, bucket.m_pathTable, offset);
									if (path.Contains(filterString.data))
										return true;
									if (Contains(CasKeyString(casKey).str, filterString.data))
										return true;
								}
								return false;
							};

						for (auto& entry : entries.entries)
						{
							entries.Flatten(temp, entry);
							if (findString(temp) || findString(entry.outputCasKeyOffsets))
								visibleIndices.insert(index);
							++index;
						}
						if (visibleIndices.empty())
							continue;
					}


					writeLine(CasKeyString(kv2.first).str);
					u32 index = 0;
					for (auto& entry : entries.entries)
					{
						if (!visibleIndices.empty() && visibleIndices.find(index) == visibleIndices.end())
						{
							++index;
							continue;
						}

						u64 age = GetFileTimeAsTime(now - entry.creationTime);
						writeLine(line.Clear().Appendf(TC("  #%u (%s ago)"), index, TimeToText(age, true).str).data);

						auto writeOffsets = [&](const Vector<u8>& offsets)
							{
								BinaryReader reader2(offsets.data(), 0, offsets.size());
								while (reader2.GetLeft())
								{
									u64 offset = reader2.Read7BitEncoded();
									CasKey casKey;
									StringBuffer<> path;
									bucket.m_casKeyTable.GetPathAndKey(path, casKey, bucket.m_pathTable, offset);
									writeLine(line.Clear().Appendf(TC("    %s - %s"), path.data, CasKeyString(casKey).str).data);
								}
							};

						writeLine(line.Clear().Append(TC("   Inputs:")).data);
						entries.Flatten(temp, entry);
						writeOffsets(temp);
						writeLine(line.Clear().Append(TC("   Outputs:")).data);
						writeOffsets(entry.outputCasKeyOffsets);
						++index;
					}
				}
			}
		}
		else if (command.Equals(TC("status")))
		{
			writeLine(TC("UbaCacheServer status"));
			writeLine(line.Clear().Appendf(TC("  CreationTime: %s ago"), TimeToText(GetFileTimeAsTime(GetSystemTimeAsFileTime() - m_creationTime), true).str).data);
			writeLine(line.Clear().Appendf(TC("  UpTime: %s"), TimeToText(GetTime() - m_bootTime, true).str).data);
			writeLine(line.Clear().Appendf(TC("  Longest maintenance: %s"), TimeToText(m_longestMaintenance).str).data);
			writeLine(line.Clear().Appendf(TC("  Buckets:")).data);
			u32 index = 0;

			{
				SCOPED_READ_LOCK(m_bucketsLock, bucketsLock);
				for (auto& kv : m_buckets)
				{
					Bucket& bucket = kv.second;
					SCOPED_READ_LOCK(bucket.m_cacheEntryLookupLock, lock2);
					u64 mostEntries = 0;
					u64 lastUsed = 0;
					u64 totalEntryCount = 0;
					for (auto& kv2 : bucket.m_cacheEntryLookup)
					{
						CacheEntries& entries = kv2.second;
						SCOPED_READ_LOCK(entries.lock, lock3);
						mostEntries = Max(mostEntries, u64(entries.entries.size()));
						for (auto& entry : entries.entries)
							lastUsed = Max(lastUsed, entry.lastUsedTime);
						totalEntryCount += entries.entries.size();
					}
					lock2.Leave();
					u64 lastUsedTime = 0;
					if (lastUsed)
						lastUsedTime = GetFileTimeAsTime(GetSystemTimeAsFileTime() - (m_creationTime + lastUsed));

					writeLine(line.Clear().Appendf(TC("    #%u - %llu"), index++, kv.first).data);
					writeLine(line.Clear().Appendf(TC("      PathTable: %lls (%s)"), bucket.m_pathTable.GetPathCount(), BytesToText(bucket.m_pathTable.GetSize()).str).data);
					writeLine(line.Clear().Appendf(TC("      CasKeyTable: %llu (%s)"), bucket.m_cacheEntryLookup.size(), BytesToText(bucket.m_casKeyTable.GetSize()).str).data);
					writeLine(line.Clear().Appendf(TC("      TotalEntries: %llu"), totalEntryCount).data);
					writeLine(line.Clear().Appendf(TC("      KeyMostEntries: %llu"), mostEntries).data);
					writeLine(line.Clear().Appendf(TC("      LastEntryUsed: %s ago"), TimeToText(lastUsedTime, true).str).data);
				}
			}
			u64 totalCasSize = 0;
			u64 totalCasCount = 0;
			m_storage.TraverseAllCasFiles([&](const CasKey& casKey, u64 size) { ++totalCasCount; totalCasSize += size; });
			writeLine(line.Clear().Appendf(TC("  CasDb:")).data);
			writeLine(line.Clear().Appendf(TC("    Count: %llu"), totalCasCount).data);
			writeLine(line.Clear().Appendf(TC("    Size: %s"), BytesToText(totalCasSize).str).data);
		}
		else if (command.Equals(TC("obliterate")))
		{
			m_shouldWipe = true;
			m_addsSinceMaintenance = 1;
			writeLine(line.Clear().Appendf(TC("Cache server database obliteration queued!")).data);
		}
		else if (command.Equals(TC("maintenance")))
		{
			m_forceAllSteps = true;
			m_addsSinceMaintenance = 1;
			writeLine(line.Clear().Appendf(TC("Cache server maintenance queued!")).data);
		}
		else
		{
			writeLine(line.Clear().Appendf(TC("Unknown command: %s"), command.data).data);
		}

		Write("", 1);

		if (!writeSuccess || !file.Close())
			return false;

		CasKey key;
		bool deferCreation = false;
		bool fileIsCompressed = false;
		if (!m_storage.StoreCasFile(key, tempFile.data, CasKeyZero, deferCreation, fileIsCompressed))
			return false;

		writer.WriteCasKey(key);

		DeleteFileW(tempFile.data);
		return true;
	}
}
