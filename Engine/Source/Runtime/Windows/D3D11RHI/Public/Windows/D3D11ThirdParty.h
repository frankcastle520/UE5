// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "HAL/Platform.h"
#include "Microsoft/AllowMicrosoftPlatformTypes.h"

#if PLATFORM_64BITS
	#pragma pack(push,16)
#else
	#pragma pack(push,8)
#endif

THIRD_PARTY_INCLUDES_START
	#include <d3d11_2.h>
	#include <dxgi1_6.h>
	#include <dxgidebug.h>
THIRD_PARTY_INCLUDES_END

#undef DrawText

#pragma pack(pop)

#include "Microsoft/HideMicrosoftPlatformTypes.h"
