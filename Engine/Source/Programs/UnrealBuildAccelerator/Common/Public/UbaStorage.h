// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#define Local_GetLongPathNameW uba::GetLongPathNameW

#include "UbaFile.h"
#include "UbaFileMapping.h"
#include "UbaLogger.h"
#include "UbaMemory.h"
#include "UbaPathUtils.h"
#include "UbaStats.h"

#define UBA_USE_SPARSEFILE 0

namespace uba
{
	class Config;
	class FileAccessor;
	class Trace;
	class WorkManager;
	struct DirectoryEntry;
	extern CasKey EmptyFileKey;
	
	class Storage
	{
	public:
		virtual ~Storage() {}
		virtual bool StoreCompressed() const = 0;
		virtual void PrintSummary(Logger& logger) = 0;
		virtual bool Reset() = 0;
		virtual bool SaveCasTable(bool deleteIsRunningfile, bool deleteDropped = true) = 0;
		virtual u64 GetStorageCapacity() = 0;
		virtual bool GetZone(StringBufferBase& out) = 0;
		virtual bool HasProxy(u32 clientId) { return false; }

		virtual bool DecompressFileToMemory(const tchar* fileName, FileHandle fileHandle, u8* dest, u64 decompressedSize, const tchar* writeHint) = 0;
		virtual bool DecompressMemoryToMemory(u8* compressedData, u8* writeData, u64 decompressedSize, const tchar* readHint, const tchar* writeHint) = 0;
		virtual bool CreateDirectory(const tchar* dir) = 0;
		virtual bool DeleteCasForFile(const tchar* file) = 0;

		struct RetrieveResult { CasKey casKey; u64 size = 0; MappedView view; };
		virtual bool RetrieveCasFile(RetrieveResult& out, const CasKey& casKey, const tchar* hint, FileMappingBuffer* mappingBuffer = nullptr, u64 memoryMapAlignment = 1, bool allowProxy = true) = 0;

		struct CachedFileInfo { CasKey casKey; };
		virtual bool VerifyAndGetCachedFileInfo(CachedFileInfo& out, StringKey fileNameKey, u64 verifiedLastWriteTime, u64 verifiedSize) = 0;
		virtual bool InvalidateCachedFileInfo(StringKey fileNameKey) = 0;

		virtual bool StoreCasFile(CasKey& out, const tchar* fileName, const CasKey& casKeyOverride, bool deferCreation, bool fileIsCompressed) = 0;
		virtual bool StoreCasFile(CasKey& out, StringKey fileNameKey, const tchar* fileName, FileMappingHandle mappingHandle, u64 mappingOffset, u64 fileSize, const tchar* hint, bool deferCreation = false, bool keepMappingInMemory = false) = 0;
		virtual bool DropCasFile(const CasKey& casKey, bool forceDelete, const tchar* hint) = 0;
		virtual bool ReportBadCasFile(const CasKey& casKey) = 0;
		virtual bool CalculateCasKey(CasKey& out, const tchar* fileName) = 0;
		virtual bool CopyOrLink(const CasKey& casKey, const tchar* destination, u32 fileAttributes, bool writeCompressed = false) = 0;
		virtual bool FakeCopy(const CasKey& casKey, const tchar* destination, u64 size = 0, u64 lastWritten = 0, bool deleteExisting = true) = 0;
#if !UBA_USE_SPARSEFILE
		virtual bool GetCasFileName(StringBufferBase& out, const CasKey& casKey) = 0;
#endif
		virtual MappedView MapView(const CasKey& casKey, const tchar* hint) = 0;
		virtual void UnmapView(const MappedView& view, const tchar* hint) = 0;

		virtual void ReportFileWrite(StringKey fileNameKey, const tchar* fileName) = 0;
		
		virtual StorageStats& Stats() = 0;
		virtual void AddStats(StorageStats& stats) = 0;

		static void GetMappingString(StringBufferBase& out, FileMappingHandle mappingHandle, u64 offset);

		virtual void SetTrace(Trace* trace, bool detailed) {}
		virtual void Ping() {}

		struct WriteResult { FileMappingHandle mappingHandle; u64 size = InvalidValue; u64 offset = InvalidValue; };
		virtual bool WriteCompressed(WriteResult& out, const tchar* from, FileHandle readHandle, u8* readMem, u64 fileSize, const tchar* toFile, const void* header, u64 headerSize, u64 lastWriteTime = 0) = 0;
	};

	struct StorageCreateInfo
	{
		StorageCreateInfo(const tchar* rootDir_, LogWriter& w) : writer(w), rootDir(rootDir_) {}

		void Apply(Config& config);

		LogWriter& writer;
		const tchar* rootDir;
		u64 casCapacityBytes = 20llu * 1024 * 1024 * 1024;
		u32 maxParallelCopyOrLink = 1000;
		bool storeCompressed = true;
		bool manuallyHandleOverflow = false;
		WorkManager* workManager = nullptr;
	};

