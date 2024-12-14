// Copyright Epic Games, Inc. All Rights Reserved.

#include "UbaStorageClient.h"
#include "UbaConfig.h"
#include "UbaDirectoryIterator.h"
#include "UbaFileAccessor.h"
#include "UbaNetworkClient.h"
#include "UbaNetworkMessage.h"
#include "UbaNetworkBackendTcp.h"
#include "UbaStorageUtils.h"

namespace uba
{
	void StorageClientCreateInfo::Apply(Config& config)
	{
		StorageCreateInfo::Apply(config);

		const ConfigTable* tablePtr = config.GetTable(TC("Storage"));
		if (!tablePtr)
			return;
		const ConfigTable& table = *tablePtr;
		table.GetValueAsBool(sendCompressed, TC("SendCompressed"));
		table.GetValueAsBool(allowProxy, TC("AllowProxy"));
	}

	StorageClient::StorageClient(const StorageClientCreateInfo& info)
	:	StorageImpl(info, TC("UbaStorageClient"))
	,	m_client(info.client)
	,	m_sendCompressed(info.sendCompressed)
	,	m_allowProxy(info.allowProxy)
	,	m_zone(info.zone)
	,	m_getProxyBackendCallback(info.getProxyBackendCallback)
	,	m_getProxyBackendUserData(info.getProxyBackendUserData)
	,	m_startProxyCallback(info.startProxyCallback)
	,	m_startProxyUserData(info.startProxyUserData)
	,	m_proxyPort(info.proxyPort)
	{
	}

	struct StorageClient::ProxyClient
	{
		ProxyClient(bool& outCtorSuccess, const NetworkClientCreateInfo& info) : client(outCtorSuccess, info, TC("UbaProxyClient")) {}
		~ProxyClient() { client.Disconnect(); }
		NetworkClient client;
		u32 refCount = 0;
	};

	bool StorageClient::Start()
	{
		m_client.RegisterOnConnected([this]()
			{
				StackBinaryWriter<1024> writer;
				NetworkMessage msg(m_client, ServiceId, StorageMessageType_Connect, writer);
				writer.WriteString(TC("Client"));
				writer.WriteU32(StorageNetworkVersion);
				writer.WriteBool(false); // Is Proxy
				writer.WriteU16(m_proxyPort);
				writer.WriteString(m_zone);
				writer.WriteU64(m_casTotalBytes);

				TraverseNetworkAddresses(m_logger, [&](const StringBufferBase& addr)
					{
						writer.WriteString(addr);
						return false;
					});
				writer.WriteString(TC(""));


				StackBinaryReader<1024> reader;
				if (!msg.Send(reader))
					return;

				m_storageServerUid = reader.ReadGuid();
				m_casCompressor = reader.ReadByte();
				m_casCompressionLevel = reader.ReadByte();

			});

		m_client.RegisterOnDisconnected([this]() { m_logger.isMuted = true; });
		return true;
	}

	StorageClient::~StorageClient()
	{
		delete m_proxyClient;
		for (auto& pair : m_localStorageFiles)
			CloseFileMapping(pair.second.casEntry.mappingHandle);
	}

	bool StorageClient::IsUsingProxy()
	{
		SCOPED_READ_LOCK(m_proxyClientLock, proxyLock);
		return m_proxyClient != nullptr;
	}

	void StorageClient::StopProxy()
	{
		SCOPED_WRITE_LOCK(m_proxyClientLock, proxyLock);
		if (m_proxyClient)
			m_proxyClient->client.Disconnect();
	}

