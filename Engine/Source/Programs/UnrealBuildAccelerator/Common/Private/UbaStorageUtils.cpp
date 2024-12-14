// Copyright Epic Games, Inc. All Rights Reserved.

#include "UbaStorageUtils.h"
#include "UbaCompressedObjFileHeader.h"
#include "UbaStorage.h"
#include "UbaFileAccessor.h"
#include "UbaStats.h"
#include <oodle2.h>

namespace uba
{
	CasKey CalculateCasKey(u8* fileMem, u64 fileSize, bool storeCompressed, WorkManager* workManager, const tchar* hint)
	{
		constexpr u32 MaxWorkItemsPerAction2 = 128; // Cap this to not starve other things
		CasKeyHasher hasher;

		if (fileSize == 0)
			return ToCasKey(hasher, storeCompressed);

		UBA_ASSERTF(fileSize < sizeof(CompressedObjFileHeader) || !((CompressedObjFileHeader*)fileMem)->IsValid(), TC("CalculateCasKey should never run on compressed file"));

		#ifndef __clang_analyzer__

		if (fileSize > BufferSlotSize) // Note that when filesize is larger than BufferSlotSize the hash becomes a hash of hashes
		{
			struct WorkRec
			{
				Atomic<u64> refCount;
				Atomic<u64> counter;
				Atomic<u64> doneCounter;
				u8* fileMem = nullptr;
				u64 workCount = 0;
				u64 fileSize = 0;
				bool error = false;
				Vector<CasKey> keys;
				Event done;
			};

			u32 workCount = u32((fileSize + BufferSlotSize - 1) / BufferSlotSize);

			WorkRec* rec = new WorkRec();
			rec->fileMem = fileMem;
			rec->workCount = workCount;
			rec->fileSize = fileSize;
			rec->keys.resize(workCount);
			rec->done.Create(true);
			rec->refCount = 2;

			auto work = [rec]()
			{
				while (true)
				{
					u64 index = rec->counter++;
					if (index >= rec->workCount)
					{
						if (!--rec->refCount)
							delete rec;
						return 0;
					}

					u64 startOffset = BufferSlotSize*index;
					u64 toRead = Min(BufferSlotSize, rec->fileSize - startOffset);
					u8* slot = rec->fileMem + startOffset;
					CasKeyHasher hasher;
					hasher.Update(slot, toRead);
					rec->keys[index] = ToCasKey(hasher, false);

					if (++rec->doneCounter == rec->workCount)
						rec->done.Set();
				}
				return 0;
			};

			u32 workerCount = 0;
			if (workManager)
			{
				workerCount = Min(workCount, workManager->GetWorkerCount()-1); // We are a worker ourselves
				workerCount = Min(workerCount, MaxWorkItemsPerAction2); // Cap this to not starve other things
				rec->refCount += workerCount;
				workManager->AddWork(work, workerCount, TC("CalculateKey"));
			}

			work();
			rec->done.IsSet();

			hasher.Update(rec->keys.data(), rec->keys.size()*sizeof(CasKey));

			bool error = rec->error;

			if (!--rec->refCount)
				delete rec;

			if (error)
				return CasKeyZero;
		}
		else
		{
			hasher.Update(fileMem, fileSize);
		}

		#endif // __clang_analyzer__

		return ToCasKey(hasher, storeCompressed);
	}

