// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UbaDetoursFunctionsWin.h"
#include "UbaDetoursFileMappingTable.h"

namespace uba
{
	struct FileObject
	{
		void* operator new(size_t size);
		void operator delete(void* p);
		FileInfo* fileInfo = nullptr;
		u32 refCount = 1;
		u32 closeId = 0;
		u32 desiredAccess = 0;
		bool deleteOnClose = false;
		bool ownsFileInfo = false;
		TString newName;
	};
	extern BlockAllocator<FileObject> g_fileObjectAllocator;
	inline void* FileObject::operator new(size_t size) { return g_fileObjectAllocator.Allocate(); }
	inline void FileObject::operator delete(void* p) { g_fileObjectAllocator.Free(p); }


	enum HandleType
	{
		HandleType_File,
		HandleType_FileMapping,
		HandleType_Process,
		HandleType_StdErr,
		HandleType_StdOut,
		HandleType_StdIn,
		// Std handle types must be last
	};


	struct DetouredHandle
	{
		void* operator new(size_t size);
		void operator delete(void* p);

		DetouredHandle(HandleType t, HANDLE th = INVALID_HANDLE_VALUE) : trueHandle(th), type(t) {}

		HANDLE trueHandle;
		u32 dirTableOffset = ~u32(0);
		HandleType type;

		// Only for files
		FileObject* fileObject = nullptr;
		u64 pos = 0;
	};


	struct MemoryFile
	{
		MemoryFile(u8* data = nullptr, bool localOnly = true) : baseAddress(data), isLocalOnly(localOnly) {}
		MemoryFile(bool localOnly, u64 reserveSize_) : isLocalOnly(localOnly)
		{
			Reserve(reserveSize_);
		}

		void Reserve(u64 reserveSize_)
		{
			reserveSize = reserveSize_;
			if (isLocalOnly)
			{
				baseAddress = (u8*)VirtualAlloc(NULL, reserveSize, MEM_RESERVE, PAGE_READWRITE);
				if (!baseAddress)
					FatalError(1354, L"VirtualAlloc failed trying to reserve %llu. (Error code: %u)", reserveSize, GetLastError());
				mappedSize = reserveSize;
			}
			else
			{
				{
					mappedSize = 32 * 1024 * 1024;
					TimerScope ts(g_kernelStats.createFileMapping);
					mappingHandle = True_CreateFileMappingW(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE | SEC_RESERVE, ToHigh(reserveSize), ToLow(reserveSize), NULL);
					if (!mappingHandle)
						FatalError(1348, L"CreateFileMappingW failed trying to reserve %llu. (Error code: %u)", reserveSize, GetLastError());
				}
				TimerScope ts(g_kernelStats.mapViewOfFile);
				baseAddress = (u8*)True_MapViewOfFile(mappingHandle, FILE_MAP_READ | FILE_MAP_WRITE, 0, 0, mappedSize);
				if (!baseAddress)
					FatalError(1353, L"MapViewOfFile failed trying to map %llu. ReservedSize: %llu (Error code: %u)", mappedSize, reserveSize, GetLastError());
			}
		}

		void Unreserve()
		{
			if (isLocalOnly)
			{
				VirtualFree(baseAddress, 0, MEM_RELEASE);
			}
			else
			{
				True_UnmapViewOfFile(baseAddress);
				CloseHandle(mappingHandle);
				mappingHandle = nullptr;
			}
			baseAddress = nullptr;
			committedSize = 0;
		}

		void Write(struct DetouredHandle& handle, LPCVOID lpBuffer, u64 nNumberOfBytesToWrite);
		void EnsureCommitted(const struct DetouredHandle& handle, u64 size);
		void Remap(const struct DetouredHandle& handle, u64 size);

		u64 fileIndex = ~u64(0);
		u64 fileTime = ~u64(0);
		u32 volumeSerial = 0;

		HANDLE mappingHandle = nullptr;
		u8* baseAddress;
		u64 reserveSize = 0;
		u64 mappedSize = 0;
		u64 committedSize = 0;
		u64 writtenSize = 0;
		bool isLocalOnly;
		bool isReported = false;
	};
}