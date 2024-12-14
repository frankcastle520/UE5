// Copyright Epic Games, Inc. All Rights Reserved.

#include "UbaCacheClient.h"
#include "UbaApplicationRules.h"
#include "UbaCacheEntry.h"
#include "UbaCompactTables.h"
#include "UbaCompressedObjFileHeader.h"
#include "UbaConfig.h"
#include "UbaDirectoryIterator.h"
#include "UbaFileAccessor.h"
#include "UbaNetworkMessage.h"
#include "UbaProcessStartInfo.h"
#include "UbaRootPaths.h"
#include "UbaSession.h"
#include "UbaStorage.h"
#include "UbaStorageUtils.h"

#define UBA_LOG_WRITE_CACHE_INFO 0 // 0 = Disabled, 1 = Normal, 2 = Detailed
#define UBA_LOG_FETCH_CACHE_INFO 0 // 0 = Disabled, 1 = Misses, 2 = Both misses and hits

namespace uba
{
	void CacheClientCreateInfo::Apply(Config& config)
	{
		const ConfigTable* tablePtr = config.GetTable(TC("CacheClient"));
		if (!tablePtr)
			return;
		const ConfigTable& table = *tablePtr;
		table.GetValueAsBool(useDirectoryPreparsing, TC("UseDirectoryPreparsing"));
		table.GetValueAsBool(validateCacheWritesInput, TC("ValidateCacheWritesInput"));
		table.GetValueAsBool(validateCacheWritesOutput, TC("ValidateCacheWritesOutput"));
		table.GetValueAsBool(reportMissReason, TC("ReportMissReason"));
		table.GetValueAsBool(useRoots, TC("UseRoots"));
		table.GetValueAsBool(useCacheHit, TC("UseCacheHit"));
	}


	struct CacheClient::Bucket
	{
		Bucket(u32 id_)
		:	id(id_)
		,	serverPathTable(CachePathTableMaxSize, CompactPathTable::V1, CaseInsensitiveFs)
		,	serverCasKeyTable(CacheCasKeyTableMaxSize)
		,	sendPathTable(CachePathTableMaxSize, CompactPathTable::V1, CaseInsensitiveFs)
		,	sendCasKeyTable(CacheCasKeyTableMaxSize)
		{
		}

		u32 id = 0;

		CompactPathTable serverPathTable;
		CompactCasKeyTable serverCasKeyTable;

		CompactPathTable sendPathTable;
		CompactCasKeyTable sendCasKeyTable;

		ReaderWriterLock pathTableNetworkLock;
		u32 pathTableSizeSent = 0;

		ReaderWriterLock casKeyTableNetworkLock;
		u32 casKeyTableSizeSent = 0;

		Atomic<u32> availableCasKeyTableSize;
	};

	CacheClient::CacheClient(const CacheClientCreateInfo& info)
	:	m_logger(info.writer, TC("UbaCacheClient"))
	,	m_storage(info.storage)
	,	m_client(info.client)
	,	m_session(info.session)
	{
		m_reportMissReason = info.reportMissReason;
		#if UBA_LOG_FETCH_CACHE_INFO
		m_reportMissReason = true;
		#endif

		m_useDirectoryPreParsing = info.useDirectoryPreparsing;
		m_validateCacheWritesInput = info.validateCacheWritesInput;
		m_validateCacheWritesOutput = info.validateCacheWritesOutput;
		m_useCacheHit = info.useCacheHit;
		m_useRoots = info.useRoots;

		m_client.RegisterOnConnected([this]()
			{
				u32 retryCount = 0;
				while (retryCount < 10)
				{
					StackBinaryWriter<1024> writer;
					NetworkMessage msg(m_client, CacheServiceId, CacheMessageType_Connect, writer);
					writer.WriteU32(CacheNetworkVersion);
					StackBinaryReader<1024> reader;
					u64 sendTime = GetTime();
					if (!msg.Send(reader))
					{
						m_logger.Info(TC("Failed to send connect message to cache server (%u). Version mismatch? (%s)"), msg.GetError(), TimeToText(GetTime() - sendTime).str);
						return;
					}
					bool success = reader.ReadBool();
					if (success)
					{
						if (retryCount != 0)
							m_logger.Info(TC("Connected to cache server"));
						m_connected = true;
						return;
					}

					if (retryCount == 0)
					{
						StringBuffer<> reason;
						reader.ReadString(reason);
						m_logger.Info(TC("Cache server busy, retrying... (Reason: %s)"), reason.data);
					}
					Sleep(1000);
					++retryCount;
				}

				m_logger.Info(TC("Failed to connect to cache server after %u retries. Giving up."), retryCount);

			});

		m_client.RegisterOnDisconnected([this]()
			{
				m_connected = false;
			});
	}

	CacheClient::~CacheClient() = default;

