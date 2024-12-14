// Copyright Epic Games, Inc. All Rights Reserved.

#include "RTCStatsCollector.h"

#include "Logging.h"
#include "PixelStreaming2PluginSettings.h"
#include "PixelStreaming2StatNames.h"
#include "Streamer.h"
#include "UtilsString.h"

namespace UE::PixelStreaming2
{
	TSharedPtr<FRTCStatsCollector> FRTCStatsCollector::Create(const FString& PlayerId)
	{
		TSharedPtr<FRTCStatsCollector> StatsCollector = TSharedPtr<FRTCStatsCollector>(new FRTCStatsCollector(PlayerId));

		if (UPixelStreaming2PluginSettings::FDelegates* Delegates = UPixelStreaming2PluginSettings::Delegates())
		{
			Delegates->OnWebRTCDisableStatsChanged.AddSP(StatsCollector.ToSharedRef(), &FRTCStatsCollector::OnWebRTCDisableStatsChanged);
		}

		return StatsCollector;
	}

	FRTCStatsCollector::FRTCStatsCollector()
		: FRTCStatsCollector(INVALID_PLAYER_ID)
	{
	}

	FRTCStatsCollector::FRTCStatsCollector(const FString& PlayerId)
		: AssociatedPlayerId(PlayerId)
		, LastCalculationCycles(FPlatformTime::Cycles64())
		, bIsEnabled(!UPixelStreaming2PluginSettings::CVarWebRTCDisableStats.GetValueOnAnyThread())
		, LocalVideoTrackStatsSink(MakeUnique<FRTPLocalVideoTrackStatsSink>())
		, LocalAudioTrackStatsSink(MakeUnique<FRTPLocalAudioTrackStatsSink>())
		, VideoSourceStatsSink(MakeUnique<FVideoSourceStatsSink>())
		, AudioSourceStatsSink(MakeUnique<FAudioSourceStatsSink>())
		, RemoteVideoTrackStatsSink(MakeUnique<FRTPRemoteTrackStatsSink>(RTCStatCategories::RemoteVideoTrack))
		, RemoteAudioTrackStatsSink(MakeUnique<FRTPRemoteTrackStatsSink>(RTCStatCategories::RemoteAudioTrack))
		, DataTrackStatsSink(MakeUnique<FDataTrackStatsSink>())
		, CandidatePairStatsSink(MakeUnique<FCandidatePairStatsSink>())
	{
	}

	void FRTCStatsCollector::OnWebRTCDisableStatsChanged(IConsoleVariable* Var)
	{
		bIsEnabled = !Var->GetBool();
	}

