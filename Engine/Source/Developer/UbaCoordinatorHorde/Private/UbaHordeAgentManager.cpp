// Copyright Epic Games, Inc. All Rights Reserved.

#include "UbaHordeAgentManager.h"
#include "HAL/Event.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "Storage/StorageClient.h"
#include "Storage/Clients/BundleStorageClient.h"
#include "Storage/Clients/MemoryStorageClient.h"
#include "Storage/Clients/FileStorageClient.h"
#include "Storage/Nodes/ChunkNode.h"
#include "Storage/Nodes/DirectoryNode.h"
#include "Storage/BlobWriter.h"
#include "UbaBase.h"
#include "UbaHordeAgent.h"
#include <filesystem>
#include <fstream>

namespace UbaCoordinatorHordeModule
{
	static bool bHordeForwardAgentLogs = false;
	static FAutoConsoleVariableRef CVarUbaControllerHordeForwardAgentLogs(
		TEXT("r.UbaHorde.ForwardAgentLogs"),
		bHordeForwardAgentLogs,
		TEXT("Enables or disables logging of stdout on agent side to show in controller log."));
}

FUbaHordeAgentManager::FUbaHordeAgentManager(const FString& InWorkingDir, const FString& InBinariesPath)
	:	WorkingDir(InWorkingDir)
	,	BinariesPath(InBinariesPath)
	,	LastRequestFailTime(1)
	,	TargetCoreCount(0)
	,	EstimatedCoreCount(0)
	,   ActiveCoreCount(0)
	,	bAskForAgents(true)
{
}

FUbaHordeAgentManager::~FUbaHordeAgentManager()
{
	FScopeLock AgentsScopeLock(&AgentsLock);
	for (TUniquePtr<FHordeAgentWrapper>& Agent : Agents)
	{
		Agent->ShouldExit->Trigger();
		Agent->Thread.Join();
		FGenericPlatformProcess::ReturnSynchEventToPool(Agent->ShouldExit);
	}
}

void FUbaHordeAgentManager::SetTargetCoreCount(uint32 Count)
{
	TargetCoreCount = FMath::Min(MaxCores, Count);

	while (EstimatedCoreCount < TargetCoreCount)
	{
		if (!bAskForAgents)
		{
			return;
		}

		//UE_LOG(LogUbaHorde, Display, TEXT("Requested new agent. Estimated core count: %u, Target core count: %u"), EstimatedCoreCount.Load(), TargetCoreCount.Load());
		RequestAgent();
	}

	FScopeLock AgentsScopeLock(&AgentsLock);
	for (auto Iterator = Agents.CreateIterator(); Iterator; ++Iterator)
	{
		TUniquePtr<FHordeAgentWrapper>& Agent = *Iterator;
		if (Agent->ShouldExit->Wait(0))
		{
			Agent->Thread.Join();
			FGenericPlatformProcess::ReturnSynchEventToPool(Agent->ShouldExit);
			Iterator.RemoveCurrentSwap();
		}
	}
}

void FUbaHordeAgentManager::SetAddClientCallback(AddClientCallback* callback, void* userData)
{
	m_callback = callback;
	m_userData = userData;
}

int32 FUbaHordeAgentManager::GetAgentCount() const
{
	FScopeLock AgentsScopeLock(&AgentsLock);
	return Agents.Num();
}

uint32 FUbaHordeAgentManager::GetActiveCoreCount() const
{
	return ActiveCoreCount;
}

// Creates a bundle blob (one of several chunks of a file) to be uploaded to Horde
// This code has been adopted from the HordeTest project.
// See 'Engine/Source/Programs/Horde/Samples/HordeTest/Main.cpp'.
static FBlobHandleWithHash CreateHordeBundleBlob(const std::filesystem::path& Path, FBlobWriter& Writer, int64& OutLength, FIoHash& OutStreamHash)
{
	OutLength = 0;

	FChunkNodeWriter ChunkWriter(Writer);

	std::ifstream Stream(Path, std::ios::binary);

	char ReadBuffer[4096];
	while (!Stream.eof())
	{
		Stream.read(ReadBuffer, sizeof(ReadBuffer));

		const int64 ReadSize = static_cast<int64>(Stream.gcount());
		if (ReadSize == 0)
		{
			break;
		}
		OutLength += ReadSize;

		ChunkWriter.Write(FMemoryView(ReadBuffer, ReadSize));
	}

	return ChunkWriter.Flush(OutStreamHash);
}