	bool CacheClient::WriteToCache(const RootPaths& rootPaths, u32 bucketId, const ProcessStartInfo& info, const u8* inputs, u64 inputsSize, const u8* outputs, u64 outputsSize, const u8* logLines, u64 logLinesSize, u32 processId)
	{
		if (!m_connected)
			return false;

		if (!inputsSize)
			return false;

		CasKey cmdKey = GetCmdKey(rootPaths, info);
		if (cmdKey == CasKeyZero)
		{
			#if UBA_LOG_WRITE_CACHE_INFO
			m_logger.Info(TC("WRITECACHE FAIL: %s"), info.GetDescription());
			#endif
			return false;
		}

		bool finished = false;
		u64 bytesSent = 0;
		if (processId)
			m_session.GetTrace().CacheBeginWrite(processId);
		auto tg = MakeGuard([&]() { if (processId) m_session.GetTrace().CacheEndWrite(processId, finished, bytesSent); });

		BinaryReader inputsReader(inputs, 0, inputsSize);
		BinaryReader outputsReader(outputs, 0, outputsSize);

		Map<u32, u32> inputsStringToCasKey;
		Map<u32, u32> outputsStringToCasKey;
		u32 requiredPathTableSize = 0;
		u32 requiredCasTableSize = 0;
		bool success = true;

		SCOPED_WRITE_LOCK(m_bucketsLock, bucketsLock);
		Bucket& bucket = m_buckets.try_emplace(bucketId, bucketId).first->second;
		bucketsLock.Leave();

		TString qualifiedPath;

		// Traverse all inputs and outputs. to create cache entry that we can send to server
		while (true)
		{
			CasKey casKey;

			StringBuffer<512> path;
			bool isOutput = outputsReader.GetLeft();
			if (isOutput)
				outputsReader.ReadString(path);
			else if (inputsReader.GetLeft())
				inputsReader.ReadString(path);
			else
				break;
			
			if (path.count < 2)
			{
				m_logger.Info(TC("Got messed up path from caller to WriteToCache: %s (%s)"), path.data, info.GetDescription());
				success = false;
			}

			// For .exe and .dll we sometimes get relative paths so we need to expand them to full
			if (path[1] != ':' && (path.EndsWith(TC(".dll")) || path.EndsWith(TC(".exe"))))
			{
				tchar temp[512];
				bool res = SearchPathW(NULL, path.data, NULL, 512, temp, NULL);
				path.Clear().Append(temp);
				if (!res)
				{
					m_logger.Info(TC("Can't find file: %s"), path.data);
					return false;
				}
			}
			else if (ShouldNormalize(path)) // Paths can be absolute in rsp files so we need to normalize those paths
			{
				casKey = rootPaths.NormalizeAndHashFile(m_logger, path.data);
				if (casKey == CasKeyZero)
				{
					success = false;
					continue;
				}
				casKey = IsNormalized(casKey) ? AsCompressed(casKey, true) : CasKeyZero;
			}
			else if (path[path.count-1] == ':')
			{
				m_logger.Info(TC("GOT UNKNOWN RELATIVE PATH: %s (%s)"), path.data, info.GetDescription());
				success = false;
				continue;
			}

			if (m_useRoots)
			{
				// Find root for path in order to be able to normalize it.
				auto root = rootPaths.FindRoot(path);
				if (!root)
				{
					m_logger.Info(TC("FILE WITHOUT ROOT: %s (%s)"), path.data, info.GetDescription());
					success = false;
					continue;
				}

				if (!root->includeInKey)
					continue;

				u32 rootLen = u32(root->path.size());
				qualifiedPath = path.data + rootLen - 1;
				qualifiedPath[0] = tchar(RootPaths::RootStartByte + root->index);
			}
			else
			{
				qualifiedPath = path.data;
			}


			u32 pathOffset = bucket.sendPathTable.Add(qualifiedPath.c_str(), u32(qualifiedPath.size()), &requiredPathTableSize);

			if (!isOutput) // Output files should be removed from input files.. For example when cl.exe compiles pch it reads previous pch file and we don't want it to be input
			{
				if (outputsStringToCasKey.find(pathOffset) != outputsStringToCasKey.end())
					continue;
				//m_logger.Info(TC("INPUT ENTRY: %s -> %u"), qualifiedPath.c_str(), pathOffset);
			}
			else
			{
				inputsStringToCasKey.erase(pathOffset);
				//m_logger.Info(TC("OUT ENTRY: %s -> %u"), qualifiedPath.c_str(), pathOffset);
			}

			auto& stringToCasKey = isOutput ? outputsStringToCasKey : inputsStringToCasKey;
			auto insres = stringToCasKey.try_emplace(pathOffset);
			
			if (!insres.second)
			{
				//m_logger.Warning(TC("Input file %s exists multiple times"), qualifiedPath.c_str()); 
				continue;
			}

			// Get file caskey using storage
			if (casKey == CasKeyZero)
			{
				bool shouldValidate = (m_validateCacheWritesInput && !isOutput) || (m_validateCacheWritesOutput && isOutput);
				bool deferCreation = true;
				bool fileIsCompressed = IsFileCompressed(info, path);

				if (isOutput)
				{
					if (!m_storage.StoreCasFile(casKey, path.data, CasKeyZero, deferCreation, fileIsCompressed))
						return false;
				}
				else
				{
					if (!m_storage.StoreCasKey(casKey, path.data, CasKeyZero, fileIsCompressed))
						return false;
				}

				if (casKey == CasKeyZero) // If file is not found it was a temporary file that was deleted and is not really an output
				{
					
					if (shouldValidate && FileExists(m_logger, path.data))
					{
						m_logger.Warning(TC("CasDb claims file %s does not exist but it does! Will not populate cache for %s"), path.data, info.GetDescription()); 
						return false;
					}

					//m_logger.Warning(TC("Can't find file %s"), path.data); 
					stringToCasKey.erase(insres.first);
					continue; // m_logger.Info(TC("This should never happen! (%s)"), path.data);
				}


				if (shouldValidate)
				{
					FileAccessor fa(m_logger, path.data);
					if (!fa.OpenMemoryRead())
					{
						m_logger.Warning(TC("CasDb claims file %s does exist but can't open it. Will not populate cache for %s"), path.data, info.GetDescription()); 
						return false;
					}

					CasKey oldKey = AsCompressed(casKey, false);
					CasKey newKey;

					u64 fileSize = fa.GetSize();
					u8* fileMem = fa.GetData();

					if (fileSize > sizeof(CompressedObjFileHeader) && ((CompressedObjFileHeader*)fileMem)->IsValid())
						newKey = AsCompressed(((CompressedObjFileHeader*)fileMem)->casKey, false);
					else
						newKey = CalculateCasKey(fileMem, fileSize, false, nullptr, path.data);

					if (newKey != oldKey)
					{
						FileInformation fileInfo;
						fa.GetFileInformationByHandle(fileInfo);

						auto& fileEntry = m_storage.GetOrCreateFileEntry(CaseInsensitiveFs ? ToStringKeyLower(path) : ToStringKey(path));
						SCOPED_READ_LOCK(fileEntry.lock, lock);

						auto ToString = [](bool b) { return b ? TC("true") : TC("false"); };
						m_logger.Warning(TC("CasDb claims file %s has caskey %s but recalculating it gives us %s (FileEntry: %llu/%llu/%s, Real: %llu/%llu). Will not populate cache for %s"),
							path.data, CasKeyString(oldKey).str, CasKeyString(newKey).str, fileEntry.size, fileEntry.lastWritten, ToString(fileEntry.verified), fileSize, fileInfo.lastWriteTime, info.GetDescription());
						return false;
					}
				}
			}

			UBA_ASSERT(IsCompressed(casKey));
			insres.first->second = bucket.sendCasKeyTable.Add(casKey, pathOffset, &requiredCasTableSize);
		}

		if (!success)
			return false;

		if (outputsStringToCasKey.empty())
			m_logger.Warning(TC("NO OUTPUTS FROM process %s"), info.GetDescription()); 

		// Make sure server has enough of the path table to be able to resolve offsets from cache entry
		if (!SendPathTable(bucket, requiredPathTableSize))
			return false;

		// Make sure server has enough of the cas table to be able to resolve offsets from cache entry
		if (!SendCasTable(bucket, requiredCasTableSize))
			return false;

		// actual cache entry now when we know server has the needed tables
		if (!SendCacheEntry(bucket, rootPaths, cmdKey, inputsStringToCasKey, outputsStringToCasKey, logLines, logLinesSize, bytesSent))
			return false;


		#if UBA_LOG_WRITE_CACHE_INFO
		m_logger.BeginScope();
		m_logger.Info(TC("WRITECACHE: %s -> %u %s"), info.GetDescription(), bucketId, CasKeyString(cmdKey).str);
		#if UBA_LOG_WRITE_CACHE_INFO == 2
		for (auto& kv : inputsStringToCasKey)
		{
			StringBuffer<> path;
			CasKey casKey;
			bucket.sendCasKeyTable.GetPathAndKey(path, casKey, bucket.sendPathTable, kv.second);
			m_logger.Info(TC("   IN: %s -> %s"), path.data, CasKeyString(casKey).str);
		}
		for (auto& kv : outputsStringToCasKey)
		{
			StringBuffer<> path;
			CasKey casKey;
			bucket.sendCasKeyTable.GetPathAndKey(path, casKey, bucket.sendPathTable, kv.second);
			m_logger.Info(TC("   OUT: %s -> %s"), path.data, CasKeyString(casKey).str);
		}
		#endif // 2
		m_logger.EndScope();
		#endif

		finished = true;
		return true;
	}