	struct BufferSlots
	{
		u8* Pop();
		void Push(u8* slot);

		~BufferSlots();

		ReaderWriterLock m_slotsLock;
		Vector<u8*> m_slots;
	};

	static constexpr u64 BufferSlotSize = 16*1024*1024;
	static constexpr u64 BufferSlotHalfSize = BufferSlotSize/2; // This must be three times a msg size or more.

	class StorageImpl : public Storage
	{
	public:
		StorageImpl(const StorageCreateInfo& info, const tchar* logPrefix = TC("UbaStorage"));
		virtual ~StorageImpl();

		bool LoadCasTable(bool logStats = true, bool alwaysCheckAllFiles = false);
		bool CheckCasContent(u32 workerCount);
		bool CheckFileTable(const tchar* searchPath, u32 workerCount);
		const tchar* GetTempPath();

		virtual bool SaveCasTable(bool deleteIsRunningfile, bool deleteDropped = true) override;
		virtual u64 GetStorageCapacity() override;
		virtual bool GetZone(StringBufferBase& out) override;
		virtual bool Reset() override;
		bool DeleteAllCas();

		virtual bool StoreCompressed() const final { return m_storeCompressed; }
		virtual void PrintSummary(Logger& logger) override;

		virtual bool DecompressFileToMemory(const tchar* fileName, FileHandle fileHandle, u8* dest, u64 decompressedSize, const tchar* writeHint) override;
		virtual bool CreateDirectory(const tchar* dir) override;
		virtual bool DeleteCasForFile(const tchar* file) override;
		virtual bool RetrieveCasFile(RetrieveResult& out, const CasKey& casKey, const tchar* hint, FileMappingBuffer* mappingBuffer = nullptr, u64 memoryMapAlignment = 1, bool allowProxy = true) override;
		virtual bool VerifyAndGetCachedFileInfo(CachedFileInfo& out, StringKey fileNameKey, u64 verifiedLastWriteTime, u64 verifiedSize) override;
		virtual bool InvalidateCachedFileInfo(StringKey fileNameKey) override;
		virtual bool StoreCasFile(CasKey& out, const tchar* fileName, const CasKey& casKeyOverride, bool deferCreation, bool fileIsCompressed) override;
		virtual bool StoreCasFile(CasKey& out, StringKey fileNameKey, const tchar* fileName, FileMappingHandle mappingHandle, u64 mappingOffset, u64 fileSize, const tchar* hint, bool deferCreation = false, bool keepMappingInMemory = false) override;
		virtual bool DropCasFile(const CasKey& casKey, bool forceDelete, const tchar* hint) override;
		virtual bool ReportBadCasFile(const CasKey& casKey) override;
		virtual bool CalculateCasKey(CasKey& out, const tchar* fileName) override;
		virtual bool CopyOrLink(const CasKey& casKey, const tchar* destination, u32 fileAttributes, bool writeCompressed = false) override;
		virtual bool FakeCopy(const CasKey& casKey, const tchar* destination, u64 size = 0, u64 lastWritten = 0, bool deleteExisting = true) override;
#if !UBA_USE_SPARSEFILE
		virtual bool GetCasFileName(StringBufferBase& out, const CasKey& casKey) override;
#endif
		virtual MappedView MapView(const CasKey& casKey, const tchar* hint) override;
		virtual void UnmapView(const MappedView& view, const tchar* hint) override;

		virtual void ReportFileWrite(StringKey fileNameKey, const tchar* fileName) override;
		
		virtual StorageStats& Stats() final;
		virtual void AddStats(StorageStats& stats) override;

		struct CasEntry;

		virtual bool HasCasFile(const CasKey& casKey, CasEntry** out = nullptr);
		bool EnsureCasFile(const CasKey& casKey, const tchar* fileName);
		CasKey CalculateCasKey(const tchar* fileName, FileHandle fileHandle, u64 fileSize, bool storeCompressed);
		CasKey CalculateCasKey(u8* fileMem, u64 fileSize, bool storeCompressed);
		bool StoreCasKey(CasKey& out, const tchar* fileName, const CasKey& casKeyOverride, bool fileIsCompressed);
		bool StoreCasKey(CasKey& out, const StringKey& fileNameKey, const tchar* fileName, const CasKey& casKeyOverride, bool fileIsCompressed);
		bool IsFileVerified(const StringKey& fileNameKey);
		void ReportFileInfoWeak(const StringKey& fileNameKey, u64 verifiedLastWriteTime, u64 verifiedSize);

