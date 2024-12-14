// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"

#include "Containers/StringFwd.h"
#include "GenericPlatform/GenericPlatformMisc.h"
#include "Internationalization/Text.h"
#include "Misc/DateTime.h"
#include "Styling/SlateColor.h"
#include "Templates/SharedPointer.h"

class SEditableTextBox;

namespace UE::Insights
{

struct FTraceViewModel
{
	static constexpr uint32 InvalidTraceId = 0;

	uint32 TraceId = InvalidTraceId;

	uint64 ChangeSerial = 0;

	FText Name;
	FText Uri;
	FSlateColor DirectoryColor;

	FDateTime Timestamp = 0;
	uint64 Size = 0;

	FText Platform;
	FText AppName;
	FText CommandLine;
	FText Branch;
	FText BuildVersion;
	uint32 Changelist = 0;
	EBuildConfiguration ConfigurationType = EBuildConfiguration::Unknown;
	EBuildTargetType TargetType = EBuildTargetType::Unknown;

	bool bIsMetadataUpdated = false;
	bool bIsRenaming = false;
	bool bIsLive = false;
	uint32 IpAddress = 0;

	TWeakPtr<SEditableTextBox> RenameTextBox;

	FTraceViewModel() = default;

	static FDateTime ConvertTimestamp(uint64 InTimestamp)
	{
		return FDateTime(static_cast<int64>(InTimestamp));
	}

	static FText AnsiStringViewToText(const FAnsiStringView& AnsiStringView);
};

} // namespace UE::Insights
