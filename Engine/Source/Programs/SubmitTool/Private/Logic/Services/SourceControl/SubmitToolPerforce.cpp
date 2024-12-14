// Copyright Epic Games, Inc. All Rights Reserved.

#include "SubmitToolPerforce.h"
#include "SourceControlInitSettings.h"
#include "ISourceControlModule.h"
#include "Logic/ProcessWrapper.h"
#include "SubmitToolUtils.h"
#include "Configuration/Configuration.h"
#include "Logging/SubmitToolLog.h"
#include "Logic/CredentialsService.h"

#include "CommandLine/CmdLineParameters.h"
#include "Misc/Base64.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"

constexpr size_t MaxRecentUsers = 12;
constexpr size_t RecentUsersDatVersion = 1;
constexpr size_t MaxRecentGroups = 12;
constexpr size_t RecentGroupsDatVersion = 1;
constexpr size_t MaxConnections = 1;
constexpr size_t MaxConnectionsAttempts = 10;


FP4Connection::FP4Connection(FClientApiWrapper& InConnection, FCriticalSection& InMutex)
	: Connection(InConnection), P4ConnectionMutex(InMutex)
{
	FScopeLock Lock(&P4ConnectionMutex);
	Connection.bIsReady = false;
}

FP4Connection::~FP4Connection()
{
	FScopeLock Lock(&P4ConnectionMutex);
	Connection.bIsReady = true;
}

FConnectionPool::~FConnectionPool()
{
	for(TUniquePtr<FClientApiWrapper>& ConWrapper : P4Connections)
	{
		Error P4Error;
		ConWrapper->Connection->Final(&P4Error);
		if(P4Error.Test())
		{
			StrBuf ErrorMsg;
			P4Error.Fmt(&ErrorMsg);
			UE_LOG(LogSubmitToolP4, Error, TEXT("P4ERROR: Invalid connection to server."));
			UE_LOG(LogSubmitToolP4, Error, TEXT("%s"), ANSI_TO_TCHAR(ErrorMsg.Text()));
		}
	}
}

TUniquePtr<FP4Connection> FConnectionPool::GetAvailableConnection()
{
	FScopeLock Lock(&Mutex);
	for(TUniquePtr<FClientApiWrapper>& ConWrapper : P4Connections)
	{
		if(ConWrapper->bIsReady)
		{
			return MakeUnique<FP4Connection>(*ConWrapper, Mutex);
		}
	}


	if(P4Connections.Num() < MaxConnections)
	{
		size_t Idx = CreateConnection();
		if(Idx != -1)
		{
			return MakeUnique<FP4Connection>(*P4Connections[Idx], Mutex);
		}
	}

	return nullptr;
}

size_t FConnectionPool::CreateConnection()
{
	if(bConnectionFailed)
	{
		return -1;
	}

	TUniquePtr<ClientApi> P4Client = MakeUnique<ClientApi>();
	FString Port;
	FCmdLineParameters::Get().GetValue(FSubmitToolCmdLine::P4Server, Port);
	P4Client->SetPort(TCHAR_TO_ANSI(*Port));

	Error P4Error;
	P4Client->Init(&P4Error);
	if(P4Error.Test())
	{
		StrBuf ErrorMsg;
		P4Error.Fmt(&ErrorMsg);
		UE_LOG(LogSubmitToolP4, Error, TEXT("P4ERROR: Invalid connection to server."));
		UE_LOG(LogSubmitToolP4, Error, TEXT("%s"), ANSI_TO_TCHAR(ErrorMsg.Text()));
		bConnectionFailed = true;
		return -1;
	}

	FScopeLock Lock(&Mutex);
	return P4Connections.Add(MakeUnique<FClientApiWrapper>(MoveTemp(P4Client)));
}