	void FRTCStatsCollector::Process(const EpicRtcConnectionStats& InStats)
	{
		FStats* PSStats = FStats::Get();
		if (!bIsEnabled || !PSStats)
		{
			return;
		}

		uint64 CyclesNow = FPlatformTime::Cycles64();
		double SecondsDelta = FGenericPlatformTime::ToSeconds64(CyclesNow - LastCalculationCycles);

		if (InStats._localVideoTracks._size > 0)
		{
			if (InStats._localVideoTracks._size > 1)
			{
				UE_LOGFMT(LogPixelStreaming2, Warning, "Multiple local video tracks is unsupported. Expected: 1, Actual: {0}", InStats._localVideoTracks._size);
			}
			LocalVideoTrackStatsSink->Process(InStats._localVideoTracks._ptr[0], AssociatedPlayerId, SecondsDelta);
			VideoSourceStatsSink->Process(InStats._localVideoTracks._ptr[0]._source, AssociatedPlayerId, SecondsDelta);
		}

		if (InStats._localAudioTracks._size > 0)
		{
			if (InStats._localAudioTracks._size > 1)
			{
				UE_LOGFMT(LogPixelStreaming2, Warning, "Multiple local audio tracks is unsupported. Expected: 1, Actual: {0}", InStats._localAudioTracks._size);
			}
			LocalAudioTrackStatsSink->Process(InStats._localAudioTracks._ptr[0]._rtp, AssociatedPlayerId, SecondsDelta);
			AudioSourceStatsSink->Process(InStats._localAudioTracks._ptr[0]._source, AssociatedPlayerId, SecondsDelta);
		}

		if (InStats._remoteVideoTracks._size > 0)
		{
			if (InStats._remoteVideoTracks._size > 1)
			{
				UE_LOGFMT(LogPixelStreaming2, Warning, "Multiple remote video tracks is unsupported. Expected: 1, Actual: {0}", InStats._remoteVideoTracks._size);
			}
			RemoteVideoTrackStatsSink->Process(InStats._remoteVideoTracks._ptr[0]._rtp, AssociatedPlayerId, SecondsDelta);
		}

		if (InStats._remoteAudioTracks._size > 0)
		{
			if (InStats._remoteAudioTracks._size > 1)
			{
				UE_LOGFMT(LogPixelStreaming2, Warning, "Multiple remote audio tracks is unsupported. Expected: 1, Actual: {0}", InStats._remoteAudioTracks._size);
			}
			RemoteAudioTrackStatsSink->Process(InStats._remoteAudioTracks._ptr[0]._rtp, AssociatedPlayerId, SecondsDelta);
		}

		if (InStats._dataTracks._size > 0)
		{
			if (InStats._dataTracks._size > 1)
			{
				UE_LOGFMT(LogPixelStreaming2, Warning, "Multiple data tracks is unsupported. Expected: 1, Actual: {0}", InStats._dataTracks._size);
			}
			DataTrackStatsSink->Process(InStats._dataTracks._ptr[0], AssociatedPlayerId, SecondsDelta);
		}

		if (InStats._transports._size > 0)
		{
			//(Nazar.Rudenko): More than one transport is possible only if we are not using bundle which we do
			const EpicRtcTransportStats& Transport = InStats._transports._ptr[0];
			FString						 SelectedPairId = ToString(Transport._selectedCandidatePairId);
			for (int i = 0; i < Transport._candidatePairs._size; i++)
			{
				FString PairId = ToString(Transport._candidatePairs._ptr[i]._id);
				if (SelectedPairId == PairId)
				{
					CandidatePairStatsSink->Process(Transport._candidatePairs._ptr[i], AssociatedPlayerId, SecondsDelta);
				}
			}
		}

		LastCalculationCycles = FPlatformTime::Cycles64();
	}
	/**
	 * ---------- FRTPLocalVideoTrackSink ----------
	 */
	FRTCStatsCollector::FRTPLocalVideoTrackStatsSink::FRTPLocalVideoTrackStatsSink()
		: FStatsSink(RTCStatCategories::LocalVideoTrack)
	{
		// These stats will be extracted from the stat reports and emitted straight to screen
		Add(PixelStreaming2StatNames::FirCount, 0);
		Add(PixelStreaming2StatNames::PliCount, 0);
		Add(PixelStreaming2StatNames::NackCount, 0);
		Add(PixelStreaming2StatNames::RetransmittedBytesSent, 0);
		Add(PixelStreaming2StatNames::TotalEncodeBytesTarget, 0);
		Add(PixelStreaming2StatNames::KeyFramesEncoded, 0);
		Add(PixelStreaming2StatNames::FrameWidth, 0);
		Add(PixelStreaming2StatNames::FrameHeight, 0);
		Add(PixelStreaming2StatNames::HugeFramesSent, 0);
		Add(PixelStreaming2StatNames::PacketsLost, 0);
		Add(PixelStreaming2StatNames::Jitter, 0);
		Add(PixelStreaming2StatNames::RoundTripTime, 0);

		// These are values used to calculate extra values (stores time deltas etc)
		AddNonRendered(PixelStreaming2StatNames::TargetBitrate);
		AddNonRendered(PixelStreaming2StatNames::FramesSent);
		AddNonRendered(PixelStreaming2StatNames::FramesReceived);
		AddNonRendered(PixelStreaming2StatNames::BytesSent);
		AddNonRendered(PixelStreaming2StatNames::BytesReceived);
		AddNonRendered(PixelStreaming2StatNames::QPSum);
		AddNonRendered(PixelStreaming2StatNames::TotalEncodeTime);
		AddNonRendered(PixelStreaming2StatNames::FramesEncoded);
		AddNonRendered(PixelStreaming2StatNames::FramesDecoded);
		AddNonRendered(PixelStreaming2StatNames::TotalPacketSendDelay);

		// Calculated stats below:
		// FrameSent Per Second
		AddStatCalculator([](FStatsSink& StatSource, double Period) -> TOptional<FStatData> {
			FRTCTrackedStat* FramesSentStat = StatSource.Get(PixelStreaming2StatNames::FramesSent);
			if (FramesSentStat && FramesSentStat->GetLatestStat().StatValue > 0)
			{
				const double FramesSentPerSecond = FramesSentStat->CalculateDelta(Period);
				FStatData	 FpsStat = FStatData(PixelStreaming2StatNames::FramesSentPerSecond, FramesSentPerSecond, 0);
				FpsStat.DisplayFlags = FStatData::EDisplayFlags::TEXT | FStatData::EDisplayFlags::GRAPH;
				return FpsStat;
			}
			return {};
		});

		// FramesReceived Per Second
		AddStatCalculator([](FStatsSink& StatSource, double Period) -> TOptional<FStatData> {
			FRTCTrackedStat* FramesReceivedStat = StatSource.Get(PixelStreaming2StatNames::FramesReceived);
			if (FramesReceivedStat && FramesReceivedStat->GetLatestStat().StatValue > 0)
			{
				const double FramesReceivedPerSecond = FramesReceivedStat->CalculateDelta(Period);
				return FStatData(PixelStreaming2StatNames::FramesReceivedPerSecond, FramesReceivedPerSecond, 0);
			}
			return {};
		});

		// Megabits sent Per Second
		AddStatCalculator([](FStatsSink& StatSource, double Period) -> TOptional<FStatData> {
			FRTCTrackedStat* BytesSentStat = StatSource.Get(PixelStreaming2StatNames::BytesSent);
			if (BytesSentStat && BytesSentStat->GetLatestStat().StatValue > 0)
			{
				const double BytesSentPerSecond = BytesSentStat->CalculateDelta(Period);
				const double MegabitsPerSecond = BytesSentPerSecond / 1'000'000.0 * 8.0;
				return FStatData(PixelStreaming2StatNames::BitrateMegabits, MegabitsPerSecond, 2);
			}
			return {};
		});

		// Bits sent Per Second
		AddStatCalculator([](FStatsSink& StatSource, double Period) -> TOptional<FStatData> {
			FRTCTrackedStat* BytesSentStat = StatSource.Get(PixelStreaming2StatNames::BytesSent);
			if (BytesSentStat && BytesSentStat->GetLatestStat().StatValue > 0)
			{
				const double BytesSentPerSecond = BytesSentStat->CalculateDelta(Period);
				const double BitsPerSecond = BytesSentPerSecond * 8.0;
				FStatData	 Stat = FStatData(PixelStreaming2StatNames::Bitrate, BitsPerSecond, 0);
				Stat.DisplayFlags = FStatData::EDisplayFlags::HIDDEN; // We don't want to display bits per second (too many digits)
				return Stat;
			}
			return {};
		});

		// Target megabits sent Per Second
		AddStatCalculator([](FStatsSink& StatSource, double Period) -> TOptional<FStatData> {
			FRTCTrackedStat* TargetBpsStats = StatSource.Get(PixelStreaming2StatNames::TargetBitrate);
			if (TargetBpsStats && TargetBpsStats->GetLatestStat().StatValue > 0)
			{
				const double TargetBps = TargetBpsStats->Average();
				const double MegabitsPerSecond = TargetBps / 1'000'000.0;
				return FStatData(PixelStreaming2StatNames::TargetBitrateMegabits, MegabitsPerSecond, 2);
			}
			return {};
		});

		// Megabits received Per Second
		AddStatCalculator([](FStatsSink& StatSource, double Period) -> TOptional<FStatData> {
			FRTCTrackedStat* BytesReceivedStat = StatSource.Get(PixelStreaming2StatNames::BytesReceived);
			if (BytesReceivedStat && BytesReceivedStat->GetLatestStat().StatValue > 0)
			{
				const double BytesReceivedPerSecond = BytesReceivedStat->CalculateDelta(Period);
				const double MegabitsPerSecond = BytesReceivedPerSecond / 1000.0 * 8.0;
				return FStatData(PixelStreaming2StatNames::Bitrate, MegabitsPerSecond, 2);
			}
			return {};
		});

		// Encoded fps
		AddStatCalculator([](FStatsSink& StatSource, double Period) -> TOptional<FStatData> {
			FRTCTrackedStat* EncodedFramesStat = StatSource.Get(PixelStreaming2StatNames::FramesEncoded);
			if (EncodedFramesStat && EncodedFramesStat->GetLatestStat().StatValue > 0)
			{
				const double EncodedFramesPerSecond = EncodedFramesStat->CalculateDelta(Period);
				return FStatData(PixelStreaming2StatNames::EncodedFramesPerSecond, EncodedFramesPerSecond, 0);
			}
			return {};
		});

		// Decoded fps
		AddStatCalculator([](FStatsSink& StatSource, double Period) -> TOptional<FStatData> {
			FRTCTrackedStat* DecodedFramesStat = StatSource.Get(PixelStreaming2StatNames::FramesDecoded);
			if (DecodedFramesStat && DecodedFramesStat->GetLatestStat().StatValue > 0)
			{
				const double DecodedFramesPerSecond = DecodedFramesStat->CalculateDelta(Period);
				return FStatData(PixelStreaming2StatNames::DecodedFramesPerSecond, DecodedFramesPerSecond, 0);
			}
			return {};
		});

		// Avg QP Per Second
		AddStatCalculator([](FStatsSink& StatSource, double Period) -> TOptional<FStatData> {
			FRTCTrackedStat* QPSumStat = StatSource.Get(PixelStreaming2StatNames::QPSum);
			FStatData*		 EncodedFramesPerSecond = StatSource.GetCalculatedStat(PixelStreaming2StatNames::EncodedFramesPerSecond);
			if (QPSumStat && QPSumStat->GetLatestStat().StatValue > 0
				&& EncodedFramesPerSecond && EncodedFramesPerSecond->StatValue > 0.0)
			{
				const double QPSumDeltaPerSecond = QPSumStat->CalculateDelta(Period);
				const double MeanQPPerFrame = QPSumDeltaPerSecond / EncodedFramesPerSecond->StatValue;
				FName		 StatName = PixelStreaming2StatNames::MeanQPPerSecond;
				return FStatData(StatName, MeanQPPerFrame, 0);
			}
			return {};
		});

		// Mean EncodeTime (ms) Per Frame
		AddStatCalculator([](FStatsSink& StatSource, double Period) -> TOptional<FStatData> {
			FRTCTrackedStat* TotalEncodeTimeStat = StatSource.Get(PixelStreaming2StatNames::TotalEncodeTime);
			FStatData*		 EncodedFramesPerSecond = StatSource.GetCalculatedStat(PixelStreaming2StatNames::EncodedFramesPerSecond);
			if (TotalEncodeTimeStat && TotalEncodeTimeStat->GetLatestStat().StatValue > 0
				&& EncodedFramesPerSecond && EncodedFramesPerSecond->StatValue > 0.0)
			{
				const double TotalEncodeTimePerSecond = TotalEncodeTimeStat->CalculateDelta(Period);
				const double MeanEncodeTimePerFrameMs = TotalEncodeTimePerSecond / EncodedFramesPerSecond->StatValue * 1000.0;
				return FStatData(PixelStreaming2StatNames::MeanEncodeTime, MeanEncodeTimePerFrameMs, 2);
			}
			return {};
		});

		// Mean SendDelay (ms) Per Frame
		AddStatCalculator([](FStatsSink& StatSource, double Period) -> TOptional<FStatData> {
			FRTCTrackedStat* TotalSendDelayStat = StatSource.Get(PixelStreaming2StatNames::TotalPacketSendDelay);
			FStatData*		 FramesSentPerSecond = StatSource.GetCalculatedStat(PixelStreaming2StatNames::FramesSentPerSecond);
			if (TotalSendDelayStat && TotalSendDelayStat->GetLatestStat().StatValue > 0
				&& FramesSentPerSecond && FramesSentPerSecond->StatValue > 0.0)
			{
				const double TotalSendDelayPerSecond = TotalSendDelayStat->CalculateDelta(Period);
				const double MeanSendDelayPerFrameMs = TotalSendDelayPerSecond / FramesSentPerSecond->StatValue * 1000.0;
				return FStatData(PixelStreaming2StatNames::MeanSendDelay, MeanSendDelayPerFrameMs, 2);
			}
			return {};
		});

		// JitterBufferDelay (ms)
		AddStatCalculator([](FStatsSink& StatSource, double Period) -> TOptional<FStatData> {
			FRTCTrackedStat* JitterBufferDelayStat = StatSource.Get(PixelStreaming2StatNames::JitterBufferDelay);
			FStatData*		 FramesReceivedPerSecond = StatSource.GetCalculatedStat(PixelStreaming2StatNames::FramesReceivedPerSecond);
			if (JitterBufferDelayStat && JitterBufferDelayStat->GetLatestStat().StatValue > 0
				&& FramesReceivedPerSecond && FramesReceivedPerSecond->StatValue > 0.0)
			{
				const double TotalJitterBufferDelayPerSecond = JitterBufferDelayStat->CalculateDelta(Period);
				const double MeanJitterBufferDelayMs = TotalJitterBufferDelayPerSecond / FramesReceivedPerSecond->StatValue * 1000.0;
				return FStatData(PixelStreaming2StatNames::JitterBufferDelay, MeanJitterBufferDelayMs, 2);
			}
			return {};
		});
	}

