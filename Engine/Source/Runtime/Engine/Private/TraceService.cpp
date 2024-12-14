// Copyright Epic Games, Inc. All Rights Reserved.
#include "TraceService.h"

#include "MessageEndpoint.h"
#include "MessageEndpointBuilder.h"
#include "Misc/App.h"
#include "ProfilingDebugging/TraceAuxiliary.h"
#include "ProfilingDebugging/TraceScreenshot.h"
#include "Trace/Trace.h"
#include "TraceControlMessages.h"

class FTraceServiceImpl
{
public:
	FTraceServiceImpl();
	FTraceServiceImpl(const TSharedPtr<IMessageBus>&);
	virtual ~FTraceServiceImpl() {};
	
private:
	void OnStatusPing(const FTraceControlStatusPing& Message, const TSharedRef<IMessageContext>& Context);
	void OnChannelsPing(const FTraceControlChannelsPing& Message, const TSharedRef<IMessageContext>& Context);
	void OnSettingsPing(const FTraceControlSettingsPing& Message, const TSharedRef<IMessageContext, ESPMode::ThreadSafe>& Context);
	void OnDiscoveryPing(const FTraceControlDiscoveryPing& Message, const TSharedRef<IMessageContext>& Context);
	void OnStop(const FTraceControlStop& Message, const TSharedRef<IMessageContext>& Context);
	void OnSend(const FTraceControlSend& Message, const TSharedRef<IMessageContext>& Context);
	void OnChannelSet(const FTraceControlChannelsSet& Message, const TSharedRef<IMessageContext>& Context);
	void OnFile(const FTraceControlFile& Message, const TSharedRef<IMessageContext>& Context);
	void OnSnapshotSend(const FTraceControlSnapshotSend& Message, const TSharedRef<IMessageContext>& Context);
	void OnSnapshotFile(const FTraceControlSnapshotFile& Message, const TSharedRef<IMessageContext>& Context);
	void OnPause(const FTraceControlPause& Message, const TSharedRef<IMessageContext>& Context);
	void OnResume(const FTraceControlResume& Message, const TSharedRef<IMessageContext>& Context);
	void OnBookmark(const FTraceControlBookmark& Message, const TSharedRef<IMessageContext>& Context);
#if UE_SCREENSHOT_TRACE_ENABLED
	void OnScreenshot(const FTraceControlScreenshot& Message, const TSharedRef<IMessageContext>& Context);
#endif
	void OnSetStatNamedEvents(const FTraceControlSetStatNamedEvents& Message, const TSharedRef<IMessageContext>& Context);
	
	static void FillTraceStatusMessage(FTraceControlStatus* Message);

	TSharedPtr<FMessageEndpoint, ESPMode::ThreadSafe> MessageEndpoint;
	FGuid SessionId;
	FGuid InstanceId;
};


FTraceServiceImpl::FTraceServiceImpl()
	: FTraceServiceImpl(IMessagingModule::Get().GetDefaultBus())
{
}