	bool StorageClient::PopulateCasFromDirs(const DirVector& directories, u32 workerCount, const Function<bool()>& shouldExit)
	{
		if (directories.empty())
			return true;

		u64 start = GetTime();

		WorkManagerImpl workManager(workerCount);
		ReaderWriterLock lock;
		bool success = true;
		UnorderedSet<u64> seenIds;
		ReaderWriterLock seenIdsLock;

		for (auto& dir : directories)
			success = PopulateCasFromDirsRecursive(dir.c_str(), workManager, seenIds, seenIdsLock, shouldExit) && success;
		workManager.FlushWork();

		if (u32 fileCount = u32(m_localStorageFiles.size()))
			m_logger.Info(TC("Prepopulated %u files to cas in %s"), fileCount, TimeToText(GetTime() - start).str);

		return success;
	}

#if !UBA_USE_SPARSEFILE
	bool StorageClient::GetCasFileName(StringBufferBase& out, const CasKey& casKey)
	{
		SCOPED_READ_LOCK(m_localStorageFilesLock, tempLock);
		auto findIt = m_localStorageFiles.find(AsCompressed(casKey, false));
		if (findIt != m_localStorageFiles.end())
		{
			if (findIt->second.casEntry.mappingHandle.IsValid())
				GetMappingString(out, findIt->second.casEntry.mappingHandle, 0);
			else
				out.Append(findIt->second.fileName);
			return true;
		}
		tempLock.Leave();

		return StorageImpl::GetCasFileName(out, casKey);
	}
#endif

	MappedView StorageClient::MapView(const CasKey& casKey, const tchar* hint)
	{
		SCOPED_READ_LOCK(m_localStorageFilesLock, tempLock);
		auto findIt = m_localStorageFiles.find(AsCompressed(casKey, false));
		bool isValid = findIt != m_localStorageFiles.end();
		tempLock.Leave();
		if (!isValid)
			return StorageImpl::MapView(casKey, hint);
		LocalFile& file = findIt->second;

		MappedView view;
		view.handle = file.casEntry.mappingHandle;
		view.size = file.casEntry.size;
		view.offset = 0;
		view.isCompressed = false;
		return view;
	}

	bool StorageClient::GetZone(StringBufferBase& out)
	{
		if (m_zone.empty())
			return false;
		out.Append(m_zone);
		return true;
	}