	void FRTCStatsCollector::FRTPLocalVideoTrackStatsSink::Process(const EpicRtcLocalVideoTrackStats& InStats, const FString& PeerId, double SecondsDelta)
	{
		FStats* PSStats = FStats::Get();
		if (!PSStats)
		{
			return;
		}

		if (InStats._rtp._size == 0)
		{
			return;
		}

		// Huge assumption, but assume that normal peers only have 1 video rtp stats and simulcast is anything > 1
		bool bIsSFU = InStats._rtp._size > 1;

		for (int i = 0; i < InStats._rtp._size; i++)
		{
			const EpicRtcLocalTrackRtpStats& RtpStats = InStats._rtp._ptr[i];
			const FString					 StatsId = bIsSFU ? FString::FromInt(RtpStats._local._ssrc) : PeerId;

			for (TPair<FName, FRTCTrackedStat>& Tuple : Stats)
			{
				double NewValue = 0;
				if (Tuple.Key == PixelStreaming2StatNames::FirCount)
				{
					NewValue = RtpStats._local._firCount;
				}
				else if (Tuple.Key == PixelStreaming2StatNames::PliCount)
				{
					NewValue = RtpStats._local._pliCount;
				}
				else if (Tuple.Key == PixelStreaming2StatNames::NackCount)
				{
					NewValue = RtpStats._local._nackCount;
				}
				else if (Tuple.Key == PixelStreaming2StatNames::RetransmittedBytesSent)
				{
					NewValue = RtpStats._local._retransmittedBytesSent;
				}
				else if (Tuple.Key == PixelStreaming2StatNames::TotalEncodeBytesTarget)
				{
					NewValue = RtpStats._local._totalEncodedBytesTarget;
				}
				else if (Tuple.Key == PixelStreaming2StatNames::KeyFramesEncoded)
				{
					NewValue = RtpStats._local._keyFramesEncoded;
				}
				else if (Tuple.Key == PixelStreaming2StatNames::FrameWidth)
				{
					NewValue = RtpStats._local._frameWidth;
				}
				else if (Tuple.Key == PixelStreaming2StatNames::FrameHeight)
				{
					NewValue = RtpStats._local._frameHeight;
				}
				else if (Tuple.Key == PixelStreaming2StatNames::HugeFramesSent)
				{
					NewValue = RtpStats._local._hugeFramesSent;
				}
				else if (Tuple.Key == PixelStreaming2StatNames::TotalPacketSendDelay)
				{
					NewValue = RtpStats._local._totalPacketSendDelay;
				}
				else if (Tuple.Key == PixelStreaming2StatNames::TargetBitrate)
				{
					NewValue = RtpStats._local._targetBitrate;
				}
				else if (Tuple.Key == PixelStreaming2StatNames::FramesSent)
				{
					NewValue = RtpStats._local._framesSent;
				}
				else if (Tuple.Key == PixelStreaming2StatNames::FramesReceived)
				{
					// TODO(Nazar.Rudenko): Available for inbound tracks only
				}
				else if (Tuple.Key == PixelStreaming2StatNames::BytesSent)
				{
					NewValue = RtpStats._local._bytesSent;
				}
				else if (Tuple.Key == PixelStreaming2StatNames::BytesReceived)
				{
					// TODO(Nazar.Rudenko): Available for inbound tracks only
				}
				else if (Tuple.Key == PixelStreaming2StatNames::QPSum)
				{
					NewValue = RtpStats._local._qpSum;
				}
				else if (Tuple.Key == PixelStreaming2StatNames::TotalEncodeTime)
				{
					NewValue = RtpStats._local._totalEncodeTime;
				}
				else if (Tuple.Key == PixelStreaming2StatNames::FramesEncoded)
				{
					NewValue = RtpStats._local._framesEncoded;
				}
				else if (Tuple.Key == PixelStreaming2StatNames::FramesDecoded)
				{
					// TODO(Nazar.Rudenko): Available for inbound tracks only
				}
				else if (Tuple.Key == PixelStreaming2StatNames::PacketsLost)
				{
					NewValue = RtpStats._remote._packetsLost;
				}
				else if (Tuple.Key == PixelStreaming2StatNames::Jitter)
				{
					NewValue = RtpStats._remote._jitter;
				}
				else if (Tuple.Key == PixelStreaming2StatNames::RoundTripTime)
				{
					NewValue = RtpStats._remote._roundTripTime;
				}

				if (UpdateValue(NewValue, &Tuple.Value))
				{
					PSStats->StorePeerStat(StatsId, RTCStatCategories::LocalVideoTrack, Tuple.Value.GetLatestStat());
				}
			}

			PostProcess(PSStats, StatsId, SecondsDelta);
		}
	}
	/**
	 * ---------- FRTPLocalAudioTrackStatsSink ----------
	 */
	FRTCStatsCollector::FRTPLocalAudioTrackStatsSink::FRTPLocalAudioTrackStatsSink()
		: FStatsSink(RTCStatCategories::LocalAudioTrack)
	{
		// These stats will be extracted from the stat reports and emitted straight to screen
		Add(PixelStreaming2StatNames::FirCount, 0);
		Add(PixelStreaming2StatNames::PliCount, 0);
		Add(PixelStreaming2StatNames::NackCount, 0);
		Add(PixelStreaming2StatNames::RetransmittedBytesSent, 0);
		Add(PixelStreaming2StatNames::TotalEncodeBytesTarget, 0);
		Add(PixelStreaming2StatNames::KeyFramesEncoded, 0);
		Add(PixelStreaming2StatNames::FrameWidth, 0);
		Add(PixelStreaming2StatNames::FrameHeight, 0);
		Add(PixelStreaming2StatNames::HugeFramesSent, 0);
		Add(PixelStreaming2StatNames::PacketsLost, 0);
		Add(PixelStreaming2StatNames::Jitter, 0);
		Add(PixelStreaming2StatNames::RoundTripTime, 0);

		// These are values used to calculate extra values (stores time deltas etc)
		AddNonRendered(PixelStreaming2StatNames::TargetBitrate);
		AddNonRendered(PixelStreaming2StatNames::FramesSent);
		AddNonRendered(PixelStreaming2StatNames::FramesReceived);
		AddNonRendered(PixelStreaming2StatNames::BytesSent);
		AddNonRendered(PixelStreaming2StatNames::BytesReceived);
		AddNonRendered(PixelStreaming2StatNames::QPSum);
		AddNonRendered(PixelStreaming2StatNames::TotalEncodeTime);
		AddNonRendered(PixelStreaming2StatNames::FramesEncoded);
		AddNonRendered(PixelStreaming2StatNames::FramesDecoded);
		AddNonRendered(PixelStreaming2StatNames::TotalPacketSendDelay);

		// Calculated stats below:
		// FrameSent Per Second
		AddStatCalculator([](FStatsSink& StatSource, double Period) -> TOptional<FStatData> {
			FRTCTrackedStat* FramesSentStat = StatSource.Get(PixelStreaming2StatNames::FramesSent);
			if (FramesSentStat && FramesSentStat->GetLatestStat().StatValue > 0)
			{
				const double FramesSentPerSecond = FramesSentStat->CalculateDelta(Period);
				FStatData	 FpsStat = FStatData(PixelStreaming2StatNames::FramesSentPerSecond, FramesSentPerSecond, 0);
				FpsStat.DisplayFlags = FStatData::EDisplayFlags::TEXT | FStatData::EDisplayFlags::GRAPH;
				return FpsStat;
			}
			return {};
		});

		// FramesReceived Per Second
		AddStatCalculator([](FStatsSink& StatSource, double Period) -> TOptional<FStatData> {
			FRTCTrackedStat* FramesReceivedStat = StatSource.Get(PixelStreaming2StatNames::FramesReceived);
			if (FramesReceivedStat && FramesReceivedStat->GetLatestStat().StatValue > 0)
			{
				const double FramesReceivedPerSecond = FramesReceivedStat->CalculateDelta(Period);
				return FStatData(PixelStreaming2StatNames::FramesReceivedPerSecond, FramesReceivedPerSecond, 0);
			}
			return {};
		});

		// Megabits sent Per Second
		AddStatCalculator([](FStatsSink& StatSource, double Period) -> TOptional<FStatData> {
			FRTCTrackedStat* BytesSentStat = StatSource.Get(PixelStreaming2StatNames::BytesSent);
			if (BytesSentStat && BytesSentStat->GetLatestStat().StatValue > 0)
			{
				const double BytesSentPerSecond = BytesSentStat->CalculateDelta(Period);
				const double MegabitsPerSecond = BytesSentPerSecond / 1'000'000.0 * 8.0;
				return FStatData(PixelStreaming2StatNames::BitrateMegabits, MegabitsPerSecond, 2);
			}
			return {};
		});

		// Bits sent Per Second
		AddStatCalculator([](FStatsSink& StatSource, double Period) -> TOptional<FStatData> {
			FRTCTrackedStat* BytesSentStat = StatSource.Get(PixelStreaming2StatNames::BytesSent);
			if (BytesSentStat && BytesSentStat->GetLatestStat().StatValue > 0)
			{
				const double BytesSentPerSecond = BytesSentStat->CalculateDelta(Period);
				const double BitsPerSecond = BytesSentPerSecond * 8.0;
				FStatData	 Stat = FStatData(PixelStreaming2StatNames::Bitrate, BitsPerSecond, 0);
				Stat.DisplayFlags = FStatData::EDisplayFlags::HIDDEN; // We don't want to display bits per second (too many digits)
				return Stat;
			}
			return {};
		});

		// Target megabits sent Per Second
		AddStatCalculator([](FStatsSink& StatSource, double Period) -> TOptional<FStatData> {
			FRTCTrackedStat* TargetBpsStats = StatSource.Get(PixelStreaming2StatNames::TargetBitrate);
			if (TargetBpsStats && TargetBpsStats->GetLatestStat().StatValue > 0)
			{
				const double TargetBps = TargetBpsStats->Average();
				const double MegabitsPerSecond = TargetBps / 1'000'000.0;
				return FStatData(PixelStreaming2StatNames::TargetBitrateMegabits, MegabitsPerSecond, 2);
			}
			return {};
		});

		// Megabits received Per Second
		AddStatCalculator([](FStatsSink& StatSource, double Period) -> TOptional<FStatData> {
			FRTCTrackedStat* BytesReceivedStat = StatSource.Get(PixelStreaming2StatNames::BytesReceived);
			if (BytesReceivedStat && BytesReceivedStat->GetLatestStat().StatValue > 0)
			{
				const double BytesReceivedPerSecond = BytesReceivedStat->CalculateDelta(Period);
				const double MegabitsPerSecond = BytesReceivedPerSecond / 1000.0 * 8.0;
				return FStatData(PixelStreaming2StatNames::Bitrate, MegabitsPerSecond, 2);
			}
			return {};
		});

		// Encoded fps
		AddStatCalculator([](FStatsSink& StatSource, double Period) -> TOptional<FStatData> {
			FRTCTrackedStat* EncodedFramesStat = StatSource.Get(PixelStreaming2StatNames::FramesEncoded);
			if (EncodedFramesStat && EncodedFramesStat->GetLatestStat().StatValue > 0)
			{
				const double EncodedFramesPerSecond = EncodedFramesStat->CalculateDelta(Period);
				return FStatData(PixelStreaming2StatNames::EncodedFramesPerSecond, EncodedFramesPerSecond, 0);
			}
			return {};
		});

		// Decoded fps
		AddStatCalculator([](FStatsSink& StatSource, double Period) -> TOptional<FStatData> {
			FRTCTrackedStat* DecodedFramesStat = StatSource.Get(PixelStreaming2StatNames::FramesDecoded);
			if (DecodedFramesStat && DecodedFramesStat->GetLatestStat().StatValue > 0)
			{
				const double DecodedFramesPerSecond = DecodedFramesStat->CalculateDelta(Period);
				return FStatData(PixelStreaming2StatNames::DecodedFramesPerSecond, DecodedFramesPerSecond, 0);
			}
			return {};
		});

		// Avg QP Per Second
		AddStatCalculator([](FStatsSink& StatSource, double Period) -> TOptional<FStatData> {
			FRTCTrackedStat* QPSumStat = StatSource.Get(PixelStreaming2StatNames::QPSum);
			FStatData*		 EncodedFramesPerSecond = StatSource.GetCalculatedStat(PixelStreaming2StatNames::EncodedFramesPerSecond);
			if (QPSumStat && QPSumStat->GetLatestStat().StatValue > 0
				&& EncodedFramesPerSecond && EncodedFramesPerSecond->StatValue > 0.0)
			{
				const double QPSumDeltaPerSecond = QPSumStat->CalculateDelta(Period);
				const double MeanQPPerFrame = QPSumDeltaPerSecond / EncodedFramesPerSecond->StatValue;
				FName		 StatName = PixelStreaming2StatNames::MeanQPPerSecond;
				return FStatData(StatName, MeanQPPerFrame, 0);
			}
			return {};
		});

		// Mean EncodeTime (ms) Per Frame
		AddStatCalculator([](FStatsSink& StatSource, double Period) -> TOptional<FStatData> {
			FRTCTrackedStat* TotalEncodeTimeStat = StatSource.Get(PixelStreaming2StatNames::TotalEncodeTime);
			FStatData*		 EncodedFramesPerSecond = StatSource.GetCalculatedStat(PixelStreaming2StatNames::EncodedFramesPerSecond);
			if (TotalEncodeTimeStat && TotalEncodeTimeStat->GetLatestStat().StatValue > 0
				&& EncodedFramesPerSecond && EncodedFramesPerSecond->StatValue > 0.0)
			{
				const double TotalEncodeTimePerSecond = TotalEncodeTimeStat->CalculateDelta(Period);
				const double MeanEncodeTimePerFrameMs = TotalEncodeTimePerSecond / EncodedFramesPerSecond->StatValue * 1000.0;
				return FStatData(PixelStreaming2StatNames::MeanEncodeTime, MeanEncodeTimePerFrameMs, 2);
			}
			return {};
		});

		// Mean SendDelay (ms) Per Frame
		AddStatCalculator([](FStatsSink& StatSource, double Period) -> TOptional<FStatData> {
			FRTCTrackedStat* TotalSendDelayStat = StatSource.Get(PixelStreaming2StatNames::TotalPacketSendDelay);
			FStatData*		 FramesSentPerSecond = StatSource.GetCalculatedStat(PixelStreaming2StatNames::FramesSentPerSecond);
			if (TotalSendDelayStat && TotalSendDelayStat->GetLatestStat().StatValue > 0
				&& FramesSentPerSecond && FramesSentPerSecond->StatValue > 0.0)
			{
				const double TotalSendDelayPerSecond = TotalSendDelayStat->CalculateDelta(Period);
				const double MeanSendDelayPerFrameMs = TotalSendDelayPerSecond / FramesSentPerSecond->StatValue * 1000.0;
				return FStatData(PixelStreaming2StatNames::MeanSendDelay, MeanSendDelayPerFrameMs, 2);
			}
			return {};
		});

		// JitterBufferDelay (ms)
		AddStatCalculator([](FStatsSink& StatSource, double Period) -> TOptional<FStatData> {
			FRTCTrackedStat* JitterBufferDelayStat = StatSource.Get(PixelStreaming2StatNames::JitterBufferDelay);
			FStatData*		 FramesReceivedPerSecond = StatSource.GetCalculatedStat(PixelStreaming2StatNames::FramesReceivedPerSecond);
			if (JitterBufferDelayStat && JitterBufferDelayStat->GetLatestStat().StatValue > 0
				&& FramesReceivedPerSecond && FramesReceivedPerSecond->StatValue > 0.0)
			{
				const double TotalJitterBufferDelayPerSecond = JitterBufferDelayStat->CalculateDelta(Period);
				const double MeanJitterBufferDelayMs = TotalJitterBufferDelayPerSecond / FramesReceivedPerSecond->StatValue * 1000.0;
				return FStatData(PixelStreaming2StatNames::JitterBufferDelay, MeanJitterBufferDelayMs, 2);
			}
			return {};
		});
	}