	u64 CacheClient::MakeId(u32 bucketId)
	{
		return u64(bucketId) | ((u64(!CaseInsensitiveFs) + (RootPathsVersion << 1) + (u8(!m_useRoots) << 2)) << 32);
	}


	bool CacheClient::FetchFromCache(CacheResult& outResult, const RootPaths& rootPaths, u32 bucketId, const ProcessStartInfo& info)
	{
		outResult.hit = false;

		if (!m_connected)
			return false;

		CacheStats cacheStats;
		StorageStats storageStats;
		KernelStats kernelStats;
		auto kg = MakeGuard([&]() { KernelStats::GetGlobal().Add(kernelStats); m_storage.AddStats(storageStats); });

		StorageStatsScope __(storageStats);
		KernelStatsScope _(kernelStats);

		CasKey cmdKey = GetCmdKey(rootPaths, info);
		if (cmdKey == CasKeyZero)
			return false;

		u8 memory[SendMaxSize];

		u32 fetchId = m_session.CreateProcessId();
		m_session.GetTrace().CacheBeginFetch(fetchId, info.GetDescription());
		bool success = false;
		auto tg = MakeGuard([&]()
			{
				cacheStats.testEntry.time -= (cacheStats.fetchCasTable.time + cacheStats.normalizeFile.time);
				BinaryWriter writer(memory, 0, sizeof_array(memory));
				cacheStats.Write(writer);
				storageStats.Write(writer);
				kernelStats.Write(writer);
				m_session.GetTrace().CacheEndFetch(fetchId, success, memory, writer.GetPosition());
			});

		BinaryReader reader(memory, 0, sizeof_array(memory));

		SCOPED_WRITE_LOCK(m_bucketsLock, bucketsLock);
		Bucket& bucket = m_buckets.try_emplace(bucketId, bucketId).first->second;
		bucketsLock.Leave();

		{
			TimerScope ts(cacheStats.fetchEntries);
			// Fetch entries.. server will provide as many as fits. TODO: Should it be possible to ask for more entries?
			StackBinaryWriter<32> writer;
			NetworkMessage msg(m_client, CacheServiceId, CacheMessageType_FetchEntries, writer);
			writer.Write7BitEncoded(MakeId(bucket.id));
			writer.WriteCasKey(cmdKey);
			if (!msg.Send(reader))
				return false;
		}

		u32 entryCount = reader.ReadU16();

		#if UBA_LOG_FETCH_CACHE_INFO
		auto mg = MakeGuard([&]()
			{
				if (!success || UBA_LOG_FETCH_CACHE_INFO == 2)
					m_logger.Info(TC("FETCHCACHE %s: %s -> %u %s (%u)"), success ? TC("SUCC") : TC("FAIL"), info.GetDescription(), bucketId, CasKeyString(cmdKey).str, entryCount);
			});
		#endif

		if (!entryCount)
			return false;

		struct MissInfo { TString path; u32 entryIndex; CasKey cache; CasKey local; };
		Vector<MissInfo> misses;

		UnorderedMap<StringKey, CasKey> normalizedCasKeys;
		UnorderedMap<u32, bool> isCasKeyMatchCache;

		auto IsCasKeyMatch = [&](bool& outIsMatch, u32 casKeyOffset, u32 entryIndex, bool useLookup)
			{
				outIsMatch = false;
				StringBuffer<MaxPath> path;

				CasKey cacheCasKey;
				CasKey localCasKey;

				bool* cachedIsMatch = nullptr;
				if (useLookup)
				{
					auto insres = isCasKeyMatchCache.try_emplace(casKeyOffset);
					if (!insres.second)
					{
						outIsMatch = insres.first->second;
						return true;
					}
					cachedIsMatch = &insres.first->second;
				}

				if (!FetchCasTable(bucket, cacheStats, casKeyOffset))
					return false;

				if (!GetLocalPathAndCasKey(bucket, rootPaths, path, cacheCasKey, bucket.serverCasKeyTable, bucket.serverPathTable, casKeyOffset))
					return false;
				UBA_ASSERTF(IsCompressed(cacheCasKey), TC("Cache entry for %s has uncompressed cache key for path %s (%s)"), info.GetDescription(), path.data, CasKeyString(cacheCasKey).str);

				if (IsNormalized(cacheCasKey)) // Need to normalize caskey for these files since they contain absolute paths
				{
					auto insres2 = normalizedCasKeys.try_emplace(ToStringKeyNoCheck(path.data, path.count));
					if (insres2.second)
					{
						TimerScope ts(cacheStats.normalizeFile);
						localCasKey = rootPaths.NormalizeAndHashFile(m_logger, path.data);
						if (localCasKey != CasKeyZero)
							localCasKey = AsCompressed(localCasKey, true);
						insres2.first->second = localCasKey;
					}
					else
						localCasKey = insres2.first->second;

				}
				else
				{
					StringBuffer<MaxPath> forKey;
					forKey.Append(path);
					if (CaseInsensitiveFs)
						forKey.MakeLower();
					StringKey fileNameKey = ToStringKey(forKey);

					if (m_useDirectoryPreParsing)
						PreparseDirectory(fileNameKey, path);

					bool fileIsCompressed = IsFileCompressed(info, path);
					m_storage.StoreCasKey(localCasKey, fileNameKey, path.data, CasKeyZero, fileIsCompressed);
					UBA_ASSERT(localCasKey == CasKeyZero || IsCompressed(localCasKey));
				}

				outIsMatch = localCasKey == cacheCasKey;
				if (useLookup)
					*cachedIsMatch = outIsMatch;

				if (!outIsMatch)
					if (m_reportMissReason && path.count) // if empty this has already been reported
						misses.push_back({TString(path.data), entryIndex, cacheCasKey, localCasKey });
				return true;
			};


		struct Range
		{
			u32 begin;
			u32 end;
		};
		Vector<Range> sharedMatchingRanges;

		const u8* sharedLogLines;
		u64 sharedLogLinesSize;

		// Create ranges out of shared offsets that matches local state
		{
			TimerScope ts(cacheStats.testEntry);
			u64 sharedSize = reader.Read7BitEncoded();

			BinaryReader sharedReader(reader.GetPositionData(), 0, sharedSize);
			reader.Skip(sharedSize);

			sharedLogLinesSize = reader.Read7BitEncoded();
			sharedLogLines = reader.GetPositionData();
			reader.Skip(sharedLogLinesSize);

			u32 rangeBegin = 0;

			auto addRange = [&](u32 rangeEnd)
				{
					if (rangeBegin != rangeEnd)
					{
						auto& range = sharedMatchingRanges.emplace_back();
						range.begin = rangeBegin;
						range.end = rangeEnd;
					}
				};
			while (sharedReader.GetLeft())
			{
				u32 position = u32(sharedReader.GetPosition());
				bool isMatch;
				if (!IsCasKeyMatch(isMatch, u32(sharedReader.Read7BitEncoded()), 0, false))
					return false;

				if (isMatch)
				{
					if (rangeBegin != ~0u)
						continue;
					rangeBegin = position;
				}
				else
				{
					if (rangeBegin == ~0u)
						continue;
					addRange(position);
					rangeBegin = ~0u;
				}
			}
			if (rangeBegin != ~0u)
				addRange(u32(sharedReader.GetPosition()));
			if (sharedMatchingRanges.empty())
			{
				auto& range = sharedMatchingRanges.emplace_back();
				range.begin = 0;
				range.end = 0;
			}
		}

		// Read entries
		{
			--cacheStats.testEntry.count; // Remove the shared one

			BinaryReader entryReader(reader.GetPositionData(), 0, reader.GetLeft());
			u32 entryIndex = 0;
			for (; entryIndex!=entryCount; ++entryIndex)
			{
				u32 entryId = u32(reader.Read7BitEncoded());
				u64 extraSize = reader.Read7BitEncoded();
				BinaryReader extraReader(reader.GetPositionData(), 0, extraSize);
				reader.Skip(extraSize);
				u64 rangeSize = reader.Read7BitEncoded();
				BinaryReader rangeReader(reader.GetPositionData(), 0, rangeSize);
				reader.Skip(rangeSize);
				u64 outSize = reader.Read7BitEncoded();
				BinaryReader outputsReader(reader.GetPositionData(), 0, outSize);
				reader.Skip(outSize);
				
				auto logLinesType = LogLinesType(reader.ReadByte());

				{
					TimerScope ts(cacheStats.testEntry);

					bool isMatch = true;

					// Check ranges first
					auto sharedRangeIt = sharedMatchingRanges.begin();
					while (isMatch && rangeReader.GetLeft())
					{
						u64 begin = rangeReader.Read7BitEncoded();
						u64 end = rangeReader.Read7BitEncoded();
					
						Range matchingRange = *sharedRangeIt;

						while (matchingRange.end <= begin)
						{
							++sharedRangeIt;
							if (sharedRangeIt == sharedMatchingRanges.end())
								break;
							matchingRange = *sharedRangeIt;
						}

						isMatch = matchingRange.begin <= begin && matchingRange.end >= end;
					}

					// Check extra keys after
					while (isMatch && extraReader.GetLeft())
						if (!IsCasKeyMatch(isMatch, u32(extraReader.Read7BitEncoded()), entryIndex, true))
							return false;

					if (!isMatch)
						continue;
				}

				if (!m_useCacheHit)
					return false;


				if (logLinesType == LogLinesType_Shared)
					if (!PopulateLogLines(outResult.logLines, sharedLogLines, sharedLogLinesSize))
						return false;

				if (!ReportUsedEntry(outResult.logLines, logLinesType == LogLinesType_Owned, bucket, cmdKey, entryId))
					return false;

				// Fetch output files from cache (and some files need to be "denormalized" before written to disk

				struct DowngradedLogger : public LoggerWithWriter
				{
					DowngradedLogger(LogWriter& writer, const tchar* prefix) : LoggerWithWriter(writer, prefix) {}
					virtual void Log(LogEntryType type, const tchar* str, u32 strLen) override { LoggerWithWriter::Log(Max(type, LogEntryType_Info), str, strLen); }
				};

				while (outputsReader.GetLeft())
				{
					u32 casKeyOffset = u32(outputsReader.Read7BitEncoded());
					if (!FetchCasTable(bucket, cacheStats, casKeyOffset))
						return false;

					TimerScope fts(cacheStats.fetchOutput);

					StringBuffer<MaxPath> path;
					CasKey casKey;
					if (!GetLocalPathAndCasKey(bucket, rootPaths, path, casKey, bucket.serverCasKeyTable, bucket.serverPathTable, casKeyOffset))
						return false;
					UBA_ASSERT(IsCompressed(casKey));

					FileFetcher fetcher { m_storage.m_bufferSlots, storageStats };
					fetcher.m_errorOnFail = false;

					if (IsNormalized(casKey))
					{
						DowngradedLogger logger(m_logger.m_writer, TC("UbaCacheClientNormalizedDownload"));
						// Fetch into memory, file is in special format without absolute paths
						MemoryBlock normalizedBlock(4*1024*1024);
						bool destinationIsCompressed = false;
						if (!fetcher.RetrieveFile(logger, m_client, casKey, path.data, destinationIsCompressed, &normalizedBlock))
							return logger.Error(TC("Failed to download cache output for %s"), info.GetDescription());

						MemoryBlock localBlock(4*1024*1024);

						u32 rootOffsets = *(u32*)(normalizedBlock.memory);
						char* fileStart = (char*)(normalizedBlock.memory + sizeof(u32));
						UBA_ASSERT(rootOffsets <= normalizedBlock.writtenSize);

						// "denormalize" fetched file into another memory block that will be written to disk
						u64 lastWritten = 0;
						BinaryReader reader2(normalizedBlock.memory, rootOffsets, normalizedBlock.writtenSize);
						while (reader2.GetLeft())
						{
							u64 rootOffset = reader2.Read7BitEncoded();
							if (u64 toWrite = rootOffset - lastWritten)
								memcpy(localBlock.Allocate(toWrite, 1, TC("")), fileStart + lastWritten, toWrite);
							u8 rootIndex = fileStart[rootOffset] - RootPaths::RootStartByte;
							const TString& root = rootPaths.GetRoot(rootIndex);
							if (root.empty())
								return logger.Error(TC("Cache entry uses root path index %u which is not set for this startupinfo (%s)"), rootIndex, info.GetDescription());

							#if PLATFORM_WINDOWS
							StringBuffer<> pathTemp;
							pathTemp.Append(root);
							char rootPath[512];
							u32 rootPathLen = pathTemp.Parse(rootPath, sizeof_array(rootPath));
							#else
							const char* rootPath = root.data();
							u32 rootPathLen = root.size();
							#endif

							if (u32 toWrite = rootPathLen - 1)
								memcpy(localBlock.Allocate(toWrite, 1, TC("")), rootPath, toWrite);
							lastWritten = rootOffset + 1;
						}

						u64 fileSize = rootOffsets - sizeof(u32);
						if (u64 toWrite = fileSize - lastWritten)
							memcpy(localBlock.Allocate(toWrite, 1, TC("")), fileStart + lastWritten, toWrite);

						FileAccessor destFile(logger, path.data);

						bool useFileMapping = true;
						if (useFileMapping)
						{
							if (!destFile.CreateMemoryWrite(false, DefaultAttributes(), localBlock.writtenSize))
								return logger.Error(TC("Failed to create file for cache output %s for %s"), path.data, info.GetDescription());
							MapMemoryCopy(destFile.GetData(), localBlock.memory, localBlock.writtenSize);
						}
						else
						{
							if (!destFile.CreateWrite())
								return logger.Error(TC("Failed to create file for cache output %s for %s"), path.data, info.GetDescription());
							if (!destFile.Write(localBlock.memory, localBlock.writtenSize))
								return false;
						}
						if (!destFile.Close(&fetcher.lastWritten))
							return false;

						fetcher.sizeOnDisk = localBlock.writtenSize;
						casKey = CalculateCasKey(localBlock.memory, localBlock.writtenSize, false, nullptr, path.data);
					}
					else
					{
						DowngradedLogger logger(m_logger.m_writer, TC("UbaCacheClientDownload"));
						bool destinationIsCompressed = IsFileCompressed(info, path);
						if (!fetcher.RetrieveFile(logger, m_client, casKey, path.data, destinationIsCompressed))
							return logger.Error(TC("Failed to download cache output %s for %s"), path.data, info.GetDescription());
					}

					cacheStats.fetchBytesRaw += fetcher.sizeOnDisk;
					cacheStats.fetchBytesComp += fetcher.bytesReceived;

					if (!m_storage.FakeCopy(casKey, path.data, fetcher.sizeOnDisk, fetcher.lastWritten, false))
						return false;
					if (!m_session.RegisterNewFile(path.data))
						return false;
				}
				outResult.hit = true;
				success = true;
				return true;
			}
		}

		for (auto& miss : misses)
			m_logger.Info(TC("Cache miss on %s because of mismatch of %s (entry: %u, local: %s cache: %s)"), info.GetDescription(), miss.path.data(), miss.entryIndex, CasKeyString(miss.local).str, CasKeyString(miss.cache).str);

		return false;
	}

