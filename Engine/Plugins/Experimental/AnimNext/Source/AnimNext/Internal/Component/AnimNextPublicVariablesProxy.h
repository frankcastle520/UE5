// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "StructUtils/PropertyBag.h"
#include "AnimNextPublicVariablesProxy.generated.h"

// Proxy struct for public variables held on the component
// TODO: Instead of a dirty flag bitfield (and copying parameters), with some minor refactoring of RigVM external variables
// we could switch this to use a double-buffered external variables array too, so each write to a variable from the game thread
// would just write to the current 'GT-side' buffer, then update the external variable ptr to point to the 'latest' version written
// Then when it comes to swap buffers before WT execution, we just swap the ptrs for the external variables and dont have to copy. 
USTRUCT()
struct FAnimNextPublicVariablesProxy
{
	GENERATED_BODY()

	void Reset()
	{
		Data.Reset();
		DirtyFlags.Reset();
		bIsDirty = false;
	}

	void Empty()
	{
		Data.Reset();
		DirtyFlags.Empty();
		bIsDirty = false;
	}

	// Proxy public variables
	UPROPERTY()
	FInstancedPropertyBag Data;

	// Dirty flags for each public variable
	TBitArray<> DirtyFlags;

	// Global dirty flag
	bool bIsDirty = false;
};