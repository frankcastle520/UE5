// Copyright Epic Games, Inc. All Rights Reserved.

#include "BaseObjectMessage.h"

#include "TraceServices/Model/AnalysisSession.h"

namespace UE::ConcertInsightsVisualizer
{
	namespace ObjectTraceMessage
	{
		static const TCHAR* GetObjectPath(
			const Trace::IAnalyzer::FEventData& EventData,
			TraceServices::IAnalysisSession& Session UE_LIFETIMEBOUND
			)
		{
			FString ObjectPath;
			EventData.GetString("ObjectPath", ObjectPath);
			return Session.StoreString(*ObjectPath);
		}
	}
	
	FBaseObjectMessage::FBaseObjectMessage(
		const Trace::IAnalyzer::FEventData& EventData,
		TraceServices::IAnalysisSession& Session UE_LIFETIMEBOUND
		)
		: Protocol(EventData.GetValue<FProtocolId>("Protocol"))
		, SequenceId(EventData.GetValue<FSequenceId>("SequenceId"))
		, ObjectPath(ObjectTraceMessage::GetObjectPath(EventData, Session))
	{}
}