FSubmitToolPerforce::FSubmitToolPerforce(const FSubmitToolParameters& InParameters)
: Parameters(InParameters)
{
	FModuleManager::LoadModuleChecked<ISourceControlModule>(FName("PerforceSourceControl"));

	FSourceControlInitSettings SCCSettings(FSourceControlInitSettings::EBehavior::OverrideAll);

	const FSourceControlInitSettings::EConfigBehavior IniBehavior = FSourceControlInitSettings::EConfigBehavior::ReadOnly;
	SCCSettings.SetConfigBehavior(IniBehavior);

	FString PerforceServerAndPort;
	FCmdLineParameters::Get().GetValue(FSubmitToolCmdLine::P4Server, PerforceServerAndPort);

	FString PerforceUserName;
	FCmdLineParameters::Get().GetValue(FSubmitToolCmdLine::P4User, PerforceUserName);

	FString PerforceClientName;
	FCmdLineParameters::Get().GetValue(FSubmitToolCmdLine::P4Client, PerforceClientName);

	SCCSettings.AddSetting(TEXT("P4Port"), PerforceServerAndPort);
	SCCSettings.AddSetting(TEXT("P4User"), PerforceUserName);
	SCCSettings.AddSetting(TEXT("P4Client"), PerforceClientName);

	SCCProvider = ISourceControlModule::Get().CreateProvider(FName("Perforce"), TEXT("SubmitTool"), SCCSettings);

	if(SCCProvider.IsValid())
	{
		UE_LOG(LogSubmitTool, Log, TEXT("Setting Perforce Connection parameters: %s | User: %s | Workspace: %s"), *PerforceServerAndPort, *PerforceUserName, *PerforceClientName);

		ISourceControlProvider::FInitResult ConnectionResult = SCCProvider->Init(ISourceControlProvider::EInitFlags::AttemptConnection);
		if(!ConnectionResult.bIsAvailable)
		{
			UE_LOG(LogSubmitTool, Error, TEXT("%s"), *ConnectionResult.Errors.ErrorMessage.ToString());
			for(FText& AdditionalErrorMessage : ConnectionResult.Errors.AdditionalErrors)
			{
				UE_LOG(LogSubmitTool, Error, TEXT("%s"), *AdditionalErrorMessage.ToString());
			}
		}
	}
	else
	{
		UE_LOG(LogSubmitTool, Error, TEXT("Failed to create a perforce revision control provider"));
	}

	TickHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateRaw(this, &FSubmitToolPerforce::Tick));
	GetStream();
}

FSubmitToolPerforce::~FSubmitToolPerforce()
{
	OnUsersGetCallbacks.Clear();
	OnGroupsGetCallbacks.Clear();

	if(SCCProvider.IsValid())
	{
		SCCProvider->Close();
	}

	FTSTicker::GetCoreTicker().RemoveTicker(TickHandle);
}

const TUniquePtr<ISourceControlProvider>& FSubmitToolPerforce::GetProvider() const
{
	return SCCProvider;
}

UE::Tasks::TTask<bool> FSubmitToolPerforce::GetStream(const FString& InStream, bool bRequestHierarchy)
{
	TArray<FString> Args = { TEXT("-o") };

	if(!InStream.IsEmpty())
	{
		Args.Add(InStream);
	}

	return RunCommand(TEXT("stream"), Args, FOnSCCCommandComplete::CreateLambda(
		[this, bRequestHierarchy, InStream](bool bSuccess, const FSCCRecordSet& InResultValues, const FSourceControlResultInfo& InResultsInfo)
		{
			FScopeLock StreamLock(&StreamMutex);
			if(bSuccess && InResultValues.Num() > 0 && InResultValues[0].Contains(TEXT("Stream")))
			{
				TUniquePtr<FSCCStream> Stream = MakeUnique<FSCCStream>(InResultValues[0][TEXT("Stream")], InResultValues[0][TEXT("Parent")], InResultValues[0][TEXT("Type")]);

				const FString Base = TEXT("Paths");
				size_t i = 1;
				FString PathsKey = FString::Printf(TEXT("%s%d"), *Base, i);
				while(InResultValues[0].Contains(PathsKey))
				{
					FString Value = InResultValues[0][PathsKey];
					if(Value.StartsWith(TEXT("import")))
					{
						Value = Value.RightChop(Value.Find(TEXT("//")));
						while(!Value.EndsWith(TEXT("/")))
						{
							Value.RemoveAt(Value.Len() - 1);
						}

						Stream->AdditionalImportPaths.Add(Value);
					}

					++i;
					PathsKey = FString::Printf(TEXT("%s%d"), *Base, i);
				}

				const FString Parent = Stream->Parent;

				if (!Streams.Contains(Stream->Name))
				{
					if (bRequestHierarchy)
					{
						StreamHierarchy.Insert(Stream.Get(), 0);
					}

					Streams.Add(Stream->Name, MoveTemp(Stream));

					if (bRequestHierarchy && !Parent.IsEmpty() && !Parent.Equals(TEXT("none")))
					{
						GetStream(Parent);
					}
				}
			}
			else
			{
				TUniquePtr<FSCCStream> Stream = MakeUnique<FSCCStream>(TEXT("Invalid"), FString(), TEXT("Invalid"));
				if (!InStream.IsEmpty())
				{
					if (!Streams.Contains(InStream))
					{
						Streams.Add(InStream, MoveTemp(Stream));
					}
				}
				else
				{
					if (!Streams.Contains(TEXT("Invalid")))
					{
						StreamHierarchy.Add(Stream.Get());
						Streams.Add(TEXT("Invalid"), MoveTemp(Stream));
					}
				}
			}
		}
	));
}