static FDirectoryEntry CreateHordeBundleDirectoryEntry(const std::filesystem::path& Path, FBlobWriter& Writer)
{
	FDirectoryNode DirectoryNode;

	int64 BlobLength = 0;
	FIoHash StreamHash;
	FBlobHandleWithHash Target = CreateHordeBundleBlob(Path, Writer, BlobLength, StreamHash);

	EFileEntryFlags Flags = EFileEntryFlags::Executable;
	FFileEntry NewEntry(Target, FUtf8String(Path.filename().string().c_str()), Flags, BlobLength, StreamHash, FSharedBufferView());

	const FUtf8String Name = NewEntry.Name;
	const int64 Length = NewEntry.Length;
	DirectoryNode.NameToFile.Add(Name, MoveTemp(NewEntry));

	FBlobHandle DirectoryHandle = DirectoryNode.Write(Writer);

	return FDirectoryEntry(DirectoryHandle, FIoHash(), FUtf8String(Path.filename().string().c_str()), Length);
}

bool CreateHordeBundleFromFile(const std::filesystem::path& InputFilename, const std::filesystem::path& OutputFilename)
{
	TSharedRef<FFileStorageClient> FileStorage = MakeShared<FFileStorageClient>(OutputFilename.parent_path());
	TSharedRef<FBundleStorageClient> Storage = MakeShared<FBundleStorageClient>(FileStorage);

	TUniquePtr<FBlobWriter> Writer = Storage->CreateWriter("");
	FDirectoryEntry RootEntry = CreateHordeBundleDirectoryEntry(InputFilename, *Writer.Get());
	Writer->Flush();

	FFileStorageClient::WriteRefToFile(OutputFilename, RootEntry.Target->GetLocator());
	return true;
}

void FUbaHordeAgentManager::RequestAgent()
{
	EstimatedCoreCount += 32; // We estimate a typical agent to have 32 cores

	FScopeLock AgentsScopeLock(&AgentsLock);
	FHordeAgentWrapper& Wrapper = *Agents.Emplace_GetRef(MakeUnique<FHordeAgentWrapper>());

	Wrapper.ShouldExit = FGenericPlatformProcess::GetSynchEventFromPool(true);
	Wrapper.Thread = FThread(TEXT("HordeAgent"), [this, WrapperPtr = &Wrapper]() { ThreadAgent(*WrapperPtr); });
}

