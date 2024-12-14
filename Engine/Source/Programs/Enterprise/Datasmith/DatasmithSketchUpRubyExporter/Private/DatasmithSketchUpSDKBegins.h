// Copyright Epic Games, Inc. All Rights Reserved.

#include "HAL/Platform.h"

#if PLATFORM_WINDOWS

// Temporary solution for static analysis of DatasmithSketchUpSDKCeases.h
#define SKETCHUP_WINDOWS_PLATFORM_TYPES_GUARD

// Begin Datasmith platform include guard.
#include "Windows/AllowWindowsPlatformTypes.h"

// Turn off some compiler warnings.
#pragma warning(push)
#pragma warning(disable: 4100) // C4100: unreferenced formal parameter

#elif defined(PLATFORM_MAC) && PLATFORM_MAC

#define volatile

#endif