	void FRTCStatsCollector::FRTPLocalAudioTrackStatsSink::Process(const EpicRtcLocalTrackRtpStats& InStats, const FString& PeerId, double SecondsDelta)
	{
		FStats* PSStats = FStats::Get();
		if (!PSStats)
		{
			return;
		}

		for (TPair<FName, FRTCTrackedStat>& Tuple : Stats)
		{
			double NewValue = 0;
			if (Tuple.Key == PixelStreaming2StatNames::TotalPacketSendDelay)
			{
				NewValue = InStats._local._totalPacketSendDelay;
			}
			else if (Tuple.Key == PixelStreaming2StatNames::TargetBitrate)
			{
				NewValue = InStats._local._targetBitrate;
			}
			else if (Tuple.Key == PixelStreaming2StatNames::BytesSent)
			{
				NewValue = InStats._local._bytesSent;
			}
			else if (Tuple.Key == PixelStreaming2StatNames::PacketsLost)
			{
				NewValue = InStats._remote._packetsLost;
			}
			else if (Tuple.Key == PixelStreaming2StatNames::Jitter)
			{
				NewValue = InStats._remote._jitter;
			}
			else if (Tuple.Key == PixelStreaming2StatNames::RoundTripTime)
			{
				NewValue = InStats._remote._roundTripTime;
			}

			if (UpdateValue(NewValue, &Tuple.Value))
			{
				PSStats->StorePeerStat(PeerId, RTCStatCategories::LocalAudioTrack, Tuple.Value.GetLatestStat());
			}
		}

		PostProcess(PSStats, PeerId, SecondsDelta);
	}