void FUbaHordeAgentManager::ThreadAgent(FHordeAgentWrapper& Wrapper)
{
	FEvent& ShouldExit = *Wrapper.ShouldExit;
	TUniquePtr<FUbaHordeAgent> Agent;
	bool bSuccess = false;

	ON_SCOPE_EXIT
	{
		if (Agent)
		{
			Agent->CloseConnection();
		}

		ShouldExit.Trigger();
	};

	int MachineCoreCount = 0;

	// If no host is specified, we need to start the agent in listen mode
	const bool bUseListen = UbaHost.IsEmpty();

	{
		ON_SCOPE_EXIT{ EstimatedCoreCount -= 32; };

		#if PLATFORM_WINDOWS
		const char* AppName = "UbaAgent.exe";
		#else
		const char* AppName = "UbaAgent";
		#endif

		FScopeLock ScopeLock(&BundleRefPathsLock);
		if (BundleRefPaths.IsEmpty())
		{
			struct FBundleRec
			{
				const TCHAR* Filename;
				const TCHAR* BundleRef;
			};
			const FBundleRec BundleRecs[] =
			{
#if PLATFORM_WINDOWS
				{ TEXT("UbaAgent.exe"), TEXT("UbaAgent.Bundle.ref") },
#elif PLATFORM_LINUX
				{ TEXT("UbaAgent"), TEXT("UbaAgent.Bundle.ref") },
				{ TEXT("UbaAgent.debug"), TEXT("UbaAgent.debug.Bundle.ref") },
//				{ TEXT("libclang_rt.tsan.so"), TEXT("Tsan.Bundle.ref") }, // for debugging
#elif PLATFORM_MAC
				{ TEXT("UbaAgent"), TEXT("UbaAgent.Bundle.ref") },
#endif
			};

			for (const FBundleRec& Rec : BundleRecs)
			{
				const FString FilePath = FPaths::Combine(BinariesPath, Rec.Filename);
				FString BundlePath = FPaths::Combine(WorkingDir, Rec.BundleRef);

				if (!CreateHordeBundleFromFile(*FilePath, *BundlePath))
				{
					UE_LOG(LogUbaHorde, Error, TEXT("Failed to create Horde bundle for: %s"), *FilePath);
					bAskForAgents = false;
					return;
				}
				UE_LOG(LogUbaHorde, Display, TEXT("Created Horde bundle for: %s"), *FilePath);
				BundleRefPaths.Add(BundlePath);
			}
		}

		if (!HordeMetaClient)
		{
			// Create Horde meta client right before we need it to make sure the CVar for the server URL has been read by now
			HordeMetaClient = MakeUnique<FUbaHordeMetaClient>();
			if (!HordeMetaClient->RefreshHttpClient())
			{
				UE_LOG(LogUbaHorde, Error, TEXT("Failed to create HttpClient for UbaAgent"));
				bAskForAgents = false;
				return;
			}
		}

		if (!bAskForAgents)
		{
			return;
		}

		if (LastRequestFailTime == 0)
		{
			ScopeLock.Unlock();
		}
		else
		{
			// Try to reduce pressure on horde by not asking for machines more frequent than every 5 seconds if failed to retrieve last time
			uint64 CurrentTime = FPlatformTime::Cycles64();
			uint32 MsSinceLastFail = uint32(double(CurrentTime - LastRequestFailTime) * FPlatformTime::GetSecondsPerCycle() * 1000);
			if (MsSinceLastFail < 5000)
			{
				if (ShouldExit.Wait(5000 - MsSinceLastFail))
				{
					return;
				}
			}
		}

		TSharedPtr<FUbaHordeMetaClient::HordeMachinePromise, ESPMode::ThreadSafe> Promise = HordeMetaClient->RequestMachine(Pool);
		if (!Promise)
		{
			//UE_LOG(LogUbaHorde, Error, TEXT("Failed to create Horde bundle for UbaAgent executable: %s"), *UbaAgentFilePath);
			return;
		}
		TFuture<TTuple<FHttpResponsePtr, FHordeRemoteMachineInfo>> Future = Promise->GetFuture();
		Future.Wait();
		FHordeRemoteMachineInfo MachineInfo = Future.Get().Value;

		// If the machine couldn't be assigned, just ignore this agent slot
		if (MachineInfo.Ip == TEXT(""))
		{
			if (!LastRequestFailTime)
			{
				UE_LOG(LogUbaHorde, Verbose, TEXT("No resources available in Horde. Will keep retrying until %u cores are used (Currently have %u)"), TargetCoreCount.Load(), ActiveCoreCount.Load());
			}
			LastRequestFailTime = FPlatformTime::Cycles64();
			return;
		}

		LastRequestFailTime = 0;

		ScopeLock.Unlock();

		if (ShouldExit.Wait(0))
		{
			return;
		}

		Agent = MakeUnique<FUbaHordeAgent>(MachineInfo);

		if (!Agent->IsValid())
		{
			return;
		}

		if (!Agent->BeginCommunication())
		{
			return;
		}

		for (const FString& Bundle : BundleRefPaths)
		{
			TArray<uint8> Locator;
			if (!FFileHelper::LoadFileToArray(Locator, *Bundle))
			{
				UE_LOG(LogUbaHorde, Error, TEXT("Cannot launch Horde processes for UBA controller because bundle path could not be found: %s"), *Bundle);
				return;
			}
			Locator.Add('\0');

			FString BundleDirectory = FPaths::GetPath(Bundle);

			if (ShouldExit.Wait(0))
			{
				return;
			}

			if (!Agent->UploadBinaries(BundleDirectory, reinterpret_cast<const char*>(Locator.GetData())))
			{
				return;
			}
		}

		// Start the UBA Agent that will connect to us, requesting for work

		const FAnsiString AgentConnectionArg = bUseListen ? FAnsiString::Printf("-listen=%u", UbaPort) : FAnsiString::Printf("-Host=%s:%u", *UbaHost, UbaPort);

		const char* UbaAgentArgs[] =
		{
			*AgentConnectionArg,
			"-nopoll",				// -nopoll recommended when running on remote Horde agents to make sure they exit after completion. Otherwise, it keeps running.
			"-listenTimeout=5",		// Agent will wait 5 seconds for this thread to connect (Server_AddClient does the connect)
			"-quiet",				// Skip all the agent logging that would be sent over to here
			"-maxidle=15",			// After 15 seconds of idling agent will automatically disconnect
			"-Dir=%UE_HORDE_SHARED_DIR%\\Uba",
			"-Eventfile=%UE_HORDE_TERMINATION_SIGNAL_FILE%",
		};

		// If the machine does not run Windows, enable the compatibility layer Wine to run UbaAgent.exe on POSIX systems
		#if PLATFORM_WINDOWS
		const bool bRunsWindowsOS = Agent->GetMachineInfo().bRunsWindowOS;
		const bool bUseWine = !bRunsWindowsOS;
		#else
		const bool bUseWine = false;
		#endif

		if (ShouldExit.Wait(0))
		{
			return;
		}

		Agent->Execute(AppName, UbaAgentArgs, UE_ARRAY_COUNT(UbaAgentArgs), nullptr, nullptr, 0, bUseWine);

		// Log remote execution
		FString UbaAgentCmdArgs = ANSI_TO_TCHAR(AppName);
		for (const char* Arg : UbaAgentArgs)
		{
			UbaAgentCmdArgs += TEXT(" ");
			UbaAgentCmdArgs += ANSI_TO_TCHAR(Arg);
		}
		UE_LOG(LogUbaHorde, Log, TEXT("Remote execution on Horde machine [%s:%u]: %s"), *Agent->GetMachineInfo().Ip, UbaPort, *UbaAgentCmdArgs);

		MachineCoreCount = MachineInfo.LogicalCores;
		EstimatedCoreCount += MachineCoreCount;
		ActiveCoreCount += MachineCoreCount;
	}

	uint32 callCounter = 0; // TODO: This should react on the listen string instead of waiting for two text messages :)

	while (Agent->IsValid() && !ShouldExit.Wait(100))
	{
		Agent->Poll(UbaCoordinatorHordeModule::bHordeForwardAgentLogs);

		if (!bUseListen)
		{
			continue;
		}

		if (callCounter++ == 2)
		{
			// Add this machine as client to the remote agent
			const FString& IpAddress = Agent->GetMachineInfo().Ip;
			const bool bAddClientSuccess = 	m_callback(m_userData, StringCast<uba::tchar>(*IpAddress).Get(), static_cast<uint16>(UbaPort));

			if (!bAddClientSuccess)
			{
				UE_LOG(LogUbaHorde, Display, TEXT("Server_AddClient(%s:%u) failed"), *IpAddress, UbaPort);
				return;
			}
		}
	}

	ActiveCoreCount -= MachineCoreCount;
	EstimatedCoreCount -= MachineCoreCount;
}
