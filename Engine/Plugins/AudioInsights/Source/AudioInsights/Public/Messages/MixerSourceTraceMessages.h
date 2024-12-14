// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "AudioDefines.h"
#include "DSP/Dsp.h"
#include "Messages/AnalyzerMessageQueue.h"
#include "Trace/Analyzer.h"
#include "Views/TableDashboardViewFactory.h"

#if !WITH_EDITOR
#include "Common/PagedArray.h"
#include "TraceServices/Model/AnalysisSession.h"
#endif // !WITH_EDITOR

namespace UE::Audio::Insights
{
	struct FMixerSourceMessageBase
	{
		FMixerSourceMessageBase() = default;
		FMixerSourceMessageBase(const Trace::IAnalyzer::FOnEventContext& InContext)
		{
			const Trace::IAnalyzer::FEventData& EventData = InContext.EventData;
			DeviceId = static_cast<::Audio::FDeviceId>(EventData.GetValue<uint32>("DeviceId"));
			Timestamp = InContext.EventTime.AsSeconds(EventData.GetValue<uint64>("Timestamp"));
			PlayOrder = EventData.GetValue<uint32>("PlayOrder");
		}

		::Audio::FDeviceId DeviceId = INDEX_NONE;
		uint32 PlayOrder = INDEX_NONE;
		uint64 ComponentId = TNumericLimits<uint64>::Max();
		double Timestamp = 0.0;
	};

	using FMixerSourceStopMessage = FMixerSourceMessageBase;

	struct FMixerSourceStartMessage : public FMixerSourceMessageBase
	{
		FMixerSourceStartMessage() = default;
		FMixerSourceStartMessage(const Trace::IAnalyzer::FOnEventContext& InContext)
			: FMixerSourceMessageBase(InContext)
		{
			const Trace::IAnalyzer::FEventData& EventData = InContext.EventData;
			EventData.GetString("Name", Name);
			ComponentId = EventData.GetValue<uint64>("ComponentId");
			SourceId = EventData.GetValue<int32>("SourceId");
		}

		FString Name;
		int32 SourceId = INDEX_NONE;
		uint64 ComponentId = TNumericLimits<uint64>::Max();
	};

#define DEFINE_MIXERSOURCE_PARAM_MESSAGE(ClassName, ParamName, Type, Default)			\
	struct ClassName : public FMixerSourceMessageBase									\
	{																					\
		ClassName() = default;															\
		ClassName(const Trace::IAnalyzer::FOnEventContext& InContext)					\
			: FMixerSourceMessageBase(InContext)										\
		{																				\
			const Trace::IAnalyzer::FEventData& EventData = InContext.EventData;		\
			ParamName = EventData.GetValue<Type>(#ParamName);							\
		}																				\
		Type ParamName = Default;														\
	};

	DEFINE_MIXERSOURCE_PARAM_MESSAGE(FMixerSourceDistanceAttenuationMessage, DistanceAttenuation, float, 0.0f)
	DEFINE_MIXERSOURCE_PARAM_MESSAGE(FMixerSourceEnvelopeMessage, Envelope, float, 0.0f)
	DEFINE_MIXERSOURCE_PARAM_MESSAGE(FMixerSourceHPFFreqMessage, HPFFrequency, float, MIN_FILTER_FREQUENCY)
	DEFINE_MIXERSOURCE_PARAM_MESSAGE(FMixerSourceLPFFreqMessage, LPFFrequency, float, MAX_FILTER_FREQUENCY)
	DEFINE_MIXERSOURCE_PARAM_MESSAGE(FMixerSourcePitchMessage, Pitch, float, 1.0f)
	DEFINE_MIXERSOURCE_PARAM_MESSAGE(FMixerSourceVolumeMessage, Volume, float, 1.0f)
#undef DEFINE_MIXERSOURCE_PARAM_MESSAGE

	using FDataPoint = TPair<double, float>; // (Timestamp, Value)