	/**
	 * ---------- FRTPRemoteTrackStatsSink ----------
	 */
	FRTCStatsCollector::FRTPRemoteTrackStatsSink::FRTPRemoteTrackStatsSink(FName InCategory)
		: FStatsSink(InCategory)
	{
		// These stats will be extracted from the stat reports and emitted straight to screen
		Add(PixelStreaming2StatNames::FirCount, 0);
		Add(PixelStreaming2StatNames::PliCount, 0);
		Add(PixelStreaming2StatNames::NackCount, 0);
		Add(PixelStreaming2StatNames::RetransmittedBytesReceived, 0);
		Add(PixelStreaming2StatNames::RetransmittedPacketsReceived, 0);
		Add(PixelStreaming2StatNames::TotalEncodeBytesTarget, 0);
		Add(PixelStreaming2StatNames::KeyFramesDecoded, 0);
		Add(PixelStreaming2StatNames::FrameWidth, 0);
		Add(PixelStreaming2StatNames::FrameHeight, 0);
		Add(PixelStreaming2StatNames::HugeFramesSent, 0);
		Add(PixelStreaming2StatNames::PacketsLost, 0);
		Add(PixelStreaming2StatNames::Jitter, 0);
		Add(PixelStreaming2StatNames::RoundTripTime, 0);

		// These are values used to calculate extra values (stores time deltas etc)
		AddNonRendered(PixelStreaming2StatNames::TargetBitrate);
		AddNonRendered(PixelStreaming2StatNames::FramesSent);
		AddNonRendered(PixelStreaming2StatNames::FramesReceived);
		AddNonRendered(PixelStreaming2StatNames::BytesSent);
		AddNonRendered(PixelStreaming2StatNames::BytesReceived);
		AddNonRendered(PixelStreaming2StatNames::QPSum);
		AddNonRendered(PixelStreaming2StatNames::TotalEncodeTime);
		AddNonRendered(PixelStreaming2StatNames::FramesEncoded);
		AddNonRendered(PixelStreaming2StatNames::FramesDecoded);
		AddNonRendered(PixelStreaming2StatNames::TotalPacketSendDelay);

		// Calculated stats below:
		// FrameSent Per Second
		AddStatCalculator([](FStatsSink& StatSource, double Period) -> TOptional<FStatData> {
			FRTCTrackedStat* FramesSentStat = StatSource.Get(PixelStreaming2StatNames::FramesSent);
			if (FramesSentStat && FramesSentStat->GetLatestStat().StatValue > 0)
			{
				const double FramesSentPerSecond = FramesSentStat->CalculateDelta(Period);
				FStatData	 FpsStat = FStatData(PixelStreaming2StatNames::FramesSentPerSecond, FramesSentPerSecond, 0);
				FpsStat.DisplayFlags = FStatData::EDisplayFlags::TEXT | FStatData::EDisplayFlags::GRAPH;
				return FpsStat;
			}
			return {};
		});

		// FramesReceived Per Second
		AddStatCalculator([](FStatsSink& StatSource, double Period) -> TOptional<FStatData> {
			FRTCTrackedStat* FramesReceivedStat = StatSource.Get(PixelStreaming2StatNames::FramesReceived);
			if (FramesReceivedStat && FramesReceivedStat->GetLatestStat().StatValue > 0)
			{
				const double FramesReceivedPerSecond = FramesReceivedStat->CalculateDelta(Period);
				return FStatData(PixelStreaming2StatNames::FramesReceivedPerSecond, FramesReceivedPerSecond, 0);
			}
			return {};
		});

		// Megabits sent Per Second
		AddStatCalculator([](FStatsSink& StatSource, double Period) -> TOptional<FStatData> {
			FRTCTrackedStat* BytesSentStat = StatSource.Get(PixelStreaming2StatNames::BytesSent);
			if (BytesSentStat && BytesSentStat->GetLatestStat().StatValue > 0)
			{
				const double BytesSentPerSecond = BytesSentStat->CalculateDelta(Period);
				const double MegabitsPerSecond = BytesSentPerSecond / 1'000'000.0 * 8.0;
				return FStatData(PixelStreaming2StatNames::BitrateMegabits, MegabitsPerSecond, 2);
			}
			return {};
		});

		// Bits sent Per Second
		AddStatCalculator([](FStatsSink& StatSource, double Period) -> TOptional<FStatData> {
			FRTCTrackedStat* BytesSentStat = StatSource.Get(PixelStreaming2StatNames::BytesSent);
			if (BytesSentStat && BytesSentStat->GetLatestStat().StatValue > 0)
			{
				const double BytesSentPerSecond = BytesSentStat->CalculateDelta(Period);
				const double BitsPerSecond = BytesSentPerSecond * 8.0;
				FStatData	 Stat = FStatData(PixelStreaming2StatNames::Bitrate, BitsPerSecond, 0);
				Stat.DisplayFlags = FStatData::EDisplayFlags::HIDDEN; // We don't want to display bits per second (too many digits)
				return Stat;
			}
			return {};
		});

		// Target megabits sent Per Second
		AddStatCalculator([](FStatsSink& StatSource, double Period) -> TOptional<FStatData> {
			FRTCTrackedStat* TargetBpsStats = StatSource.Get(PixelStreaming2StatNames::TargetBitrate);
			if (TargetBpsStats && TargetBpsStats->GetLatestStat().StatValue > 0)
			{
				const double TargetBps = TargetBpsStats->Average();
				const double MegabitsPerSecond = TargetBps / 1'000'000.0;
				return FStatData(PixelStreaming2StatNames::TargetBitrateMegabits, MegabitsPerSecond, 2);
			}
			return {};
		});

		// Megabits received Per Second
		AddStatCalculator([](FStatsSink& StatSource, double Period) -> TOptional<FStatData> {
			FRTCTrackedStat* BytesReceivedStat = StatSource.Get(PixelStreaming2StatNames::BytesReceived);
			if (BytesReceivedStat && BytesReceivedStat->GetLatestStat().StatValue > 0)
			{
				const double BytesReceivedPerSecond = BytesReceivedStat->CalculateDelta(Period);
				const double MegabitsPerSecond = BytesReceivedPerSecond / 1000.0 * 8.0;
				return FStatData(PixelStreaming2StatNames::Bitrate, MegabitsPerSecond, 2);
			}
			return {};
		});

		// Encoded fps
		AddStatCalculator([](FStatsSink& StatSource, double Period) -> TOptional<FStatData> {
			FRTCTrackedStat* EncodedFramesStat = StatSource.Get(PixelStreaming2StatNames::FramesEncoded);
			if (EncodedFramesStat && EncodedFramesStat->GetLatestStat().StatValue > 0)
			{
				const double EncodedFramesPerSecond = EncodedFramesStat->CalculateDelta(Period);
				return FStatData(PixelStreaming2StatNames::EncodedFramesPerSecond, EncodedFramesPerSecond, 0);
			}
			return {};
		});

		// Decoded fps
		AddStatCalculator([](FStatsSink& StatSource, double Period) -> TOptional<FStatData> {
			FRTCTrackedStat* DecodedFramesStat = StatSource.Get(PixelStreaming2StatNames::FramesDecoded);
			if (DecodedFramesStat && DecodedFramesStat->GetLatestStat().StatValue > 0)
			{
				const double DecodedFramesPerSecond = DecodedFramesStat->CalculateDelta(Period);
				return FStatData(PixelStreaming2StatNames::DecodedFramesPerSecond, DecodedFramesPerSecond, 0);
			}
			return {};
		});

		// Avg QP Per Second
		AddStatCalculator([](FStatsSink& StatSource, double Period) -> TOptional<FStatData> {
			FRTCTrackedStat* QPSumStat = StatSource.Get(PixelStreaming2StatNames::QPSum);
			FStatData*		 EncodedFramesPerSecond = StatSource.GetCalculatedStat(PixelStreaming2StatNames::EncodedFramesPerSecond);
			if (QPSumStat && QPSumStat->GetLatestStat().StatValue > 0
				&& EncodedFramesPerSecond && EncodedFramesPerSecond->StatValue > 0.0)
			{
				const double QPSumDeltaPerSecond = QPSumStat->CalculateDelta(Period);
				const double MeanQPPerFrame = QPSumDeltaPerSecond / EncodedFramesPerSecond->StatValue;
				FName		 StatName = PixelStreaming2StatNames::MeanQPPerSecond;
				return FStatData(StatName, MeanQPPerFrame, 0);
			}
			return {};
		});

		// Mean EncodeTime (ms) Per Frame
		AddStatCalculator([](FStatsSink& StatSource, double Period) -> TOptional<FStatData> {
			FRTCTrackedStat* TotalEncodeTimeStat = StatSource.Get(PixelStreaming2StatNames::TotalEncodeTime);
			FStatData*		 EncodedFramesPerSecond = StatSource.GetCalculatedStat(PixelStreaming2StatNames::EncodedFramesPerSecond);
			if (TotalEncodeTimeStat && TotalEncodeTimeStat->GetLatestStat().StatValue > 0
				&& EncodedFramesPerSecond && EncodedFramesPerSecond->StatValue > 0.0)
			{
				const double TotalEncodeTimePerSecond = TotalEncodeTimeStat->CalculateDelta(Period);
				const double MeanEncodeTimePerFrameMs = TotalEncodeTimePerSecond / EncodedFramesPerSecond->StatValue * 1000.0;
				return FStatData(PixelStreaming2StatNames::MeanEncodeTime, MeanEncodeTimePerFrameMs, 2);
			}
			return {};
		});

		// Mean SendDelay (ms) Per Frame
		AddStatCalculator([](FStatsSink& StatSource, double Period) -> TOptional<FStatData> {
			FRTCTrackedStat* TotalSendDelayStat = StatSource.Get(PixelStreaming2StatNames::TotalPacketSendDelay);
			FStatData*		 FramesSentPerSecond = StatSource.GetCalculatedStat(PixelStreaming2StatNames::FramesSentPerSecond);
			if (TotalSendDelayStat && TotalSendDelayStat->GetLatestStat().StatValue > 0
				&& FramesSentPerSecond && FramesSentPerSecond->StatValue > 0.0)
			{
				const double TotalSendDelayPerSecond = TotalSendDelayStat->CalculateDelta(Period);
				const double MeanSendDelayPerFrameMs = TotalSendDelayPerSecond / FramesSentPerSecond->StatValue * 1000.0;
				return FStatData(PixelStreaming2StatNames::MeanSendDelay, MeanSendDelayPerFrameMs, 2);
			}
			return {};
		});

		// JitterBufferDelay (ms)
		AddStatCalculator([](FStatsSink& StatSource, double Period) -> TOptional<FStatData> {
			FRTCTrackedStat* JitterBufferDelayStat = StatSource.Get(PixelStreaming2StatNames::JitterBufferDelay);
			FStatData*		 FramesReceivedPerSecond = StatSource.GetCalculatedStat(PixelStreaming2StatNames::FramesReceivedPerSecond);
			if (JitterBufferDelayStat && JitterBufferDelayStat->GetLatestStat().StatValue > 0
				&& FramesReceivedPerSecond && FramesReceivedPerSecond->StatValue > 0.0)
			{
				const double TotalJitterBufferDelayPerSecond = JitterBufferDelayStat->CalculateDelta(Period);
				const double MeanJitterBufferDelayMs = TotalJitterBufferDelayPerSecond / FramesReceivedPerSecond->StatValue * 1000.0;
				return FStatData(PixelStreaming2StatNames::JitterBufferDelay, MeanJitterBufferDelayMs, 2);
			}
			return {};
		});
	}