void FSubmitToolPerforce::GetUsers(const FOnUsersGet::FDelegate& Callback)
{
	if(!CachedUsersArray.IsEmpty())
	{
		Callback.ExecuteIfBound(CachedUsersArray);
		return;
	}

	FScopeLock Lock(&Mutex);
	OnUsersGetCallbacks.Add(Callback);
	Lock.Unlock();

	if(!UserTask.IsValid())
	{
		UserTask = RunCommand(TEXT("users"), {}, FOnSCCCommandComplete::CreateLambda(
			[this](bool bSuccess, const FSCCRecordSet& InResultValues, const FSourceControlResultInfo& InResultsInfo)
			{
				for(const TMap<FString, FString>& Record : InResultValues)
				{
					if(Record.Contains("User"))
					{
						TSharedPtr<FUserData> User = MakeShared<FUserData>(Record[TEXT("User")], Record[TEXT("FullName")], Record[TEXT("Email")]);
						CachedUsersArray.Add(User);
						CachedUsers.Add(User->Username, User);
					}
				}

				LoadRecentUsers();

				FScopeLock Lock(&Mutex);
				OnUsersGetCallbacks.Broadcast(CachedUsersArray);
				OnUsersGetCallbacks.Clear();
				Lock.Unlock();
			}
		));
	}
}

void FSubmitToolPerforce::LoadRecentUsers()
{
	const TUniquePtr<FAES::FAESKey>& Key = FCredentialsService::GetEncryptionKey();

	if(!Key.IsValid())
	{
		return;
	}

	FString RecentUsersString;

	const FString FilePath = GetRecentUsersFilepath();
	if(IFileManager::Get().FileExists(*FilePath))
	{
		FArchive* File = IFileManager::Get().CreateFileReader(*FilePath);

		if(File != nullptr)
		{
			int32 Version;
			*File << Version;

			// Check Versions here
			if(Version != RecentUsersDatVersion)
			{
				UE_LOG(LogSubmitToolDebug, Warning, TEXT("Unexpected Recent Users Version, aborting issues loading."));
				File->Close();
				delete File;
				File = nullptr;
				return;
			}

			int32 PaddedLength;
			int32 LengthWithoutPadding;

			*File << PaddedLength;
			*File << LengthWithoutPadding;

			TArray<uint8> DeserializedBytes;
			DeserializedBytes.SetNum(PaddedLength);
			File->Serialize(DeserializedBytes.GetData(), PaddedLength);

			FAES::DecryptData(DeserializedBytes.GetData(), DeserializedBytes.Num(), *Key);

			RecentUsersString = BytesToString(DeserializedBytes.GetData(), LengthWithoutPadding);

			File->Close();
			delete File;
			File = nullptr;
		}
		else
		{
			UE_LOG(LogSubmitTool, Warning, TEXT("Could not read file '%s'."), *FilePath);
		}
	}
	else
	{
		UE_LOG(LogSubmitToolDebug, Log, TEXT("File %s does not exists, no recent users were loaded"), *FilePath);
	}

	TArray<FString> Usernames;
	RecentUsersString.ParseIntoArray(Usernames, TEXT(";"));

	RecentUsers.Empty();
	for(FString& Username : Usernames)
	{
		if(const TSharedPtr<FUserData>* User = CachedUsers.Find(Username))
		{
			RecentUsers.Add(*User);
		}
	}
}