FTraceServiceImpl::FTraceServiceImpl(const TSharedPtr<IMessageBus>& InBus)
{
	SessionId = FApp::GetSessionId();
	InstanceId = FApp::GetInstanceId();

	if (InBus.IsValid())
	{
		MessageEndpoint = FMessageEndpoint::Builder("FTraceService", InBus.ToSharedRef())
			.Handling<FTraceControlDiscoveryPing>(this, &FTraceServiceImpl::OnDiscoveryPing)
			.Handling<FTraceControlChannelsSet>(this, &FTraceServiceImpl::OnChannelSet)
			.Handling<FTraceControlStop>(this, &FTraceServiceImpl::OnStop)
			.Handling<FTraceControlSend>(this, &FTraceServiceImpl::OnSend)
			.Handling<FTraceControlFile>(this, &FTraceServiceImpl::OnFile)
			.Handling<FTraceControlSnapshotSend>(this, &FTraceServiceImpl::OnSnapshotSend)
			.Handling<FTraceControlSnapshotFile>(this, &FTraceServiceImpl::OnSnapshotFile)
			.Handling<FTraceControlPause>(this, &FTraceServiceImpl::OnPause)
			.Handling<FTraceControlResume>(this, &FTraceServiceImpl::OnResume)
			.Handling<FTraceControlBookmark>(this, &FTraceServiceImpl::OnBookmark)
#if UE_SCREENSHOT_TRACE_ENABLED
			.Handling<FTraceControlScreenshot>(this, &FTraceServiceImpl::OnScreenshot)
#endif
			.Handling<FTraceControlSetStatNamedEvents>(this, &FTraceServiceImpl::OnSetStatNamedEvents)
			.Handling<FTraceControlStatusPing>(this, &FTraceServiceImpl::OnStatusPing)
			.Handling<FTraceControlSettingsPing>(this, &FTraceServiceImpl::OnSettingsPing)
			.Handling<FTraceControlChannelsPing>(this, &FTraceServiceImpl::OnChannelsPing);

		if (!MessageEndpoint.IsValid())
		{
			return;
		}

		MessageEndpoint->Subscribe<FTraceControlStatusPing>();
		MessageEndpoint->Subscribe<FTraceControlSettingsPing>();
		MessageEndpoint->Subscribe<FTraceControlDiscoveryPing>();
		MessageEndpoint->Subscribe<FTraceControlChannelsPing>();
		MessageEndpoint->Subscribe<FTraceControlStop>();
		MessageEndpoint->Subscribe<FTraceControlSend>();
		MessageEndpoint->Subscribe<FTraceControlChannelsSet>();
		MessageEndpoint->Subscribe<FTraceControlFile>();
		MessageEndpoint->Subscribe<FTraceControlSnapshotSend>();
		MessageEndpoint->Subscribe<FTraceControlSnapshotSend>();
		MessageEndpoint->Subscribe<FTraceControlPause>();
		MessageEndpoint->Subscribe<FTraceControlResume>();
		MessageEndpoint->Subscribe<FTraceControlBookmark>();
#if UE_SCREENSHOT_TRACE_ENABLED
		MessageEndpoint->Subscribe<FTraceControlScreenshot>();
#endif
		MessageEndpoint->Subscribe<FTraceControlSetStatNamedEvents>();
	}
}

void FTraceServiceImpl::FillTraceStatusMessage(FTraceControlStatus* Message)
{
	// Get the current endpoint and ids
	Message->Endpoint = FTraceAuxiliary::GetTraceDestinationString();
	Message->bIsTracing = FTraceAuxiliary::IsConnected(Message->SessionGuid, Message->TraceGuid);
	
	// For stats we can query TraceLog directly.
	UE::Trace::FStatistics Stats;
	UE::Trace::GetStatistics(Stats);
	Message->BytesSent = Stats.BytesSent;
	Message->BytesTraced = Stats.BytesTraced;
	Message->MemoryUsed = Stats.MemoryUsed;
	Message->CacheAllocated = Stats.CacheAllocated;
	Message->CacheUsed = Stats.CacheUsed;
	Message->CacheWaste = Stats.CacheWaste;
	Message->bAreStatNamedEventsEnabled = GCycleStatsShouldEmitNamedEvents > 0;
	Message->bIsPaused = FTraceAuxiliary::IsPaused();
	Message->StatusTimestamp = FDateTime::Now();
	Message->TraceSystemStatus = static_cast<uint8>(FTraceAuxiliary::GetTraceSystemStatus());
}

void FTraceServiceImpl::OnChannelSet(const FTraceControlChannelsSet& Message, const TSharedRef<IMessageContext>& Context)
{
	FTraceAuxiliary::EnableChannels(Message.ChannelIdsToEnable);
	FTraceAuxiliary::DisableChannels(Message.ChannelIdsToDisable);
}