	bool SendBatchMessages(Logger& logger, NetworkClient& client, u16 fetchId, u8* slot, u64 capacity, u64 left, u32 messageMaxSize, u32& readIndex, u32& responseSize, const Function<bool()>& runInWaitFunc)
	{
		responseSize = 0;

		struct Entry
		{
			Entry(u8* slot, u32 i, u32 messageMaxSize) : reader(slot + i * messageMaxSize, 0, SendMaxSize), done(true) {}
			NetworkMessage message;
			BinaryReader reader;
			Event done;
		};

		u64 sendCountCapacity = capacity / messageMaxSize;
		u64 sendCount = left/messageMaxSize;

		if (sendCount > sendCountCapacity)
			sendCount = sendCountCapacity;
		else if (sendCount < sendCountCapacity && (left - sendCount * messageMaxSize) > 0)
			++sendCount;


		u64 entriesMem[sizeof(Entry) * 8];
		UBA_ASSERT(sizeof(Entry)*sendCount <= sizeof(entriesMem));

		Entry* entries = (Entry*)entriesMem;

		bool success = true;
		u32 inFlightCount = u32(sendCount);
		for (u32 i=0; i!=sendCount; ++i)
		{
			auto& entry = *new (entries + i) Entry(slot, i, messageMaxSize);
			StackBinaryWriter<32> writer;
			entry.message.Init(client, StorageServiceId, StorageMessageType_FetchSegment, writer);
			writer.WriteU16(fetchId);
			writer.WriteU32(readIndex + i + 1);
			if (entry.message.SendAsync(entry.reader, [](bool error, void* userData) { ((Event*)userData)->Set(); }, &entry.done))
				continue;
			entry.~Entry();
			inFlightCount = i;
			success = false;
			break;
		}

		if (runInWaitFunc)
			if (!runInWaitFunc())
				return false;

		u32 timeOutTimeMs = 5*60*1000;

		for (u32 i=0; i!=inFlightCount; ++i)
		{
			Entry& entry = entries[i];
			if (!entry.done.IsSet(timeOutTimeMs))
			{
				logger.Error(TC("SendBatchMessages timed out after 5 minutes getting async message response (%u/%u). Received %llu bytes so far. FetchId: %u"), i, inFlightCount, responseSize, fetchId);
				timeOutTimeMs = 10;
			}
			if (!entry.message.ProcessAsyncResults(entry.reader))
				success = false;
			else
				responseSize += u32(entry.reader.GetLeft());
		}

		for (u32 i=0; i!=inFlightCount; ++i)
			entries[i].~Entry();

		readIndex += u32(sendCount);
		return success;
	}

	bool SendFile(Logger& logger, NetworkClient& client, const CasKey& casKey, const u8* sourceMem, u64 sourceSize, const tchar* hint)
	{
		UBA_ASSERT(casKey != CasKeyZero);

		const u8* readData = sourceMem;
		u64 fileSize = sourceSize;

		u16 storeId = 0;
		bool isFirst = true;
		bool sendEnd = false;
		u64 sendLeft = fileSize;
		u64 sendPos = 0;

		while (sendLeft)
		{
			StackBinaryWriter<SendMaxSize> writer;
			NetworkMessage msg(client, StorageServiceId, isFirst ? StorageMessageType_StoreBegin : StorageMessageType_StoreSegment, writer);
			if (isFirst)
			{
				writer.WriteCasKey(casKey);
				writer.WriteU64(fileSize);
				writer.WriteU64(sourceSize);
				writer.WriteString(hint);
			}
			else
			{
				UBA_ASSERT(storeId != 0);
				writer.WriteU16(storeId);
				writer.WriteU64(sendPos);
			}

			u64 capacityLeft = writer.GetCapacityLeft();
			u64 toWrite = Min(sendLeft, capacityLeft);
			writer.WriteBytes(readData, toWrite);

			readData += toWrite;
			sendLeft -= toWrite;
			sendPos += toWrite;

			bool isDone = sendLeft == 0;

			if (isFirst) // First message must always be acknowledged (provide a reader) to make sure there is an entry on server that can be waited on.
			{
				StackBinaryReader<128> reader;
				if (!msg.Send(reader))
					return false;
				storeId = reader.ReadU16();
				sendEnd = reader.ReadBool();
				if (isDone)
					break;

				if (!storeId) // Zero means error
					return logger.Error(TC("Server failed to start storing file %s (%s)"), CasKeyString(casKey).str, hint);

				if (storeId == u16(~0)) // File already exists on server
				{
					logger.Info(TC("Server already has file %s (%s)"), CasKeyString(casKey).str, hint);
					return true;
				}

				isFirst = false;
			}
			else
			{
				if (!msg.Send())
					return false;
				if (isDone)
					break;
			}
		}

		if (sendEnd)
		{
			StackBinaryWriter<128> writer;
			NetworkMessage msg(client, StorageServiceId, StorageMessageType_StoreEnd, writer);
			writer.WriteCasKey(casKey);
			if (!msg.Send())
				return false;
		}

		return true;
	}

