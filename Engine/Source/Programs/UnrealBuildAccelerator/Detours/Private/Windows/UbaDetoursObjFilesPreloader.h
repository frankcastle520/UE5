// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UbaCompressedObjFileHeader.h"
#include "UbaDetoursUtilsWin.h"
#include "UbaEvent.h"
#include "UbaProcessUtils.h"
#include <oodle2.h>
#define _NTDEF_
#include <ntsecapi.h>

namespace uba
{
	constexpr bool UseLargePages = false;

	u64 GetLargePageSize()
	{
		HINSTANCE hDll = LoadLibrary(TEXT("kernel32.dll"));
		if (!hDll)
			return 0;
		auto lg = MakeGuard([hDll]() { FreeLibrary(hDll); });

		using GetLargePageMinimumType = int(void);
		auto getLargePageMinimumFunc = (GetLargePageMinimumType*)GetProcAddress(hDll, "GetLargePageMinimum");
		if (getLargePageMinimumFunc == NULL)
			return 0;

		DWORD size = (*getLargePageMinimumFunc)();
		if (!size)
			return 0;

		HANDLE hToken;
		if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hToken))
			return 0;
		auto tg = MakeGuard([hToken]() { CloseHandle(hToken); });

		TOKEN_PRIVILEGES tp;
		tp.PrivilegeCount = 1;
		tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
		if (!LookupPrivilegeValue(NULL, L"SeLockMemoryPrivilege", &tp.Privileges[0].Luid))
			return 0;

		if (!AdjustTokenPrivileges(hToken, FALSE, &tp, 0, (PTOKEN_PRIVILEGES)NULL, 0))
			return 0;

		// It is possible for AdjustTokenPrivileges to return TRUE and still not succeed. So always check for the last error value.
		if (GetLastError() != ERROR_SUCCESS)
			return 0;

		return size;
	}

	class ObjFilesPreloader
	{
	public:
		void ParseRsp(const StringView& rspFile)
		{
			HANDLE rspFileHandle = CreateFileW(rspFile.data, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
			if (rspFileHandle == INVALID_HANDLE_VALUE)
				return;
			LARGE_INTEGER rspFileSize;
			if (!GetFileSizeEx(rspFileHandle, &rspFileSize))
			{
				CloseHandle(rspFileHandle);
				return;
			}
			HANDLE rspFileMappingHandle = CreateFileMappingW(rspFileHandle, NULL, PAGE_READONLY, rspFileSize.HighPart, rspFileSize.LowPart, NULL);
			CloseHandle(rspFileHandle);
			if (!rspFileMappingHandle)
				return;
			const void* rspMem = MapViewOfFile(rspFileMappingHandle, FILE_MAP_READ, 0, 0, rspFileSize.QuadPart);
			CloseHandle(rspFileMappingHandle);
			if (!rspMem)
				return;

			ParseArguments((const char*)rspMem, rspFileSize.QuadPart, [&](const char* arg, u32 argLen)
				{
					StringBuffer<> sb;
					sb.Append(arg, argLen);
					HandleLine(sb);
				});

			UnmapViewOfFile(rspMem);
		}

		void HandleLine(const StringView& line)
		{
			if (line.data[0] == '/' || line.data[0] == '-')
				return;

			if (line.data[0] == '@')
			{
				const wchar_t* begin = line.data + 1;
				const wchar_t* end = line.data + line.count;
				StringBuffer<> rspFile;
				if (*begin == '\"')
				{
					++begin;
					--end;
					if (!end)
						return;
				}
				rspFile.Append(begin, end - begin);
				ParseRsp(rspFile);
				return;
			}

			StringBuffer<> file;
			file.Append(line);
			if (!g_rules->ShouldDecompressFiles(file))
				return;
			StringBuffer<> fileFull;
			FixPath(fileFull, file.data);

			HANDLE fileHandle = CreateFileW(fileFull.data, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
			UBA_ASSERT(fileHandle != INVALID_HANDLE_VALUE);

			StringKey fileNameKey = ToStringKey(fileFull.MakeLower());
			auto insres = m_preloadedObjFiles.try_emplace(fileNameKey);
			UBA_ASSERT(insres.second);
			insres.first->second.handle = fileHandle;
			insres.first->second.event.Create(true);

			SCOPED_READ_LOCK(g_mappedFileTable.m_lookupLock, _);
			auto findIt = g_mappedFileTable.m_lookup.find(fileNameKey);
			insres.first->second.fileInfo = &findIt->second;
		}

		void Start(const wchar_t* cmdLine)
		{
			Oodle_SetUsageWarnings(Oodle_UsageWarnings_Disabled);

			ParseArguments(cmdLine, [&](const tchar* arg, u32 argLen) { HandleLine(StringView(arg, argLen)); });

			if (!m_preloadedObjFiles.empty())
				m_threadHandle = CreateThread(NULL, 0, [](LPVOID p) -> DWORD { ((ObjFilesPreloader*)p)->ThreadPreload(); return 0; }, this, 0, NULL);
		}

		void Stop()
		{
			if (!m_threadHandle)
				return;
			WaitForSingleObject(m_threadHandle, INFINITE);
			CloseHandle(m_threadHandle);
			WaitForMultipleObjects(sizeof_array(m_helperThreadHandles), m_helperThreadHandles, true, INFINITE);
			for (u32 i=0;i!=sizeof_array(m_helperThreadHandles); ++i)
				CloseHandle(m_helperThreadHandles[i]);
		}

		void ThreadPreload()
		{
			u64 totalMemSize = 0;
			for (auto& kv : m_preloadedObjFiles)
			{
				Preload& preload = kv.second;
				HANDLE objFileHandle = preload.handle;
				if (!objFileHandle)
					continue;

				LARGE_INTEGER objFileSize;
				BOOL res = GetFileSizeEx(objFileHandle, &objFileSize);
				UBA_ASSERT(res);(void)res;
				if (objFileSize.QuadPart < sizeof(CompressedObjFileHeader))
					continue;

				HANDLE objFileMappingHandle = CreateFileMappingW(objFileHandle, NULL, PAGE_READONLY, objFileSize.HighPart, objFileSize.LowPart, NULL);
				UBA_ASSERT(objFileMappingHandle);
				if (!objFileMappingHandle)
					continue;
				CloseHandle(objFileHandle);
				preload.handle = NULL;
				preload.objMem = (u8*)MapViewOfFile(objFileMappingHandle, FILE_MAP_READ, 0, 0, objFileSize.QuadPart);
				UBA_ASSERT(preload.objMem);
				if (!preload.objMem)
					continue;
				if (!((CompressedObjFileHeader*)preload.objMem)->IsValid())
				{
					preload.event.Set();
					continue;
				}
				preload.objCompressedSize = objFileSize.QuadPart;
				preload.objDecompressedSize = *(u64*)(preload.objMem + sizeof(CompressedObjFileHeader));
				preload.objReadOffset = 8 + sizeof(CompressedObjFileHeader);
				preload.objWriteOffset = 0;
				preload.objLeft = preload.objDecompressedSize;
				CloseHandle(objFileMappingHandle);

				if (ToView(preload.fileInfo->originalName).EndsWith(TC(".h.obj"))) // TODO: Copied this from UbaSession align code
					totalMemSize = AlignUp(totalMemSize, 4 * 1024);

				totalMemSize += preload.objDecompressedSize;
			}

			u64 alignedMem = AlignUp(totalMemSize, 64*1024);
			DWORD allocationType = MEM_RESERVE | MEM_COMMIT;

			if (UseLargePages)
			{
				if (u64 largePageSize = GetLargePageSize())
				{
					alignedMem = AlignUp(totalMemSize, largePageSize);
					allocationType |= MEM_LARGE_PAGES;
				}
			}
			m_totalMem = (u8*)VirtualAlloc(NULL, alignedMem, allocationType, PAGE_READWRITE);
			m_it = m_preloadedObjFiles.begin();

			for (u32 i=0;i!=sizeof_array(m_helperThreadHandles); ++i)
				m_helperThreadHandles[i] = CreateThread(NULL, 0, [](LPVOID p) -> DWORD { ((ObjFilesPreloader*)p)->ThreadHelper(); return 0; }, this, 0, NULL);
			ThreadHelper();
		}

		void ThreadHelper()
		{
			OO_SINTa decoredMemSize = OodleLZDecoder_MemorySizeNeeded(OodleLZ_Compressor_Kraken);
			void* decoderMem = malloc(decoredMemSize);
			auto mg = MakeGuard([decoderMem]() { free(decoderMem); });

			while (true)
			{
				SCOPED_WRITE_LOCK(m_threadLock, lock);
				if (m_it == m_preloadedObjFiles.end())
					break;
				Preload& preload = m_it->second;
				if (preload.objReadOffset == preload.objCompressedSize)
				{
					++m_it;
					continue;
				}

				FileInfo& info = *preload.fileInfo;

				if (preload.objWriteOffset == 0)
				{
					if (ToView(info.originalName).EndsWith(TC(".h.obj"))) // TODO: Copied this from UbaSession align code
						m_totalMemOffset = AlignUp(m_totalMemOffset, 4 * 1024);

					info.fileMapMem = m_totalMem + m_totalMemOffset;
					m_totalMemOffset += preload.objDecompressedSize;
				}

				BinaryReader reader(preload.objMem, preload.objReadOffset, preload.objCompressedSize);

				u32 compressedBlockSize = reader.ReadU32();
				u32 decompressedBlockSize = reader.ReadU32();

				u8* destMem = info.fileMapMem + preload.objWriteOffset;

				preload.objReadOffset += compressedBlockSize + 8;
				preload.objWriteOffset += decompressedBlockSize;

				lock.Leave();

				OO_SINTa decompLen = OodleLZ_Decompress(reader.GetPositionData(), (OO_SINTa)compressedBlockSize, destMem, (OO_SINTa)decompressedBlockSize,
					OodleLZ_FuzzSafe_Yes, OodleLZ_CheckCRC_No, OodleLZ_Verbosity_None, NULL, 0, NULL, NULL, decoderMem, decoredMemSize);
				if (decompLen != decompressedBlockSize)
				{
					FatalError(1356, TC("Failed to decompress .obj file %s (%s)"), info.name, info.originalName);
				}

				bool isDone = preload.objLeft.fetch_sub(decompressedBlockSize) == decompressedBlockSize;
				if (!isDone)
					continue;

				info.size = preload.objDecompressedSize;
				info.memoryFile = nullptr;
				info.name = L":";
				info.isFileMap = true;
				info.fileMapMemEnd = info.fileMapMem + info.size;
				info.trueFileMapHandle = nullptr;
				info.trueFileMapOffset = 0;

				{
					SCOPED_WRITE_LOCK(g_mappedFileTable.m_memLookupLock, lock3);
					auto insres = g_mappedFileTable.m_memLookup.try_emplace(destMem);
					UBA_ASSERT(insres.second);
					insres.first->second = 1;
				}

				preload.event.Set();

				UnmapViewOfFile(preload.objMem);
			}
		}

		void Wait(const StringKey& key)
		{
			auto findIt = m_preloadedObjFiles.find(key);
			if (findIt == m_preloadedObjFiles.end())
				return;
			TimerScope ts(g_stats.waitDecompress);
			findIt->second.event.IsSet();
		}

		struct Preload
		{
			Event event;
			HANDLE handle;
			u8* objMem;
			u64 objCompressedSize;
			u64 objDecompressedSize;
			FileInfo* fileInfo;
			u64 objReadOffset;
			u64 objWriteOffset;
			Atomic<u64> objLeft;
		};

		using PreloadedObjFiles = Map<StringKey, Preload>;
		PreloadedObjFiles m_preloadedObjFiles;
		HANDLE m_threadHandle = 0;
		HANDLE m_helperThreadHandles[5];
		u8* m_totalMem = nullptr;
		u64 m_totalMemOffset = 0;

		ReaderWriterLock m_threadLock;
		PreloadedObjFiles::iterator m_it;
	};
}