	bool CacheClient::RequestServerShutdown(const tchar* reason)
	{
		StackBinaryWriter<1024> writer;
		NetworkMessage msg(m_client, CacheServiceId, CacheMessageType_RequestShutdown, writer);
		writer.WriteString(reason);
		StackBinaryReader<512> reader;
		if (!msg.Send(reader))
			return false;
		return reader.ReadBool();
	}

	bool CacheClient::ExecuteCommand(Logger& logger, const tchar* command, const tchar* destinationFile, const tchar* additionalInfo)
	{
		StackBinaryWriter<1024> writer;
		NetworkMessage msg(m_client, CacheServiceId, CacheMessageType_ExecuteCommand, writer);
		writer.WriteString(command);
		writer.WriteString(additionalInfo ? additionalInfo : TC(""));

		CasKey statusFileCasKey;
		{
			StackBinaryReader<512> reader;
			if (!msg.Send(reader))
				return false;
			statusFileCasKey = reader.ReadCasKey();
			if (statusFileCasKey == CasKeyZero)
				return false;
		}

		StorageStats storageStats;
		FileFetcher fetcher { m_storage.m_bufferSlots, storageStats };
		bool destinationIsCompressed = false;
		if (destinationFile)
		{
			if (!fetcher.RetrieveFile(m_logger, m_client, statusFileCasKey, destinationFile, destinationIsCompressed))
				return false;
		}
		else
		{
			MemoryBlock block(4*1024*1024);
			if (!fetcher.RetrieveFile(m_logger, m_client, statusFileCasKey, TC("CommandString"), destinationIsCompressed, &block))
				return false;
			BinaryReader reader(block.memory, 3, block.writtenSize); // Skipping bom

			tchar line[1024];
			tchar* it = line;
			while (true)
			{
				tchar c = reader.ReadUtf8Char<tchar>();
				if (c != '\n' && c != 0)
				{
					*it++ = c;
					continue;
				}

				if (c == 0 && it == line)
					break;
				*it = 0;
				logger.Log(LogEntryType_Info, line, u32(it - line));
				it = line;
				if (c == 0)
					break;
			}
		}
		return true;
	}

