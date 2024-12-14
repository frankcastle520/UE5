// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UbaCacheClient.h"
#include "UbaCacheEntry.h"
#include "UbaCacheServer.h"
#include "UbaNetworkBackendTcp.h"
#include "UbaNetworkClient.h"
#include "UbaNetworkServer.h"
#include "UbaSessionServer.h"
#include "UbaStorageServer.h"
#include "../Private/UbaHashMap.h"

namespace uba
{
	bool TestCacheEntry(Logger& logger, const StringBufferBase& rootDir)
	{
		CacheEntries entries;

		auto AddEntry = [&](const Set<u32>& inputs)
			{
				Vector<u8> inputOffsets;
				u64 bytes = 0;
				for (u32 i : inputs)
					bytes += Get7BitEncodedCount(i);
				inputOffsets.resize(bytes);
				BinaryWriter writer(inputOffsets.data(), 0, inputOffsets.size());
				for (u32 i : inputs)
					writer.Write7BitEncoded(i);

				CacheEntry entry;
				entries.BuildInputs(entry, inputs);
				entries.entries.push_back(entry);
				entries.ValidateEntry(logger, entry, inputOffsets);
			};

		AddEntry(Set<u32>{ 1, 4, 6 });
		AddEntry(Set<u32>{ 0, 4, 6 });
		AddEntry(Set<u32>{ 2, 4, 6 });
		AddEntry(Set<u32>{ 1, 4, 5 });
		AddEntry(Set<u32>{ 1, 4, 7 });
		AddEntry(Set<u32>{ 1, 3, 6 });
		AddEntry(Set<u32>{ 1, 5, 6 });
		AddEntry(Set<u32>{ 1, 4, 6, 7 });
		AddEntry(Set<u32>{ 0, 1, 4, 6 });

		return true;
	}

	void GetTestAppPath(LoggerWithWriter& logger, StringBufferBase& out);
	bool CreateTextFile(StringBufferBase& outPath, LoggerWithWriter& logger, const tchar* workingDir, const tchar* fileName, const char* text);
	StringKey GetKeyAndFixedName(StringBuffer<>& fixedFilePath, const tchar* filePath);
	void InvalidateCachedInfo(StorageImpl& storage, StringBufferBase& fileName)
	{
		StringBuffer<> fixedFilePath;
		storage.InvalidateCachedFileInfo(GetKeyAndFixedName(fixedFilePath, fileName.data));
	}

