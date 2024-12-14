// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UbaNetworkClient.h"
#include "UbaStorage.h"

namespace uba
{
	class NetworkClient;

	using StartProxyCallback = bool(void* userData, u16 port, const Guid& storageServerUid);
	using GetProxyBackendCallback = NetworkBackend&(void* userData, const tchar* proxyHost);

	struct StorageClientCreateInfo : StorageCreateInfo
	{
		StorageClientCreateInfo(NetworkClient& c, const tchar* rootDir_) : StorageCreateInfo(rootDir_, c.GetLogWriter()), client(c) {}

		void Apply(Config& config);

		NetworkClient& client;
		const tchar* zone = TC("");
		u16 proxyPort = DefaultStorageProxyPort;
		bool sendCompressed = true;
		bool allowProxy = true;
		
		GetProxyBackendCallback* getProxyBackendCallback = nullptr;
		void* getProxyBackendUserData = nullptr;
		StartProxyCallback* startProxyCallback = nullptr;
		void* startProxyUserData = nullptr;
	};


	class StorageClient final : public StorageImpl
	{
	public:
		StorageClient(const StorageClientCreateInfo& info);
		~StorageClient();

		bool Start();

		bool IsUsingProxy();
		void StopProxy();

		using DirVector = Vector<TString>;
		bool PopulateCasFromDirs(const DirVector& directories, u32 workerCount, const Function<bool()>& shouldExit = {});

		#if !UBA_USE_SPARSEFILE
		virtual bool GetCasFileName(StringBufferBase& out, const CasKey& casKey) override;
		#endif

		virtual MappedView MapView(const CasKey& casKey, const tchar* hint) override;

		virtual bool GetZone(StringBufferBase& out) override;
		virtual bool RetrieveCasFile(RetrieveResult& out, const CasKey& casKey, const tchar* hint, FileMappingBuffer* mappingBuffer = nullptr, u64 memoryMapAlignment = 1, bool allowProxy = true) override;
		virtual bool StoreCasFile(CasKey& out, const tchar* fileName, const CasKey& casKeyOverride, bool deferCreation, bool fileIsCompressed) override;
		virtual bool StoreCasFile(CasKey& out, StringKey fileNameKey, const tchar* fileName, FileMappingHandle mappingFile, u64 mappingOffset, u64 fileSize, const tchar* hint, bool deferCreation = false, bool keepMappingInMemory = false) override;
		virtual bool HasCasFile(const CasKey& casKey, CasEntry** out = nullptr) override;
		virtual void Ping() override;
		virtual void PrintSummary(Logger& logger) override;

	private:
		bool SendFile(const CasKey& casKey, const tchar* fileName, u8* sourceMem, u64 sourceSize, const tchar* hint);
		bool PopulateCasFromDirsRecursive(const tchar* dir, WorkManager& workManager, UnorderedSet<u64>& seenIds, ReaderWriterLock& seenIdsLock, const Function<bool()>& shouldExit);

		NetworkClient& m_client;
		bool m_sendCompressed;
		bool m_allowProxy;

		Guid m_storageServerUid;

		TString m_zone;

		ReaderWriterLock m_localStorageFilesLock;
		struct LocalFile { CasEntry casEntry; TString fileName; };
		UnorderedMap<CasKey, LocalFile> m_localStorageFiles;

		ReaderWriterLock m_sendOneAtTheTimeLock;
		ReaderWriterLock m_retrieveOneBatchAtTheTimeLock;

		static constexpr u8 ServiceId = StorageServiceId;

		struct ProxyClient;
		TString m_lastTestedProxyIp;
		ReaderWriterLock m_proxyClientLock;
		ProxyClient* m_proxyClient = nullptr;
		u64 m_proxyClientKeepAliveTime = 0;

		GetProxyBackendCallback* m_getProxyBackendCallback = nullptr;
		void* m_getProxyBackendUserData = nullptr;
		StartProxyCallback* m_startProxyCallback = nullptr;
		void* m_startProxyUserData = nullptr;
		u16 m_proxyPort = 0;
	};

}