	bool CacheClient::SendPathTable(Bucket& bucket, u32 requiredPathTableSize)
	{
		SCOPED_WRITE_LOCK(bucket.pathTableNetworkLock, lock);
		if (requiredPathTableSize <= bucket.pathTableSizeSent)
			return true;

		u32 left = requiredPathTableSize - bucket.pathTableSizeSent;
		while (left)
		{
			StackBinaryWriter<SendMaxSize> writer;
			NetworkMessage msg(m_client, CacheServiceId, CacheMessageType_StorePathTable, writer);
			writer.Write7BitEncoded(MakeId(bucket.id));
			u32 toSend = Min(requiredPathTableSize - bucket.pathTableSizeSent, u32(m_client.GetMessageMaxSize() - 32));
			left -= toSend;
			writer.WriteBytes(bucket.sendPathTable.GetMemory() + bucket.pathTableSizeSent, toSend);
			bucket.pathTableSizeSent += toSend;

			StackBinaryReader<16> reader;
			if (!msg.Send(reader))
				return false;
		}
		return true;
	}

	bool CacheClient::SendCasTable(Bucket& bucket, u32 requiredCasTableSize)
	{
		SCOPED_WRITE_LOCK(bucket.casKeyTableNetworkLock, lock);
		if (requiredCasTableSize <= bucket.casKeyTableSizeSent)
			return true;

		u32 left = requiredCasTableSize - bucket.casKeyTableSizeSent;
		while (left)
		{
			StackBinaryWriter<SendMaxSize> writer;
			NetworkMessage msg(m_client, CacheServiceId, CacheMessageType_StoreCasTable, writer);
			writer.Write7BitEncoded(MakeId(bucket.id));
			u32 toSend = Min(requiredCasTableSize - bucket.casKeyTableSizeSent, u32(m_client.GetMessageMaxSize() - 32));
			left -= toSend;
			writer.WriteBytes(bucket.sendCasKeyTable.GetMemory() + bucket.casKeyTableSizeSent, toSend);
			bucket.casKeyTableSizeSent += toSend;

			StackBinaryReader<16> reader;
			if (!msg.Send(reader))
				return false;
		}
		return true;
	}

