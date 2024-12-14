// Copyright Epic Games, Inc. All Rights Reserved.

#include "EpicRtcLogging.h"

DEFINE_LOG_CATEGORY(LogPixelStreaming2EpicRtc);

namespace UE::PixelStreaming2
{

	void FEpicRtcLogsRedirector::Log(const EpicRtcLogMessage& Message)
	{
#if !NO_LOGGING
		static const ELogVerbosity::Type EpicRtcToUnrealLogCategoryMap[] = {
			ELogVerbosity::VeryVerbose,
			ELogVerbosity::Verbose,
			ELogVerbosity::Log,
			ELogVerbosity::Warning,
			ELogVerbosity::Error,
			ELogVerbosity::Fatal,
			ELogVerbosity::NoLogging
		};

		if (LogPixelStreaming2EpicRtc.IsSuppressed(EpicRtcToUnrealLogCategoryMap[static_cast<uint8_t>(Message._level)]))
		{
			return;
		}
		FString Msg{ (int32)Message._message._length, Message._message._ptr };

		switch (Message._level)
		{
			case EpicRtcLogLevel::Trace:
			{
				UE_LOG(LogPixelStreaming2EpicRtc, VeryVerbose, TEXT("%s"), *Msg);
				break;
			}
			case EpicRtcLogLevel::Debug:
			{
				UE_LOG(LogPixelStreaming2EpicRtc, Verbose, TEXT("%s"), *Msg);
				break;
			}
			case EpicRtcLogLevel::Info:
			{
				UE_LOG(LogPixelStreaming2EpicRtc, Log, TEXT("%s"), *Msg);
				break;
			}
			case EpicRtcLogLevel::Warning:
			{
				UE_LOG(LogPixelStreaming2EpicRtc, Warning, TEXT("%s"), *Msg);
				break;
			}
			case EpicRtcLogLevel::Error:
			{
				UE_LOG(LogPixelStreaming2EpicRtc, Error, TEXT("%s"), *Msg);
				break;
			}
			case EpicRtcLogLevel::Critical:
			{
				UE_LOG(LogPixelStreaming2EpicRtc, Fatal, TEXT("%s"), *Msg);
				break;
			}
		}
#endif
	}

} // namespace UE::PixelStreaming2