	bool FileSender::SendFileCompressed(const CasKey& casKey, const tchar* fileName, const u8* sourceMem, u64 sourceSize, const tchar* hint)
	{
		UBA_ASSERT(casKey != CasKeyZero);

		NetworkClient& client = m_client;

		TimerScope ts(m_stats.sendCas);

		u64 firstMessageOverHead = (sizeof(CasKey) + sizeof(u64)*2 + GetStringWriteSize(hint, TStrlen(hint)));

		u64 messageHeader = client.GetMessageHeaderSize();
		u64 messageHeaderMaxSize = messageHeader + firstMessageOverHead;

		MemoryBlock memoryBlock(sourceSize + messageHeaderMaxSize + 1024);
		{
			const u8* uncompressedData = sourceMem;
			u8* compressBufferStart = memoryBlock.memory + messageHeaderMaxSize;
			u8* compressBuffer = compressBufferStart;
			u64 totalWritten = messageHeaderMaxSize; // Make sure there is room for msg header in the memory since we are using it to send
			u64 left = sourceSize;

			compressBuffer += 8;
			totalWritten += 8;
			memoryBlock.Allocate(totalWritten, 1, hint);

			u64 diff = u64(OodleLZ_GetCompressedBufferSizeNeeded((OodleLZ_Compressor)m_casCompressor, BufferSlotHalfSize)) - BufferSlotHalfSize;
			u64 maxUncompressedBlock = BufferSlotHalfSize - diff - totalWritten - 8; // 8 bytes block header

			OodleLZ_CompressOptions oodleOptions = *OodleLZ_CompressOptions_GetDefault();
			while (left)
			{
				u32 uncompressedBlockSize = (u32)Min(left, maxUncompressedBlock);

				u64 reserveSize = totalWritten + uncompressedBlockSize + diff + 8;
				if (reserveSize > memoryBlock.mappedSize)
				{
					u64 toAllocate = reserveSize - memoryBlock.writtenSize;
					memoryBlock.Allocate(toAllocate, 1, hint);
				}

				u8* destBuf = compressBuffer;
				u32 compressedBlockSize;
				{
					TimerScope cts(m_stats.compressSend);
					compressedBlockSize = (u32)OodleLZ_Compress((OodleLZ_Compressor)m_casCompressor, uncompressedData, (int)uncompressedBlockSize, destBuf + 8, (OodleLZ_CompressionLevel)m_casCompressionLevel, &oodleOptions);
					if (compressedBlockSize == OODLELZ_FAILED)
						return m_logger.Error(TC("Failed to compress %u bytes at %llu for %s (%s) (%s) (uncompressed size: %llu)"), uncompressedBlockSize, totalWritten, fileName, CasKeyString(casKey).str, hint, sourceSize);
				}

				*(u32*)destBuf =  u32(compressedBlockSize);
				*(u32*)(destBuf+4) =  u32(uncompressedBlockSize);

				u32 writeBytes = u32(compressedBlockSize) + 8;

				totalWritten += writeBytes;
				memoryBlock.writtenSize = totalWritten;

				left -= uncompressedBlockSize;
				uncompressedData += uncompressedBlockSize;
				compressBuffer += writeBytes;
			}

			*(u64*)compressBufferStart = sourceSize;
		}


		u8* readData = memoryBlock.memory + messageHeaderMaxSize;
		u64 fileSize = memoryBlock.writtenSize - messageHeaderMaxSize;

		u16 storeId = 0;
		bool isFirst = true;
		bool sendEnd = false;
		u64 sendLeft = fileSize;
		u64 sendPos = 0;

		bool hasSendOneAtTheTimeLock = false;
		auto lockGuard = MakeGuard([&]() { if (hasSendOneAtTheTimeLock) m_sendOneAtTheTimeLock.LeaveWrite(); });

		while (sendLeft)
		{
			u64 writerStartOffset = messageHeader + (isFirst ? firstMessageOverHead : (sizeof(u16) + sizeof(u64)));
			BinaryWriter writer(readData + sendPos - writerStartOffset, 0, client.GetMessageMaxSize());
			NetworkMessage msg(client, StorageServiceId, isFirst ? StorageMessageType_StoreBegin : StorageMessageType_StoreSegment, writer);
			if (isFirst)
			{
				writer.WriteCasKey(casKey);
				writer.WriteU64(fileSize);
				writer.WriteU64(sourceSize);
				writer.WriteString(hint);
			}
			else
			{
				UBA_ASSERT(storeId != 0);
				writer.WriteU16(storeId);
				writer.WriteU64(sendPos);
			}

			u64 capacityLeft = writer.GetCapacityLeft();
			u64 toWrite = Min(sendLeft, capacityLeft);
			writer.AllocWrite(toWrite);

			sendLeft -= toWrite;
			sendPos += toWrite;

			bool isDone = sendLeft == 0;

			if (isFirst && !isDone)
			{
				m_sendOneAtTheTimeLock.EnterWrite();
				hasSendOneAtTheTimeLock = true;
			}

			if (isFirst) // First message must always be acknowledged (provide a reader) to make sure there is an entry on server that can be waited on.
			{
				StackBinaryReader<128> reader;
				if (!msg.Send(reader))
					return false;
				storeId = reader.ReadU16();
				sendEnd = reader.ReadBool();
				if (isDone)
					break;

				if (!storeId) // Zero means error
					return m_logger.Error(TC("Server failed to start storing file %s (%s)"), CasKeyString(casKey).str, hint);

				if (storeId == u16(~0)) // File already exists on server
				{
					m_logger.Info(TC("Server already has file %s (%s)"), CasKeyString(casKey).str, hint);
					return true;
				}

				isFirst = false;
			}
			else
			{
				if (!msg.Send())
					return false;
				if (isDone)
					break;
			}
		}

		if (sendEnd)
		{
			StackBinaryWriter<128> writer;
			NetworkMessage msg(client, StorageServiceId, StorageMessageType_StoreEnd, writer);
			writer.WriteCasKey(casKey);
			if (!msg.Send())
				return false;
		}

		m_stats.sendCasBytesRaw += sourceSize;
		m_stats.sendCasBytesComp += fileSize;
		m_bytesSent = fileSize;

		return true;
	}

