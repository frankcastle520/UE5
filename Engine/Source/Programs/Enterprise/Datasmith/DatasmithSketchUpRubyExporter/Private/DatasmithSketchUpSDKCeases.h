// Copyright Epic Games, Inc. All Rights Reserved.

#if PLATFORM_WINDOWS

// Temporary solution for static analysis of this include
#ifdef SKETCHUP_WINDOWS_PLATFORM_TYPES_GUARD

// Restore back the compiler warnings.
#pragma warning(pop)

// Cease Datasmith platform include guard.
#include "Windows/HideWindowsPlatformTypes.h"

#endif

#elif defined(PLATFORM_MAC) && PLATFORM_MAC

#undef volatile

#endif