TSharedPtr<FUserData> FSubmitToolPerforce::GetUserDataFromCache(const FString& Username) const
{
	if(CachedUsers.Contains(Username))
	{
		return CachedUsers[Username];
	}
	return nullptr;
}

void FSubmitToolPerforce::SaveRecentUsers()
{
	const FString RecentUsersString = FString::JoinBy(RecentUsers, TEXT(";"), [](const TSharedPtr<FUserData>& User) { return User->Username; });
	const TUniquePtr<FAES::FAESKey>& Key = FCredentialsService::GetEncryptionKey();

	const FString FilePath = GetRecentUsersFilepath();
	FArchive* File = IFileManager::Get().CreateFileWriter(*FilePath, EFileWrite::FILEWRITE_EvenIfReadOnly);

	if(File == nullptr)
	{
		UE_LOG(LogSubmitTool, Warning, TEXT("Could not create file '%s'."), *FilePath);
		return;
	}

	TArray<uint8> Bytes;
	Bytes.SetNumUninitialized(RecentUsersString.Len());
	StringToBytes(RecentUsersString, Bytes.GetData(), RecentUsersString.Len());

	int32 ActualLength = Bytes.Num();

	int32 NumBytesEncrypted = Align(Bytes.Num(), FAES::AESBlockSize);
	Bytes.SetNum(NumBytesEncrypted);
	FAES::EncryptData(Bytes.GetData(), Bytes.Num(), *Key);

	int32 Version = RecentUsersDatVersion;
	*File << Version;

	*File << NumBytesEncrypted;
	*File << ActualLength;

	File->Serialize(Bytes.GetData(), Bytes.Num());

	File->Close();
	delete File;
	File = nullptr;
}

bool FSubmitToolPerforce::Tick(float InDeltaTime)
{
	if(SCCProvider.IsValid())
	{
		SCCProvider->Tick();
	}

	return true;
}

const TArray<TSharedPtr<FUserData>>& FSubmitToolPerforce::GetRecentUsers() const
{
	return RecentUsers;
}

void FSubmitToolPerforce::AddRecentUser(TSharedPtr<FUserData>& User)
{
	if(RecentUsers.Contains(User))
	{
		// Remove so we can push the user to the top
		RecentUsers.Remove(User);
	}

	if(RecentUsers.Num() >= MaxRecentUsers)
	{
		RecentUsers.RemoveAt(MaxRecentUsers - 1);
	}

	RecentUsers.EmplaceAt(0, User);
	SaveRecentUsers();
}

const FString FSubmitToolPerforce::GetRecentUsersFilepath() const
{
	return FPaths::Combine(FSubmitToolUtils::GetLocalAppDataPath(), TEXT("SubmitTool"), TEXT("recent_users.dat"));
}

