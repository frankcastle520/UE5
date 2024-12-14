// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreGlobals.h"
#include "CoreMinimal.h"
#include "Framework/Application/SlateApplication.h"

namespace UE::PixelStreaming2
{
	PIXELSTREAMING2CORE_API inline bool IsStreamingSupported()
	{
		// Pixel Streaming does not make sense without an RHI so we don't run in commandlets without one.
		if (IsRunningCommandlet() && !IsAllowCommandletRendering())
		{
			return false;
		}

		return true;
	}
}