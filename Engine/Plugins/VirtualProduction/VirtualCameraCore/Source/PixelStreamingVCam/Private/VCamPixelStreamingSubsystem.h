// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "LiveLink/LiveLinkManager.h"
#include "Subsystems/EngineSubsystem.h"

#include "Networking/SignalingServerLifecycle.h"
#include "Notifications/MissingSignallingServerNotifier.h"

#include "VCamPixelStreamingSubsystem.generated.h"

class FPixelStreamingLiveLinkSource;
class UVCamPixelStreamingSession;

/**
 * Keeps track of which UVCamPixelStreamingSessions are active and manages systems related to the list of active sessions.
 */
UCLASS()
class PIXELSTREAMINGVCAM_API UVCamPixelStreamingSubsystem : public UEngineSubsystem
{
	GENERATED_BODY()
public:
	
	/** Convenience function for accessing the subsystem */
	static UVCamPixelStreamingSubsystem* Get();
	
	//~ Begin USubsystem Interface
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	//~ End USubsystem Interface

	/** Tracks this OutputProvider creating its Live Link subject. It is valid to call this multiple times doing so updates is subject. */
	void RegisterActiveOutputProvider(UVCamPixelStreamingSession* OutputProvider);
	/** Stops tracking this OutputProvider and clears the Live Link subject. */
	void UnregisterActiveOutputProvider(UVCamPixelStreamingSession* OutputProvider);

	/** Updates the live link source possibly updating its name to match the StreamerId. */
	void UpdateLiveLinkSource(const UVCamPixelStreamingSession& OutputProvider) const
	{
		LiveLinkManager->CreateOrRefreshSubjectFor(OutputProvider);
	}
	void PushTransformForSubject(const UVCamPixelStreamingSession& OutputProvider, const FTransform& Transform, double Timestamp) const
	{
		LiveLinkManager->PushTransformForSubject(OutputProvider, Transform, Timestamp);
	}

	void LaunchSignallingServerIfNeeded(UVCamPixelStreamingSession& Session);
	void StopSignallingServerIfNeeded(UVCamPixelStreamingSession& Session);

	const TArray<TWeakObjectPtr<UVCamPixelStreamingSession>>& GetRegisteredSessions() const { return RegisteredSessions; }
	
private:
	
	TSharedPtr<FPixelStreamingLiveLinkSource> LiveLinkSource;

	/** The active sessions. */
	TArray<TWeakObjectPtr<UVCamPixelStreamingSession>> RegisteredSessions;

	/** Tells the user when the server needs manual launching. */
	TUniquePtr<UE::PixelStreamingVCam::FMissingSignallingServerNotifier> MissingSignallingServerNotifier;
	/** Manages the lifecycle of the signalling server. */
	TUniquePtr<UE::PixelStreamingVCam::FSignalingServerLifecycle> SignalingServerLifecycle;

	/** Manages a Live Link Source shared by all output providers. */
	TUniquePtr<UE::PixelStreamingVCam::FLiveLinkManager> LiveLinkManager;
};