	bool StorageClient::RetrieveCasFile(RetrieveResult& out, const CasKey& casKey, const tchar* hint, FileMappingBuffer* mappingBuffer, u64 memoryMapAlignment, bool allowProxy)
	{
		FileMappingType mappingType = MappedView_Transient;
		bool shouldStore = mappingBuffer == nullptr;
		#if UBA_USE_SPARSEFILE
		if (shouldStore)
		{
			mappingType = MappedView_Persistent;
			mappingBuffer = &m_casDataBuffer;
		}
		#endif

		UBA_ASSERT(AsCompressed(casKey, false) != CasKeyZero);

		out.casKey = casKey;
		out.size = InvalidValue;

		// Cas file might have been created by this client and in that case we can just reuse the file we just wrote. No need to fetch from server
		// This needs to be first so it doesnt end up in the cas table even though it is not in there. (otherwise it might be garbage collected)
		SCOPED_READ_LOCK(m_localStorageFilesLock, tempLock);
		auto findIt = m_localStorageFiles.find(AsCompressed(casKey, false));
		if (findIt != m_localStorageFiles.end())
		{
			out.casKey = findIt->first;
			LocalFile& mf = findIt->second;
			out.size = mf.casEntry.size;
			out.view.handle = mf.casEntry.mappingHandle;
			out.view.size = mf.casEntry.size;
			out.view.isCompressed = false;
			return true;
		}
		tempLock.Leave();

		StorageStats& stats = Stats();
		CasEntry* casEntry = nullptr;
		auto casEntryLock = MakeGuard([&]() { if (casEntry) casEntry->lock.LeaveWrite(); });
		if (shouldStore)
		{
			TimerScope ts(stats.ensureCas);

			if (EnsureCasFile(casKey, nullptr))
				return true;

			SCOPED_READ_LOCK(m_casLookupLock, lock);
			casEntry = &m_casLookup.find(casKey)->second;
			lock.Leave();

			casEntry->lock.EnterWrite();
			if (casEntry->verified && casEntry->exists)
				return true;

			casEntry->dropped = false;  // In case this comes from a retry where previous cas was dropped
			casEntry->verified = true;
		}

		TimerScope ts2(stats.recvCas);

		StringBuffer<> casFile;
		#if !UBA_USE_SPARSEFILE
		GetCasFileName(casFile, casKey);
		#else
		casFile.Append(CasKeyString(casKey).str);
		#endif

		u8* slot = m_bufferSlots.Pop();
		auto slotGuard = MakeGuard([&](){ m_bufferSlots.Push(slot); });

		MappedView mappedView;
		auto mvg = MakeGuard([&]() { if (mappingBuffer) mappingBuffer->UnmapView(mappedView, hint); });
		u8* writeMem = nullptr;

		u64 fileSize = 0;
		u64 actualSize = 0;
		u64 sizeOnDisk = 0;

		while (true)
		{
			u8* readBuffer = nullptr;
			u8* readPosition = nullptr;

			u16 fetchId = 0;
			u32 responseSize = 0;
			bool isCompressed = false;
			bool sendEnd = false;
			u64 left = ~u64(0);

			u32 sizeOfFirstMessage = 0;


			NetworkClient* client = &m_client;
			ProxyClient* proxy = nullptr;

			bool wantsProxy = false;
			if (allowProxy && m_allowProxy)
			{
				SCOPED_WRITE_LOCK(m_proxyClientLock, proxyLock);
				if (m_proxyClient)
				{
					if (m_proxyClient->client.IsConnected())
					{
						m_proxyClientKeepAliveTime = GetTime();
						++m_proxyClient->refCount;
						proxy = m_proxyClient;
						client = &proxy->client;
					}
					else if (!m_proxyClient->refCount)
					{
						delete m_proxyClient;
						m_proxyClient = nullptr;
						m_lastTestedProxyIp.clear();
					}
				}
				wantsProxy = proxy == nullptr && m_startProxyCallback;
			}

			auto pg = MakeGuard([&]()
				{
					if (proxy)
					{
						SCOPED_WRITE_LOCK(m_proxyClientLock, proxyLock);
						if (!--proxy->refCount)
						{
							if (!proxy->client.IsConnected())
							{
								delete proxy;
								if (proxy == m_proxyClient)
								{
									m_proxyClient = nullptr;
									m_lastTestedProxyIp.clear();
								}
							}
						}
					}
				});

			{
				StackBinaryWriter<1024> writer;
				NetworkMessage msg(*client, ServiceId, StorageMessageType_FetchBegin, writer);
				writer.WriteBool(wantsProxy);
				writer.WriteCasKey(casKey);
				writer.WriteString(hint);
				BinaryReader reader(slot, 0, SendMaxSize);
				if (!msg.Send(reader))
				{
					if (proxy)
						continue;
					return m_logger.Error(TC("Failed to send fetch begin message for cas %s (%s). Error: %u"), casFile.data, hint, msg.GetError());
				}
				sizeOfFirstMessage = u32(reader.GetLeft());
				fetchId = reader.ReadU16();
				if (fetchId == 0)
					return m_logger.Error(TC("Failed to fetch cas %s (%s)"), casFile.data, hint);

				fileSize = reader.Read7BitEncoded();

				u8 flags = reader.ReadByte();

				if ((flags >> 2) & 1)
				{
					StringBuffer<> proxyHost;
					u16 proxyPort;
					bool isInProcessClient = false;

					if (reader.ReadBool()) // This will be true for only one message.. no need to guard it
					{
						proxyPort = reader.ReadU16();
						if (!m_startProxyCallback(m_startProxyUserData, proxyPort, m_storageServerUid))
						{
							// TODO: Tell server we failed
							m_logger.Warning(TC("Failed to create proxy server. This should never happen!"));
							continue;
						}
						proxyHost.Append(TC("inprocess"));
						isInProcessClient = true;
					}
					else
					{
						reader.ReadString(proxyHost);
						proxyPort = reader.ReadU16();
					}

					SCOPED_WRITE_LOCK(m_proxyClientLock, proxyLock2);
					if (m_proxyClient)
						continue;

					// If we have x processes sitting here with an already closed proxy we would do x connect attempts to that bad proxy
					// Therefore we keep track of last checked proxy to prevent repetition of already failing proxy
					// There is a risk we have multiple threads asking for two bad proxies.. but that is low enough for us not to care
					if (m_lastTestedProxyIp == proxyHost.data)
						continue;
					m_lastTestedProxyIp = proxyHost.data;

					NetworkClientCreateInfo ncci(m_logger.m_writer);
					bool ctorSuccess = true;
					proxy = new ProxyClient(ctorSuccess, ncci);

					auto destroyProxy = MakeGuard([&]()
						{
							delete proxy;
							proxy = nullptr;
							allowProxy = false;
						});

					if (!ctorSuccess)
						continue;

					NetworkBackend& proxyBackend = m_getProxyBackendCallback(m_getProxyBackendUserData, proxyHost.data);

					u64 startTime = GetTime();
					if (!proxy->client.Connect(proxyBackend, proxyHost.data, proxyPort))
					{
						m_logger.Detail(TC("Redirection to proxy %s:%u for cas %s download failed! (%s)"), proxyHost.data, proxyPort, casFile.data, hint);
						continue;
					}
					
					//for (u32 i=0;i!=4;++i)
					//	proxy->client.Connect(proxyBackend, proxyHost.data, proxyPort);

					u64 connectTime = GetTime() - startTime;
					if (connectTime > MsToTime(2000))
						m_logger.Info(TC("Took %s to connect to proxy %s:%u"), TimeToText(connectTime).str, proxyHost.data, proxyPort);

					// Send a message to the proxy just to validate that this is still part of this build
					{
						StackBinaryWriter<256> proxyWriter;
						NetworkMessage proxyMsg(proxy->client, ServiceId, StorageMessageType_Connect, proxyWriter);

						proxyWriter.WriteString(TC("ProxyClient"));
						proxyWriter.WriteU32(StorageNetworkVersion);
						proxyWriter.WriteBool(isInProcessClient);
						StackBinaryReader<256> proxyReader;
						if (!proxyMsg.Send(proxyReader))
							continue;
						if (proxyReader.ReadGuid() != m_storageServerUid)
						{
							m_logger.Info(TC("Proxy %s:%u is not the correct proxy anymore. Will ask storage server for new proxy"), proxyHost.data, proxyPort);
							continue;
						}
					}

					destroyProxy.Cancel();

					++proxy->refCount;
					proxy->client.SetWorkTracker(m_client.GetWorkTracker());
					m_proxyClient = proxy;
					continue;
				}


				isCompressed = (flags >> 0) & 1;
				sendEnd = (flags >> 1) & 1;

				left = fileSize;

				responseSize = u32(reader.GetLeft());
				readBuffer = (u8*)reader.GetPositionData();
				readPosition = readBuffer;

				actualSize = fileSize;
				if (isCompressed)
					actualSize = *(u64*)readBuffer;
			}

			sizeOnDisk = IsCompressed(casKey) ? fileSize : actualSize;

#if !UBA_USE_SPARSEFILE
			FileAccessor destinationFile(m_logger, casFile.data);
			if (!mappingBuffer)
			{
				u32 extraFlags = DefaultAttributes();
				bool useOverlap = !IsRunningWine() && isCompressed == IsCompressed(casKey) && sizeOnDisk > 1024 * 1024;
				if (useOverlap)
					extraFlags |= FILE_FLAG_OVERLAPPED;
				if (!destinationFile.CreateWrite(false, extraFlags, sizeOnDisk, m_tempPath.data))
					return false;
			}
#endif

			if (mappingBuffer)
			{
				UBA_ASSERT(!writeMem || mappedView.size == sizeOnDisk);
				if (!writeMem)
				{
					mappedView = mappingBuffer->AllocAndMapView(mappingType, sizeOnDisk, memoryMapAlignment, hint);
					writeMem = mappedView.memory;
					if (!writeMem)
						return false;
				}
			}
			u8* writePos = writeMem;

			// This is here just to prevent server from getting a million messages at the same time.
			// In theory we could have 10 clients with 48 processes each where each one of the processes asks for a large file (64 messages in flight)
			// So worst case in that scenario would be 10*48*64 = 30000 messages.
			bool oneAtTheTime = false;//left > client->GetMessageMaxSize() * 2;
			if (oneAtTheTime)
				m_retrieveOneBatchAtTheTimeLock.EnterWrite();
			auto oatg = MakeGuard([&]() { if (oneAtTheTime) m_retrieveOneBatchAtTheTimeLock.LeaveWrite(); });


			if (isCompressed == IsCompressed(casKey))
			{
				bool tryAgain = false;
				bool sendSegmentMessage = responseSize == 0;
				u32 readIndex = 0;
				while (left)
				{
					if (sendSegmentMessage)
					{
						if (fetchId == u16(~0))
							return m_logger.Error(TC("Cas content error. Server believes %s was only one segment but client sees more. Size: %llu Left to read: %llu ResponseSize: %u. (%s)"), hint, fileSize, left, responseSize, casFile.data);
						readBuffer = slot;
						if (!SendBatchMessages(m_logger, *client, fetchId, readBuffer, BufferSlotSize, left, sizeOfFirstMessage, readIndex, responseSize))
						{
							if (proxy)
							{
								tryAgain = true;
								break;
							}
							return m_logger.Error(TC("Failed to send batched messages to server (%s)"), casFile.data);
						}
					}
					else
					{
						sendSegmentMessage = true;
					}
					if (!mappingBuffer)
					{
						if (!destinationFile.Write(readBuffer, responseSize, writePos - writeMem))
							return false;
						writePos += responseSize;
					}
					else
					{
						MapMemoryCopy(writePos, readBuffer, responseSize);
						writePos += responseSize;
					}

					UBA_ASSERT(left >= responseSize);
					left -= responseSize;
				}
				if (tryAgain)
					continue;
			}
			else
			{
				UBA_ASSERT(isCompressed); // Not implemented. Receiving non compressed and want to store it compressed.

				bool sendSegmentMessage = responseSize == 0;
				u64 leftUncompressed = actualSize;
				readBuffer += sizeof(u64); // Size is stored first
				u64 maxReadSize = BufferSlotHalfSize - sizeof(u64);

				if (actualSize)
				{
					u64 leftCompressed = fileSize - responseSize;
					u32 readIndex = 0;
					bool tryAgain = false;
					do
					{
						// First read in a full decompressable block
						bool isFirstInBlock = true;
						u32 compressedSize = ~u32(0);
						u32 uncompressedSize = ~u32(0);
						left = 0;
						u32 overflow = 0;
						do
						{
							if (sendSegmentMessage)
							{
								if (fetchId == u16(~0))
									return m_logger.Error(TC("Cas content error (2). Server believes %s was only one segment but client sees more. UncompressedSize: %llu LeftUncompressed: %llu Size: %llu Left to read: %llu ResponseSize: %u. (%s)"), hint, actualSize, leftUncompressed, fileSize, left, responseSize, casFile.data);
								if (!SendBatchMessages(m_logger, *client, fetchId, readPosition, maxReadSize - u32(readPosition - readBuffer), leftCompressed, sizeOfFirstMessage, readIndex, responseSize))
								{
									if (proxy)
									{
										tryAgain = true;
										break;
									}
									return m_logger.Error(TC("Failed to send batched messages to server (%s)"), casFile.data);
								}
								leftCompressed -= responseSize;
							}
							else
							{
								sendSegmentMessage = true;
							}

							if (isFirstInBlock)
							{
								if ((readPosition - readBuffer) + responseSize < sizeof(u32) * 2)
								{
									return m_logger.Error(TC("Received less than minimum amount of data. Most likely corrupt cas file %s (Available: %u UncompressedSize: %llu LeftUncompressed: %llu)"), casFile.data, u32(readPosition - readBuffer), actualSize, leftUncompressed);
								}
								isFirstInBlock = false;
								u32* blockSize = (u32*)readBuffer;
								compressedSize = blockSize[0];
								uncompressedSize = blockSize[1];
								readBuffer += sizeof(u32) * 2;
								maxReadSize = BufferSlotHalfSize - sizeof(u32) * 2;
								u32 read = (responseSize + u32(readPosition - readBuffer));
								//UBA_ASSERTF(read <= compressedSize, TC("Error in datastream fetching cas. Read size: %u CompressedSize: %u %s (%s)"), read, compressedSize, casFile.data, hint);
								if (read > compressedSize)
								{
									//UBA_ASSERT(!responseSize); // TODO: This has not really been tested
									left = 0;
									overflow = read - compressedSize;
									sendSegmentMessage = false;
								}
								else
								{
									left = compressedSize - read;
								}
								readPosition += responseSize;
							}
							else
							{
								readPosition += responseSize;
								if (responseSize > left)
								{
									overflow = responseSize - u32(left);
									UBA_ASSERTF(overflow < BufferSlotHalfSize, TC("Something went wrong. Overflow: %u responseSize: %u, left: %u"), overflow, responseSize, left);
									if (overflow >= 8)
									{
										responseSize = 0;
										sendSegmentMessage = false;
									}
									left = 0;
								}
								else
								{
									left -= responseSize;
								}
							}
						} while (left);

						if (tryAgain)
							break;

						// Then decompress
						{
							u8* decompressBuffer = slot + BufferSlotHalfSize;

							TimerScope ts(stats.decompressRecv);
							OO_SINTa decompLen = OodleLZ_Decompress(readBuffer, int(compressedSize), decompressBuffer, int(uncompressedSize));
							if (decompLen != uncompressedSize)
								return m_logger.Error(TC("Expected %u but got %i when decompressing %u bytes for file %s"), uncompressedSize, int(decompLen), compressedSize, hint);

							if (!mappingBuffer)
							{
								if (!destinationFile.Write(decompressBuffer, uncompressedSize, actualSize - leftUncompressed))
									return false;
							}
							else
							{
								MapMemoryCopy(writePos, decompressBuffer, uncompressedSize);
								writePos += uncompressedSize;
							}

							leftUncompressed -= uncompressedSize;
						}

						// Move overflow back to the beginning of the buffer and start the next block (if there is one)
						readBuffer = slot;
						maxReadSize = BufferSlotHalfSize;
						UBA_ASSERTF(readPosition - overflow >= readBuffer, TC("ReadPosition - overflow is before beginning of buffer (overflow: %u) for file %s"), overflow, hint);
						UBA_ASSERTF(readPosition <= readBuffer + BufferSlotHalfSize, TC("ReadPosition is outside readBuffer size (pos: %llu, overflow: %u) for file %s"), readPosition - readBuffer, overflow, hint);
						memmove(readBuffer, readPosition - overflow, overflow);
						readPosition = readBuffer + overflow;
						if (overflow)
						{
							if (overflow < sizeof(u32) * 2) // Must always have the compressed and uncompressed size to be able to move on with logic above
								sendSegmentMessage = true;
							else
								responseSize = 0;
						}
					} while (leftUncompressed);

					if (tryAgain)
						continue;
				}
			}

			if (sendEnd)
			{
				StackBinaryWriter<128> writer;
				NetworkMessage msg(*client, ServiceId, StorageMessageType_FetchEnd, writer);
				writer.WriteCasKey(casKey);
				if (!msg.Send() && !proxy)
					return false;
			}

			if (!mappingBuffer)
				if (!destinationFile.Close())
					return false;

			break;
		}

		if (shouldStore)
		{
			casEntry->mappingHandle = mappedView.handle;
			casEntry->mappingOffset = mappedView.offset;
			casEntry->mappingSize = fileSize;

			casEntry->exists = true;
			casEntryLock.Execute();

			CasEntryWritten(*casEntry, sizeOnDisk);
		}
		else
		{
			out.view = mappedView;
			out.view.memory = nullptr;
			out.view.isCompressed = IsCompressed(casKey);
		}

		stats.recvCasBytesRaw += actualSize;
		stats.recvCasBytesComp += fileSize;

		out.size = actualSize;

		return true;
	}