		virtual bool WriteCompressed(WriteResult& out, const tchar* from, const tchar* toFile);
		virtual bool WriteCompressed(WriteResult& out, const tchar* from, FileHandle readHandle, u8* readMem, u64 fileSize, const tchar* toFile, const void* header, u64 headerSize, u64 lastWriteTime = 0) final;
		bool WriteMemToCompressedFile(FileAccessor& destination, u32 workCount, const u8* uncompressedData, u64 fileSize, u64 maxUncompressedBlock, u64& totalWritten);
		bool WriteCasFileNoCheck(WriteResult& out, const tchar* fileName, bool fileIsCompressed, const tchar* casFile, bool storeCompressed);
		bool WriteCasFile(WriteResult& out, const tchar* fileName, bool fileIsCompressed, const CasKey& casKey);
		bool VerifyExisting(bool& outReturnValue, ScopedWriteLock& entryLock, const CasKey& casKey, CasEntry& casEntry, StringBufferBase& casFile);
		bool AddCasFile(StringKey fileNameKey, const tchar* fileName, const CasKey& casKey, bool deferCreation, bool fileIsCompressed);
		void CasEntryAccessed(const CasKey& casKey);
		virtual bool IsDisallowedPath(const tchar* fileName);
		virtual bool DecompressMemoryToMemory(u8* compressedData, u8* writeData, u64 decompressedSize, const tchar* readHint, const tchar* writeHint) override;
		bool DecompressMemoryToFile(u8* compressedData, FileAccessor& destination, u64 decompressedSize, bool useNoBuffering);

		void CasEntryAccessed(CasEntry& entry);
		void CasEntryWritten(CasEntry& entry, u64 size);
		void CasEntryDeleted(CasEntry& entry, u64 size);
		void AttachEntry(CasEntry& entry);
		void DetachEntry(CasEntry& entry);
		void TraverseAllCasFiles(const tchar* dir, const Function<void(const StringBufferBase& fullPath, const DirectoryEntry& e)>& func, bool allowParallel = false);
		void TraverseAllCasFiles(const Function<void(const CasKey& key, u64 size)>& func, bool allowParallel = false);
		bool CheckAllCasFiles(u64 checkContentOfFilesNewerThanTime = ~u64(0));
		void HandleOverflow(UnorderedSet<CasKey>* outDeletedFiles);
		bool OpenCasDataFile(u32 index, u64 size);
		bool CreateCasDataFiles();

		struct FileEntry;
		FileEntry& GetOrCreateFileEntry(StringKey fileNameKey);


		WorkManager* m_workManager;
		MutableLogger m_logger;

		BufferSlots m_bufferSlots;

		StringBuffer<> m_rootDir;
		StringBuffer<> m_tempPath;

		ReaderWriterLock m_fileTableLookupLock;
		struct FileEntry
		{
			ReaderWriterLock lock;
			CasKey casKey = CasKeyZero;
			u64 size = 0;
			u64 lastWritten = 0;
			bool verified = false;
		};
		UnorderedMap<StringKey, FileEntry> m_fileTableLookup;

		ReaderWriterLock m_casLookupLock;
		struct CasEntry
		{
			ReaderWriterLock lock;
			CasKey key;
			CasEntry* prevAccessed = nullptr;
			CasEntry* nextAccessed = nullptr;
			u64 size = 0;
			bool verified = false; // This flag needs to be set for below flags to be reliable. if this is false below flags are assumptions
			bool exists = false; // File exists on disk
			bool dropped = false; // This file is not seen anymore. will be deleted during shutdown
			bool beingWritten = false; // This is set while file is being written (when coming from network)..
			bool disallowed = false; // This is set if cas is created from disallowed file

			FileMappingHandle mappingHandle;
			u64 mappingOffset = 0;
			u64 mappingSize = 0;
		};

		UnorderedMap<CasKey, CasEntry> m_casLookup;
		ReaderWriterLock m_accessLock;
		CasEntry* m_newestAccessed = nullptr;
		CasEntry* m_oldestAccessed = nullptr;
		u64 m_casTotalBytes = 0;
		u64 m_casMaxBytes = 0;
		u64 m_casCapacityBytes = 0;
		u64 m_casEvictedBytes = 0;
		u32 m_casEvictedCount = 0;
		u64 m_casDroppedBytes = 0;
		u32 m_casDroppedCount = 0;
		bool m_overflowReported = false;
		bool m_storeCompressed = false;
		bool m_manuallyHandleOverflow = false;

		u32 m_maxParallelCopyOrLink;
		ReaderWriterLock m_activeCopyOrLinkLock;
		Event m_activeCopyOrLinkEvent;
		u32 m_activeCopyOrLink = 0;

		ReaderWriterLock m_casTableLoadSaveLock;
		bool m_casTableLoaded = false;

		FileMappingBuffer m_casDataBuffer;

		ReaderWriterLock m_deferredCasCreationLookupLock;
		struct DeferedCasCreation { StringKey fileNameKey; TString fileName; bool fileIsCompressed; };
		UnorderedMap<CasKey, DeferedCasCreation> m_deferredCasCreationLookup;
		UnorderedMap<StringKey, CasKey> m_deferredCasCreationLookupByName;

		DirectoryCache m_dirCache;

		u8 m_casCompressor;
		u8 m_casCompressionLevel;

		StorageStats m_stats;

		StorageImpl(const StorageImpl&) = delete;
		void operator=(const StorageImpl&) = delete;
	};
}
