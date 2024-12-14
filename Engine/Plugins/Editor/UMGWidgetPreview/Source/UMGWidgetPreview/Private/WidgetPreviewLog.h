// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Logging/LogMacros.h"

namespace UE::UMGWidgetPreview::Private
{
	static const FLazyName MessageLogName = "WidgetPreviewLog";
}

DECLARE_LOG_CATEGORY_EXTERN(LogWidgetPreview, VeryVerbose, All);