	void FRTCStatsCollector::FRTPRemoteTrackStatsSink::Process(const EpicRtcRemoteTrackRtpStats& InStats, const FString& PeerId, double SecondsDelta)
	{
		FStats* PSStats = FStats::Get();
		if (!PSStats)
		{
			return;
		}

		for (TPair<FName, FRTCTrackedStat>& Tuple : Stats)
		{
			double NewValue = 0;
			if (Tuple.Key == PixelStreaming2StatNames::FirCount)
			{
				NewValue = InStats._local._firCount;
			}
			else if (Tuple.Key == PixelStreaming2StatNames::PliCount)
			{
				NewValue = InStats._local._pliCount;
			}
			else if (Tuple.Key == PixelStreaming2StatNames::NackCount)
			{
				NewValue = InStats._local._nackCount;
			}
			else if (Tuple.Key == PixelStreaming2StatNames::RetransmittedBytesReceived)
			{
				NewValue = InStats._local._retransmittedBytesReceived;
			}
			else if (Tuple.Key == PixelStreaming2StatNames::RetransmittedPacketsReceived)
			{
				NewValue = InStats._local._retransmittedPacketsReceived;
			}
			else if (Tuple.Key == PixelStreaming2StatNames::KeyFramesDecoded)
			{
				NewValue = InStats._local._keyFramesDecoded;
			}
			else if (Tuple.Key == PixelStreaming2StatNames::FrameWidth)
			{
				NewValue = InStats._local._frameWidth;
			}
			else if (Tuple.Key == PixelStreaming2StatNames::FrameHeight)
			{
				NewValue = InStats._local._frameHeight;
			}
			else if (Tuple.Key == PixelStreaming2StatNames::FramesReceived)
			{
				NewValue = InStats._local._framesReceived;
			}
			else if (Tuple.Key == PixelStreaming2StatNames::BytesReceived)
			{
				NewValue = InStats._local._bytesReceived;
			}
			else if (Tuple.Key == PixelStreaming2StatNames::QPSum)
			{
				NewValue = InStats._local._qpSum;
			}
			else if (Tuple.Key == PixelStreaming2StatNames::FramesDecoded)
			{
				NewValue = InStats._local._framesDecoded;
			}
			else if (Tuple.Key == PixelStreaming2StatNames::PacketsLost)
			{
				NewValue = InStats._local._packetsLost;
			}
			else if (Tuple.Key == PixelStreaming2StatNames::Jitter)
			{
				NewValue = InStats._local._jitter;
			}
			else if (Tuple.Key == PixelStreaming2StatNames::RoundTripTime)
			{
				NewValue = InStats._remote._roundTripTime;
			}

			if (UpdateValue(NewValue, &Tuple.Value))
			{
				PSStats->StorePeerStat(PeerId, RTCStatCategories::RemoteVideoTrack, Tuple.Value.GetLatestStat());
			}
		}

		PostProcess(PSStats, PeerId, SecondsDelta);
	}