	bool StorageClient::StoreCasFile(CasKey& out, const tchar* fileName, const CasKey& casKeyOverride, bool deferCreation, bool fileIsCompressed)
	{
		UBA_ASSERTF(false, TC("This StoreCasFile function should not be used on the client side"));
		return true;
	}

	bool StorageClient::HasCasFile(const CasKey& casKey, CasEntry** out)
	{
		CasKey localKey = AsCompressed(casKey, false);
		SCOPED_READ_LOCK(m_localStorageFilesLock, lock);
		auto findIt = m_localStorageFiles.find(localKey);
		if (findIt != m_localStorageFiles.end())
		{
			if (out)
				*out = &findIt->second.casEntry;
			return true;
		}
		lock.Leave();
		return StorageImpl::HasCasFile(casKey, out);
	}

	bool StorageClient::StoreCasFile(CasKey& out, StringKey fileNameKey, const tchar* fileName, FileMappingHandle mappingHandle, u64 mappingOffset, u64 fileSize, const tchar* hint, bool deferCreation, bool keepMappingInMemory)
	{
		NetworkClient& client = m_client; // Don't use proxy

		out = CasKeyZero;

		bool isPersistentMapping = false;
		u8* fileMem = nullptr;

		FileAccessor source(m_logger, fileName);
		if (!mappingHandle.IsValid())
		{
			if (!source.OpenMemoryRead())
				return false;
			fileSize = source.GetSize();
			fileMem = source.GetData();
		}
		else
		{
			fileMem = MapViewOfFile(mappingHandle, FILE_MAP_READ, mappingOffset, fileSize);
			if (!fileMem)
				return m_logger.Error(TC("%s - MapViewOfFile failed (%s)"), fileName, LastErrorToText().data);
			isPersistentMapping = true;
		}

		auto unmapGuard = MakeGuard([&](){ if (isPersistentMapping) UnmapViewOfFile(fileMem, fileSize, fileName); });

		CasKey casKey;

		bool isTemporaryStorage = true; // This function is currently only used for temporary storage (output from client)
		//UBA_ASSERT(wcsstr(fileName, TC("\\output\\"))); // Just make sure our assumption is correct :)
		if (isTemporaryStorage)
		{
			bool storeCompressed = true;
			casKey = CalculateCasKey(fileMem, fileSize, storeCompressed);
			if (casKey == CasKeyZero)
				return false;

			if (keepMappingInMemory)
			{
				SCOPED_WRITE_LOCK(m_localStorageFilesLock, lock);
				auto insres = m_localStorageFiles.try_emplace(AsCompressed(casKey, false));
				LocalFile& localFile = insres.first->second;
				if (insres.second || !localFile.casEntry.mappingHandle.IsValid())
				{
					if (isPersistentMapping)
					{
						FileMappingHandle mappingHandle2;
						if (DuplicateFileMapping(GetCurrentProcessHandle(), mappingHandle, GetCurrentProcessHandle(), &mappingHandle2, FILE_MAP_READ, false, 0))
						{
							localFile.casEntry.mappingHandle = mappingHandle2;
							localFile.casEntry.size = fileSize;
						}
						else
							m_logger.Warning(TC("Failed to duplicate handle for file mapping %s (%s)"), fileName, LastErrorToText().data);
					}
					else
					{
						localFile.casEntry.size = fileSize;
#if !UBA_USE_SPARSEFILE
						localFile.fileName = fileName;
#else
						localFile.casEntry.mappingHandle = mappingHandle2;
						lock.Leave();
						mappingClose.Cancel();
#endif
					}
				}
			}
		}
		else
		{
			UBA_ASSERT(false);
		}

		StackBinaryWriter<128> writer;
		NetworkMessage msg(client, ServiceId, StorageMessageType_ExistsOnServer, writer);
		writer.WriteCasKey(casKey);
		StackBinaryReader<128> reader;
		if (!msg.Send(reader))
			return false;
		if (!reader.ReadBool())
		{
			if (!SendFile(casKey, fileName, fileMem, fileSize, hint))
				return false;
		}
		else
		{
			//m_logger.Warning(TC("File already exists on server %s"), fileName);
		}
		out = casKey;
		return true;
	}

