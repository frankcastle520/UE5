// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Logging/LogMacros.h"

#include "epic_rtc/common/logging.h"

DECLARE_LOG_CATEGORY_EXTERN(LogPixelStreaming2EpicRtc, NoLogging, All);

namespace UE::PixelStreaming2
{
	class PIXELSTREAMING2_API FEpicRtcLogsRedirector : public EpicRtcLoggerInterface
	{
	public:
		virtual void Log(const EpicRtcLogMessage& Message) override;
	};
} // namespace UE::PixelStreaming2