	/**
	 * ---------- FRTPVideoSourceSink ----------
	 */
	FRTCStatsCollector::FVideoSourceStatsSink::FVideoSourceStatsSink()
		: FStatsSink(RTCStatCategories::VideoSource)
	{
		// Track video source fps
		Add(PixelStreaming2StatNames::SourceFps, 0);
	}

	void FRTCStatsCollector::FVideoSourceStatsSink::Process(const EpicRtcVideoSourceStats& InStats, const FString& PeerId, double SecondsDelta)
	{
		FStats* PSStats = FStats::Get();
		if (!PSStats)
		{
			return;
		}

		for (TPair<FName, FRTCTrackedStat>& Tuple : Stats)
		{
			double NewValue = 0;
			if (Tuple.Key == PixelStreaming2StatNames::SourceFps)
			{
				NewValue = InStats._framesPerSecond;
			}

			if (UpdateValue(NewValue, &Tuple.Value))
			{
				PSStats->StorePeerStat(PeerId, RTCStatCategories::VideoSource, Tuple.Value.GetLatestStat());
			}
		}

		PostProcess(PSStats, PeerId, SecondsDelta);
	}

	/**
	 * ---------- FRTPAudioSourceSink ----------
	 */
	FRTCStatsCollector::FAudioSourceStatsSink::FAudioSourceStatsSink()
		: FStatsSink(RTCStatCategories::AudioSource)
	{
		Add(PixelStreaming2StatNames::AudioLevel, 0);
		Add(PixelStreaming2StatNames::TotalSamplesDuration, 0);
	}

