// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	GarbageCollectionGlobals.h: Garbage Collection Global State Vars
=============================================================================*/

#pragma once

#include "UObject/ObjectMacros.h"

namespace UE::GC
{
	/** Current EInternalObjectFlags value representing a reachable object */
	UE_DEPRECATED(5.5, "GUnreachableObjectFlag should no longer be used.")
	extern COREUOBJECT_API EInternalObjectFlags GReachableObjectFlag;

	/** Current EInternalObjectFlags value representing an unreachable object */
	UE_DEPRECATED(5.5, "GUnreachableObjectFlag should no longer be used. Use Object->IsUnreachable() instead.")
	extern COREUOBJECT_API EInternalObjectFlags GUnreachableObjectFlag;

	/** Current EInternalObjectFlags value representing a maybe unreachable object */
	UE_DEPRECATED(5.5, "GMaybeUnreachableObjectFlag should no longer be used.")
	extern COREUOBJECT_API EInternalObjectFlags GMaybeUnreachableObjectFlag;

	/** true if incremental reachability analysis is in progress (global for faster access in low level structs and functions otherwise use IsIncrementalReachabilityAnalisysPending()) */
	extern COREUOBJECT_API bool GIsIncrementalReachabilityPending;
}