	bool CacheClient::SendCacheEntry(Bucket& bucket, const RootPaths& rootPaths, const CasKey& cmdKey, const Map<u32, u32>& inputsStringToCasKey, const Map<u32, u32>& outputsStringToCasKey, const u8* logLines, u64 logLinesSize, u64& outBytesSent)
	{
		StackBinaryReader<1024> reader;
		{
			StackBinaryWriter<SendMaxSize> writer;

			NetworkMessage msg(m_client, CacheServiceId, CacheMessageType_StoreEntry, writer);
			writer.Write7BitEncoded(MakeId(bucket.id));
			writer.WriteCasKey(cmdKey);

			writer.Write7BitEncoded(inputsStringToCasKey.size());
			writer.Write7BitEncoded(outputsStringToCasKey.size());
			for (auto& kv : outputsStringToCasKey)
				writer.Write7BitEncoded(kv.second);

			for (auto& kv : inputsStringToCasKey)
				writer.Write7BitEncoded(kv.second);

			if (logLinesSize)
				if (writer.GetCapacityLeft() > logLinesSize + Get7BitEncodedCount(logLinesSize))
					writer.WriteBytes(logLines, logLinesSize);

			if (!msg.Send(reader))
				return false;
		}

		// Server has all content for caskeys.. upload is done
		if (!reader.GetLeft())
			return true;

		bool success = false;
		auto doneGuard = MakeGuard([&]()
			{
				// Send done.. confirm to server
				StackBinaryWriter<SendMaxSize> writer;
				NetworkMessage msg(m_client, CacheServiceId, CacheMessageType_StoreEntryDone, writer);
				writer.Write7BitEncoded(MakeId(bucket.id));
				writer.WriteCasKey(cmdKey);
				writer.WriteBool(success);
				return msg.Send(reader);
			});

		// There is content we need to upload to server
		while (reader.GetLeft())
		{
			u32 casKeyOffset = u32(reader.Read7BitEncoded());

			StringBuffer<MaxPath> path;
			CasKey casKey;
			if (!GetLocalPathAndCasKey(bucket, rootPaths, path, casKey, bucket.sendCasKeyTable, bucket.sendPathTable, casKeyOffset))
				return false;

			casKey = AsCompressed(casKey, true);

			StorageImpl::CasEntry* casEntry;
			if (m_storage.HasCasFile(casKey, &casEntry))
			{
				UBA_ASSERT(!IsNormalized(casKey));
				StringBuffer<> casKeyFileName;
				if (!m_storage.GetCasFileName(casKeyFileName, casKey))
					return false;

				const u8* fileData;
				u64 fileSize;

				MappedView mappedView;
				auto mapViewGuard = MakeGuard([&](){ m_storage.m_casDataBuffer.UnmapView(mappedView, path.data); });

				FileAccessor file(m_logger, casKeyFileName.data);

				if (casEntry->mappingHandle.IsValid()) // If file was created by helper it will be in the transient mapped memory
				{
					mappedView = m_storage.m_casDataBuffer.MapView(casEntry->mappingHandle, casEntry->mappingOffset, casEntry->mappingSize, path.data);
					fileData = mappedView.memory;
					fileSize = mappedView.size;
				}
				else
				{
					if (!file.OpenMemoryRead())
						return false;
					fileData = file.GetData();
					fileSize = file.GetSize();
				}

				if (!SendFile(m_logger, m_client, casKey, fileData, fileSize, casKeyFileName.data))
					return false;

				outBytesSent += fileSize;
			}
			else // If we don't have the cas key it should be one of the normalized files.... otherwise there is a bug
			{
				if (!IsNormalized(casKey))
					return m_logger.Error(TC("Can't find output file %s to send to cache server"), path.data);

				FileAccessor file(m_logger, path.data);
				if (!file.OpenMemoryRead())
					return false;
				MemoryBlock block(AlignUp(file.GetSize() + 16, 64*1024));
				u32& rootOffsetsStart = *(u32*)block.Allocate(sizeof(u32), 1, TC(""));
				rootOffsetsStart = 0;
				Vector<u32> rootOffsets;
				u32 rootOffsetsSize = 0;

				auto handleString = [&](const char* str, u64 strLen, u32 rootPos)
					{
						void* mem = block.Allocate(strLen, 1, TC(""));
						memcpy(mem, str, strLen);
						if (rootPos != ~0u)
						{
							rootOffsets.push_back(rootPos);
							rootOffsetsSize += Get7BitEncodedCount(rootPos);
						}
					};

				if (!rootPaths.NormalizeString<char>(m_logger, (const char*)file.GetData(), file.GetSize(), handleString, path.data))
					return false;

				if (rootOffsetsSize)
				{
					u8* mem = (u8*)block.Allocate(rootOffsetsSize, 1, TC(""));
					rootOffsetsStart = u32(mem - block.memory);
					BinaryWriter writer(mem, 0, rootOffsetsSize);
					for (u32 rootOffset : rootOffsets)
						writer.Write7BitEncoded(rootOffset);
				}
				else
					rootOffsetsStart = u32(block.writtenSize);


				auto& s = m_storage;
				FileSender sender { m_logger, m_client, s.m_bufferSlots, s.Stats(), m_sendOneAtTheTimeLock, s.m_casCompressor, s.m_casCompressionLevel };

				u8* dataToSend = block.memory;
				u64 sizeToSend = block.writtenSize;

				if (!sender.SendFileCompressed(casKey, path.data, dataToSend, sizeToSend, TC("SendCacheEntry")))
					return m_logger.Error(TC("Failed to send cas content for file %s"), path.data);

				outBytesSent += sender.m_bytesSent;
			}

		}

		success = true;
		return doneGuard.Execute();
	}

