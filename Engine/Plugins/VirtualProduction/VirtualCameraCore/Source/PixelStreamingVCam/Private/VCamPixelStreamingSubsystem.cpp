// Copyright Epic Games, Inc. All Rights Reserved.

#include "VCamPixelStreamingSubsystem.h"

#include "BuiltinProviders/VCamPixelStreamingSession.h"

#include "Engine/Engine.h"
#include "Features/IModularFeatures.h"
#include "ILiveLinkClient.h"
#include "PixelStreamingVCamLog.h"
#include "LiveLink/VCamPixelStreamingLiveLink.h"

UVCamPixelStreamingSubsystem* UVCamPixelStreamingSubsystem::Get()
{
	return GEngine ? GEngine->GetEngineSubsystem<UVCamPixelStreamingSubsystem>() : nullptr;
}

void UVCamPixelStreamingSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	MissingSignallingServerNotifier = MakeUnique<UE::PixelStreamingVCam::FMissingSignallingServerNotifier>(*this);
	SignalingServerLifecycle = MakeUnique<UE::PixelStreamingVCam::FSignalingServerLifecycle>(*this);
	LiveLinkManager = MakeUnique<UE::PixelStreamingVCam::FLiveLinkManager>();
}

void UVCamPixelStreamingSubsystem::Deinitialize()
{
	Super::Deinitialize();
	RegisteredSessions.Empty();
	
	IModularFeatures& ModularFeatures = IModularFeatures::Get();
	if (LiveLinkSource && ModularFeatures.IsModularFeatureAvailable(ILiveLinkClient::ModularFeatureName))
	{
		ILiveLinkClient* LiveLinkClient = &ModularFeatures.GetModularFeature<ILiveLinkClient>(ILiveLinkClient::ModularFeatureName);
		LiveLinkClient->RemoveSource(LiveLinkSource);
	}
	LiveLinkSource.Reset();

	MissingSignallingServerNotifier.Reset();
	SignalingServerLifecycle.Reset();
}

void UVCamPixelStreamingSubsystem::RegisterActiveOutputProvider(UVCamPixelStreamingSession* OutputProvider)
{
	check(OutputProvider);
	RegisteredSessions.AddUnique(OutputProvider);
	LiveLinkManager->CreateOrRefreshSubjectFor(*OutputProvider);
}

void UVCamPixelStreamingSubsystem::UnregisterActiveOutputProvider(UVCamPixelStreamingSession* OutputProvider)
{
	check(OutputProvider);
	RegisteredSessions.RemoveSingle(OutputProvider);
	LiveLinkManager->DestroySubjectFor(*OutputProvider);
}

void UVCamPixelStreamingSubsystem::LaunchSignallingServerIfNeeded(UVCamPixelStreamingSession& Session)
{
	SignalingServerLifecycle->LaunchSignallingServerIfNeeded(Session);
}

void UVCamPixelStreamingSubsystem::StopSignallingServerIfNeeded(UVCamPixelStreamingSession& Session)
{
	SignalingServerLifecycle->StopSignallingServerIfNeeded(Session);
}