void FSubmitToolPerforce::GetGroups(const FOnGroupsGet::FDelegate& Callback)
{
	if(!CachedGroupsArray.IsEmpty())
	{
		Callback.ExecuteIfBound(CachedGroupsArray);
		return;
	}

	FScopeLock Lock(&Mutex);
	OnGroupsGetCallbacks.Add(Callback);
	Lock.Unlock();

	if(!GroupTask.IsValid())
	{
		GroupTask = UE::Tasks::Launch(UE_SOURCE_LOCATION,
			[this] {
				TUniquePtr<FP4Connection> Connection = Connections.GetAvailableConnection();
				
				size_t Attempts = 0;
				while(!Connection.IsValid())
				{
					if(Attempts >= MaxConnectionsAttempts)
					{
						return false;
					}

					FPlatformProcess::Sleep(1.f);
					Connection = Connections.GetAvailableConnection();

					++Attempts;
				}

				ClientApi& P4Client = Connection->GetConnection();

				FString UserName;
				FCmdLineParameters::Get().GetValue(FSubmitToolCmdLine::P4User, UserName);
				P4Client.SetUser(TCHAR_TO_ANSI(*UserName));

				FString Client;
				FCmdLineParameters::Get().GetValue(FSubmitToolCmdLine::P4Client, Client);
				P4Client.SetClient(TCHAR_TO_ANSI(*Client));

				P4Client.SetProtocol("tag", "");

				FSCCRecordSet ResultValues;
				FSourceControlResultInfo ResultInfo;

				class FGroupsClientUser : public FSTClientUser
				{
					public:
					FGroupsClientUser(FSCCRecordSet& InRecords, EP4ClientUserFlags InFlags, FSourceControlResultInfo& OutResultInfo, TArray<TSharedPtr<FString>>& OutGroupsArray, const TArray<FString>& InGroupsToExclude)
					: FSTClientUser(InRecords, InFlags, OutResultInfo), GroupsArray(OutGroupsArray), ExcludedGroups(InGroupsToExclude)
					{}

					virtual void OutputStat(StrDict* VarList) override
					{
						StrRef Var, Value;

						FString Group;
						bool bIsUser = false;
						bool bIsSubGroup = false;
						// Iterate over each variable and add to records
						for(int32 Index = 0; VarList->GetVar(Index, Var, Value); Index++)
						{
							if(Var == "isSubgroup")
							{
								bIsSubGroup = Value != "0";
							}

							if(Var == "group")
							{
								Group = TO_TCHAR(Value.Text(), IsUnicodeServer());
							}
						}

						if(!Group.IsEmpty() && !GroupsArray.ContainsByPredicate([&Group](const TSharedPtr<FString>& InStr) { return InStr->Equals(Group, ESearchCase::IgnoreCase); }) && !bIsSubGroup)
						{
							bool bIsExcluded = false;
							for(const FString& Filter : ExcludedGroups)
							{
								if(Group.StartsWith(Filter))
								{
									bIsExcluded = true;
									break;
								}
							}

							if(!bIsExcluded)
							{
								GroupsArray.Add(MakeShared<FString>(Group));
							}
						}
					}

					TArray<TSharedPtr<FString>>& GroupsArray;
					const TArray<FString>& ExcludedGroups;
				};

				FGroupsClientUser P4User(ResultValues, bIsUnicodeServer ? EP4ClientUserFlags::UnicodeServer : EP4ClientUserFlags::None, ResultInfo, CachedGroupsArray, Parameters.GeneralParameters.GroupsToExclude);

				UE_LOG(LogSubmitToolP4Debug, Log, TEXT("Running command: p4 -p %s -u %s -c %s -ztag groups"), TO_TCHAR(P4Client.GetPort().Text(), bIsUnicodeServer), *UserName, *Client);
				P4Client.Run(FROM_TCHAR(TEXT("groups"), bIsUnicodeServer), &P4User);

				for(const FText& Msg : ResultInfo.InfoMessages)
				{
					UE_LOG(LogSubmitToolP4Debug, Verbose, TEXT("p4 groups: %s"), *Msg.ToString());
				}

				if(ResultInfo.HasErrors())
				{
					for(const FText& Error : ResultInfo.ErrorMessages)
					{
						UE_LOG(LogSubmitToolP4, Error, TEXT("p4 groups: %s"), *Error.ToString());
					}
				}

				LoadRecentGroups();

				FScopeLock Lock(&Mutex);
				OnGroupsGetCallbacks.Broadcast(CachedGroupsArray);
				OnGroupsGetCallbacks.Clear();

				return !ResultInfo.HasErrors();

			});
	}
}

void FSubmitToolPerforce::LoadRecentGroups()
{
	const TUniquePtr<FAES::FAESKey>& Key = FCredentialsService::GetEncryptionKey();

	if(!Key.IsValid())
	{
		return;
	}

	FString RecentGroupsString;

	const FString FilePath = GetRecentGroupsFilepath();
	if(IFileManager::Get().FileExists(*FilePath))
	{
		FArchive* File = IFileManager::Get().CreateFileReader(*FilePath);

		if(File != nullptr)
		{
			int32 Version;
			*File << Version;

			// Check Versions here
			if(Version != RecentGroupsDatVersion)
			{
				UE_LOG(LogSubmitToolDebug, Warning, TEXT("Unexpected Recent Groups Version, aborting issues loading."));
				File->Close();
				delete File;
				File = nullptr;
				return;
			}

			int32 PaddedLength;
			int32 LengthWithoutPadding;

			*File << PaddedLength;
			*File << LengthWithoutPadding;

			TArray<uint8> DeserializedBytes;
			DeserializedBytes.SetNum(PaddedLength);
			File->Serialize(DeserializedBytes.GetData(), PaddedLength);

			FAES::DecryptData(DeserializedBytes.GetData(), DeserializedBytes.Num(), *Key);

			RecentGroupsString = BytesToString(DeserializedBytes.GetData(), LengthWithoutPadding);

			File->Close();
			delete File;
			File = nullptr;
		}
		else
		{
			UE_LOG(LogSubmitTool, Warning, TEXT("Could not read file '%s'."), *FilePath);
		}
	}
	else
	{
		UE_LOG(LogSubmitToolDebug, Log, TEXT("File %s does not exists, no recent groups were loaded"), *FilePath);
	}

	TArray<FString> GroupNames;
	RecentGroupsString.ParseIntoArray(GroupNames, TEXT(";"));

	RecentGroups.Empty();
	for(FString& Name : GroupNames)
	{
		if(const TSharedPtr<FString>* Group = CachedGroupsArray.FindByPredicate([&Name](TSharedPtr<FString>& InStr) { return InStr->Equals(Name, ESearchCase::IgnoreCase); }))
		{
			RecentGroups.Add(*Group);
		}
	}
}

