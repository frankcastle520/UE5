// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UbaBinaryReaderWriter.h"
#include "UbaHash.h"
#include "UbaNetworkMessage.h"
#include <oodle2.h>

namespace uba
{
	struct BufferSlots;
	struct StorageStats;

	static constexpr u8 DefaultCompressor = OodleLZ_Compressor_Kraken;
	static constexpr u8 DefaultCompressionLevel = OodleLZ_CompressionLevel_SuperFast;

	CasKey CalculateCasKey(u8* fileMem, u64 fileSize, bool storeCompressed, WorkManager* workManager, const tchar* hint);
	bool SendBatchMessages(Logger& logger, NetworkClient& client, u16 fetchId, u8* slot, u64 capacity, u64 left, u32 messageMaxSize, u32& readIndex, u32& responseSize, const Function<bool()>& runInWaitFunc = {});
	bool SendFile(Logger& logger, NetworkClient& client, const CasKey& casKey, const u8* sourceMem, u64 sourceSize, const tchar* hint);

	struct FileSender
	{
		bool SendFileCompressed(const CasKey& casKey, const tchar* fileName, const u8* sourceMem, u64 sourceSize, const tchar* hint);

		Logger& m_logger;
		NetworkClient& m_client;
		BufferSlots& m_bufferSlots;
		StorageStats& m_stats;
		ReaderWriterLock& m_sendOneAtTheTimeLock;
		u8 m_casCompressor = DefaultCompressor;
		u8 m_casCompressionLevel = DefaultCompressionLevel;
		u64 m_bytesSent = 0;
	};

	struct FileFetcher
	{
		bool RetrieveFile(Logger& logger, NetworkClient& client, const CasKey& casKey, const tchar* destination, bool destinationIsCompressed, MemoryBlock* destinationMem = nullptr);

		BufferSlots& m_bufferSlots;
		StorageStats& m_stats;
		StringBuffer<> m_tempPath;
		bool m_errorOnFail = true;

		u64 lastWritten = 0;
		u64 sizeOnDisk = 0;
		u64 bytesReceived = 0;
	};
}