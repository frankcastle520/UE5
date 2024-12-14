// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Stats.h"

#include "epic_rtc/core/stats.h"

namespace UE::PixelStreaming2
{
	namespace RTCStatCategories
	{
		const FName LocalVideoTrack = FName(TEXT("outbound-video-track"));
		const FName LocalAudioTrack = FName(TEXT("outbound-audio-track"));
		const FName VideoSource = FName(TEXT("video-source"));
		const FName AudioSource = FName(TEXT("audio-source"));
		const FName DataChannel = FName(TEXT("data-channel"));
		const FName RemoteVideoTrack = FName(TEXT("inbound-video-track"));
		const FName RemoteAudioTrack = FName(TEXT("inbound-audio-track"));
		const FName CandidatePair = FName(TEXT("candidate-pair"));
	} // namespace RTCStatCategories

	class FRTCStatsCollector
	{
	public:
		static TSharedPtr<FRTCStatsCollector> Create(const FString& PlayerId);

	private:
		FRTCStatsCollector();
		explicit FRTCStatsCollector(const FString& PlayerId);

		void OnWebRTCDisableStatsChanged(IConsoleVariable* Var);

	public:
		void Process(const EpicRtcConnectionStats& InStats);

	private:
		/**
		 * ---------- FRTCTrackedStat -------------------
		 * Tracks a stat from WebRTC. Stores the current value and previous value.
		 */
		class FRTCTrackedStat
		{
		public:
			FRTCTrackedStat(FName StatName, int NDecimalPlaces)
				: LatestStat(StatName, 0.0, NDecimalPlaces)
			{
			}

			FRTCTrackedStat(FName StatName, int NDecimalPlaces, uint8 DisplayFlags)
				: LatestStat(StatName, 0.0, NDecimalPlaces)
			{
				LatestStat.DisplayFlags = DisplayFlags;
			}

			FRTCTrackedStat(FName StatName, FName Alias, int NDecimalPlaces, uint8 DisplayFlags);

			double			 CalculateDelta(double Period) const;
			double			 Average() const;
			const FStatData& GetLatestStat() const { return LatestStat; }
			void			 SetLatestValue(double InValue);

		private:
			FStatData LatestStat;
			double	  PrevValue = 0.0;
		};

		class FStatsSink
		{
		public:
			FStatsSink(FName InCategory);
			virtual ~FStatsSink() = default;
			virtual void Add(FName StatName, int NDecimalPlaces)
			{
				Stats.Add(StatName, FRTCTrackedStat(StatName, NDecimalPlaces, FStatData::EDisplayFlags::TEXT));
			}

			virtual void AddAliased(FName StatName, FName AliasedName, int NDecimalPlaces, uint8 DisplayFlags);
			virtual void AddNonRendered(FName StatName)
			{
				Stats.Add(StatName, FRTCTrackedStat(StatName, 2, FStatData::EDisplayFlags::HIDDEN));
			}
			virtual void AddStatCalculator(const TFunction<TOptional<FStatData>(FStatsSink&, double)>& Calculator)
			{
				Calculators.Add(Calculator);
			}
			// returns true if the value is worth storing (false if it started and remains zero)
			virtual bool			 UpdateValue(double NewValue, FRTCTrackedStat* SetValueHere);
			virtual FRTCTrackedStat* Get(const FName& StatName)
			{
				return Stats.Find(StatName);
			}
			virtual FStatData* GetCalculatedStat(const FName& StatName)
			{
				return CalculatedStats.Find(StatName);
			}
			virtual void PostProcess(FStats* PSStats, const FString& PeerId, double SecondsDelta);

			// Stats that are stored as is.
			TMap<FName, FRTCTrackedStat> Stats;
			// Stats we calculate based on the stats map above. This calculation is done in FStatsSink::PostProcess by the `Calculators` below.
			TMap<FName, FStatData>										 CalculatedStats;
			TArray<TFunction<TOptional<FStatData>(FStatsSink&, double)>> Calculators;

		protected:
			FName Category;
		};

		class FRTPLocalVideoTrackStatsSink : public FStatsSink
		{
		public:
			FRTPLocalVideoTrackStatsSink();
			virtual ~FRTPLocalVideoTrackStatsSink() = default;
			void Process(const EpicRtcLocalVideoTrackStats& InStats, const FString& PeerId, double SecondsDelta);
		};

		class FRTPLocalAudioTrackStatsSink : public FStatsSink
		{
		public:
			FRTPLocalAudioTrackStatsSink();
			virtual ~FRTPLocalAudioTrackStatsSink() = default;
			void Process(const EpicRtcLocalTrackRtpStats& InStats, const FString& PeerId, double SecondsDelta);
		};

		class FRTPRemoteTrackStatsSink : public FStatsSink
		{
		public:
			FRTPRemoteTrackStatsSink(FName Category);
			virtual ~FRTPRemoteTrackStatsSink() = default;
			void Process(const EpicRtcRemoteTrackRtpStats& InStats, const FString& PeerId, double SecondsDelta);
		};

		class FVideoSourceStatsSink : public FStatsSink
		{
		public:
			FVideoSourceStatsSink();
			virtual ~FVideoSourceStatsSink() = default;
			void Process(const EpicRtcVideoSourceStats& InStats, const FString& PeerId, double SecondsDelta);
		};

		class FAudioSourceStatsSink : public FStatsSink
		{
		public:
			FAudioSourceStatsSink();
			virtual ~FAudioSourceStatsSink() = default;
			void Process(const EpicRtcAudioSourceStats& InStats, const FString& PeerId, double SecondsDelta);
		};

		/**
		 * ---------- FDataChannelSink -------------------
		 */
		class FDataTrackStatsSink : public FStatsSink
		{
		public:
			FDataTrackStatsSink();
			virtual ~FDataTrackStatsSink() = default;

			void Process(const EpicRtcDataTrackStats& InStats, const FString& PeerId, double SecondsDelta);
		};

		class FCandidatePairStatsSink : public FStatsSink
		{
		public:
			FCandidatePairStatsSink();
			virtual ~FCandidatePairStatsSink() = default;
			void Process(const EpicRtcIceCandidatePairStats& InStats, const FString& PeerId, double SecondsDelta);
		};

	private:
		FString									 AssociatedPlayerId;
		uint64									 LastCalculationCycles;
		bool									 bIsEnabled;

		TUniquePtr<FRTPLocalVideoTrackStatsSink> LocalVideoTrackStatsSink;
		TUniquePtr<FRTPLocalAudioTrackStatsSink> LocalAudioTrackStatsSink;

		TUniquePtr<FVideoSourceStatsSink>		 VideoSourceStatsSink;
		TUniquePtr<FAudioSourceStatsSink>	 AudioSourceStatsSink;

		TUniquePtr<FRTPRemoteTrackStatsSink> RemoteVideoTrackStatsSink;
		TUniquePtr<FRTPRemoteTrackStatsSink> RemoteAudioTrackStatsSink;

		TUniquePtr<FDataTrackStatsSink>		 DataTrackStatsSink;
		
		TUniquePtr<FCandidatePairStatsSink>	 CandidatePairStatsSink;
	};
} // namespace UE::PixelStreaming2