void FSubmitToolPerforce::SaveRecentGroups()
{
	const FString RecentGroupsString = FString::JoinBy(RecentGroups, TEXT(";"), [](const TSharedPtr<FString>& Group) { return *Group; });
	const TUniquePtr<FAES::FAESKey>& Key = FCredentialsService::GetEncryptionKey();

	const FString FilePath = GetRecentGroupsFilepath();
	FArchive* File = IFileManager::Get().CreateFileWriter(*FilePath, EFileWrite::FILEWRITE_EvenIfReadOnly);

	if(File == nullptr)
	{
		UE_LOG(LogSubmitTool, Warning, TEXT("Could not create file '%s'."), *FilePath);
		return;
	}

	TArray<uint8> Bytes;
	Bytes.SetNumUninitialized(RecentGroupsString.Len());
	StringToBytes(RecentGroupsString, Bytes.GetData(), RecentGroupsString.Len());

	int32 ActualLength = Bytes.Num();

	int32 NumBytesEncrypted = Align(Bytes.Num(), FAES::AESBlockSize);
	Bytes.SetNum(NumBytesEncrypted);
	FAES::EncryptData(Bytes.GetData(), Bytes.Num(), *Key);

	int32 Version = RecentGroupsDatVersion;
	*File << Version;

	*File << NumBytesEncrypted;
	*File << ActualLength;

	File->Serialize(Bytes.GetData(), Bytes.Num());

	File->Close();
	delete File;
	File = nullptr;
}

const TArray<TSharedPtr<FString>>& FSubmitToolPerforce::GetRecentGroups() const
{
	return RecentGroups;
}

void FSubmitToolPerforce::AddRecentGroup(TSharedPtr<FString>& Group)
{
	if(RecentGroups.Contains(Group))
	{
		// Remove so we can push the user to the top
		RecentGroups.Remove(Group);
	}

	if(RecentGroups.Num() >= MaxRecentGroups)
	{
		RecentGroups.RemoveAt(MaxRecentGroups - 1);
	}

	RecentGroups.EmplaceAt(0, Group);
	SaveRecentGroups();
}

const FString FSubmitToolPerforce::GetRecentGroupsFilepath() const
{
	return FPaths::Combine(FSubmitToolUtils::GetLocalAppDataPath(), TEXT("SubmitTool"), TEXT("recent_groups.dat"));
}

UE::Tasks::TTask<bool> FSubmitToolPerforce::DownloadFiles(const FString& InFilepath, TArray<FSharedBuffer>& OutFileBuffers)
{
	return RunCommand(TEXT("print"), { TEXT("-q"), InFilepath }, nullptr, &OutFileBuffers);
}

void FSubmitToolPerforce::GetUsersAndGroups(const FOnUsersAndGroupsGet::FDelegate& Callback)
{
	OnUsersAndGroupsGetCallbacks.Add(Callback);

	auto OnUsersAndGroupsReady = [this]() {
		if(bUsersReady && bGroupsReady)
		{
			OnUsersAndGroupsGetCallbacks.Broadcast(CachedUsersArray, CachedGroupsArray);
			OnUsersAndGroupsGetCallbacks.Clear();

		}
	};

	GetUsers(FOnUsersGet::FDelegate::CreateLambda([OnUsersAndGroupsReady, this](TArray<TSharedPtr<FUserData>>&) { bUsersReady = true; OnUsersAndGroupsReady(); }));
	GetGroups(FOnGroupsGet::FDelegate::CreateLambda([OnUsersAndGroupsReady, this](TArray<TSharedPtr<FString>>&) { bGroupsReady = true; OnUsersAndGroupsReady(); }));
}