	bool TestCacheClientAndServer(LoggerWithWriter& logger, const StringBufferBase& testRootDir)
	{
		LogWriter& logWriter = logger.m_writer;
		NetworkBackendTcp tcpBackend(logWriter);

		bool ctorSuccess = true;
		NetworkServer server(ctorSuccess, { logWriter });

		StringBuffer<MaxPath> rootDir;
		rootDir.Append(testRootDir).Append(TC("Uba"));
		if (!DeleteAllFiles(logger, rootDir.data))
			return false;

		StorageServerCreateInfo storageServerInfo(server, rootDir.data, logWriter);
		storageServerInfo.casCapacityBytes = 1024ull * 1024 * 1024;
		auto& storageServer = *new StorageServer(storageServerInfo);
		auto ssg = MakeGuard([&]() { delete &storageServer; });

		CacheServerCreateInfo csci(storageServer, rootDir.data, logWriter);
		CacheServer cacheServer(csci);


		SessionServerCreateInfo sessionInfo(storageServer, server, logWriter);
		sessionInfo.rootDir = rootDir.data;
		auto& session = *new SessionServer(sessionInfo);
		auto sg = MakeGuard([&]() { delete &session; });

		u16 port = 1356;
		if (!server.StartListen(tcpBackend, port))
			return logger.Error(TC("Failed to listen"));
		auto disconnectServer = MakeGuard([&]() { server.DisconnectClients(); });

		StringBuffer<MaxPath> workingDir;
		workingDir.Append(testRootDir).Append(TC("WorkingDir"));
		if (!DeleteAllFiles(logger, workingDir.data))
			return false;
		if (!storageServer.CreateDirectory(workingDir.data))
			return false;
		if (!DeleteAllFiles(logger, workingDir.data, false))
			return false;

		StringBuffer<> testApp;
		GetTestAppPath(logger, testApp);

		StringBuffer<MaxPath> inputFile;
		if (!CreateTextFile(inputFile, logger, workingDir.data, TC("Input.txt"), "Foo"))
			return false;
		StringBuffer<MaxPath> outputFile;
		if (!CreateTextFile(outputFile, logger, workingDir.data, TC("Output.txt"), "Foo"))
			return false;

		StackBinaryWriter<256> inputs;
		inputs.WriteString(inputFile);

		StackBinaryWriter<256> outputs;
		outputs.WriteString(outputFile);

		StackBinaryWriter<256> logLines;
		logLines.WriteString(TC("Hello"));
		logLines.WriteByte(1);

		ProcessStartInfo psi;
		psi.application = testApp.data;

		{
			NetworkClient client(ctorSuccess, { logWriter });
			CacheClientCreateInfo ccci(logWriter, storageServer, client, session);
			ccci.useRoots = false;
			CacheClient cacheClient(ccci);

			if (!client.Connect(tcpBackend, TC("127.0.0.1"), port))
				return logger.Error(TC("Failed to connect"));
			auto disconnectClient = MakeGuard([&]() { client.Disconnect(); });

			{
				CacheResult result;
				if (cacheClient.FetchFromCache(result, RootPaths(), 0, psi) || result.hit)
					return false;

				if (!cacheClient.WriteToCache(RootPaths(), 0, psi, inputs.GetData(), inputs.GetPosition(), outputs.GetData(), outputs.GetPosition(), logLines.GetData(), logLines.GetPosition()))
					return false;

				if (!DeleteFileW(outputFile.data))
					return false;
				if (FileExists(logger, outputFile.data))
					return false;
				if (!cacheClient.FetchFromCache(result, RootPaths(), 0, psi))
					return false;
				if (!FileExists(logger, outputFile.data))
					return false;
				if (result.logLines.size() != 1)
					return false;
				if (result.logLines[0].text != TC("Hello"))
					return false;
			}

			{
				if (!DeleteFileW(inputFile.data))
					return false;
				if (!CreateTextFile(inputFile, logger, workingDir.data, TC("Input.txt"), "Bar"))
					return false;
				InvalidateCachedInfo(storageServer, inputFile);

				CacheResult result;
				if (cacheClient.FetchFromCache(result, RootPaths(), 0, psi) || result.hit)
					return false;

				if (!cacheClient.WriteToCache(RootPaths(), 0, psi, inputs.GetData(), inputs.GetPosition(), outputs.GetData(), outputs.GetPosition(), logLines.GetData(), logLines.GetPosition()))
					return false;

				if (!DeleteFileW(outputFile.data))
					return false;
				if (FileExists(logger, outputFile.data))
					return false;
				if (!cacheClient.FetchFromCache(result, RootPaths(), 0, psi))
					return false;
				if (!FileExists(logger, outputFile.data))
					return false;
				if (result.logLines.size() != 1)
					return false;
				if (result.logLines[0].text != TC("Hello"))
					return false;
			}
		}

		if (!cacheServer.RunMaintenance(true, []() { return false; }))
			return false;

		{
			NetworkClient client(ctorSuccess, { logWriter });
			CacheClientCreateInfo ccci(logWriter, storageServer, client, session);
			ccci.useRoots = false;
			CacheClient cacheClient(ccci);

			if (!client.Connect(tcpBackend, TC("127.0.0.1"), port))
				return logger.Error(TC("Failed to connect"));
			auto disconnectClient = MakeGuard([&]() { client.Disconnect(); });

			{
				CacheResult result;
				if (!cacheClient.FetchFromCache(result, RootPaths(), 0, psi))
					return false;
				if (result.logLines.size() != 1)
					return false;
				if (result.logLines[0].text != TC("Hello"))
					return false;
			}
		}
		return true;
	}

	bool TestHashTable(Logger& logger, const StringBufferBase& rootDir)
	{
		{
			MemoryBlock memoryBlock(1024*1024);
			HashMap<u32, u32> casMap;
			casMap.Init(memoryBlock, 3);
			if (casMap.Find(1))
				return false;
			casMap.Insert(1) = 2;
			if (*casMap.Find(1) != 2)
				return false;
			casMap.Insert(1) = 3;
			if (*casMap.Find(1) != 3)
				return false;
		}

#if 0
		struct CasFileInfo { CasFileInfo(u32 s = 0) : size(s) {} u32 size; bool isUsed; }; // These are compressed cas, should never be over 4gb
		constexpr u64 memoryReserveSize = 192*1024*1024;
		struct ProfileScope
		{
			ProfileScope() : startTime(GetTime()) {}
			~ProfileScope() { u64 duration = GetTime() - startTime; LoggerWithWriter(g_consoleLogWriter, TC("")).Info(TC("Time: %s"), TimeToText(duration).str); }
			u64 startTime;
		};

		u32 totalCasCount = 1'800'000;

		{
			ProfileScope _;
			MemoryBlock memoryBlock;
			if (!memoryBlock.Init(memoryReserveSize, nullptr, true))
				memoryBlock.Init(memoryReserveSize);

			HashMap<CasKey, CasFileInfo> casMap;
			casMap.Init(memoryBlock, totalCasCount);

			CasKey key;

			for (u32 i=0; i!=totalCasCount; ++i)
			{
				key.a = i;
				casMap.Insert(key);
			}

			for (u32 i=0; i!=totalCasCount; ++i)
			{
				key.a = i;
				casMap.Find(key);
			}
		}

		{
			ProfileScope _;
			MemoryBlock memoryBlock;
			if (!memoryBlock.Init(memoryReserveSize, nullptr, true))
				memoryBlock.Init(memoryReserveSize);

			GrowingNoLockUnorderedMap<CasKey, CasFileInfo> casMap(&memoryBlock);
			casMap.reserve(totalCasCount);

			CasKey key;

			for (u32 i=0; i!=totalCasCount; ++i)
			{
				key.a = i;
				casMap.try_emplace(key);
			}

			for (u32 i=0; i!=totalCasCount; ++i)
			{
				key.a = i;
				auto it = casMap.find(key);
			}
		}
#endif
		return true;
	}
}