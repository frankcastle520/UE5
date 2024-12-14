// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "AudioDefines.h"
#include "Math/NumericLimits.h"
#include "Messages/AnalyzerMessageQueue.h"
#include "Trace/Analyzer.h"
#include "Views/TableDashboardViewFactory.h"

namespace AudioModulationEditor
{

	using FBusId = uint32;

	struct FControlBusMessageBase
	{
		FControlBusMessageBase() = default;
		FControlBusMessageBase(const UE::Trace::IAnalyzer::FOnEventContext& InContext)
		{
			const UE::Trace::IAnalyzer::FEventData& EventData = InContext.EventData;
			DeviceId = static_cast<::Audio::FDeviceId>(EventData.GetValue<uint32>("DeviceId"));
			Timestamp = InContext.EventTime.AsSeconds(EventData.GetValue<uint64>("Timestamp"));
			ControlBusId = static_cast<FBusId>(EventData.GetValue<uint32>("ControlBusId"));
		}

		Audio::FDeviceId DeviceId = INDEX_NONE;
		FBusId ControlBusId;
		double Timestamp = 0.0;
	};

	struct FControlBusActivateMessage : public FControlBusMessageBase
	{
		FControlBusActivateMessage() = default;
		FControlBusActivateMessage(const UE::Trace::IAnalyzer::FOnEventContext& InContext)
			: FControlBusMessageBase(InContext)
		{
			const UE::Trace::IAnalyzer::FEventData& EventData = InContext.EventData;
			EventData.GetString("Name", BusName);
			EventData.GetString("ParamName", ParamName);
		}

		FString BusName;
		FString ParamName;
	};

	using FControlBusDeactivateMessage = FControlBusMessageBase;

	struct FControlBusUpdateMessage : public FControlBusMessageBase
	{
		FControlBusUpdateMessage() = default;
		FControlBusUpdateMessage(const UE::Trace::IAnalyzer::FOnEventContext& InContext)
			: FControlBusMessageBase(InContext)
		{
			const UE::Trace::IAnalyzer::FEventData& EventData = InContext.EventData;
			Value = EventData.GetValue<float>("Value");
			EventData.GetString("Name", BusName);
			EventData.GetString("ParamName", ParamName);
		}

		FString BusName;
		FString ParamName;
		float Value = 1.0f;
	};

	class FControlBusDashboardEntry : public UE::Audio::Insights::FSoundAssetDashboardEntry
	{
	public:
		FControlBusDashboardEntry() = default;
		virtual ~FControlBusDashboardEntry() = default;

		FText GetParamNameAsFText() const { return FText::FromString(ParamName); }

		FBusId ControlBusId = INDEX_NONE;
		float Value = 1.0f;
		FString ParamName;
	};

	class FControlBusMessages
	{
		UE::Audio::Insights::TAnalyzerMessageQueue<FControlBusUpdateMessage> UpdateMessages{ 2.0 };
		UE::Audio::Insights::TAnalyzerMessageQueue<FControlBusActivateMessage> ActivateMessages{ 0.1 };
		UE::Audio::Insights::TAnalyzerMessageQueue<FControlBusDeactivateMessage> DeactivateMessages{ 0.1 };

		friend class FControlBusTraceProvider;
	};

} // namespace AudioModulationEditor