UE::Tasks::TTask<bool> FSubmitToolPerforce::RunCommand(const FString& InCommand, const TArray<FString>& InAdditionalArgs, FOnSCCCommandComplete InCompleteCallback, TArray<FSharedBuffer>* OutData)
{
	return UE::Tasks::Launch(UE_SOURCE_LOCATION, [this, InCommand, InAdditionalArgs, InCompleteCallback, OutData] {
		FSCCRecordSet ResultValues;
		FSourceControlResultInfo ResultInfo;

		EP4ClientUserFlags Flags = DefaultFlags;
		Flags |= bIsUnicodeServer ? EP4ClientUserFlags::UnicodeServer : EP4ClientUserFlags::None;

		bool bResult = RunP4Command(InCommand, InAdditionalArgs, ResultValues, ResultInfo, Flags, OutData);

		InCompleteCallback.ExecuteIfBound(bResult, ResultValues, ResultInfo);

		return bResult;
		});
}

const TArray<FSCCStream*>& FSubmitToolPerforce::GetClientStreams() const
{
	FScopeLock Stream(&StreamMutex);
	return StreamHierarchy;
}

const FSCCStream* FSubmitToolPerforce::GetSCCStream(const FString& InStreamName)
{
	if(Streams.Contains(InStreamName))
	{
		FScopeLock Stream(&StreamMutex);
		return Streams[InStreamName].Get();
	}
	else
	{
		if(GetStream(InStreamName, false).GetResult() && Streams.Contains(InStreamName))
		{
			FScopeLock Stream(&StreamMutex);
			return Streams[InStreamName].Get();
		}
		else
		{
			return nullptr;
		}
	}
}

const FString FSubmitToolPerforce::GetRootStreamName()
{
	if(StreamHierarchy.Num() == 0)
	{
		GetStream().GetResult();
	}

	FScopeLock Stream(&StreamMutex);
	if(StreamHierarchy.Num() > 0)
	{
		return StreamHierarchy[0]->Name;
	}

	return FString();
}

const FString FSubmitToolPerforce::GetCurrentStreamName()
{
	if(StreamHierarchy.Num() == 0)
	{
		GetStream().GetResult();
	}

	FScopeLock Stream(&StreamMutex);
	if(StreamHierarchy.Num() > 0)
	{
		return StreamHierarchy.Last()->Name;
	}

	return FString();
}

const size_t FSubmitToolPerforce::GetDepotStreamLength(const FString& InDepotName)
{
	if(!DepotStreamLengths.Contains(InDepotName))
	{
		RunCommand(TEXT("depot"), { TEXT("-o"), InDepotName }, FOnSCCCommandComplete::CreateLambda(
			[this, InDepotName](bool bSuccess, const FSCCRecordSet& InResultValues, const FSourceControlResultInfo& InResultsInfo)
			{
				if(bSuccess)
				{
					if(InResultValues[0].Contains(TEXT("StreamDepth")))
					{
						const FString& StreamDepth = InResultValues[0][TEXT("StreamDepth")];

						size_t Depth = 0;
						for(size_t i = 2; i < StreamDepth.Len(); ++i)
						{
							if(StreamDepth[i] == TCHAR('/'))
							{
								Depth++;
							}
						}

						DepotStreamLengths.Add(InDepotName, Depth);
					}
				}
			})).Wait();
	}

	return DepotStreamLengths[InDepotName];
}

const FAuthTicket& FSubmitToolPerforce::GetAuthTicket()
{
	if(!P4Ticket.IsValid())
	{
		TUniquePtr<FP4Connection> Connection = Connections.GetAvailableConnection();
		const StrPtr& Username = Connection->GetConnection().GetUser();
		const StrPtr& Ticket = Connection->GetConnection().GetPassword();
		P4Ticket = FAuthTicket(TO_TCHAR(Username.Text(), bIsUnicodeServer), TO_TCHAR(Ticket.Text(), bIsUnicodeServer));
	}

	return P4Ticket;
}

