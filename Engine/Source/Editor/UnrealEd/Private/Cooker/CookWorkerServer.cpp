// Copyright Epic Games, Inc. All Rights Reserved.

#include "CookWorkerServer.h"

#include "Algo/Find.h"
#include "Commandlets/AssetRegistryGenerator.h"
#include "Containers/AnsiString.h"
#include "Cooker/CompactBinaryTCP.h"
#include "Cooker/CookDirector.h"
#include "Cooker/CookGenerationHelper.h"
#include "Cooker/CookPackageData.h"
#include "Cooker/CookPlatformManager.h"
#include "HAL/Platform.h"
#include "HAL/PlatformProcess.h"
#include "Interfaces/ITargetPlatform.h"
#include "Interfaces/ITargetPlatformManagerModule.h"
#include "Logging/StructuredLog.h"
#include "Logging/StructuredLogFormat.h"
#include "Math/NumericLimits.h"
#include "Misc/AssertionMacros.h"
#include "Misc/Char.h"
#include "Misc/FeedbackContext.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/ScopeLock.h"
#include "PackageResultsMessage.h"
#include "PackageTracker.h"
#include "UnrealEdMisc.h"

namespace UE::Cook
{

FCookWorkerServer::FCookWorkerServer(FCookDirector& InDirector, int32 InProfileId, FWorkerId InWorkerId)
	: Director(InDirector)
	, COTFS(InDirector.COTFS)
	, ProfileId(InProfileId)
	, WorkerId(InWorkerId)
{
}

FCookWorkerServer::~FCookWorkerServer()
{
	FCommunicationScopeLock ScopeLock(this, ECookDirectorThread::CommunicateThread, ETickAction::Queue);

	checkf(PendingPackages.IsEmpty() && PackagesToAssign.IsEmpty(),
		TEXT("CookWorkerServer still has assigned packages when it is being destroyed; we will leak them and block the cook."));

	if (ConnectStatus == EConnectStatus::Connected || ConnectStatus == EConnectStatus::PumpingCookComplete
		|| ConnectStatus == EConnectStatus::WaitForDisconnect)
	{
		UE_LOG(LogCook, Error,
			TEXT("CookWorkerServer %d was destroyed before it finished Disconnect. The remote process may linger and may interfere with writes of future packages."),
			ProfileId);
	}
	DetachFromRemoteProcess(EWorkerDetachType::StillRunning);
}

void FCookWorkerServer::DetachFromRemoteProcess(EWorkerDetachType DetachType)
{
	if (Socket != nullptr)
	{
		FCoreDelegates::OnMultiprocessWorkerDetached.Broadcast({WorkerId.GetMultiprocessId(), DetachType != EWorkerDetachType::Dismissed});
	}
	Sockets::CloseSocket(Socket);
	CookWorkerHandle = FProcHandle();
	CookWorkerProcessId = 0;
	bTerminateImmediately = false;
	SendBuffer.Reset();
	ReceiveBuffer.Reset();

	if (bNeedCrashDiagnostics)
	{
		SendCrashDiagnostics();
	}
}

bool TryParseLogCategoryVerbosityMessage(FStringView Line, FName& OutCategory, ELogVerbosity::Type& OutVerbosity,
	FStringView& OutMessage)
{
	TPair<FStringView, ELogVerbosity::Type> VerbosityMarkers[]{
		{ TEXTVIEW(": Fatal:"), ELogVerbosity::Fatal },
		{ TEXTVIEW(": Error:"), ELogVerbosity::Error },
		{ TEXTVIEW(": Warning:"), ELogVerbosity::Warning},
		{ TEXTVIEW(": Display:"), ELogVerbosity::Display },
		{ TEXTVIEW(":"), ELogVerbosity::Log },
	};


	// Find the first colon not in brackets and look for ": <Verbosity>:". This is complicated by Log verbosity not
	// printing out the Verbosity:
	// [2023.03.20-16.32.48:878][  0]LogCook: MessageText
	// [2023.03.20-16.32.48:878][  0]LogCook: Display: MessageText

	int32 FirstColon = INDEX_NONE;
	int32 SubExpressionLevel = 0;
	for (int32 Index = 0; Index < Line.Len(); ++Index)
	{
		switch (Line[Index])
		{
		case '[':
			++SubExpressionLevel;
			break;
		case ']':
			if (SubExpressionLevel > 0)
			{
				--SubExpressionLevel;
			}
			break;
		case ':':
			if (SubExpressionLevel == 0)
			{
				FirstColon = Index;
			}
			break;
		default:
			break;
		}
		if (FirstColon != INDEX_NONE)
		{
			break;
		}
	}
	if (FirstColon == INDEX_NONE)
	{
		return false;
	}

	FStringView RestOfLine = FStringView(Line).RightChop(FirstColon);
	for (TPair<FStringView, ELogVerbosity::Type>& VerbosityPair : VerbosityMarkers)
	{
		if (RestOfLine.StartsWith(VerbosityPair.Key, ESearchCase::IgnoreCase))
		{
			int32 CategoryEndIndex = FirstColon;
			while (CategoryEndIndex > 0 && FChar::IsWhitespace(Line[CategoryEndIndex - 1])) --CategoryEndIndex;
			int32 CategoryStartIndex = CategoryEndIndex > 0 ? CategoryEndIndex - 1 : CategoryEndIndex;
			while (CategoryStartIndex > 0 && FChar::IsAlnum(Line[CategoryStartIndex - 1])) --CategoryStartIndex;
			int32 MessageStartIndex = CategoryEndIndex + VerbosityPair.Key.Len();
			while (MessageStartIndex < Line.Len() && FChar::IsWhitespace(Line[MessageStartIndex])) ++MessageStartIndex;

			OutCategory = FName(FStringView(Line).SubStr(CategoryStartIndex, CategoryEndIndex - CategoryStartIndex));
			OutVerbosity = VerbosityPair.Value;
			OutMessage = FStringView(Line).SubStr(MessageStartIndex, Line.Len() - MessageStartIndex);
			return true;
		}
	}
	return false;
}

void FCookWorkerServer::SendCrashDiagnostics()
{
	FString LogFileName = Director.GetWorkerLogFileName(ProfileId);
	UE_LOG(LogCook, Display,
		TEXT("LostConnection to CookWorker %d. Log messages written after communication loss:"), ProfileId);
	FString LogText;
	// To be able to open a file for read that might be open for write from another process,
	// we have to specify FILEREAD_AllowWrite
	int32 ReadFlags = FILEREAD_AllowWrite;
	bool bLoggedErrorMessage = false;
	if (!FFileHelper::LoadFileToString(LogText, *LogFileName, FFileHelper::EHashOptions::None, ReadFlags))
	{
		UE_LOG(LogCook, Warning, TEXT("No log file found for CookWorker %d."), ProfileId);
	}
	else
	{
		FString LastSentHeartbeat = FString::Printf(TEXT("%.*s %d"), HeartbeatCategoryText.Len(),
			HeartbeatCategoryText.GetData(), LastReceivedHeartbeatNumber);
		int32 StartIndex = INDEX_NONE;
		for (FStringView MarkerText : { FStringView(LastSentHeartbeat),
			HeartbeatCategoryText, TEXTVIEW("Connection to CookDirector successful") })
		{
			StartIndex = UE::String::FindLast(LogText, MarkerText);
			if (StartIndex >= 0)
			{
				break;
			}
		}
		const TCHAR* StartText = *LogText;
		FString Line;
		if (StartIndex != INDEX_NONE)
		{
			// Skip the MarkerLine
			StartText = *LogText + StartIndex;
			FParse::Line(&StartText, Line);
			if (*StartText == '\0')
			{
				// If there was no line after the MarkerLine, write out the MarkerLine
				StartText = *LogText + StartIndex;
			}
		}

		while (FParse::Line(&StartText, Line))
		{
			// Get the Category,Severity,Message out of each line and log it with that Category and Severity
			// TODO: Change the CookWorkers to write out structured logs rather than interpreting their text logs
			FName Category;
			ELogVerbosity::Type Verbosity;
			FStringView Message;
			if (!TryParseLogCategoryVerbosityMessage(Line, Category, Verbosity, Message))
			{
				Category = LogCook.GetCategoryName();
				Verbosity = ELogVerbosity::Display;
				Message = Line;
			}
			// Downgrade Fatals in our local verbosity from Fatal to Error to avoid crashing the CookDirector
			if (Verbosity == ELogVerbosity::Fatal)
			{
				Verbosity = ELogVerbosity::Error;
			}
			bLoggedErrorMessage |= Verbosity == ELogVerbosity::Error;
			FMsg::Logf(__FILE__, __LINE__, Category, Verbosity, TEXT("[CookWorker %d]: %.*s"),
				ProfileId, Message.Len(), Message.GetData());
		}
	}
	if (!CrashDiagnosticsError.IsEmpty())
	{
		if (!bLoggedErrorMessage)
		{
			UE_LOG(LogCook, Error, TEXT("%s"), *CrashDiagnosticsError);
		}
		else
		{
			// When we already logged an error from the crashed worker, log the what-went-wrong as a warning rather
			// than an error, to avoid making it seem like a separate issue.
			UE_LOG(LogCook, Warning, TEXT("%s"), *CrashDiagnosticsError);
		}
	}

	bNeedCrashDiagnostics = false;
	CrashDiagnosticsError.Empty();
}

void FCookWorkerServer::ShutdownRemoteProcess()
{
	EWorkerDetachType DetachType = EWorkerDetachType::Dismissed;
	if (CookWorkerHandle.IsValid())
	{
		FPlatformProcess::TerminateProc(CookWorkerHandle, /* bKillTree */true);
		DetachType = EWorkerDetachType::ForceTerminated;
	}
	DetachFromRemoteProcess(DetachType);
}

void FCookWorkerServer::AppendAssignments(TArrayView<FPackageData*> Assignments,
	TMap<FPackageData*, FAssignPackageExtraData>&& ExtraDatas, TArrayView<FPackageData*> InfoPackages,
	ECookDirectorThread TickThread)
{
	FCommunicationScopeLock ScopeLock(this, TickThread, ETickAction::Queue);
	++PackagesAssignedFenceMarker;
	PackagesToAssign.Append(Assignments);
	PackagesToAssignExtraDatas.Append(MoveTemp(ExtraDatas));
	PackagesToAssignInfoPackages.Append(InfoPackages);
}

void FCookWorkerServer::AbortAllAssignments(TSet<FPackageData*>& OutPendingPackages, ECookDirectorThread TickThread)
{
	FCommunicationScopeLock ScopeLock(this, TickThread, ETickAction::Queue);
	AbortAllAssignmentsInLock(OutPendingPackages);
}

void FCookWorkerServer::AbortAllAssignmentsInLock(TSet<FPackageData*>& OutPendingPackages)
{
	if (PendingPackages.Num())
	{
		if (ConnectStatus == EConnectStatus::Connected)
		{
			TArray<FName> PackageNames;
			PackageNames.Reserve(PendingPackages.Num());
			for (FPackageData* PackageData : PendingPackages)
			{
				PackageNames.Add(PackageData->GetPackageName());
			}
			SendMessageInLock(FAbortPackagesMessage(MoveTemp(PackageNames)));
		}
		OutPendingPackages.Append(MoveTemp(PendingPackages));
		PendingPackages.Empty();
	}
	OutPendingPackages.Append(PackagesToAssign);
	PackagesToAssign.Empty();
	PackagesToAssignExtraDatas.Empty();
	PackagesToAssignInfoPackages.Empty();
	++PackagesRetiredFenceMarker;
}

void FCookWorkerServer::AbortAssignment(FPackageData& PackageData, ECookDirectorThread TickThread,
	ENotifyRemote NotifyRemote)
{
	FPackageData* PackageDataPtr = &PackageData;
	AbortAssignments(TConstArrayView<FPackageData*>(&PackageDataPtr, 1), TickThread, NotifyRemote);
}

void FCookWorkerServer::AbortAssignments(TConstArrayView<FPackageData*> PackageDatas, ECookDirectorThread TickThread,
	ENotifyRemote NotifyRemote)
{
	FCommunicationScopeLock ScopeLock(this, TickThread, ETickAction::Queue);

	TArray<FName> PackageNamesToMessage;
	bool bSignalRemote = ConnectStatus == EConnectStatus::Connected && NotifyRemote == ENotifyRemote::NotifyRemote;
	for (FPackageData* PackageData : PackageDatas)
	{
		if (PendingPackages.Remove(PackageData))
		{
			if (bSignalRemote)
			{
				PackageNamesToMessage.Add(PackageData->GetPackageName());
			}
		}

		PackagesToAssign.Remove(PackageData);
		PackagesToAssignExtraDatas.Remove(PackageData);
		// We don't remove InfoPackages from PackagesToAssignInfoPackages because it would be too hard to calculate,
		// and it's not a problem to send extra InfoPackages.
	}
	++PackagesRetiredFenceMarker;
	if (!PackageNamesToMessage.IsEmpty())
	{
		SendMessageInLock(FAbortPackagesMessage(MoveTemp(PackageNamesToMessage)));
	}
}

void FCookWorkerServer::AbortWorker(TSet<FPackageData*>& OutPendingPackages, ECookDirectorThread TickThread)
{
	FCommunicationScopeLock ScopeLock(this, TickThread, ETickAction::Tick);

	AbortAllAssignmentsInLock(OutPendingPackages);
	switch (ConnectStatus)
	{
	case EConnectStatus::Uninitialized: // Fall through
	case EConnectStatus::WaitForConnect:
		SendToState(EConnectStatus::LostConnection);
		break;
	case EConnectStatus::Connected: // Fall through
	case EConnectStatus::PumpingCookComplete:
	{
		SendMessageInLock(FAbortWorkerMessage(FAbortWorkerMessage::EType::Abort));
		SendToState(EConnectStatus::WaitForDisconnect);
		break;
	}
	default:
		break;
	}
}

void FCookWorkerServer::SendToState(EConnectStatus TargetStatus)
{
	switch (TargetStatus)
	{
	case EConnectStatus::WaitForConnect:
		ConnectStartTimeSeconds = FPlatformTime::Seconds();
		ConnectTestStartTimeSeconds = ConnectStartTimeSeconds;
		break;
	case EConnectStatus::WaitForDisconnect:
		ConnectStartTimeSeconds = FPlatformTime::Seconds();
		ConnectTestStartTimeSeconds = ConnectStartTimeSeconds;
		break;
	case EConnectStatus::PumpingCookComplete:
		ConnectStartTimeSeconds = FPlatformTime::Seconds();
		ConnectTestStartTimeSeconds = ConnectStartTimeSeconds;
		break;
	case EConnectStatus::LostConnection:
		DetachFromRemoteProcess(bNeedCrashDiagnostics ? EWorkerDetachType::Crashed : EWorkerDetachType::Dismissed);
		break;
	default:
		break;
	}
	ConnectStatus = TargetStatus;
}

bool FCookWorkerServer::IsConnected() const
{
	FScopeLock CommunicationScopeLock(&CommunicationLock);
	return ConnectStatus == EConnectStatus::Connected;
}

bool FCookWorkerServer::IsShuttingDown() const
{
	FScopeLock CommunicationScopeLock(&CommunicationLock);
	return ConnectStatus == EConnectStatus::PumpingCookComplete || ConnectStatus == EConnectStatus::WaitForDisconnect
		|| ConnectStatus == EConnectStatus::LostConnection;
}

bool FCookWorkerServer::IsFlushingBeforeShutdown() const
{
	FScopeLock CommunicationScopeLock(&CommunicationLock);
	return ConnectStatus == EConnectStatus::PumpingCookComplete;
}

bool FCookWorkerServer::IsShutdownComplete() const
{
	FScopeLock CommunicationScopeLock(&CommunicationLock);
	return ConnectStatus == EConnectStatus::LostConnection;
}

int32 FCookWorkerServer::NumAssignments() const
{
	FScopeLock CommunicationScopeLock(&CommunicationLock);
	return PackagesToAssign.Num() + PendingPackages.Num();
}

bool FCookWorkerServer::HasMessages() const
{
	FScopeLock CommunicationScopeLock(&CommunicationLock);
	return !ReceiveMessages.IsEmpty();
}

int32 FCookWorkerServer::GetLastReceivedHeartbeatNumber() const
{
	FScopeLock CommunicationScopeLock(&CommunicationLock);
	return LastReceivedHeartbeatNumber;

}
void FCookWorkerServer::SetLastReceivedHeartbeatNumberInLock(int32 InHeartbeatNumber)
{
	LastReceivedHeartbeatNumber = InHeartbeatNumber;
}

int32 FCookWorkerServer::GetPackagesAssignedFenceMarker() const
{
	FScopeLock CommunicationScopeLock(&CommunicationLock);
	return PackagesAssignedFenceMarker;
}

int32 FCookWorkerServer::GetPackagesRetiredFenceMarker() const
{
	FScopeLock CommunicationScopeLock(&CommunicationLock);
	return PackagesRetiredFenceMarker;
}

bool FCookWorkerServer::TryHandleConnectMessage(FWorkerConnectMessage& Message, FSocket* InSocket,
	TArray<UE::CompactBinaryTCP::FMarshalledMessage>&& OtherPacketMessages, ECookDirectorThread TickThread)
{
	FCommunicationScopeLock ScopeLock(this, TickThread, ETickAction::Tick);

	if (ConnectStatus != EConnectStatus::WaitForConnect)
	{
		return false;
	}
	check(!Socket);
	Socket = InSocket;

	SendToState(EConnectStatus::Connected);
	UE_LOG(LogCook, Display, TEXT("CookWorker %d connected after %.3fs."), ProfileId,
		static_cast<float>(FPlatformTime::Seconds() - ConnectStartTimeSeconds));
	for (UE::CompactBinaryTCP::FMarshalledMessage& OtherMessage : OtherPacketMessages)
	{
		ReceiveMessages.Add(MoveTemp(OtherMessage));
	}
	HandleReceiveMessagesInternal();
	const FInitialConfigMessage& InitialConfigMessage = Director.GetInitialConfigMessage();
	OrderedSessionPlatforms = InitialConfigMessage.GetOrderedSessionPlatforms();
	OrderedSessionAndSpecialPlatforms.Reset(OrderedSessionPlatforms.Num() + 1);
	OrderedSessionAndSpecialPlatforms.Append(OrderedSessionPlatforms);
	OrderedSessionAndSpecialPlatforms.Add(CookerLoadingPlatformKey);
	SendMessageInLock(InitialConfigMessage);
	return true;
}

void FCookWorkerServer::TickCommunication(ECookDirectorThread TickThread)
{
	FCommunicationScopeLock ScopeLock(this, TickThread, ETickAction::Tick);

	for (;;)
	{
		switch (ConnectStatus)
		{
		case EConnectStatus::Uninitialized:
			LaunchProcess();
			break;
		case EConnectStatus::WaitForConnect:
			TickWaitForConnect();
			if (ConnectStatus == EConnectStatus::WaitForConnect)
			{
				return; // Try again later
			}
			break;
		case EConnectStatus::Connected:
			PumpReceiveMessages();
			if (ConnectStatus == EConnectStatus::Connected)
			{
				SendPendingMessages();
				PumpSendMessages();
				return; // Tick duties complete; yield the tick
			}
			break;
		case EConnectStatus::PumpingCookComplete:
		{
			PumpReceiveMessages();
			if (ConnectStatus == EConnectStatus::PumpingCookComplete)
			{
				PumpSendMessages();
				constexpr float WaitForPumpCompleteTimeout = 10.f * 60;
				if (FPlatformTime::Seconds() - ConnectStartTimeSeconds <= WaitForPumpCompleteTimeout 
					|| IsCookIgnoreTimeouts())
				{
					return; // Try again later
				}
				UE_LOG(LogCook, Error,
					TEXT("CookWorker process of CookWorkerServer %d failed to finalize its cook within %.0f seconds; we will tell it to shutdown."),
					ProfileId, WaitForPumpCompleteTimeout);
				SendMessageInLock(FAbortWorkerMessage(FAbortWorkerMessage::EType::Abort));
				SendToState(EConnectStatus::WaitForDisconnect);
			}
			break;
		}
		case EConnectStatus::WaitForDisconnect:
			TickWaitForDisconnect();
			if (ConnectStatus == EConnectStatus::WaitForDisconnect)
			{
				return; // Try again later
			}
			break;
		case EConnectStatus::LostConnection:
			return; // Nothing further to do
		default:
			checkNoEntry();
			return;
		}
	}
}

void FCookWorkerServer::SignalHeartbeat(ECookDirectorThread TickThread, int32 HeartbeatNumber)
{
	FCommunicationScopeLock ScopeLock(this, TickThread, ETickAction::Tick);

	switch (ConnectStatus)
	{
	case EConnectStatus::Connected:
		SendMessageInLock(FHeartbeatMessage(HeartbeatNumber));
		break;
	default:
		break;
	}
}

void FCookWorkerServer::SignalCookComplete(ECookDirectorThread TickThread)
{
	FCommunicationScopeLock ScopeLock(this, TickThread, ETickAction::Tick);

	switch (ConnectStatus)
	{
	case EConnectStatus::Uninitialized: // Fall through
	case EConnectStatus::WaitForConnect:
		SendToState(EConnectStatus::LostConnection);
		break;
	case EConnectStatus::Connected:
		SendMessageInLock(FAbortWorkerMessage(FAbortWorkerMessage::EType::CookComplete));
		SendToState(EConnectStatus::PumpingCookComplete);
		break;
	default:
		break; // Already in a disconnecting state
	}
}

void FCookWorkerServer::LaunchProcess()
{
	FCookDirector::FLaunchInfo LaunchInfo = Director.GetLaunchInfo(WorkerId, ProfileId);
	bool bShowCookWorkers = LaunchInfo.ShowWorkerOption == FCookDirector::EShowWorker::SeparateWindows;

	CookWorkerHandle = FPlatformProcess::CreateProc(*LaunchInfo.CommandletExecutable, *LaunchInfo.WorkerCommandLine,
		true /* bLaunchDetached */, !bShowCookWorkers /* bLaunchHidden */, !bShowCookWorkers /* bLaunchReallyHidden */,
		&CookWorkerProcessId, 0 /* PriorityModifier */, *FPaths::GetPath(LaunchInfo.CommandletExecutable),
		nullptr /* PipeWriteChild */);
	if (CookWorkerHandle.IsValid())
	{
		UE_LOG(LogCook, Display,
			TEXT("CookWorkerServer %d launched CookWorker as WorkerId %d and PID %u with commandline \"%s\"."),
			ProfileId, WorkerId.GetRemoteIndex(), CookWorkerProcessId, *LaunchInfo.WorkerCommandLine);
		FCoreDelegates::OnMultiprocessWorkerCreated.Broadcast({WorkerId.GetMultiprocessId()});
		SendToState(EConnectStatus::WaitForConnect);
	}
	else
	{
		// GetLastError information was logged by CreateProc
		CrashDiagnosticsError = FString::Printf(
			TEXT("CookWorkerCrash: Failed to create process for CookWorker %d. Assigned packages will be returned to the director."),
			ProfileId);
		bNeedCrashDiagnostics = true;
		SendToState(EConnectStatus::LostConnection);
	}
}

void FCookWorkerServer::TickWaitForConnect()
{
	constexpr float TestProcessExistencePeriod = 1.f;
	constexpr float WaitForConnectTimeout = 60.f * 20;

	// When the Socket is assigned we leave the WaitForConnect state, and we set it to null before entering
	check(!Socket);

	double CurrentTime = FPlatformTime::Seconds();
	if (CurrentTime - ConnectTestStartTimeSeconds > TestProcessExistencePeriod)
	{
		if (!FPlatformProcess::IsProcRunning(CookWorkerHandle))
		{
			CrashDiagnosticsError = FString::Printf(
				TEXT("CookWorkerCrash: CookWorker %d process terminated before connecting. Assigned packages will be returned to the director."),
				ProfileId);
			bNeedCrashDiagnostics = true;
			SendToState(EConnectStatus::LostConnection);
			return;
		}
		ConnectTestStartTimeSeconds = FPlatformTime::Seconds();
	}

	if (CurrentTime - ConnectStartTimeSeconds > WaitForConnectTimeout && !IsCookIgnoreTimeouts())
	{
		CrashDiagnosticsError = FString::Printf(
			TEXT("CookWorkerCrash: CookWorker %d process failed to connect within %.0f seconds. Assigned packages will be returned to the director."),
			ProfileId, WaitForConnectTimeout);
		bNeedCrashDiagnostics = true;
		ShutdownRemoteProcess();
		SendToState(EConnectStatus::LostConnection);
		return;
	}
}

void FCookWorkerServer::TickWaitForDisconnect()
{
	constexpr float TestProcessExistencePeriod = 1.f;
	constexpr float WaitForDisconnectTimeout = 60.f * 10;

	double CurrentTime = FPlatformTime::Seconds();
	if (CurrentTime - ConnectTestStartTimeSeconds > TestProcessExistencePeriod)
	{
		if (!FPlatformProcess::IsProcRunning(CookWorkerHandle))
		{
			SendToState(EConnectStatus::LostConnection);
			return;
		}
		ConnectTestStartTimeSeconds = FPlatformTime::Seconds();
	}

	// We might have been blocked from sending the disconnect, so keep trying to flush the buffer
	UE::CompactBinaryTCP::TryFlushBuffer(Socket, SendBuffer);
	TArray<UE::CompactBinaryTCP::FMarshalledMessage> Messages;
	TryReadPacket(Socket, ReceiveBuffer, Messages);

	if (bTerminateImmediately ||
		(CurrentTime - ConnectStartTimeSeconds > WaitForDisconnectTimeout && !IsCookIgnoreTimeouts()))
	{
		UE_CLOG(!bTerminateImmediately, LogCook, Warning,
			TEXT("CookWorker process of CookWorkerServer %d failed to disconnect within %.0f seconds; we will terminate it."),
			ProfileId, WaitForDisconnectTimeout);
		ShutdownRemoteProcess();
		SendToState(EConnectStatus::LostConnection);
	}
}

void FCookWorkerServer::PumpSendMessages()
{
	UE::CompactBinaryTCP::EConnectionStatus Status = UE::CompactBinaryTCP::TryFlushBuffer(Socket, SendBuffer);
	if (Status == UE::CompactBinaryTCP::EConnectionStatus::Failed)
	{
		UE_LOG(LogCook, Error,
			TEXT("CookWorkerCrash: CookWorker %d failed to write to socket, we will shutdown the remote process. Assigned packages will be returned to the director."),
			ProfileId);
		bNeedCrashDiagnostics = true;
		SendToState(EConnectStatus::WaitForDisconnect);
		bTerminateImmediately = true;
	}
}

void FCookWorkerServer::SendPendingMessages()
{
	SendPendingPackages();
	for (UE::CompactBinaryTCP::FMarshalledMessage& MarshalledMessage : QueuedMessagesToSendAfterPackagesToAssign)
	{
		UE::CompactBinaryTCP::QueueMessage(SendBuffer, MoveTemp(MarshalledMessage));
	}
	QueuedMessagesToSendAfterPackagesToAssign.Empty();
}

void FCookWorkerServer::SendPendingPackages()
{
	if (PackagesToAssign.IsEmpty())
	{
		PackagesToAssignExtraDatas.Empty();
		PackagesToAssignInfoPackages.Empty();
		return;
	}
	LLM_SCOPE_BYTAG(Cooker_MPCook);

	TArray<FAssignPackageData> AssignDatas;
	AssignDatas.Reserve(PackagesToAssign.Num());
	TBitArray<> SessionPlatformNeedsCook;
	TArray<FPackageDataExistenceInfo> ExistenceInfos;
	ExistenceInfos.Reserve(PackagesToAssignInfoPackages.Num());

	for (FPackageData* PackageData : PackagesToAssign)
	{
		FAssignPackageData& AssignData = AssignDatas.Emplace_GetRef();
		AssignData.ConstructData = PackageData->CreateConstructData();
		AssignData.ParentGenerator = PackageData->GetParentGenerator();
		AssignData.DoesGeneratedRequireGenerator = PackageData->DoesGeneratedRequireGenerator();
		AssignData.Instigator = PackageData->GetInstigator();
		AssignData.Urgency = PackageData->GetUrgency();
		SessionPlatformNeedsCook.Init(false, OrderedSessionPlatforms.Num());
		int32 PlatformIndex = 0;
		for (const ITargetPlatform* SessionPlatform : OrderedSessionPlatforms)
		{
			FPackagePlatformData* PlatformData = PackageData->FindPlatformData(SessionPlatform);
			SessionPlatformNeedsCook[PlatformIndex++] = PlatformData && PlatformData->NeedsCooking(SessionPlatform);
		}
		AssignData.NeedCookPlatforms = FDiscoveredPlatformSet(SessionPlatformNeedsCook);
		FAssignPackageExtraData* ExtraData = PackagesToAssignExtraDatas.Find(PackageData);
		if (ExtraData)
		{
			AssignData.GeneratorPreviousGeneratedPackages = MoveTemp(ExtraData->GeneratorPreviousGeneratedPackages);
			AssignData.PerPackageCollectorMessages = MoveTemp(ExtraData->PerPackageCollectorMessages);
		}
	}
	for (FPackageData* PackageData : PackagesToAssignInfoPackages)
	{
		FPackageDataExistenceInfo& ExistenceInfo = ExistenceInfos.Emplace_GetRef();
		ExistenceInfo.ConstructData = PackageData->CreateConstructData();
		ExistenceInfo.ParentGenerator = PackageData->GetParentGenerator();
	}
	PendingPackages.Append(PackagesToAssign);
	PackagesToAssign.Empty();
	PackagesToAssignExtraDatas.Empty();
	PackagesToAssignInfoPackages.Empty();
	FAssignPackagesMessage AssignPackagesMessage(MoveTemp(AssignDatas), MoveTemp(ExistenceInfos));
	AssignPackagesMessage.OrderedSessionPlatforms = OrderedSessionPlatforms;
	SendMessageInLock(MoveTemp(AssignPackagesMessage));
}

void FCookWorkerServer::PumpReceiveMessages()
{
	using namespace UE::CompactBinaryTCP;
	LLM_SCOPE_BYTAG(Cooker_MPCook);
	TArray<FMarshalledMessage> Messages;
	EConnectionStatus SocketStatus = TryReadPacket(Socket, ReceiveBuffer, Messages);
	if (SocketStatus != EConnectionStatus::Okay && SocketStatus != EConnectionStatus::Incomplete)
	{
		CrashDiagnosticsError = FString::Printf(
			TEXT("CookWorkerCrash: CookWorker %d failed to read from socket with description: %s. we will shutdown the remote process. Assigned packages will be returned to the director."),
			ProfileId,
			DescribeStatus(SocketStatus));
		bNeedCrashDiagnostics = true;
		SendToState(EConnectStatus::WaitForDisconnect);
		bTerminateImmediately = true;
		return;
	}
	for (FMarshalledMessage& Message : Messages)
	{
		ReceiveMessages.Add(MoveTemp(Message));
	}
	HandleReceiveMessagesInternal();
}

void FCookWorkerServer::HandleReceiveMessages(ECookDirectorThread TickThread)
{
	FCommunicationScopeLock ScopeLock(this, TickThread, ETickAction::Queue);
	HandleReceiveMessagesInternal();
}

void FCookWorkerServer::HandleReceiveMessagesInternal()
{
	while (!ReceiveMessages.IsEmpty())
	{
		UE::CompactBinaryTCP::FMarshalledMessage& PeekMessage = ReceiveMessages[0];

		if (PeekMessage.MessageType == FAbortWorkerMessage::MessageType)
		{
			UE::CompactBinaryTCP::FMarshalledMessage Message = ReceiveMessages.PopFrontValue();
			if (ConnectStatus != EConnectStatus::PumpingCookComplete
				&& ConnectStatus != EConnectStatus::WaitForDisconnect)
			{
				CrashDiagnosticsError = FString::Printf(
					TEXT("CookWorkerCrash: CookWorker %d remote process shut down unexpectedly. Assigned packages will be returned to the director."),
					ProfileId);
				bNeedCrashDiagnostics = true;
			}
			SendMessageInLock(FAbortWorkerMessage(FAbortWorkerMessage::AbortAcknowledge));
			SendToState(EConnectStatus::WaitForDisconnect);
			ReceiveMessages.Reset();
			break;
		}

		if (TickState.TickThread != ECookDirectorThread::SchedulerThread)
		{
			break;
		}

		UE::CompactBinaryTCP::FMarshalledMessage Message = ReceiveMessages.PopFrontValue();
		if (Message.MessageType == FPackageResultsMessage::MessageType)
		{
			FPackageResultsMessage ResultsMessage;
			if (!ResultsMessage.TryRead(Message.Object))
			{
				LogInvalidMessage(TEXT("FPackageResultsMessage"));
			}
			else
			{
				RecordResults(ResultsMessage);
			}
		}
		else if (Message.MessageType == FDiscoveredPackagesMessage::MessageType)
		{
			FDiscoveredPackagesMessage DiscoveredMessage;
			DiscoveredMessage.OrderedSessionAndSpecialPlatforms = OrderedSessionAndSpecialPlatforms;
			if (!DiscoveredMessage.TryRead(Message.Object))
			{
				LogInvalidMessage(TEXT("FDiscoveredPackagesMessage"));
			}
			else
			{
				for (FDiscoveredPackageReplication& DiscoveredPackage : DiscoveredMessage.Packages)
				{
					QueueDiscoveredPackage(MoveTemp(DiscoveredPackage));
				}
			}
		}
		else if (Message.MessageType == FGeneratorEventMessage::MessageType)
		{
			FGeneratorEventMessage GeneratorMessage;
			if (!GeneratorMessage.TryRead(Message.Object))
			{
				LogInvalidMessage(TEXT("FGeneratorEventMessage"));
			}
			else
			{
				HandleGeneratorMessage(GeneratorMessage);
			}
		}
		else
		{
			TRefCountPtr<IMPCollector>* Collector = Director.Collectors.Find(Message.MessageType);
			if (Collector)
			{
				check(*Collector);
				FMPCollectorServerMessageContext Context;
				Context.Server = this;
				Context.Platforms = OrderedSessionPlatforms;
				Context.WorkerId = WorkerId;
				Context.ProfileId = ProfileId;
				(*Collector)->ServerReceiveMessage(Context, Message.Object);
			}
			else
			{
				UE_LOG(LogCook, Error,
					TEXT("CookWorkerServer received message of unknown type %s from CookWorker. Ignoring it."),
					*Message.MessageType.ToString());
			}
		}
	}
}

void FCookWorkerServer::HandleReceivedPackagePlatformMessages(FPackageData& PackageData,
	const ITargetPlatform* TargetPlatform, TArray<UE::CompactBinaryTCP::FMarshalledMessage>&& Messages)
{
	check(TickState.TickThread == ECookDirectorThread::SchedulerThread);
	if (Messages.IsEmpty())
	{
		return;
	}

	FMPCollectorServerMessageContext Context;
	Context.Platforms = OrderedSessionPlatforms;
	Context.PackageName = PackageData.GetPackageName();
	Context.TargetPlatform = TargetPlatform;
	Context.Server = this;
	Context.ProfileId = ProfileId;
	Context.WorkerId = WorkerId;

	for (UE::CompactBinaryTCP::FMarshalledMessage& Message : Messages)
	{
		TRefCountPtr<IMPCollector>* Collector = Director.Collectors.Find(Message.MessageType);
		if (Collector)
		{
			check(*Collector);
			(*Collector)->ServerReceiveMessage(Context, Message.Object);
		}
		else
		{
			UE_LOG(LogCook, Error,
				TEXT("CookWorkerServer received PackageMessage of unknown type %s from CookWorker. Ignoring it."),
				*Message.MessageType.ToString());
		}
	}
}

void FCookWorkerServer::SendMessage(const IMPCollectorMessage& Message, ECookDirectorThread TickThread)
{
	FCommunicationScopeLock ScopeLock(this, TickThread, ETickAction::Tick);
	SendMessageInLock(Message);
}

void FCookWorkerServer::AppendMessage(const IMPCollectorMessage& Message, ECookDirectorThread TickThread)
{
	FCommunicationScopeLock ScopeLock(this, TickThread, ETickAction::Queue);
	QueuedMessagesToSendAfterPackagesToAssign.Add(MarshalToCompactBinaryTCP(Message));
}

void FCookWorkerServer::SendMessageInLock(const IMPCollectorMessage& Message)
{
	if (TickState.TickAction == ETickAction::Tick)
	{
		UE::CompactBinaryTCP::TryWritePacket(Socket, SendBuffer, MarshalToCompactBinaryTCP(Message));
	}
	else
	{
		check(TickState.TickAction == ETickAction::Queue);
		UE::CompactBinaryTCP::QueueMessage(SendBuffer, MarshalToCompactBinaryTCP(Message));
	}
}

void FCookWorkerServer::RecordResults(FPackageResultsMessage& Message)
{
	check(TickState.TickThread == ECookDirectorThread::SchedulerThread);

	bool bRetiredAnyPackages = false;
	for (FPackageRemoteResult& Result : Message.Results)
	{
		FPackageData* PackageData = COTFS.PackageDatas->FindPackageDataByPackageName(Result.GetPackageName());
		if (!PackageData)
		{
			UE_LOG(LogCook, Warning,
				TEXT("CookWorkerServer %d received FPackageResultsMessage for invalid package %s. Ignoring it."),
				ProfileId, *Result.GetPackageName().ToString());
			continue;
		}
		if (PendingPackages.Remove(PackageData) != 1)
		{
			UE_LOG(LogCook, Display,
				TEXT("CookWorkerServer %d received FPackageResultsMessage for package %s which is not a pending package. Ignoring it."),
				ProfileId, *Result.GetPackageName().ToString());
			continue;
		}
		bRetiredAnyPackages = true;
		PackageData->SetWorkerAssignment(FWorkerId::Invalid(), ESendFlags::QueueNone);

		if (PackageData->IsGenerated())
		{
			TRefCountPtr<FGenerationHelper> ParentGenerationHelper = PackageData->GetOrFindParentGenerationHelper();
			if (!ParentGenerationHelper)
			{
				UE_LOG(LogCook, Warning,
					TEXT("RecordResults received for generated package %s, but its ParentGenerationHelper has already been destructed so we can not update the save flag. Leaving the save flag unupdated; this might cause workers to run out of memory due to keeping the Generator referenced."),
					*PackageData->GetPackageName().ToString());
			}
			else
			{
				ParentGenerationHelper->MarkPackageSavedRemotely(COTFS, *PackageData, GetWorkerId());
				EStateChangeReason StateChangeReason =
					Result.GetSuppressCookReason() == ESuppressCookReason::NotSuppressed
					? EStateChangeReason::Saved
					: ConvertToStateChangeReason(Result.GetSuppressCookReason());
				PackageData->SetParentGenerationHelper(nullptr, StateChangeReason);
			}
		}
		TRefCountPtr<FGenerationHelper> GenerationHelper = PackageData->GetGenerationHelper();
		if (GenerationHelper)
		{
			GenerationHelper->MarkPackageSavedRemotely(COTFS, *PackageData, GetWorkerId());
			GenerationHelper.SafeRelease();
		}

		// MPCOOKTODO: Refactor FSaveCookedPackageContext::FinishPlatform and ::FinishPackage so we can call them from
		// here to reduce duplication
		if (Result.GetSuppressCookReason() == ESuppressCookReason::NotSuppressed)
		{
			int32 NumPlatforms = OrderedSessionPlatforms.Num();
			if (Result.GetPlatforms().Num() != NumPlatforms)
			{
				UE_LOG(LogCook, Warning,
					TEXT("CookWorkerServer %d received FPackageResultsMessage for package %s with an invalid number of platform results: expected %d, actual %d. Ignoring it."),
					ProfileId, *Result.GetPackageName().ToString(), NumPlatforms, Result.GetPlatforms().Num());
				continue;
			}

			HandleReceivedPackagePlatformMessages(*PackageData, nullptr, Result.ReleaseMessages());
			for (int32 PlatformIndex = 0; PlatformIndex < NumPlatforms; ++PlatformIndex)
			{
				ITargetPlatform* TargetPlatform = OrderedSessionPlatforms[PlatformIndex];
				FPackageRemoteResult::FPlatformResult& PlatformResult = Result.GetPlatforms()[PlatformIndex];
				FPackagePlatformData& ExistingData = PackageData->FindOrAddPlatformData(TargetPlatform);
				if (!ExistingData.NeedsCooking(TargetPlatform))
				{
					if (PlatformResult.GetCookResults() != ECookResult::Invalid)
					{
						UE_LOG(LogCook, Display,
							TEXT("CookWorkerServer %d received FPackageResultsMessage for package %s, platform %s, but that platform has already been cooked. Ignoring the results for that platform."),
							ProfileId, *Result.GetPackageName().ToString(), *TargetPlatform->PlatformName());
					}
					continue;
				}
				else
				{
					if (PlatformResult.GetCookResults() != ECookResult::Invalid)
					{
						PackageData->SetPlatformCooked(TargetPlatform, PlatformResult.GetCookResults());
					}
					HandleReceivedPackagePlatformMessages(*PackageData, TargetPlatform,
						PlatformResult.ReleaseMessages());
				}
			}
			COTFS.RecordExternalActorDependencies(Result.GetExternalActorDependencies());
			if (Result.IsReferencedOnlyByEditorOnlyData())
			{
				COTFS.PackageTracker->UncookedEditorOnlyPackages.AddUnique(Result.GetPackageName());
			}
			COTFS.PromoteToSaveComplete(*PackageData, ESendFlags::QueueAddAndRemove);
		}
		else
		{
			COTFS.DemoteToIdle(*PackageData, ESendFlags::QueueAddAndRemove, Result.GetSuppressCookReason());
		}
	}
	Director.ResetFinalIdleHeartbeatFence();
	if (bRetiredAnyPackages)
	{
		++PackagesRetiredFenceMarker;
	}
}

void FCookWorkerServer::LogInvalidMessage(const TCHAR* MessageTypeName)
{
	UE_LOG(LogCook, Error,
		TEXT("CookWorkerServer received invalidly formatted message for type %s from CookWorker. Ignoring it."),
		MessageTypeName);
}

void FCookWorkerServer::QueueDiscoveredPackage(FDiscoveredPackageReplication&& DiscoveredPackage)
{
	check(TickState.TickThread == ECookDirectorThread::SchedulerThread);

	FPackageDatas& PackageDatas = *COTFS.PackageDatas;
	FInstigator& Instigator = DiscoveredPackage.Instigator;
	FDiscoveredPlatformSet& Platforms = DiscoveredPackage.Platforms;
	FPackageData& PackageData = PackageDatas.FindOrAddPackageData(DiscoveredPackage.PackageName,
		DiscoveredPackage.NormalizedFileName);

	TArray<const ITargetPlatform*, TInlineAllocator<ExpectedMaxNumPlatforms>> BufferPlatforms;
	TConstArrayView<const ITargetPlatform*> DiscoveredPlatforms;
	if (!COTFS.bSkipOnlyEditorOnly)
	{
		DiscoveredPlatforms = OrderedSessionAndSpecialPlatforms;
	}
	else
	{
		DiscoveredPlatforms = Platforms.GetPlatforms(COTFS, &Instigator, OrderedSessionAndSpecialPlatforms,
			&BufferPlatforms);
	}

	if (Instigator.Category != EInstigator::ForceExplorableSaveTimeSoftDependency &&
		PackageData.HasReachablePlatforms(DiscoveredPlatforms))
	{
		// The CookWorker thought there were some new reachable platforms, but the Director already knows about
		// all of them; ignore the report
		return;
	}
	if (COTFS.bSkipOnlyEditorOnly &&
		Instigator.Category == EInstigator::Unsolicited &&
		Platforms.GetSource() == EDiscoveredPlatformSet::CopyFromInstigator &&
		PackageData.FindOrAddPlatformData(CookerLoadingPlatformKey).IsReachable())
	{
		// The CookWorker thought this package was new (previously unreachable even by editoronly references),
		// and it is not marked as a known used-in-game or editor-only issue, so it fell back to reporting it
		// as used-in-game-because-its-not-a-known-issue (see UCookOnTheFlyServer::ProcessUnsolicitedPackages's
		// use of PackageData->FindOrAddPlatformData(CookerLoadingPlatformKey).IsReachable()).
		// But we only do that fall back for unexpected packages not found by the search of editor-only AssetRegistry
		// dependencies. And this package was found by that search; the director has already marked it as reachable by
		// editoronly references. Correct the heuristic: ignore the unmarked load because the load is expected as an
		// editor-only reference.
		return;
	}

	if (!DiscoveredPackage.ParentGenerator.IsNone())
	{
		// Registration of the discovered Generated package with its generator needs to come after we early-exit
		// for already discovered packages, because when one generated package can refer to another from the same
		// generator, the message that a CookWorker has discovered the referred-to generated package can show up
		// on the director AFTER all save messages have already been processed and the GenerationHelper has shut
		// down and destroyed its information about the list of generated packages.
		PackageData.SetGenerated(DiscoveredPackage.ParentGenerator);
		PackageData.SetDoesGeneratedRequireGenerator(DiscoveredPackage.DoesGeneratedRequireGenerator);
		FPackageData* GeneratorPackageData = PackageDatas.FindPackageDataByPackageName(
			DiscoveredPackage.ParentGenerator);
		if (GeneratorPackageData)
		{
			TRefCountPtr<FGenerationHelper> GenerationHelper =
				GeneratorPackageData->CreateUninitializedGenerationHelper();
			GenerationHelper->NotifyStartQueueGeneratedPackages(COTFS, WorkerId);
			GenerationHelper->TrackGeneratedPackageListedRemotely(COTFS, PackageData, DiscoveredPackage.GeneratedPackageHash);
		}
	}

	if (PackageData.IsGenerated()
		&& (PackageData.DoesGeneratedRequireGenerator() >= ICookPackageSplitter::EGeneratedRequiresGenerator::Save
				|| COTFS.MPCookGeneratorSplit == EMPCookGeneratorSplit::AllOnSameWorker))
	{
		PackageData.SetWorkerAssignmentConstraint(GetWorkerId());
	}
	Director.ResetFinalIdleHeartbeatFence();
	Platforms.ConvertFromBitfield(OrderedSessionAndSpecialPlatforms);
	COTFS.QueueDiscoveredPackageOnDirector(PackageData, MoveTemp(Instigator), MoveTemp(Platforms),
		DiscoveredPackage.Urgency);
}

void FCookWorkerServer::HandleGeneratorMessage(FGeneratorEventMessage& GeneratorMessage)
{
	FPackageData* PackageData = COTFS.PackageDatas->FindPackageDataByPackageName(GeneratorMessage.PackageName);
	if (!PackageData)
	{
		// This error should be impossible because GeneratorMessages are only sent in response to assignment from the server.
		UE_LOG(LogCook, Error,
			TEXT("CookWorkerServer received unexpected GeneratorMessage for package %s. The PackageData %s does not exist on the CookDirector. ")
			TEXT("\n\tCook of this generator package and its generated packages will be invalid."),
			*GeneratorMessage.PackageName.ToString(),
			(!PackageData ? TEXT("does not exist") : TEXT("is not a valid generator")));
		return;
	}

	TRefCountPtr<FGenerationHelper> GenerationHelper;
	GenerationHelper = PackageData->CreateUninitializedGenerationHelper();
	check(GenerationHelper);

	switch (GeneratorMessage.Event)
	{
	case EGeneratorEvent::QueuedGeneratedPackages:
		GenerationHelper->EndQueueGeneratedPackagesOnDirector(COTFS, GetWorkerId());
		break;
	default:
		// We do not handle the remaining GeneratorEvents on the server
		break;
	}
}

FCookWorkerServer::FTickState::FTickState()
{
	TickThread = ECookDirectorThread::Invalid;
	TickAction = ETickAction::Invalid;
}

FCookWorkerServer::FCommunicationScopeLock::FCommunicationScopeLock(FCookWorkerServer* InServer,
	ECookDirectorThread TickThread, ETickAction TickAction)
	: ScopeLock(&InServer->CommunicationLock)
	, Server(*InServer)
{
	check(TickThread != ECookDirectorThread::Invalid);
	check(TickAction != ETickAction::Invalid);
	check(Server.TickState.TickThread == ECookDirectorThread::Invalid);
	Server.TickState.TickThread = TickThread;
	Server.TickState.TickAction = TickAction;
}

FCookWorkerServer::FCommunicationScopeLock::~FCommunicationScopeLock()
{
	check(Server.TickState.TickThread != ECookDirectorThread::Invalid);
	Server.TickState.TickThread = ECookDirectorThread::Invalid;
	Server.TickState.TickAction = ETickAction::Invalid;
}

UE::CompactBinaryTCP::FMarshalledMessage MarshalToCompactBinaryTCP(const IMPCollectorMessage& Message)
{
	UE::CompactBinaryTCP::FMarshalledMessage Marshalled;
	Marshalled.MessageType = Message.GetMessageType();
	FCbWriter Writer;
	Writer.BeginObject();
	Message.Write(Writer);
	Writer.EndObject();
	Marshalled.Object = Writer.Save().AsObject();
	return Marshalled;
}

FAssignPackagesMessage::FAssignPackagesMessage(TArray<FAssignPackageData>&& InPackageDatas,
	TArray<FPackageDataExistenceInfo>&& InExistenceInfos)
	: PackageDatas(MoveTemp(InPackageDatas))
	, ExistenceInfos(MoveTemp(InExistenceInfos))
{
}

void FAssignPackagesMessage::Write(FCbWriter& Writer) const
{
	Writer.BeginArray("P");
	for (const FAssignPackageData& PackageData : PackageDatas)
	{
		WriteToCompactBinary(Writer, PackageData, OrderedSessionPlatforms);
	}
	Writer.EndArray();
	Writer.BeginArray("I");
	for (const FPackageDataExistenceInfo& ExistenceInfo : ExistenceInfos)
	{
		Writer << ExistenceInfo;
	}
	Writer.EndArray();
}

bool FAssignPackagesMessage::TryRead(FCbObjectView Object)
{
	bool bOk = true;
	PackageDatas.Reset();
	for (FCbFieldView PackageField : Object["P"])
	{
		FAssignPackageData& PackageData = PackageDatas.Emplace_GetRef();
		if (!LoadFromCompactBinary(PackageField, PackageData, OrderedSessionPlatforms))
		{
			PackageDatas.Pop();
			bOk = false;
		}
	}
	ExistenceInfos.Reset();
	for (FCbFieldView PackageField : Object["I"])
	{
		FPackageDataExistenceInfo& ExistenceInfo = ExistenceInfos.Emplace_GetRef();
		if (!LoadFromCompactBinary(PackageField, ExistenceInfo))
		{
			ExistenceInfos.Pop();
			bOk = false;
		}
	}
	return bOk;
}

FGuid FAssignPackagesMessage::MessageType(TEXT("B7B1542B73254B679319D73F753DB6F8"));

void FAssignPackageData::Write(FCbWriter& Writer,
	TConstArrayView<const ITargetPlatform*> OrderedSessionPlatforms) const
{
	Writer.BeginArray();
	Writer << ConstructData;
	Writer << ParentGenerator;
	Writer << Instigator;
	Writer << static_cast<uint8>(Urgency);
	static_assert(sizeof(EUrgency) <= sizeof(uint8), "We are storing it in a uint8");
	WriteToCompactBinary(Writer, NeedCookPlatforms, OrderedSessionPlatforms);
	{
		Writer.BeginArray();
		for (const TPair<FName, FAssetPackageData>& Pair : GeneratorPreviousGeneratedPackages)
		{
			Writer.BeginArray();
			Writer << Pair.Key;
			Pair.Value.NetworkWrite(Writer);
			Writer.EndArray();
		}
		Writer.EndArray();
	}
	static_assert(sizeof(ICookPackageSplitter::EGeneratedRequiresGenerator) <= sizeof(uint8), "We are storing it in a uint8");
	Writer << static_cast<uint8>(DoesGeneratedRequireGenerator);
	Writer << PerPackageCollectorMessages;
	Writer.EndArray();
}

bool FAssignPackageData::TryRead(FCbFieldView Field, TConstArrayView<const ITargetPlatform*> OrderedSessionPlatforms)
{
	FCbFieldViewIterator It = Field.CreateViewIterator();
	bool bOk = true;
	bOk = LoadFromCompactBinary(*It++, ConstructData) & bOk;
	bOk = LoadFromCompactBinary(*It++, ParentGenerator) & bOk;
	bOk = LoadFromCompactBinary(*It++, Instigator) & bOk;
	uint8 UrgencyInt = It->AsUInt8();
	if (!(It++)->HasError() && UrgencyInt < static_cast<uint8>(EUrgency::Count))
	{
		Urgency = static_cast<EUrgency>(UrgencyInt);
	}
	else
	{
		bOk = false;
	}
	bOk = LoadFromCompactBinary(*It++, NeedCookPlatforms, OrderedSessionPlatforms) & bOk;
	{
		FCbFieldView ArrayFieldView = *It++;
		bool bGeneratorPreviousGeneratedPackagesOk = false;
		const uint64 Length = ArrayFieldView.AsArrayView().Num();
		if (Length <= MAX_int32)
		{
			GeneratorPreviousGeneratedPackages.Empty((int32)Length);
			bGeneratorPreviousGeneratedPackagesOk = !ArrayFieldView.HasError();
			for (const FCbFieldView& ElementField : ArrayFieldView)
			{
				FCbFieldViewIterator PairIt = ElementField.CreateViewIterator();
				bool bElementOk = false;
				FName Key;
				FAssetPackageData Value;
				if (LoadFromCompactBinary(*PairIt++, Key))
				{
					if (Value.TryNetworkRead(*PairIt++))
					{
						GeneratorPreviousGeneratedPackages.Add(Key, MoveTemp(Value));
						bElementOk = true;
					}
				}
				bGeneratorPreviousGeneratedPackagesOk &= bElementOk;
			}
		}
		else
		{
			GeneratorPreviousGeneratedPackages.Empty();
		}
		bOk &= bGeneratorPreviousGeneratedPackagesOk;
	}
	uint8 DoesGeneratedRequireGeneratorInt = It->AsUInt8();
	if (!(It++)->HasError() && DoesGeneratedRequireGeneratorInt
		< static_cast<uint8>(ICookPackageSplitter::EGeneratedRequiresGenerator::Count))
	{
		DoesGeneratedRequireGenerator =
			static_cast<ICookPackageSplitter::EGeneratedRequiresGenerator>(DoesGeneratedRequireGeneratorInt);
	}
	else
	{
		bOk = false;
	}
	bOk = LoadFromCompactBinary(*It++, PerPackageCollectorMessages) & bOk;
	return bOk;
}

void FPackageDataExistenceInfo::Write(FCbWriter& Writer) const
{
	Writer.BeginArray();
	Writer << ConstructData;
	Writer << ParentGenerator;
	Writer.EndArray();
}

bool FPackageDataExistenceInfo::TryRead(FCbFieldView Field)
{
	FCbFieldViewIterator It = Field.CreateViewIterator();
	bool bOk = true;
	bOk = LoadFromCompactBinary(*It++, ConstructData) & bOk;
	bOk = LoadFromCompactBinary(*It++, ParentGenerator) & bOk;
	return bOk;
}

FCbWriter& operator<<(FCbWriter& Writer, const FInstigator& Instigator)
{
	Writer.BeginObject();
	Writer << "C" << static_cast<uint8>(Instigator.Category);
	Writer << "R" << Instigator.Referencer;
	Writer.EndObject();
	return Writer;
}

bool LoadFromCompactBinary(FCbFieldView Field, FInstigator& Instigator)
{
	uint8 CategoryInt;
	bool bOk = true;
	if (LoadFromCompactBinary(Field["C"], CategoryInt) &&
		CategoryInt < static_cast<uint8>(EInstigator::Count))
	{
		Instigator.Category = static_cast<EInstigator>(CategoryInt);
	}
	else
	{
		Instigator.Category = EInstigator::InvalidCategory;
		bOk = false;
	}
	bOk = LoadFromCompactBinary(Field["R"], Instigator.Referencer) & bOk;
	return bOk;
}

FAbortPackagesMessage::FAbortPackagesMessage(TArray<FName>&& InPackageNames)
	: PackageNames(MoveTemp(InPackageNames))
{
}

void FAbortPackagesMessage::Write(FCbWriter& Writer) const
{
	Writer << "PackageNames" <<  PackageNames;
}

bool FAbortPackagesMessage::TryRead(FCbObjectView Object)
{
	return LoadFromCompactBinary(Object["PackageNames"], PackageNames);
}

FGuid FAbortPackagesMessage::MessageType(TEXT("D769F1BFF2F34978868D70E3CAEE94E7"));

FAbortWorkerMessage::FAbortWorkerMessage(EType InType)
	: Type(InType)
{
}

void FAbortWorkerMessage::Write(FCbWriter& Writer) const
{
	Writer << "Type" << (uint8)Type;
}

bool FAbortWorkerMessage::TryRead(FCbObjectView Object)
{
	Type = static_cast<EType>(Object["Type"].AsUInt8((uint8)EType::Abort));
	return true;
}

FGuid FAbortWorkerMessage::MessageType(TEXT("83FD99DFE8DB4A9A8E71684C121BE6F3"));

void FInitialConfigMessage::ReadFromLocal(const UCookOnTheFlyServer& COTFS,
	const TConstArrayView<const ITargetPlatform*>& InOrderedSessionPlatforms, const FCookByTheBookOptions& InCookByTheBookOptions,
	const FCookOnTheFlyOptions& InCookOnTheFlyOptions, const FBeginCookContextForWorker& InBeginContext)
{
	InitialSettings.CopyFromLocal(COTFS);
	BeginCookSettings.CopyFromLocal(COTFS);
	BeginCookContext = InBeginContext;
	OrderedSessionPlatforms.Reset(InOrderedSessionPlatforms.Num());
	for (const ITargetPlatform* Platform : InOrderedSessionPlatforms)
	{
		OrderedSessionPlatforms.Add(const_cast<ITargetPlatform*>(Platform));
	}
	DirectorCookMode = COTFS.GetCookMode();
	CookInitializationFlags = COTFS.GetCookFlags();
	CookByTheBookOptions = InCookByTheBookOptions;
	CookOnTheFlyOptions = InCookOnTheFlyOptions;
	bZenStore = COTFS.IsUsingZenStore();
}

void FInitialConfigMessage::Write(FCbWriter& Writer) const
{
	int32 LocalCookMode = static_cast<int32>(DirectorCookMode);
	Writer << "DirectorCookMode" << LocalCookMode;
	int32 LocalCookFlags = static_cast<int32>(CookInitializationFlags);
	Writer << "CookInitializationFlags" << LocalCookFlags;
	Writer << "ZenStore" << bZenStore;

	Writer.BeginArray("TargetPlatforms");
	for (const ITargetPlatform* TargetPlatform : OrderedSessionPlatforms)
	{
		Writer << TargetPlatform->PlatformName();
	}
	Writer.EndArray();
	Writer << "InitialSettings" << InitialSettings;
	Writer << "BeginCookSettings" << BeginCookSettings;
	Writer << "BeginCookContext" << BeginCookContext;
	Writer << "CookByTheBookOptions" << CookByTheBookOptions;
	Writer << "CookOnTheFlyOptions" << CookOnTheFlyOptions;
	Writer << "MPCollectorMessages" << MPCollectorMessages;
}

bool FInitialConfigMessage::TryRead(FCbObjectView Object)
{
	bool bOk = true;
	int32 LocalCookMode;
	bOk = LoadFromCompactBinary(Object["DirectorCookMode"], LocalCookMode) & bOk;
	DirectorCookMode = static_cast<ECookMode::Type>(LocalCookMode);
	int32 LocalCookFlags;
	bOk = LoadFromCompactBinary(Object["CookInitializationFlags"], LocalCookFlags) & bOk;
	CookInitializationFlags = static_cast<ECookInitializationFlags>(LocalCookFlags);
	bOk = LoadFromCompactBinary(Object["ZenStore"], bZenStore) & bOk;

	ITargetPlatformManagerModule& TPM(GetTargetPlatformManagerRef());
	FCbFieldView TargetPlatformsField = Object["TargetPlatforms"];
	{
		bOk = TargetPlatformsField.IsArray() & bOk;
		OrderedSessionPlatforms.Reset(TargetPlatformsField.AsArrayView().Num());
		for (FCbFieldView ElementField : TargetPlatformsField)
		{
			TStringBuilder<128> KeyName;
			if (LoadFromCompactBinary(ElementField, KeyName))
			{
				ITargetPlatform* TargetPlatform = TPM.FindTargetPlatform(KeyName.ToView());
				if (TargetPlatform)
				{
					OrderedSessionPlatforms.Add(TargetPlatform);
				}
				else
				{
					UE_LOG(LogCook, Error, TEXT("Could not find TargetPlatform \"%.*s\" received from CookDirector."),
						KeyName.Len(), KeyName.GetData());
					bOk = false;
				}

			}
			else
			{
				bOk = false;
			}
		}
	}

	bOk = LoadFromCompactBinary(Object["InitialSettings"], InitialSettings) & bOk;
	bOk = LoadFromCompactBinary(Object["BeginCookSettings"], BeginCookSettings) & bOk;
	bOk = LoadFromCompactBinary(Object["BeginCookContext"], BeginCookContext) & bOk;
	bOk = LoadFromCompactBinary(Object["CookByTheBookOptions"], CookByTheBookOptions) & bOk;
	bOk = LoadFromCompactBinary(Object["CookOnTheFlyOptions"], CookOnTheFlyOptions) & bOk;
	bOk = LoadFromCompactBinary(Object["MPCollectorMessages"], MPCollectorMessages) & bOk;

	return bOk;
}

FGuid FInitialConfigMessage::MessageType(TEXT("340CDCB927304CEB9C0A66B5F707FC2B"));

void FDiscoveredPackageReplication::Write(FCbWriter& Writer,
	TConstArrayView<const ITargetPlatform*> OrderedSessionAndSpecialPlatforms) const
{
	Writer.BeginArray();
	Writer << PackageName;
	Writer << NormalizedFileName;
	Writer << ParentGenerator;
	Writer << static_cast<uint8>(Instigator.Category);
	Writer << Instigator.Referencer;
	Writer << static_cast<uint8>(DoesGeneratedRequireGenerator);
	static_assert(sizeof(ICookPackageSplitter::EGeneratedRequiresGenerator) <= sizeof(uint8), "We are storing it in a uint8");
	Writer << static_cast<uint8>(Urgency);
	static_assert(sizeof(EUrgency) <= sizeof(uint8), "We are storing it in a uint8");
	bool bGeneratedPackageHash = !GeneratedPackageHash.IsZero();
	Writer << bGeneratedPackageHash;
	if (bGeneratedPackageHash)
	{
		Writer << GeneratedPackageHash;
	}
	WriteToCompactBinary(Writer, Platforms, OrderedSessionAndSpecialPlatforms);
	Writer.EndArray();
}

bool FDiscoveredPackageReplication::TryRead(FCbFieldView Field,
	TConstArrayView<const ITargetPlatform*> OrderedSessionAndSpecialPlatforms)
{
	FCbArrayView FieldList = Field.AsArrayView();
	if (Field.HasError())
	{
		*this = FDiscoveredPackageReplication();
		return false;
	}
	FCbFieldViewIterator Iter = FieldList.CreateViewIterator();

	bool bOk = LoadFromCompactBinary(Iter++, PackageName);
	bOk = LoadFromCompactBinary(Iter++, NormalizedFileName) & bOk;
	bOk = LoadFromCompactBinary(Iter++, ParentGenerator) & bOk;
	uint8 CategoryInt;
	if (LoadFromCompactBinary(Iter++, CategoryInt) &&
		CategoryInt < static_cast<uint8>(EInstigator::Count))
	{
		Instigator.Category = static_cast<EInstigator>(CategoryInt);
	}
	else
	{
		bOk = false;
	}
	bOk = LoadFromCompactBinary(Iter++, Instigator.Referencer) & bOk;
	uint8 DoesGeneratedRequireGeneratorInt = Iter->AsUInt8();
	if (!(Iter++)->HasError() && DoesGeneratedRequireGeneratorInt
		< static_cast<uint8>(ICookPackageSplitter::EGeneratedRequiresGenerator::Count))
	{
		DoesGeneratedRequireGenerator = static_cast<ICookPackageSplitter::EGeneratedRequiresGenerator>(
			DoesGeneratedRequireGeneratorInt);
	}
	else
	{
		bOk = false;
	}
	uint8 UrgencyInt = Iter->AsUInt8();
	if (!(Iter++)->HasError() && UrgencyInt < static_cast<uint8>(EUrgency::Count))
	{
		Urgency = static_cast<EUrgency>(UrgencyInt);
	}
	else
	{
		bOk = false;
	}
	bool bGeneratedPackageHash = false;
	bOk = LoadFromCompactBinary(Iter++, bGeneratedPackageHash) & bOk;
	if (bGeneratedPackageHash)
	{
		bOk = LoadFromCompactBinary(Iter++, GeneratedPackageHash) & bOk;
	}
	else
	{
		GeneratedPackageHash = FIoHash::Zero;
	}
	bOk = LoadFromCompactBinary(Iter++, Platforms, OrderedSessionAndSpecialPlatforms) & bOk;
	if (!bOk)
	{
		*this = FDiscoveredPackageReplication();
	}
	return bOk;
}

void FDiscoveredPackagesMessage::Write(FCbWriter& Writer) const
{
	Writer.BeginArray("Packages");
	for (const FDiscoveredPackageReplication& Package : Packages)
	{
		WriteToCompactBinary(Writer, Package, OrderedSessionAndSpecialPlatforms);
	}
	Writer.EndArray();
}

bool FDiscoveredPackagesMessage::TryRead(FCbObjectView Object)
{
	bool bOk = true;
	Packages.Reset();
	for (FCbFieldView PackageField : Object["Packages"])
	{
		FDiscoveredPackageReplication& Package = Packages.Emplace_GetRef();
		if (!LoadFromCompactBinary(PackageField, Package, OrderedSessionAndSpecialPlatforms))
		{
			Packages.Pop();
			bOk = false;
		}
	}
	return bOk;
}

FGuid FDiscoveredPackagesMessage::MessageType(TEXT("C9F5BC5C11484B06B346B411F1ED3090"));

FGeneratorEventMessage::FGeneratorEventMessage(EGeneratorEvent InEvent, FName InPackageName)
	: PackageName(InPackageName)
	, Event(InEvent)
{
}

void FGeneratorEventMessage::Write(FCbWriter& Writer) const
{
	Writer << "E" << static_cast<uint8>(Event);
	Writer << "P" << PackageName;
}

bool FGeneratorEventMessage::TryRead(FCbObjectView Object)
{
	bool bOk = true;
	FCbFieldView EventField = Object["E"];
	uint8 EventInt = EventField.AsUInt8();
	if (!EventField.HasError() && EventInt < static_cast<uint8>(EGeneratorEvent::Num))
	{
		Event = static_cast<EGeneratorEvent>(EventInt);
	}
	else
	{
		Event = EGeneratorEvent::Invalid;
		bOk = false;
	}
	bOk = LoadFromCompactBinary(Object["P"], PackageName) & bOk;
	return bOk;
}

FGuid FGeneratorEventMessage::MessageType(TEXT("B6EE94CA70EC4F40B0D2214EDC11ED03"));

FCbWriter& operator<<(FCbWriter& Writer, const FReplicatedLogData& LogData)
{
	// Serializing as an array of unnamed fields and using the quantity of fields
	// as the discriminator between structured and unstructured log data.
	Writer.BeginArray();
	if (LogData.LogDataVariant.IsType<FReplicatedLogData::FUnstructuredLogData>())
	{
		const FReplicatedLogData::FUnstructuredLogData& UnstructuredLogData = LogData.LogDataVariant.Get<FReplicatedLogData::FUnstructuredLogData>();
		Writer << UnstructuredLogData.Category;
		uint8 Verbosity = static_cast<uint8>(UnstructuredLogData.Verbosity);
		Writer << Verbosity;
		Writer << UnstructuredLogData.Message;
	}
	else if (LogData.LogDataVariant.IsType<FCbObject>())
	{
		Writer << LogData.LogDataVariant.Get<FCbObject>();
	}
	else
	{
		checkNoEntry();
	}
	Writer.EndArray();
	return Writer;
}

bool LoadFromCompactBinary(FCbFieldView Field, FReplicatedLogData& OutLogData)
{
	bool bOk = true;
	FCbArrayView ArrayView = Field.AsArrayView();
	switch (ArrayView.Num())
	{
	case 3:
	{
		OutLogData.LogDataVariant.Emplace<FReplicatedLogData::FUnstructuredLogData>();
		FReplicatedLogData::FUnstructuredLogData& UnstructuredLogData = OutLogData.LogDataVariant.Get<FReplicatedLogData::FUnstructuredLogData>();
		FCbFieldViewIterator It = ArrayView.CreateViewIterator();
		bOk = LoadFromCompactBinary(*It++, UnstructuredLogData.Category) & bOk;
		uint8 Verbosity;
		if (LoadFromCompactBinary(*It++, Verbosity))
		{
			UnstructuredLogData.Verbosity = static_cast<ELogVerbosity::Type>(Verbosity);
		}
		else
		{
			bOk = false;
			UnstructuredLogData.Verbosity = static_cast<ELogVerbosity::Type>(0);
		}
		bOk = LoadFromCompactBinary(*It++, UnstructuredLogData.Message) & bOk;
		break;
	}
	case 1:
	{
		OutLogData.LogDataVariant.Emplace<FCbObject>();
		FCbObject& StructuredLogData = OutLogData.LogDataVariant.Get<FCbObject>();
		FCbFieldViewIterator It = ArrayView.CreateViewIterator();
		if (It->IsObject())
		{
			StructuredLogData = FCbObject::Clone(It->AsObjectView());
		}
		else
		{
			bOk = false;
		}
		break;
	}
	default:
		bOk = false;
	}
	return bOk;
}

FCbWriter& FLogMessagesMessageHandler::FLogRecordSerializationContext::Serialize(FCbWriter& Writer, const FLogRecord& LogRecord)
{
	Writer.BeginArray();
	Writer << LogRecord.GetCategory();
	Writer << static_cast<uint8>(LogRecord.GetVerbosity());
	Writer << LogRecord.GetTime().GetUtcTime();
	Writer << LogRecord.GetFormat();
	Writer << LogRecord.GetFields();
	Writer << LogRecord.GetFile();
	Writer << LogRecord.GetLine();
	Writer << LogRecord.GetTextNamespace();
	Writer << LogRecord.GetTextKey();
	Writer.EndArray();
	return Writer;
}

bool FLogMessagesMessageHandler::FLogRecordSerializationContext::Deserialize(FCbFieldView Field, FLogRecord& OutLogRecord, int32 ProfileId)
{
	bool bOk = true;
	FCbFieldViewIterator It = Field.CreateViewIterator();
	if (FName Category; LoadFromCompactBinary(*It++, Category))
	{
		OutLogRecord.SetCategory(Category);
	}
	else
	{
		bOk = false;
	}
	if (uint8 Verbosity; LoadFromCompactBinary(*It++, Verbosity) && Verbosity < ELogVerbosity::NumVerbosity)
	{
		OutLogRecord.SetVerbosity(static_cast<ELogVerbosity::Type>(Verbosity));
	}
	else
	{
		bOk = false;
	}
	if (FDateTime Time; LoadFromCompactBinary(*It++, Time))
	{
		OutLogRecord.SetTime(FLogTime::FromUtcTime(Time));
	}
	else
	{
		bOk = false;
	}
	if (FString SerializedString; LoadFromCompactBinary(*It++, SerializedString))
	{
		FString& FormatString = StringTable.AddDefaulted_GetRef();
		FormatString = FString::Printf(TEXT("[CookWorker %d]: %s"), ProfileId, *SerializedString);
		OutLogRecord.SetFormat(*FormatString);
	}
	else
	{
		bOk = false;
	}

	FCbObject Object(FCbObject::Clone(It->AsObjectView()));
	OutLogRecord.SetFields(MoveTemp(Object));
	bOk = !It->HasError() && bOk;
	It++;

	if (TUtf8StringBuilder<64> FileStringBuilder; LoadFromCompactBinary(*It++, FileStringBuilder))
	{
		FAnsiString& FileString = AnsiStringTable.AddDefaulted_GetRef();
		FileString = FileStringBuilder.ToString();
		OutLogRecord.SetFile(*FileString);
	}
	else
	{
		bOk = false;
	}
	if (int32 Line; LoadFromCompactBinary(*It++, Line))
	{
		OutLogRecord.SetLine(Line);
	}
	else
	{
		bOk = false;
	}
	if (FString TextNamespaceString; LoadFromCompactBinary(*It++, TextNamespaceString))
	{
		if (!TextNamespaceString.IsEmpty())
		{
			OutLogRecord.SetTextNamespace(*StringTable.Emplace_GetRef(MoveTemp(TextNamespaceString)));
		}
		else
		{
			OutLogRecord.SetTextNamespace(nullptr);
		}
	}
	else
	{
		bOk = false;
	}
	bool bHasTextKey = false;
	if (FString TextKeyString; LoadFromCompactBinary(*It++, TextKeyString))
	{
		if (!TextKeyString.IsEmpty())
		{
			bHasTextKey = true;
			OutLogRecord.SetTextKey(*StringTable.Emplace_GetRef(MoveTemp(TextKeyString)));
		}
		else
		{
			OutLogRecord.SetTextKey(nullptr);
		}
	}
	else
	{
		bOk = false;
	}

	if (bHasTextKey)
	{
		FLogTemplate* LogTemplate = CreateLogTemplate(OutLogRecord.GetTextNamespace(), OutLogRecord.GetTextKey(), OutLogRecord.GetFormat());
		TemplateTable.Add(LogTemplate);
		OutLogRecord.SetTemplate(LogTemplate);
	}
	else
	{
		FLogTemplate* LogTemplate = CreateLogTemplate(OutLogRecord.GetFormat());
		TemplateTable.Add(LogTemplate);
		OutLogRecord.SetTemplate(LogTemplate);
	}

	return bOk;
}

void FLogMessagesMessageHandler::FLogRecordSerializationContext::ConditionalFlush(int32 TableSize)
{
	if ((StringTable.Num() > TableSize) || (AnsiStringTable.Num() > TableSize) || (TemplateTable.Num() > TableSize))
	{
		Flush();
	}
}

void FLogMessagesMessageHandler::FLogRecordSerializationContext::Flush()
{
	if (!StringTable.IsEmpty() || !AnsiStringTable.IsEmpty() || !TemplateTable.IsEmpty())
	{
		// NOTE: We only call FlushThreadedLogs on GLog even though we might serialize structured logs via GLog or GWarn.
		// GWarn is an output device, but GLog is a an output redirector, and only the redirector has/needs FlushThreadedLogs.
		// Output devices are expected to not use any pointer on a structured log record after completion of the SerializeRecord call.
		GLog->FlushThreadedLogs();
	}
	for (FLogTemplate* LogTemplate : TemplateTable)
	{
		DestroyLogTemplate(LogTemplate);
	}

	StringTable.Empty();
	AnsiStringTable.Empty();
	TemplateTable.Empty();
}

FGuid FLogMessagesMessageHandler::MessageType(TEXT("DB024D28203D4FBAAAF6AAD7080CF277"));

FLogMessagesMessageHandler::~FLogMessagesMessageHandler()
{
	if (bRegistered && GLog)
	{
		GLog->RemoveOutputDevice(this);
	}
}
void FLogMessagesMessageHandler::InitializeClient()
{
	check(!bRegistered);
	GLog->AddOutputDevice(this);
	bRegistered = true;
}

void FLogMessagesMessageHandler::ClientTick(FMPCollectorClientTickContext& Context)
{
	{
		FScopeLock QueueScopeLock(&QueueLock);
		Swap(QueuedLogs, QueuedLogsBackBuffer);
	}
	if (!QueuedLogsBackBuffer.IsEmpty())
	{
		FCbWriter Writer;
		Writer.BeginObject();
		Writer << "Messages" << QueuedLogsBackBuffer;
		Writer.EndObject();
		Context.AddMessage(Writer.Save().AsObject());
		QueuedLogsBackBuffer.Reset();
	}
}

void FLogMessagesMessageHandler::ServerReceiveMessage(FMPCollectorServerMessageContext& Context,
	FCbObjectView InMessage)
{
	TArray<FReplicatedLogData> Messages;

	if (!LoadFromCompactBinary(InMessage["Messages"], Messages))
	{
		UE_LOG(LogCook, Error, TEXT("FLogMessagesMessageHandler received corrupted message from CookWorker"));
		return;
	}

	for (FReplicatedLogData& LogData : Messages)
	{
		if (const FReplicatedLogData::FUnstructuredLogData* UnStructuredLogData = LogData.LogDataVariant.TryGet<FReplicatedLogData::FUnstructuredLogData>())
		{
			if (UnStructuredLogData->Category == LogCookName && UnStructuredLogData->Message.Contains(HeartbeatCategoryText))
			{
				// Do not spam heartbeat messages into the CookDirector log
				continue;
			}

			FMsg::Logf(__FILE__, __LINE__, UnStructuredLogData->Category, UnStructuredLogData->Verbosity, TEXT("[CookWorker %d]: %s"),
			Context.GetProfileId(), *UnStructuredLogData->Message);
		}
		else if (const FCbObject* StructuredLogObject = LogData.LogDataVariant.TryGet<FCbObject>())
		{
			FLogRecord LogRecord;
			if (LogRecordSerializationContext.Deserialize((*StructuredLogObject)["S"], LogRecord, Context.GetProfileId()))
			{
				FOutputDevice* LogOverride = nullptr;
				switch (LogRecord.GetVerbosity())
				{
				case ELogVerbosity::Error:
				case ELogVerbosity::Warning:
				case ELogVerbosity::Display:
				case ELogVerbosity::SetColor:
					LogOverride = GWarn;
					break;
				default:
					break;
				}
				if (LogOverride)
				{
					LogOverride->SerializeRecord(LogRecord);
				}
				else
				{
					GLog->SerializeRecord(LogRecord);
				}
			}
		}
		else
		{
			checkNoEntry();
		}
	}

	// Flush if the tables in the serialization context have exceeded 100 entries
	const int32 TableSizeToFlushAt = 100;
	LogRecordSerializationContext.ConditionalFlush(TableSizeToFlushAt);
}

void FLogMessagesMessageHandler::Serialize(const TCHAR* V, ELogVerbosity::Type Verbosity,
	const FName& Category)
{
	FScopeLock QueueScopeLock(&QueueLock);
	FReplicatedLogData& LogData = QueuedLogs.Emplace_GetRef();
	LogData.LogDataVariant.Emplace<FReplicatedLogData::FUnstructuredLogData>();
	FReplicatedLogData::FUnstructuredLogData& NewVal = LogData.LogDataVariant.Get<FReplicatedLogData::FUnstructuredLogData>();
	NewVal.Message = V;
	NewVal.Category = Category;
	NewVal.Verbosity = Verbosity;
}

void FLogMessagesMessageHandler::Serialize(const TCHAR* V, ELogVerbosity::Type Verbosity,
	const FName& Category, const double Time)
{
	Serialize(V, Verbosity, Category);
}

void FLogMessagesMessageHandler::SerializeRecord(const UE::FLogRecord& Record)
{
	FCbWriter Writer;
	Writer.BeginObject();
	Writer << "S";
	FLogRecordSerializationContext::Serialize(Writer, Record);
	Writer.EndObject();
	FCbObject Object = Writer.Save().AsObject();

	FScopeLock QueueScopeLock(&QueueLock);
	FReplicatedLogData& LogData = QueuedLogs.Emplace_GetRef();
	LogData.LogDataVariant.Emplace<FCbObject>(MoveTemp(Object));
}

FGuid FHeartbeatMessage::MessageType(TEXT("C08FFAF07BF34DD3A2FFB8A287CDDE83"));

FHeartbeatMessage::FHeartbeatMessage(int32 InHeartbeatNumber)
	: HeartbeatNumber(InHeartbeatNumber)
{
}

void FHeartbeatMessage::Write(FCbWriter& Writer) const
{
	Writer << "H" << HeartbeatNumber;
}

bool FHeartbeatMessage::TryRead(FCbObjectView Object)
{
	return LoadFromCompactBinary(Object["H"], HeartbeatNumber);
}

FPackageWriterMPCollector::FPackageWriterMPCollector(UCookOnTheFlyServer& InCOTFS)
	: COTFS(InCOTFS)
{

}

void FPackageWriterMPCollector::ClientTickPackage(FMPCollectorClientTickPackageContext& Context)
{
	for (const FMPCollectorClientTickPackageContext::FPlatformData& PlatformData : Context.GetPlatformDatas())
	{
		if (PlatformData.CookResults == ECookResult::Invalid)
		{
			continue;
		}
		ICookedPackageWriter& PackageWriter = COTFS.FindOrCreatePackageWriter(PlatformData.TargetPlatform);
		TFuture<FCbObject> ObjectFuture = PackageWriter.WriteMPCookMessageForPackage(Context.GetPackageName());
		Context.AddAsyncPlatformMessage(PlatformData.TargetPlatform, MoveTemp(ObjectFuture));
	}
}

void FPackageWriterMPCollector::ServerReceiveMessage(FMPCollectorServerMessageContext& Context, FCbObjectView Message)
{
	FName PackageName = Context.GetPackageName();
	const ITargetPlatform* TargetPlatform = Context.GetTargetPlatform();
	check(PackageName.IsValid() && TargetPlatform);

	ICookedPackageWriter& PackageWriter = COTFS.FindOrCreatePackageWriter(TargetPlatform);
	if (!PackageWriter.TryReadMPCookMessageForPackage(PackageName, Message))
	{
		UE_LOG(LogCook, Error,
			TEXT("CookWorkerServer received invalidly formatted PackageWriter message from CookWorker %d. Ignoring it."),
			Context.GetProfileId());
	}
}

FGuid FPackageWriterMPCollector::MessageType(TEXT("D2B1CE3FD26644AF9EC28FBADB1BD331"));

}