void FTraceServiceImpl::OnStop(const FTraceControlStop& Message, const TSharedRef<IMessageContext>& Context)
{
	FTraceAuxiliary::Stop();
}

void FTraceServiceImpl::OnSend(const FTraceControlSend& Message, const TSharedRef<IMessageContext>& Context)
{
	FTraceAuxiliary::FOptions Options;
	Options.bExcludeTail = Message.bExcludeTail;
	
	FTraceAuxiliary::Start(
		FTraceAuxiliary::EConnectionType::Network,
		*Message.Host,
		*Message.Channels,
		&Options
	);
}

void FTraceServiceImpl::OnFile(const FTraceControlFile& Message, const TSharedRef<IMessageContext>& Context)
{
	FTraceAuxiliary::FOptions Options;
	Options.bTruncateFile = Message.bTruncateFile;
	Options.bExcludeTail = Message.bExcludeTail;
	
	FTraceAuxiliary::Start(
		FTraceAuxiliary::EConnectionType::File,
		*Message.File,
		*Message.Channels,
		&Options
	);
}

void FTraceServiceImpl::OnSnapshotSend(const FTraceControlSnapshotSend& Message, const TSharedRef<IMessageContext>& Context)
{
	FTraceAuxiliary::SendSnapshot(*Message.Host);
}

void FTraceServiceImpl::OnSnapshotFile(const FTraceControlSnapshotFile& Message, const TSharedRef<IMessageContext>& Context)
{
	FTraceAuxiliary::SendSnapshot(*Message.File);
}

void FTraceServiceImpl::OnPause(const FTraceControlPause& Message, const TSharedRef<IMessageContext>& Context)
{
	FTraceAuxiliary::Pause();
}

void FTraceServiceImpl::OnResume(const FTraceControlResume& Message, const TSharedRef<IMessageContext>& Context)
{
	FTraceAuxiliary::Resume();
}

void FTraceServiceImpl::OnBookmark(const FTraceControlBookmark& Message, const TSharedRef<IMessageContext>& Context)
{
	TRACE_BOOKMARK(TEXT("%s"), *Message.Label);
}

#if UE_SCREENSHOT_TRACE_ENABLED
void FTraceServiceImpl::OnScreenshot(const FTraceControlScreenshot& Message, const TSharedRef<IMessageContext>& Context)
{
	FTraceScreenshot::RequestScreenshot(Message.Name, Message.bShowUI);
}
#endif // UE_SCREENSHOT_TRACE_ENABLED

void FTraceServiceImpl::OnSetStatNamedEvents(const FTraceControlSetStatNamedEvents& Message, const TSharedRef<IMessageContext>& Context)
{
	if (Message.bEnabled && GCycleStatsShouldEmitNamedEvents == 0)
	{
		++GCycleStatsShouldEmitNamedEvents;
	}
	if (!Message.bEnabled && GCycleStatsShouldEmitNamedEvents > 0)
	{
		GCycleStatsShouldEmitNamedEvents = 0;
	}
}

void FTraceServiceImpl::OnStatusPing(const FTraceControlStatusPing& Message, const TSharedRef<IMessageContext>& Context)
{
	FTraceControlStatus* Response = FMessageEndpoint::MakeMessage<FTraceControlStatus>();
	FillTraceStatusMessage(Response);
	
	MessageEndpoint->Send(Response, Context->GetSender());
}

