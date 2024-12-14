// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"

class UCookOnTheFlyServer;

namespace UE::Cook
{

/**
 * Holds information about the cookers garbage collection status, and communicates requests from low level structures
 * back up to the CookCommandlet that is capable of acting on those requests with additional garbage collection
 * commands.
 */
class FCookGCDiagnosticContext
{
public:
	~FCookGCDiagnosticContext();

	bool NeedsDiagnosticSecondGC() const;
	bool CurrentGCHasHistory() const;

	/**
	 * Add a request to reexecute the current GC after all of the PostGarbageCollect calls run and
	 * control returns back to the caller of CollectGarbage, and with history turned on.
	 * Returns false if not currently in post-GC, or the garbage collect that just ran already had history.
	 */
	bool TryRequestGCWithHistory();
	/**
	 * Add a request to reexecute the current GC after all of the PostGarbageCollect calls run and
	 * control returns back to the caller of CollectGarbage, and with soft GC turned off.
	 * Returns false if not currently in post-GC, or the garbage collect that just ran already was a full GC.
	 */
	bool TryRequestFullGC();

	void OnCookerStartCollectGarbage(UCookOnTheFlyServer& COTFS, uint32& ResultFlagsFromTick);
	void OnCookerEndCollectGarbage(UCookOnTheFlyServer& COTFS, uint32& ResultFlagsFromTick);
	void OnEvaluateResultsComplete();

private:
	void SetGCWithHistoryRequested(bool bValue);
	
	int32 SavedGCHistorySize = 0;
	bool bRequestsAvailable = false;
	bool bGCInProgress = false;
	bool bRequestGCWithHistory = false;
	bool bRequestFullGC = false;
	bool bCurrentGCHasHistory = false;
	bool bCurrentGCIsFull = false;
};

} // namespace UE::Cook