	bool CacheClient::FetchCasTable(Bucket& bucket, CacheStats& stats, u32 requiredCasTableOffset)
	{
		auto hasEnoughData = [&bucket, requiredCasTableOffset](u32 tableSize)
			{
				u32 neededSize = requiredCasTableOffset + 4;
				if (neededSize > tableSize)
					return false;
				BinaryReader r(bucket.serverCasKeyTable.GetMemory(), requiredCasTableOffset, tableSize);
				u8 bytesNeeded = Get7BitEncodedCount(r.Read7BitEncoded());
				neededSize = requiredCasTableOffset + bytesNeeded + sizeof(CasKey);
				return neededSize <= tableSize;
			};

		if (hasEnoughData(bucket.availableCasKeyTableSize))
			return true;

		TimerScope ts2(stats.fetchCasTable);

		StackBinaryReader<SendMaxSize> reader;

		SCOPED_WRITE_LOCK(bucket.casKeyTableNetworkLock, lock); // Use one lock over both queries
		{
			bool messageSent = false;
			while (true)
			{
				u32 tableSize = bucket.serverCasKeyTable.GetSize();
				if (hasEnoughData(tableSize))
				{
					if (!messageSent)
						return true;
					break;
				}

				StackBinaryWriter<16> writer;
				NetworkMessage msg(m_client, CacheServiceId, CacheMessageType_FetchCasTable, writer);
				writer.Write7BitEncoded(MakeId(bucket.id));
				writer.WriteU32(tableSize);

				reader.Reset();
				if (!msg.Send(reader))
					return false;
				reader.ReadU32();
				messageSent = true;
				bucket.serverCasKeyTable.ReadMem(reader, false);
			}
		}
		{
			u32 targetSize = ~0u; // For now, read all because we don't know how much we need (it would require parsing all path offsets in caskey table)
			while (bucket.serverPathTable.GetSize() < targetSize)
			{
				StackBinaryWriter<16> writer;
				NetworkMessage msg(m_client, CacheServiceId, CacheMessageType_FetchPathTable, writer);
				writer.Write7BitEncoded(MakeId(bucket.id));
				writer.WriteU32(bucket.serverPathTable.GetSize());

				reader.Reset();
				if (!msg.Send(reader))
					return false;
				u32 size = reader.ReadU32();
				if (targetSize == ~0u)
					targetSize = size;

				bucket.serverPathTable.ReadMem(reader, false);
			}
		}

		bucket.availableCasKeyTableSize = bucket.serverCasKeyTable.GetSize();
		return true;
	}

	bool CacheClient::ReportUsedEntry(Vector<ProcessLogLine>& outLogLines, bool ownedLogLines, Bucket& bucket, const CasKey& cmdKey, u32 entryId)
	{
		StackBinaryWriter<128> writer;
		NetworkMessage msg(m_client, CacheServiceId, CacheMessageType_ReportUsedEntry, writer);
		writer.Write7BitEncoded(MakeId(bucket.id));
		writer.WriteCasKey(cmdKey);
		writer.Write7BitEncoded(entryId);

		if (!ownedLogLines)
			return msg.Send();

		StackBinaryReader<SendMaxSize> reader;
		if (!msg.Send(reader))
			return false;

		return PopulateLogLines(outLogLines, reader.GetPositionData(), reader.GetLeft());
	}