	void FRTCStatsCollector::FAudioSourceStatsSink::Process(const EpicRtcAudioSourceStats& InStats, const FString& PeerId, double SecondsDelta)
	{
		FStats* PSStats = FStats::Get();
		if (!PSStats)
		{
			return;
		}

		for (TPair<FName, FRTCTrackedStat>& Tuple : Stats)
		{
			double NewValue = 0;
			if (Tuple.Key == PixelStreaming2StatNames::AudioLevel)
			{
				NewValue = InStats._audioLevel;
			}
			else if (Tuple.Key == PixelStreaming2StatNames::TotalSamplesDuration)
			{
				NewValue = InStats._totalSamplesDuration;
			}

			if (UpdateValue(NewValue, &Tuple.Value))
			{
				PSStats->StorePeerStat(PeerId, RTCStatCategories::AudioSource, Tuple.Value.GetLatestStat());
			}
		}

		PostProcess(PSStats, PeerId, SecondsDelta);
	}

	/**
	 * ----------- FDataChannelSink -----------
	 */
	FRTCStatsCollector::FDataTrackStatsSink::FDataTrackStatsSink()
		: FStatsSink(RTCStatCategories::DataChannel)
	{
		// These names are added as aliased names because `bytesSent` is ambiguous stat that is used across inbound-rtp, outbound-rtp, and data-channel
		// so to disambiguate which state we are referring to we record the `bytesSent` stat for the data-channel but store and report it as `data-channel-bytesSent`
		AddAliased(PixelStreaming2StatNames::MessagesSent, PixelStreaming2StatNames::DataChannelMessagesSent, 0, FStatData::EDisplayFlags::TEXT | FStatData::EDisplayFlags::GRAPH);
		AddAliased(PixelStreaming2StatNames::MessagesReceived, PixelStreaming2StatNames::DataChannelBytesReceived, 0, FStatData::EDisplayFlags::TEXT | FStatData::EDisplayFlags::GRAPH);
		AddAliased(PixelStreaming2StatNames::BytesSent, PixelStreaming2StatNames::DataChannelBytesSent, 0, FStatData::EDisplayFlags::TEXT | FStatData::EDisplayFlags::GRAPH);
		AddAliased(PixelStreaming2StatNames::BytesReceived, PixelStreaming2StatNames::DataChannelMessagesReceived, 0, FStatData::EDisplayFlags::TEXT | FStatData::EDisplayFlags::GRAPH);
	}

	void FRTCStatsCollector::FDataTrackStatsSink::Process(const EpicRtcDataTrackStats& InStats, const FString& PeerId, double SecondsDelta)
	{
		FStats* PSStats = FStats::Get();
		if (!PSStats)
		{
			return;
		}

		for (TPair<FName, FRTCTrackedStat>& Tuple : Stats)
		{
			double NewValue = 0;
			if (Tuple.Key == PixelStreaming2StatNames::MessagesSent)
			{
				NewValue = InStats._messagesSent;
			}
			else if (Tuple.Key == PixelStreaming2StatNames::MessagesReceived)
			{
				NewValue = InStats._messagesReceived;
			}
			else if (Tuple.Key == PixelStreaming2StatNames::BytesSent)
			{
				NewValue = InStats._bytesSent;
			}
			else if (Tuple.Key == PixelStreaming2StatNames::BytesReceived)
			{
				NewValue = InStats._bytesReceived;
			}

			if (UpdateValue(NewValue, &Tuple.Value))
			{
				PSStats->StorePeerStat(PeerId, RTCStatCategories::DataChannel, Tuple.Value.GetLatestStat());
			}
		}

		PostProcess(PSStats, PeerId, SecondsDelta);
	}

	/**
	 * ---------- FRTPAudioSourceSink ----------
	 */
	FRTCStatsCollector::FCandidatePairStatsSink::FCandidatePairStatsSink()
		: FStatsSink(RTCStatCategories::CandidatePair)
	{
		Add(PixelStreaming2StatNames::AvailableOutgoingBitrate, 0);
		Add(PixelStreaming2StatNames::AvailableIncomingBitrate, 0);
	}

	void FRTCStatsCollector::FCandidatePairStatsSink::Process(const EpicRtcIceCandidatePairStats& InStats, const FString& PeerId, double SecondsDelta)
	{
		FStats* PSStats = FStats::Get();
		if (!PSStats)
		{
			return;
		}

		for (TPair<FName, FRTCTrackedStat>& Tuple : Stats)
		{
			double NewValue = 0;
			if (Tuple.Key == PixelStreaming2StatNames::AvailableOutgoingBitrate)
			{
				NewValue = InStats._availableOutgoingBitrate;
			}
			else if (Tuple.Key == PixelStreaming2StatNames::AvailableIncomingBitrate)
			{
				NewValue = InStats._availableIncomingBitrate;
			}

			if (UpdateValue(NewValue, &Tuple.Value))
			{
				PSStats->StorePeerStat(PeerId, Category, Tuple.Value.GetLatestStat());
			}
		}

		PostProcess(PSStats, PeerId, SecondsDelta);
	}

	/**
	 * ---------- FRTCTrackedStat -------------------
	 */
	FRTCStatsCollector::FRTCTrackedStat::FRTCTrackedStat(FName StatName, FName Alias, int NDecimalPlaces, uint8 DisplayFlags)
		: LatestStat(StatName, 0.0, NDecimalPlaces)
	{
		LatestStat.DisplayFlags = DisplayFlags;
		LatestStat.Alias = Alias;
	}

	double FRTCStatsCollector::FRTCTrackedStat::CalculateDelta(double Period) const
	{
		return (LatestStat.StatValue - PrevValue) * Period;
	}

	double FRTCStatsCollector::FRTCTrackedStat::Average() const
	{
		return (LatestStat.StatValue + PrevValue) * 0.5;
	}

	void FRTCStatsCollector::FRTCTrackedStat::SetLatestValue(double InValue)
	{
		PrevValue = LatestStat.StatValue;
		LatestStat.StatValue = InValue;
	}

	/**
	 * --------- FStatsSink ------------------------
	 */
	FRTCStatsCollector::FStatsSink::FStatsSink(FName InCategory)
		: Category(MoveTemp(InCategory))
	{
	}

	void FRTCStatsCollector::FStatsSink::AddAliased(FName StatName, FName AliasedName, int NDecimalPlaces, uint8 DisplayFlags)
	{
		FRTCTrackedStat Stat = FRTCTrackedStat(StatName, AliasedName, NDecimalPlaces, DisplayFlags);
		Stats.Add(StatName, Stat);
	}

	bool FRTCStatsCollector::FStatsSink::UpdateValue(double NewValue, FRTCTrackedStat* SetValueHere)
	{
		const bool bZeroInitially = SetValueHere->GetLatestStat().StatValue == 0.0;
		SetValueHere->SetLatestValue(NewValue);
		const bool bZeroStill = SetValueHere->GetLatestStat().StatValue == 0.0;
		return !(bZeroInitially && bZeroStill);
	}

	void FRTCStatsCollector::FStatsSink::PostProcess(FStats* PSStats, const FString& PeerId, double SecondsDelta)
	{
		for (auto& Calculator : Calculators)
		{
			TOptional<FStatData> OptStatData = Calculator(*this, SecondsDelta);
			if (OptStatData.IsSet())
			{
				FStatData& StatData = *OptStatData;
				CalculatedStats.Add(StatData.StatName, StatData);
				PSStats->StorePeerStat(PeerId, Category, StatData);
			}
		}
	}
} // namespace UE::PixelStreaming2