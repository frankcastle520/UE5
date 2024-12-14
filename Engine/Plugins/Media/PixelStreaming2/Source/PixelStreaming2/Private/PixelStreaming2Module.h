// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IPixelStreaming2Module.h"
#include "EpicRtcConferenceUtils.h"
#include "EpicRtcStatsCollector.h"

#include "epic_rtc/core/platform.h"
#include "epic_rtc/plugins/signalling/signalling_type.h"

class UPixelStreaming2Input;
class SWindow;

namespace UE::PixelStreaming2
{
	class FStreamer;
	class FVideoInputBackBuffer;
	class FVideoSourceGroup;
	class FEpicRtcWebsocketFactory;

	/**
	 * This plugin allows the back buffer to be sent as a compressed video across a network.
	 */
	class FPixelStreaming2Module : public IPixelStreaming2Module
	{
	public:
		static FPixelStreaming2Module* GetModule();

		virtual ~FPixelStreaming2Module() = default;

		// Begin IPixelStreaming2Module
		virtual FReadyEvent&							  OnReady() override;
		virtual bool									  IsReady() override;
		virtual bool									  StartStreaming() override;
		virtual void									  StopStreaming() override;
		virtual TSharedPtr<IPixelStreaming2Streamer>	  CreateStreamer(const FString& StreamerId) override;
		virtual TSharedPtr<IPixelStreaming2AudioProducer> CreateAudioProducer() override;
		virtual TSharedPtr<IPixelStreaming2VideoProducer> CreateVideoProducer() override;
		virtual TArray<FString>							  GetStreamerIds() override;
		virtual TSharedPtr<IPixelStreaming2Streamer>	  FindStreamer(const FString& StreamerId) override;
		virtual TSharedPtr<IPixelStreaming2Streamer>	  DeleteStreamer(const FString& StreamerId) override;
		void											  DeleteStreamer(TSharedPtr<IPixelStreaming2Streamer> ToBeDeleted) override;
		virtual FString									  GetDefaultStreamerID() override;
		virtual FString									  GetDefaultSignallingURL() override;

		// These are staying on the module at the moment as theres no way of the BPs knowing which streamer they are relevant to
		virtual void								 AddInputComponent(UPixelStreaming2Input* InInputComponent);
		virtual void								 RemoveInputComponent(UPixelStreaming2Input* InInputComponent);
		virtual const TArray<UPixelStreaming2Input*> GetInputComponents();
		virtual void								 ForEachStreamer(const TFunction<void(TSharedPtr<IPixelStreaming2Streamer>)>& Func) override;
		// End IPixelStreaming2Module

		TSharedPtr<class FEpicRtcAudioMixingCapturer> GetAudioCapturer();
		TRefCountPtr<EpicRtcConferenceInterface>	  GetEpicRtcConference() { return EpicRtcConference; }
		TRefCountPtr<FEpicRtcStatsCollector>		  GetStatsCollector() { return StatsCollector; }

	private:
		// Begin IModuleInterface
		void StartupModule() override;
		void ShutdownModule() override;
		// End IModuleInterface

		// Own methods
		void InitDefaultStreamer();
		bool IsPlatformCompatible() const;
		void RegisterCustomHandlers(TSharedPtr<IPixelStreaming2Streamer> Streamer);
		void HandleUIInteraction(FMemoryReader Ar);

		FString GetFieldTrials();
		bool	InitializeEpicRtc();

	private:
		bool						   bModuleReady = false;
		bool						   bStartupCompleted = false;
		static FPixelStreaming2Module* PixelStreaming2Module;

		FReadyEvent		ReadyEvent;
		FDelegateHandle LogStatsHandle;

		TArray<UPixelStreaming2Input*>					  InputComponents;
		mutable FCriticalSection						  StreamersCS;
		TMap<FString, TWeakPtr<IPixelStreaming2Streamer>> Streamers;
		TSharedPtr<IPixelStreaming2Streamer>			  DefaultStreamer;

	private:
		// FEpicRtcThread must exist before any AudioTask and AudioMixingCapturer(Which contains a audio task) to ensure it is destroyed last
		TSharedPtr<class FEpicRtcThread>			  EpicRtcThread;
		TSharedPtr<class FEpicRtcAudioMixingCapturer> AudioMixingCapturer;
		TRefCountPtr<EpicRtcPlatformInterface>		  EpicRtcPlatform;
		TRefCountPtr<EpicRtcConferenceInterface>	  EpicRtcConference;
		TRefCountPtr<FEpicRtcStatsCollector>		  StatsCollector;

		TRefCountPtr<FEpicRtcWebsocketFactory>	   WebsocketFactory;
		TUniqueTaskPtr<FEpicRtcTickConferenceTask> TickConferenceTask;

		TArray<EpicRtcVideoEncoderInitializerInterface*> EpicRtcVideoEncoderInitializers;
		TArray<EpicRtcVideoDecoderInitializerInterface*> EpicRtcVideoDecoderInitializers;

		static FUtf8String EpicRtcConferenceName;
	};
} // namespace UE::PixelStreaming2
