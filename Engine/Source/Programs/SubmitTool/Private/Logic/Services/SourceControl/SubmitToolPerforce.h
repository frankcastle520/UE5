// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Logic/Services/Interfaces/ISTSourceControlService.h"
#include "Parameters/SubmitToolParameters.h"
#include "PerforceData.h"
#include "Containers/Ticker.h"
#include "Containers/LockFreeFixedSizeAllocator.h"

class FProcessWrapper;
class ISourceControlProvider;

struct FClientApiWrapper
{
	FClientApiWrapper(TUniquePtr<ClientApi>&& InConnection)
	: bIsReady(true), Connection(Forward<TUniquePtr<ClientApi>>(InConnection))
	{}

	bool bIsReady = true;
	TUniquePtr<ClientApi> Connection;
};

struct FP4Connection
{
public:
	FP4Connection(FClientApiWrapper& InConnection, FCriticalSection& InMutex);
	~FP4Connection();

	ClientApi& GetConnection() const { return *Connection.Connection; }

private:
	FClientApiWrapper& Connection;
	FCriticalSection& P4ConnectionMutex;
};

class FConnectionPool
{
public:
	FConnectionPool() = default;
	~FConnectionPool();
	TUniquePtr<FP4Connection> GetAvailableConnection();
	
private:
	size_t CreateConnection();
	bool bConnectionFailed = false;
	TArray<TUniquePtr<FClientApiWrapper>> P4Connections;
	FCriticalSection Mutex;
	const int32 MaxConnections = 5;
};

class FSubmitToolPerforce final : public ISTSourceControlService
{
public:
	FSubmitToolPerforce(const FSubmitToolParameters& InParameters);
	~FSubmitToolPerforce();

	virtual const TUniquePtr<ISourceControlProvider>& GetProvider() const override;
	virtual void GetUsers(const FOnUsersGet::FDelegate& Callback) override;
	virtual void GetGroups(const FOnGroupsGet::FDelegate& Callback) override;
	virtual void GetUsersAndGroups(const FOnUsersAndGroupsGet::FDelegate& Callback) override;

	virtual UE::Tasks::TTask<bool> DownloadFiles(const FString& InFilepath, TArray<FSharedBuffer>& OutFileBuffers) override;

	virtual bool IsAvailable() const override
	{
		return SCCProvider.IsValid() && SCCProvider->IsAvailable();
	}

	virtual bool Tick(float InDeltaTime) override;

	virtual const TArray<TSharedPtr<FUserData>>& GetRecentUsers() const override;
	virtual void AddRecentUser(TSharedPtr<FUserData>& User) override;
	virtual const TArray<TSharedPtr<FString>>& GetRecentGroups() const override;
	virtual void AddRecentGroup(TSharedPtr<FString>& Group) override;

	virtual UE::Tasks::TTask<bool> RunCommand(const FString& InCommand, const TArray<FString>& InAdditionalArgs, FOnSCCCommandComplete InCompleteCallback = nullptr, TArray<FSharedBuffer>* OutData = nullptr) override;

	virtual TSharedPtr<FUserData> GetUserDataFromCache(const FString& Username) const override;

	virtual const TArray<FSCCStream*>& GetClientStreams() const override;
	virtual const FSCCStream* GetSCCStream(const FString& InStreamName) override;
	virtual const FString GetRootStreamName() override;
	virtual const FString GetCurrentStreamName() override;
	virtual const size_t GetDepotStreamLength(const FString& InDepotName) override;
	virtual const FAuthTicket& GetAuthTicket() override;
								  
private:
	bool RunP4Command(const FString& InCommand, const TArray<FString>& InAdditionalArgs, TArray<TMap<FString, FString>>& OutResultValues, FSourceControlResultInfo& OutResults, EP4ClientUserFlags InCmdFlags, TArray<FSharedBuffer>* OutData = nullptr);

	const FSubmitToolParameters& Parameters;
	UE::Tasks::TTask<bool> GetStream(const FString& InStream = FString(), bool bRequestHierarchy = true);
	TMap<FString, TSharedPtr<FUserData>> CachedUsers;
	TArray<TSharedPtr<FUserData>> CachedUsersArray;
	TArray<TSharedPtr<FString>> CachedGroupsArray;
	UE::Tasks::TTask<bool> UserTask;
	UE::Tasks::TTask<bool> GroupTask;
	bool bUsersReady = false;
	bool bGroupsReady = false;
	FOnUsersGet OnUsersGetCallbacks;
	FOnGroupsGet OnGroupsGetCallbacks;
	FOnUsersAndGroupsGet OnUsersAndGroupsGetCallbacks;
	
	FTSTicker::FDelegateHandle TickHandle;
	TUniquePtr<ISourceControlProvider> SCCProvider;

	void LoadRecentUsers();
	void SaveRecentUsers();
	const FString GetRecentUsersFilepath() const;
	TArray<TSharedPtr<FUserData>> RecentUsers;
	void LoadRecentGroups();
	void SaveRecentGroups();
	const FString GetRecentGroupsFilepath() const;
	TArray<TSharedPtr<FString>> RecentGroups;

	FCriticalSection Mutex;
	bool bIsUnicodeServer = false;

	mutable FCriticalSection StreamMutex;
	TMap<FString, TUniquePtr<FSCCStream>> Streams;
	TMap<FString, size_t> DepotStreamLengths;

	TArray<FSCCStream*> StreamHierarchy;
	FAuthTicket P4Ticket;
	
	FConnectionPool Connections;

	static constexpr EP4ClientUserFlags DefaultFlags = EP4ClientUserFlags::UseClient | EP4ClientUserFlags::UseUser | EP4ClientUserFlags::UseZTag;
};