bool FSubmitToolPerforce::RunP4Command(const FString& InCommand, const TArray<FString>& InAdditionalArgs, TArray<TMap<FString, FString>>& OutResultValues, FSourceControlResultInfo& OutResults, EP4ClientUserFlags InCmdFlags, TArray<FSharedBuffer>* OutData)
{
	InCmdFlags |= bIsUnicodeServer ? EP4ClientUserFlags::UnicodeServer : EP4ClientUserFlags::None;
	InCmdFlags |= OutData != nullptr ? EP4ClientUserFlags::CollectData : EP4ClientUserFlags::None;

	TUniquePtr<FP4Connection> Connection = Connections.GetAvailableConnection();

	size_t Attempts = 0;
	while(!Connection.IsValid())
	{
		if(Attempts >= MaxConnectionsAttempts)
		{
			return false;
		}

		FPlatformProcess::Sleep(1.f);
		Connection = Connections.GetAvailableConnection();

		++Attempts;
	}

	ClientApi& P4Client = Connection->GetConnection();
	FString FullCommand = TEXT("p4 -p ");
	FullCommand += TO_TCHAR(P4Client.GetPort().Text(), bIsUnicodeServer);

	if(EnumHasAnyFlags(InCmdFlags, EP4ClientUserFlags::UseUser))
	{
		FString UserName;
		FCmdLineParameters::Get().GetValue(FSubmitToolCmdLine::P4User, UserName);
		P4Client.SetUser(TCHAR_TO_ANSI(*UserName));

		FullCommand += TEXT(" -u ") + UserName;
	}

	if(EnumHasAnyFlags(InCmdFlags, EP4ClientUserFlags::UseClient))
	{
		FString Client;
		FCmdLineParameters::Get().GetValue(FSubmitToolCmdLine::P4Client, Client);
		P4Client.SetClient(TCHAR_TO_ANSI(*Client));

		FullCommand += TEXT(" -c ") + Client;
	}

	if(EnumHasAnyFlags(InCmdFlags, EP4ClientUserFlags::UseZTag))
	{
		P4Client.SetProtocol("tag", "");
		FullCommand += TEXT(" -ztag");
	}

	FullCommand += TEXT(" ");
	FullCommand += InCommand;

	int32 ArgC = InAdditionalArgs.Num();
	UTF8CHAR** ArgV = new UTF8CHAR * [ArgC];
	for(int32 Index = 0; Index < ArgC; Index++)
	{
		if(bIsUnicodeServer)
		{
			FTCHARToUTF8 UTF8String(*InAdditionalArgs[Index]);
			ArgV[Index] = new UTF8CHAR[UTF8String.Length() + 1];
			FMemory::Memcpy(ArgV[Index], UTF8String.Get(), UTF8String.Length() + 1);
		}
		else
		{
			ArgV[Index] = new UTF8CHAR[InAdditionalArgs[Index].Len() + 1];
			FMemory::Memcpy(ArgV[Index], TCHAR_TO_ANSI(*InAdditionalArgs[Index]), InAdditionalArgs[Index].Len() + 1);
		}

		FullCommand += TEXT(" ") + InAdditionalArgs[Index];
	}

	P4Client.SetArgv(ArgC, (char**)ArgV);
	FSTClientUser P4User(OutResultValues, InCmdFlags, OutResults);

	UE_LOG(LogSubmitToolP4Debug, Log, TEXT("Running command: %s"), *FullCommand);
	P4Client.Run(FROM_TCHAR(*InCommand, bIsUnicodeServer), &P4User);

	for(const FText& Msg : OutResults.InfoMessages)
	{
		UE_LOG(LogSubmitToolP4Debug, Verbose, TEXT("%s: %s"), *FullCommand, *Msg.ToString());
	}

	if(OutResults.HasErrors())
	{
		for(const FText& Error : OutResults.ErrorMessages)
		{
			UE_LOG(LogSubmitToolP4, Error, TEXT("%s: %s"), *FullCommand, *Error.ToString());
		}
	}

	if(OutData != nullptr)
	{
		*OutData = P4User.ReleaseData();
	}

	// Free arguments
	for(int32 Index = 0; Index < ArgC; Index++)
	{
		delete[] ArgV[Index];
	}
	delete[] ArgV;

	return !OutResults.HasErrors();
}