	void StorageClient::Ping()
	{
		SCOPED_READ_LOCK(m_proxyClientLock, lock);
		if (!m_proxyClient)
			return;
		u64 now = GetTime();
		if (TimeToMs(now - m_proxyClientKeepAliveTime) < 30 * 1000)
			return;
		m_proxyClientKeepAliveTime = now;
		m_proxyClient->client.SendKeepAlive();
	}

	void StorageClient::PrintSummary(Logger& logger)
	{
		StorageImpl::PrintSummary(logger);
		if (m_proxyClient)
			m_proxyClient->client.PrintSummary(logger);
	}

	bool StorageClient::SendFile(const CasKey& casKey, const tchar* fileName, u8* sourceMem, u64 sourceSize, const tchar* hint)
	{
		FileSender sender { m_logger, m_client, m_bufferSlots, Stats(), m_sendOneAtTheTimeLock, m_casCompressor, m_casCompressionLevel };
		return sender.SendFileCompressed(casKey, fileName, sourceMem, sourceSize, hint);
	}

	bool StorageClient::PopulateCasFromDirsRecursive(const tchar* dir, WorkManager& workManager, UnorderedSet<u64>& seenIds, ReaderWriterLock& seenIdsLock, const Function<bool()>& shouldExit)
	{
		if (shouldExit && shouldExit())
			return true;

		StringBuffer<> fullPath;
		fullPath.Append(dir).EnsureEndsWithSlash();
		u32 dirLen = fullPath.count;
		TraverseDir(m_logger, dir, [&](const DirectoryEntry& e)
			{
				fullPath.Resize(dirLen).Append(e.name);
				if (IsDirectory(e.attributes))
				{
					SCOPED_WRITE_LOCK(seenIdsLock, lock);
					if (!seenIds.insert(e.id).second)
						return;
					lock.Leave();
					workManager.AddWork([&, filePath = TString(fullPath.data)]()
						{
							PopulateCasFromDirsRecursive(filePath.c_str(), workManager, seenIds, seenIdsLock, shouldExit);
						}, 1, TC(""));
					return;
				}

				StringBuffer<> forKey;
				FixPath(fullPath.data, nullptr, 0, forKey);
				if (CaseInsensitiveFs)
					forKey.MakeLower();
				StringKey fileNameKey = ToStringKey(forKey);
				FileEntry& fileEntry = GetOrCreateFileEntry(fileNameKey);
				fileEntry.lock.EnterWrite();
				if (e.size == fileEntry.size && e.lastWritten == fileEntry.lastWritten)
				{
					fileEntry.verified = true;
					fileEntry.casKey = AsCompressed(fileEntry.casKey, false); // TODO: Remove this when machines have flushed their db
					fileEntry.lock.LeaveWrite();

					SCOPED_WRITE_LOCK(m_localStorageFilesLock, lookupLock);
					auto insres = m_localStorageFiles.try_emplace(fileEntry.casKey);
					LocalFile& localFile = insres.first->second;
					if (insres.second)
					{
						localFile.casEntry.size = e.size;
						localFile.casEntry.verified = true;
						localFile.casEntry.exists = true;
						localFile.fileName = fullPath.data;
					}
					return;
				}

				workManager.AddWork([&, fe = &fileEntry, lw = e.lastWritten, s = e.size, filePath = TString(fullPath.data)]()
					{
						auto feLockLeave = MakeGuard([fe]() { fe->lock.LeaveWrite(); });

						if (shouldExit && shouldExit())
							return;

						CasKey casKey;
						if (!CalculateCasKey(casKey, filePath.c_str()))
						{
							m_logger.Error(TC("Failed to calculate cas key for %s"), filePath.c_str());
							return;
						}
						fe->size = s;
						fe->lastWritten = lw;
						fe->casKey = AsCompressed(casKey, false);
						fe->verified = true;
						feLockLeave.Execute();

						SCOPED_WRITE_LOCK(m_localStorageFilesLock, lookupLock);
						auto insres = m_localStorageFiles.try_emplace(fe->casKey);
						LocalFile& localFile = insres.first->second;
						if (insres.second)
						{
							localFile.casEntry.size = s;
							localFile.casEntry.verified = true;
							localFile.casEntry.exists = true;
							localFile.fileName = filePath;
						}

					}, 1, TC(""));
			});
		return true;
	}


}