	bool CacheClient::PopulateLogLines(Vector<ProcessLogLine>& outLogLines, const u8* mem, u64 memLen)
	{
		BinaryReader reader(mem, 0, memLen);
		while (reader.GetLeft())
		{
			auto& logLine = outLogLines.emplace_back();
			logLine.text = reader.ReadString();
			logLine.type = LogEntryType(reader.ReadByte());
		}
		return true;
	}

	CasKey CacheClient::GetCmdKey(const RootPaths& rootPaths, const ProcessStartInfo& info)
	{
		CasKeyHasher hasher;

		
		#if PLATFORM_WINDOWS
		// cmd.exe is special.. we can't hash it because it might be different on different os versions but should do the same thing regardless of version
		if (Contains(info.application, TC("cmd.exe")))
		{
			hasher.Update(TC("cmd.exe"), 7*sizeof(tchar));
		}
		else
		#endif
		{
			// Add hash of application binary to key
			CasKey applicationCasKey;
			bool fileIsCompressed = false;
			if (!m_storage.StoreCasKey(applicationCasKey, info.application, CasKeyZero, fileIsCompressed))
				return CasKeyZero;
			hasher.Update(&applicationCasKey, sizeof(CasKey));
		}

		// Add arguments list to key
		auto hashString = [&](const tchar* str, u64 strLen, u32 rootPos) { hasher.Update(str, strLen*sizeof(tchar)); };
		if (!rootPaths.NormalizeString(m_logger, info.arguments, TStrlen(info.arguments), hashString, TC("CmdKey "), info.GetDescription()))
			return CasKeyZero;

		// Add content of rsp file to key (This will cost a bit of perf since we need to normalize.. should this be part of key?)
		if (auto rspStart = TStrchr(info.arguments, '@'))
		{
			if (rspStart[1] == '"')
			{
				rspStart += 2;
				if (auto rspEnd = TStrchr(rspStart, '"'))
				{
					StringBuffer<MaxPath> workingDir(info.workingDir);
					workingDir.EnsureEndsWithSlash();
					StringBuffer<> rsp;
					rsp.Append(rspStart, rspEnd - rspStart);
					StringBuffer<> fullPath;
					FixPath(rsp.data, workingDir.data, workingDir.count, fullPath);
					CasKey rspCasKey = rootPaths.NormalizeAndHashFile(m_logger, rsp.data);
					hasher.Update(&rspCasKey, sizeof(CasKey));
				}
			}
		}

		return ToCasKey(hasher, false);
	}

	bool CacheClient::ShouldNormalize(const StringBufferBase& path)
	{
		if (!m_useRoots)
			return false;
		if (path.EndsWith(TC(".dep.json"))) // Contains absolute paths (dep file for msvc)
			return true;
		if (path.EndsWith(TC(".d"))) // Contains absolute paths (dep file for clang)
			return true;
		if (path.EndsWith(TC(".tlh"))) // Contains absolute path in a comment
			return true;
		if (path.EndsWith(TC(".rsp"))) // Contains absolute paths in some cases
			return true;
		if (path.EndsWith(TC(".bat"))) // Contains absolute paths in some cases
			return true;
		return false;
	}

	bool CacheClient::GetLocalPathAndCasKey(Bucket& bucket, const RootPaths& rootPaths, StringBufferBase& outPath, CasKey& outKey, CompactCasKeyTable& casKeyTable, CompactPathTable& pathTable, u32 offset)
	{
		SCOPED_READ_LOCK(bucket.casKeyTableNetworkLock, lock); // TODO: Is this needed?

		StringBuffer<MaxPath> normalizedPath;
		casKeyTable.GetPathAndKey(normalizedPath, outKey, pathTable, offset);
		UBA_ASSERT(normalizedPath.count);

		u32 rootIndex = normalizedPath[0] - RootPaths::RootStartByte;
		const TString& root = rootPaths.GetRoot(rootIndex);

		outPath.Append(root).Append(normalizedPath.data + u32(m_useRoots)); // If we use root paths, then first byte is root path table index
		return true;
	}

	bool CacheClient::IsFileCompressed(const ProcessStartInfo& info, const StringView& filename)
	{
		if (!m_session.ShouldStoreObjFilesCompressed())
			return false;
		auto rules = info.rules;
		if (!rules)
			rules = m_session.GetRules(info);
		return rules->StoreFileCompressed(filename);
	}

	void CacheClient::PreparseDirectory(const StringKey& fileNameKey, const StringBufferBase& filePath)
	{
		const tchar* lastSep = filePath.Last(PathSeparator);
		if (!lastSep)
			return;

		StringBuffer<MaxPath> path;
		path.Append(filePath.data, lastSep - filePath.data);
		if (CaseInsensitiveFs)
			path.MakeLower();

		StringKeyHasher dirHasher;
		dirHasher.Update(path.data, path.count);
		StringKey pathKey = ToStringKey(dirHasher);

		SCOPED_WRITE_LOCK(m_directoryPreparserLock, preparserLock);
		auto insres = m_directoryPreparser.try_emplace(pathKey);
		PreparedDir& dir = insres.first->second;
		preparserLock.Leave();

		SCOPED_WRITE_LOCK(dir.lock, preparserLock2);
		if (dir.done)
			return;

		dir.done = true;

		// It is likely this folder has already been handled by session if this file is verified
		if (m_storage.IsFileVerified(fileNameKey))
			return;

		// Traverse all files in directory and report the file information... but only if it has not been reported before.. we don't want to interfere with other reports
		TraverseDir(m_logger, path.data, 
			[&](const DirectoryEntry& e)
			{
				if (IsDirectory(e.attributes))
					return;

				path.Clear().Append('\\').Append(e.name, e.nameLen);
				if (CaseInsensitiveFs)
					path.MakeLower();

				StringKey fileNameKey = ToStringKey(dirHasher, path.data, path.count);
				m_storage.ReportFileInfoWeak(fileNameKey, e.lastWritten, e.size);
			});
	}
}
