// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "HAL/Platform.h"

namespace UE::ConcertInsightsVisualizer
{
	enum class EMessageType : uint8
	{
		None,
		Init,
		ObjectTraceBegin,
		ObjectTraceEnd,
		ObjectTransmissionStart,
		ObjectTransmissionReceive,
		ObjectSink
	};
}

