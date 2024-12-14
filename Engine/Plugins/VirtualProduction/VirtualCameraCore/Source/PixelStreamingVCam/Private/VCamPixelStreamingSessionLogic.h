// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Engine/EngineTypes.h"
#include "Engine/HitResult.h"
#include "IOutputProviderLogic.h"
#include "UObject/ObjectPtr.h"
#include "UObject/WeakObjectPtr.h"
#include "UObject/WeakObjectPtrTemplates.h"

class AActor;
class UVCamPixelStreamingSession;
class UPixelStreamingMediaIOCapture;
class UPixelStreamingMediaOutput;
class IPixelStreamingStreamer;

namespace UE::DecoupledOutputProvider { struct FOutputProviderLogicCreationArgs; }

namespace UE::PixelStreamingVCam
{
	/** Implements logic for UVCamPixelStreamingSession so it can be loaded on all platforms. */
	class FVCamPixelStreamingSessionLogic : public DecoupledOutputProvider::IOutputProviderLogic
	{
	public:
		
		FVCamPixelStreamingSessionLogic(const DecoupledOutputProvider::FOutputProviderLogicCreationArgs& Args);
		virtual ~FVCamPixelStreamingSessionLogic();

		//~ Begin IOutputProviderLogic Interface
		virtual void OnDeinitialize(DecoupledOutputProvider::IOutputProviderEvent& Args) override;
		virtual void OnActivate(DecoupledOutputProvider::IOutputProviderEvent& Args) override;
		virtual void OnDeactivate(DecoupledOutputProvider::IOutputProviderEvent& Args) override;
		virtual VCamCore::EViewportChangeReply PreReapplyViewport(DecoupledOutputProvider::IOutputProviderEvent& Args) override;
		virtual void PostReapplyViewport(DecoupledOutputProvider::IOutputProviderEvent& Args) override;
		virtual void OnAddReferencedObjects(DecoupledOutputProvider::IOutputProviderEvent& Args, FReferenceCollector& Collector) override;
		virtual TFuture<FVCamStringPromptResponse> PromptClientForString(DecoupledOutputProvider::IOutputProviderEvent& Args, const FVCamStringPromptRequest& Request) override;
#if WITH_EDITOR
		virtual void OnPostEditChangeProperty(DecoupledOutputProvider::IOutputProviderEvent& Args, FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
		//~ End IOutputProviderLogic Interface

	private:

		/** Used to generate unique streamer IDs */
		static int NextDefaultStreamerId;

		/** The output provider being managed by this logic object. */
		TWeakObjectPtr<UVCamPixelStreamingSession> ManagedOutputProvider;

		/** Last time viewport was touched. Updated every tick. */
		FHitResult 	LastViewportTouchResult;
		/** Whether we overwrote the widget class with the empty widget class; remember: PS needs a widget. */
		bool bUsingDummyUMG = false;
		/** Cached setting from settings object. */
		bool bOldThrottleCPUWhenNotForeground;

		TObjectPtr<UPixelStreamingMediaOutput> MediaOutput = nullptr;
		TObjectPtr<UPixelStreamingMediaIOCapture> MediaCapture = nullptr;
		
		/** Handle for ARKit stats timer */
		FTimerHandle ARKitResponseTimer; 
		size_t NumARKitEvents = 0;

		/** The next ID to use for a string request */
		int32 NextStringRequestId = 0;

		/** A map from string request IDs to promises to fulfill when the corresponding request is completed */
		TMap<int32, TPromise<FVCamStringPromptResponse>> StringPromptPromises;

#if WITH_EDITOR
		void OnEditStreamId(UVCamPixelStreamingSession& This) const;
		void OnActorLabelChanged(AActor* Actor) const;
#endif
		void RefreshStreamerName(UVCamPixelStreamingSession& Session) const;
		
		void SetupSignallingServer(UVCamPixelStreamingSession& Session);
		void StopSignallingServer(UVCamPixelStreamingSession& Session);

		void SetupCapture(TWeakObjectPtr<UVCamPixelStreamingSession> WeakThisUObjectPtr);
		void StartCapture(TWeakObjectPtr<UVCamPixelStreamingSession> WeakThisUObjectPtr);
		void StopCapture();

		void OnPreStreaming(IPixelStreamingStreamer* PreConnectionStreamer, TWeakObjectPtr<UVCamPixelStreamingSession> WeakThisUObjectPtr);
		void StopStreaming();
		void OnStreamingStarted(IPixelStreamingStreamer* StartedStreamer, TWeakObjectPtr<UVCamPixelStreamingSession> WeakThisUObjectPtr);
		void OnStreamingStopped(IPixelStreamingStreamer* StoppedStreamer);
		void StopEverything(UVCamPixelStreamingSession& Session);

		void SetupCustomInputHandling(UVCamPixelStreamingSession* This);

		void OnCaptureStateChanged(TWeakObjectPtr<UVCamPixelStreamingSession> WeakThisUObjectPtr);
		void OnRemoteResolutionChanged(const FIntPoint& RemoteResolution, TWeakObjectPtr<UVCamPixelStreamingSession> WeakThisUObjectPtr);


		void SetupARKitResponseTimer(TWeakObjectPtr<UVCamPixelStreamingSession> WeakThisUObjectPtr);
		void StopARKitResponseTimer();

		/** Called when all pixel streaming connections to a streamer are closed */
		void OnAllConnectionsClosed(FString StreamerId);

		/** Unregister any handlers for pixel streaming delegates */
		void UnregisterPixelStreamingDelegates();

		/** Sets MediaOutput to nullptr and unsubscribes from relevant delegates. */
		void CleanupMediaOutput();
	};
}