void FTraceServiceImpl::OnChannelsPing(const FTraceControlChannelsPing& Message, const TSharedRef<IMessageContext>& Context)
{
	
	struct FEnumerateUserData
	{
		TArray<FString> Channels;
		TArray<FString> Descriptions;
		TArray<uint32> Ids;
		TArray<uint32> ReadOnlyIds;
		TArray<uint32> EnabledIds;
	} UserData;
	
	UE::Trace::EnumerateChannels([](const UE::Trace::FChannelInfo& ChannelInfo, void* User)
	{
		FEnumerateUserData* UserData = static_cast<FEnumerateUserData*>(User);
		FAnsiStringView NameView = FAnsiStringView(ChannelInfo.Name).LeftChop(7); // Remove "Channel" suffix
		const uint32 ChannelId = ChannelInfo.Id;
		UserData->Channels.Emplace(NameView);
		UserData->Ids.Emplace(ChannelId);
		UserData->Descriptions.Emplace(ChannelInfo.Desc);
		if (ChannelInfo.bIsReadOnly)
		{
			UserData->ReadOnlyIds.Add(ChannelId);
		}
		if (ChannelInfo.bIsEnabled)
		{
			UserData->EnabledIds.Add(ChannelId);
		}
		return true;
	}, &UserData);

	// Only send channel description message if the number of channels has changed.
	if (Message.KnownChannelCount < uint32(UserData.Channels.Num()))
	{
		FTraceControlChannelsDesc* DescResponse = FMessageEndpoint::MakeMessage<FTraceControlChannelsDesc>();
		DescResponse->Channels = MoveTemp(UserData.Channels);
		DescResponse->Ids = MoveTemp(UserData.Ids);
		DescResponse->Descriptions = MoveTemp(UserData.Descriptions);
		DescResponse->ReadOnlyIds = MoveTemp(UserData.ReadOnlyIds);
		MessageEndpoint->Send(DescResponse, Context->GetSender());
	}

	// Always send status response
	FTraceControlChannelsStatus* StatusResponse = FMessageEndpoint::MakeMessage<FTraceControlChannelsStatus>();
	StatusResponse->EnabledIds = MoveTemp(UserData.EnabledIds);
	MessageEndpoint->Send(StatusResponse, Context->GetSender());
}

void FTraceServiceImpl::OnSettingsPing(const FTraceControlSettingsPing& Message, const TSharedRef<IMessageContext>& Context)
{
	FTraceControlSettings* Response = FMessageEndpoint::MakeMessage<FTraceControlSettings>();
	UE::Trace::FInitializeDesc const* InitDesc = FTraceAuxiliary::GetInitializeDesc();

	if (InitDesc)
	{
		Response->bUseImportantCache = InitDesc->bUseImportantCache;
		Response->bUseWorkerThread = InitDesc->bUseWorkerThread;
		Response->TailSizeBytes = InitDesc->TailSizeBytes;
	}

	auto AddPreset = [&Response](const FTraceAuxiliary::FChannelPreset& Preset)
	{
		FTraceChannelPreset TracePreset;
		TracePreset.Name = Preset.Name;
		TracePreset.ChannelList = Preset.ChannelList;
		TracePreset.bIsReadOnly = Preset.bIsReadOnly;

		Response->ChannelPresets.Add(TracePreset);
		return FTraceAuxiliary::EEnumerateResult::Continue;
	};

	FTraceAuxiliary::EnumerateFixedChannelPresets(AddPreset);
	FTraceAuxiliary::EnumerateChannelPresetsFromSettings(AddPreset);

	MessageEndpoint->Send(Response, Context->GetSender());
}

void FTraceServiceImpl::OnDiscoveryPing(const FTraceControlDiscoveryPing& Message, const TSharedRef<IMessageContext>& Context)
{
	if ((!Message.SessionId.IsValid() && !Message.InstanceId.IsValid()) || (Message.InstanceId == FApp::GetInstanceId() || Message.SessionId == FApp::GetSessionId()))
	{
		const auto Response = FMessageEndpoint::MakeMessage<FTraceControlDiscovery>();
		Response->SessionId = FApp::GetSessionId();
		Response->InstanceId = FApp::GetInstanceId();

		FillTraceStatusMessage(Response);

		MessageEndpoint->Send(
			Response,
			Context->GetSender()
		);
	}
}


FTraceService::FTraceService()
{
	Impl = MakePimpl<FTraceServiceImpl>();
}

FTraceService::FTraceService(TSharedPtr<IMessageBus> InBus)
{
	Impl = MakePimpl<FTraceServiceImpl>(InBus);
}