	bool FileFetcher::RetrieveFile(Logger& logger, NetworkClient& client, const CasKey& casKey, const tchar* destination, bool destinationIsCompressed, MemoryBlock* destinationMem)
	{
		TimerScope ts(m_stats.recvCas);
		u8* slot = m_bufferSlots.Pop();
		auto sg = MakeGuard([&](){ m_bufferSlots.Push(slot); });

		u64 fileSize = 0;
		u64 actualSize = 0;

		u8* readBuffer = nullptr;
		u8* readPosition = nullptr;

		u16 fetchId = 0;
		u32 responseSize = 0;
		bool isCompressed = false;
		bool sendEnd = false;

		u32 sizeOfFirstMessage = 0;

		{
			StackBinaryWriter<1024> writer;
			NetworkMessage msg(client, StorageServiceId, StorageMessageType_FetchBegin, writer);
			writer.WriteBool(false);
			writer.WriteCasKey(casKey);
			writer.WriteString(destination);
			BinaryReader reader(slot, 0, SendMaxSize);
			if (!msg.Send(reader))
				return logger.Error(TC("Failed to send fetch begin message for cas %s (%s)"), CasKeyString(casKey).str, destination);
			sizeOfFirstMessage = u32(reader.GetLeft());
			fetchId = reader.ReadU16();
			if (fetchId == 0)
			{
				logger.Logf(m_errorOnFail ? LogEntryType_Error : LogEntryType_Detail, TC("Failed to fetch cas %s (%s)"), CasKeyString(casKey).str, destination);
				return false;
			}

			fileSize = reader.Read7BitEncoded();

			u8 flags = reader.ReadByte();

			isCompressed = (flags >> 0) & 1;
			sendEnd = (flags >> 1) & 1;

			responseSize = u32(reader.GetLeft());
			readBuffer = (u8*)reader.GetPositionData();
			readPosition = readBuffer;

			actualSize = fileSize;
			if (isCompressed)
				actualSize = *(u64*)readBuffer;
		}

		bytesReceived = fileSize;
		sizeOnDisk = destinationIsCompressed ? (sizeof(CompressedObjFileHeader) + fileSize) : actualSize;

		FileAccessor destinationFile(logger, destination);

		constexpr bool useFileMapping = true;
		u8* fileMappingMem = nullptr;

		if (!destinationMem)
		{
			if (useFileMapping)
			{
				if (!destinationFile.CreateMemoryWrite(false, DefaultAttributes(), sizeOnDisk))
					return false;
				fileMappingMem = destinationFile.GetData();
			}
			else
			{
				if (!destinationFile.CreateWrite(false, DefaultAttributes(), sizeOnDisk, m_tempPath.data))
					return false;
			}
		}

		u64 destOffset = 0;

		auto WriteDestination = [&](const void* source, u64 sourceSize)
			{
				if (fileMappingMem)
				{
					TimerScope ts(m_stats.memoryCopy);
					MapMemoryCopy(fileMappingMem + destOffset, source, sourceSize);
					destOffset += sourceSize;
				}
				else if (destinationMem)
				{
					TimerScope ts(m_stats.memoryCopy);
					void* mem = destinationMem->Allocate(sourceSize, 1, TC(""));
					memcpy(mem, source, sourceSize);
				}
				else
				{
					if (!destinationFile.Write(source, sourceSize, destOffset))
						return false;
					destOffset += sourceSize;
				}
				return true;
			};

		u32 readIndex = 0;

		if (destinationIsCompressed)
		{
			CompressedObjFileHeader header { casKey };
			if (!WriteDestination(&header, sizeof(header)))
				return false;

			u8* source = slot + BufferSlotHalfSize;
			u8* lastSource = readBuffer;
			u64 lastResponseSize = responseSize;
			auto writePrev = [&]() { return WriteDestination(lastSource, lastResponseSize); };

			u64 leftCompressed = fileSize - responseSize;
			while (leftCompressed)
			{
				if (fetchId == u16(~0))
					return logger.Error(TC("Cas content error (2). Server believes %s was only one segment but client sees more. "));//UncompressedSize: %llu LeftUncompressed: %llu Size: %llu Left to read: %llu ResponseSize: %u. (%s)"), destination, actualSize, leftUncompressed, fileSize, left, responseSize, CasKeyString(casKey).str);

				if (!SendBatchMessages(logger, client, fetchId, source, BufferSlotHalfSize, leftCompressed, sizeOfFirstMessage, readIndex, responseSize, writePrev))
					return logger.Error(TC("Failed to send batched messages to server (%s)"), CasKeyString(casKey).str);

				lastSource = source;
				lastResponseSize = responseSize;
				source = source == slot ? slot + BufferSlotHalfSize : slot;

				leftCompressed -= responseSize;
			}
			if (!writePrev())
				return false;
		}
		else if (actualSize)
		{
			bool sendSegmentMessage = responseSize == 0;
			u64 leftUncompressed = actualSize;
			readBuffer += sizeof(u64); // Size is stored first
			u64 maxReadSize = BufferSlotHalfSize - sizeof(u64);

			u8* decompressBuffer = slot + BufferSlotHalfSize;
			u32 lastDecompressSize = 0;
			auto tryWriteDecompressed = [&]()
				{
					if (!lastDecompressSize)
						return true;
					u32 toWrite = lastDecompressSize;
					lastDecompressSize = 0;
					return WriteDestination(decompressBuffer, toWrite);
				};

			u64 leftCompressed = fileSize - responseSize;
			do
			{
				// First read in a full decompressable block
				bool isFirstInBlock = true;
				u32 compressedSize = ~u32(0);
				u32 decompressedSize = ~u32(0);
				u32 left = 0;
				u32 overflow = 0;
				do
				{
					if (sendSegmentMessage)
					{
						if (fetchId == u16(~0))
							return logger.Error(TC("Cas content error (2). Server believes %s was only one segment but client sees more. UncompressedSize: %llu LeftUncompressed: %llu Size: %llu Left to read: %llu ResponseSize: %u. (%s)"), destination, actualSize, leftUncompressed, fileSize, left, responseSize, CasKeyString(casKey).str);
						if (!SendBatchMessages(logger, client, fetchId, readPosition, maxReadSize - u32(readPosition - readBuffer), leftCompressed, sizeOfFirstMessage, readIndex, responseSize, tryWriteDecompressed))
							return logger.Error(TC("Failed to send batched messages to server (%s)"), CasKeyString(casKey).str);
						leftCompressed -= responseSize;
					}
					else
					{
						sendSegmentMessage = true;
					}

					if (isFirstInBlock)
					{
						if (readPosition - readBuffer < sizeof(u32) * 2)
							return logger.Error(TC("Received less than minimum amount of data. Most likely corrupt cas file %s (Available: %u UncompressedSize: %llu LeftUncompressed: %llu)"), CasKeyString(casKey).str, u32(readPosition - readBuffer), actualSize, leftUncompressed);
						isFirstInBlock = false;
						u32* blockSize = (u32*)readBuffer;
						compressedSize = blockSize[0];
						decompressedSize = blockSize[1];
						readBuffer += sizeof(u32) * 2;
						maxReadSize = BufferSlotHalfSize - sizeof(u32) * 2;
						u32 read = (responseSize + u32(readPosition - readBuffer));
						//UBA_ASSERTF(read <= compressedSize, TC("Error in datastream fetching cas. Read size: %u CompressedSize: %u %s (%s)"), read, compressedSize, CasKeyString(casKey).str, destination);
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


				// Second, decompress
				while (true)
				{
					tryWriteDecompressed();

					{
						TimerScope ts2(m_stats.decompressRecv);
						OO_SINTa decompLen = OodleLZ_Decompress(readBuffer, int(compressedSize), decompressBuffer, int(decompressedSize));
						if (decompLen != decompressedSize)
							return logger.Error(TC("Expected %u but got %i when decompressing %u bytes for file %s"), decompressedSize, int(decompLen), compressedSize, destination);
					}

					lastDecompressSize = decompressedSize;
					leftUncompressed -= decompressedSize;

					constexpr bool decompressMultiple = false; // This does not seem to be a win.. it batches more but didn't save any time

					if (!decompressMultiple)
						break;

					if (overflow < 8)
						break;
					u8* nextBlock = readBuffer + compressedSize;
					u32* blockSize = (u32*)nextBlock;
					u32 compressedSize2 = blockSize[0];
					if (overflow < compressedSize2 + 8)
						break;
					readBuffer += compressedSize + 8;

					decompressedSize = blockSize[1];
					compressedSize = compressedSize2;
					overflow -= compressedSize + 8;
				}

				// Move overflow back to the beginning of the buffer and start the next block (if there is one)
				readBuffer = slot;
				maxReadSize = BufferSlotHalfSize;
				UBA_ASSERTF(readPosition - overflow >= readBuffer, TC("ReadPosition - overflow is before beginning of buffer (overflow: %u) for file %s"), overflow, destination);
				UBA_ASSERTF(readPosition <= readBuffer + BufferSlotHalfSize, TC("ReadPosition is outside readBuffer size (pos: %llu, overflow: %u) for file %s"), readPosition - readBuffer, overflow, destination);
				memmove(readBuffer, readPosition - overflow, overflow);
				readPosition = readBuffer + overflow;
				if (overflow)
					responseSize = 0;
			} while (leftUncompressed);

			if (!tryWriteDecompressed())
				return false;
		}

		if (sendEnd)
		{
			StackBinaryWriter<128> writer;
			NetworkMessage msg(client, StorageServiceId, StorageMessageType_FetchEnd, writer);
			writer.WriteCasKey(casKey);
			if (!msg.Send())
				return false;
		}

		if (!destinationMem)
			if (!destinationFile.Close(&lastWritten))
				return false;

		m_stats.recvCasBytesRaw += actualSize;
		m_stats.recvCasBytesComp += fileSize;

		return true;
	}


}
