// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include <string>
#include "Containers/StringConv.h"
#include "Containers/UnrealString.h"
#include "Containers/Utf8String.h"
#include "Dom/JsonObject.h"
#include "Policies/PrettyJsonPrintPolicy.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonSerializer.h"
#include "epic_rtc/common/common.h"
#include "epic_rtc/containers/epic_rtc_string_view.h"
#include "epic_rtc/core/video/video_common.h"

namespace UE::PixelStreaming2
{
	inline FString ToString(EpicRtcErrorCode Error)
	{
		switch (Error)
		{
			case EpicRtcErrorCode::Ok:
			{
				return TEXT("Ok");
			}
			case EpicRtcErrorCode::GeneralError:
			{
				return TEXT("GeneralError");
			}
			case EpicRtcErrorCode::BadState:
			{
				return TEXT("BadState");
			}
			case EpicRtcErrorCode::Timeout:
			{
				return TEXT("Timeout");
			}
			case EpicRtcErrorCode::Unsupported:
			{
				return TEXT("Unsupported");
			}
			case EpicRtcErrorCode::PlatformError:
			{
				return TEXT("PlatformError");
			}
			case EpicRtcErrorCode::FoundExistingPlatform:
			{
				return TEXT("FoundExistingPlatform");
			}
			case EpicRtcErrorCode::ConferenceAlreadyExists:
			{
				return TEXT("ConferenceAlreadyExists");
			}
			case EpicRtcErrorCode::ConferenceDoesNotExists:
			{
				return TEXT("ConferenceDoesNotExists");
			}
			case EpicRtcErrorCode::ImATeapot:
			{
				return TEXT("ImATeapot");
			}
			case EpicRtcErrorCode::ConferenceError:
			{
				return TEXT("ConferenceError");
			}
			case EpicRtcErrorCode::SessionAlreadyExists:
			{
				return TEXT("SessionAlreadyExists");
			}
			case EpicRtcErrorCode::SessionDoesNotExist:
			{
				return TEXT("SessionDoesNotExist");
			}
			case EpicRtcErrorCode::SessionError:
			{
				return TEXT("SessionError");
			}
			case EpicRtcErrorCode::SessionCannotConnect:
			{
				return TEXT("SessionCannotConnect");
			}
			case EpicRtcErrorCode::SessionDisconnected:
			{
				return TEXT("SessionDisconnected");
			}
			case EpicRtcErrorCode::SessionCannotCreateRoom:
			{
				return TEXT("SessionCannotCreateRoom");
			}
			default:
			{
				return TEXT("Unknown");
			}
		}
	}

	inline FString ToString(EpicRtcVideoCodec Codec)
	{
		switch (Codec)
		{
			case EpicRtcVideoCodec::AV1:
			{
				return TEXT("AV1");
			}
			case EpicRtcVideoCodec::H264:
			{
				return TEXT("H264");
			}
			case EpicRtcVideoCodec::VP8:
			{
				return TEXT("VP8");
			}
			case EpicRtcVideoCodec::VP9:
			{
				return TEXT("VP9");
			}
			default:
			{
				return TEXT("Unknown");
			}
		}
	}

	inline FString ToString(EpicRtcRoomState State)
	{
		switch (State)
		{
			case EpicRtcRoomState::New:
				return TEXT("New");
			case EpicRtcRoomState::Pending:
				return TEXT("Pending");
			case EpicRtcRoomState::Joined:
				return TEXT("Joined");
			case EpicRtcRoomState::Left:
				return TEXT("Left");
			case EpicRtcRoomState::Failed:
				return TEXT("Failed");
			case EpicRtcRoomState::Exiting:
				return TEXT("Exiting");
		}
		return TEXT("Unknown");
	}

	inline FString ToString(const TSharedPtr<FJsonObject>& JsonObj, bool bPretty = true)
	{
		FString Res;
		if (bPretty)
		{
			auto JsonWriter = TJsonWriterFactory<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>::Create(&Res);
			FJsonSerializer::Serialize(JsonObj.ToSharedRef(), JsonWriter);
		}
		else
		{
			auto JsonWriter = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Res);
			FJsonSerializer::Serialize(JsonObj.ToSharedRef(), JsonWriter);
		}
		return Res;
	}

	inline FString ToString(const EpicRtcStringView& Str)
	{
		FUtf8String Utf8String = FUtf8String::ConstructFromPtrSize(Str._ptr, Str._length);
		return FString(Utf8String);
	}

	inline EpicRtcStringView ToEpicRtcStringView(const FUtf8String& Str)
	{
		return EpicRtcStringView{ ._ptr = (const char*)*Str, ._length = static_cast<uint64>(Str.Len()) };
	}

	/**
	 * Reads a string represented by 2 bytes length (in bytes) followed by UTF16 characters.
	 * String and length are encoded in little endian format.
	 */
	inline FString ReadString(const uint8*& Data, uint32_t& Size)
	{
		uint16_t BytesLength = Data[1] << 8 | Data[0];
		check(Size >= (uint32_t)(BytesLength + 2));
		Data += 2;
		Size -= 2;

		FString Message(BytesLength / sizeof(TCHAR), reinterpret_cast<const TCHAR*>(Data));
		Data += BytesLength;
		Size -= BytesLength;

		return Message;
	}
} // namespace UE::PixelStreaming2