	class FMixerSourceDashboardEntry : public FSoundAssetDashboardEntry
	{
	public:
		FMixerSourceDashboardEntry()
		{
			constexpr uint32 DataPointsCapacity = 256;

			VolumeDataPoints.SetCapacity(DataPointsCapacity);
			PitchDataPoints.SetCapacity(DataPointsCapacity);
			LPFFreqDataPoints.SetCapacity(DataPointsCapacity);
			HPFFreqDataPoints.SetCapacity(DataPointsCapacity);
			EnvelopeDataPoints.SetCapacity(DataPointsCapacity);
			DistanceAttenuationDataPoints.SetCapacity(DataPointsCapacity);
		}

		virtual ~FMixerSourceDashboardEntry() = default;

		int32 SourceId = INDEX_NONE;

		::Audio::TCircularAudioBuffer<FDataPoint> VolumeDataPoints;
		::Audio::TCircularAudioBuffer<FDataPoint> PitchDataPoints;
		::Audio::TCircularAudioBuffer<FDataPoint> LPFFreqDataPoints;
		::Audio::TCircularAudioBuffer<FDataPoint> HPFFreqDataPoints;
		::Audio::TCircularAudioBuffer<FDataPoint> EnvelopeDataPoints;
		::Audio::TCircularAudioBuffer<FDataPoint> DistanceAttenuationDataPoints;
	};

	class FMixerSourceMessages
	{
	public:
		TAnalyzerMessageQueue<FMixerSourceDistanceAttenuationMessage> DistanceAttenuationMessages { 0.1 };
		TAnalyzerMessageQueue<FMixerSourceEnvelopeMessage> EnvelopeMessages { 0.1 };
		TAnalyzerMessageQueue<FMixerSourceHPFFreqMessage> HPFFreqMessages { 0.1 };
		TAnalyzerMessageQueue<FMixerSourceLPFFreqMessage> LPFFreqMessages { 0.1 };
		TAnalyzerMessageQueue<FMixerSourcePitchMessage> PitchMessages { 0.1 };
		TAnalyzerMessageQueue<FMixerSourceStartMessage> StartMessages { 0.1 };
		TAnalyzerMessageQueue<FMixerSourceStopMessage> StopMessages { 0.1 };
		TAnalyzerMessageQueue<FMixerSourceVolumeMessage> VolumeMessages { 0.1 };
	};

#if !WITH_EDITOR
	struct FMixerSourceSessionCachedMessages
	{
		FMixerSourceSessionCachedMessages(TraceServices::IAnalysisSession& InSession)
			: DistanceAttenuationCachedMessages(InSession.GetLinearAllocator(), 16384)
			, EnvelopeCachedMessages(InSession.GetLinearAllocator(), 16384)
			, HPFFreqCachedMessages(InSession.GetLinearAllocator(), 16384)
			, LPFFreqCachedMessages(InSession.GetLinearAllocator(), 16384)
			, PitchCachedMessages(InSession.GetLinearAllocator(), 16384)
			, StartCachedMessages(InSession.GetLinearAllocator(), 16384)
			, StopCachedMessages(InSession.GetLinearAllocator(), 4096)
			, VolumeCachedMessages(InSession.GetLinearAllocator(), 16384)
		{

		}

		TraceServices::TPagedArray<FMixerSourceDistanceAttenuationMessage> DistanceAttenuationCachedMessages;
		TraceServices::TPagedArray<FMixerSourceEnvelopeMessage> EnvelopeCachedMessages;
		TraceServices::TPagedArray<FMixerSourceHPFFreqMessage> HPFFreqCachedMessages;
		TraceServices::TPagedArray<FMixerSourceLPFFreqMessage> LPFFreqCachedMessages;
		TraceServices::TPagedArray<FMixerSourcePitchMessage> PitchCachedMessages;
		TraceServices::TPagedArray<FMixerSourceStartMessage> StartCachedMessages;
		TraceServices::TPagedArray<FMixerSourceStopMessage> StopCachedMessages;
		TraceServices::TPagedArray<FMixerSourceVolumeMessage> VolumeCachedMessages;
	};
#endif // !WITH_EDITOR

} // namespace UE::Audio::Insights
