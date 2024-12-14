// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "BaseObjectMessage.h"
#include "EMessageType.h"

#include "HAL/Platform.h"
#include "Trace/Analyzer.h"

#include <type_traits>

namespace UE::ConcertInsightsVisualizer
{
	struct FObjectTransmissionReceiveMessage : FBaseObjectMessage
	{
		/** The time at which the message was generated. Seconds since start of trace. */
		double Time;
		
		FObjectTransmissionReceiveMessage() = default;
		FObjectTransmissionReceiveMessage(
			const Trace::IAnalyzer::FEventData& EventData,
			const Trace::IAnalyzer::FEventTime& EventTime,
			TraceServices::IAnalysisSession& Session UE_LIFETIMEBOUND
			);
		
		static constexpr EMessageType Type() { return EMessageType::ObjectTransmissionReceive; }
	};
	static_assert(std::is_trivial_v<FObjectTransmissionReceiveMessage>, "FObjectTransmissionReceiveMessage must be unique so it can be put into a union.");
}
