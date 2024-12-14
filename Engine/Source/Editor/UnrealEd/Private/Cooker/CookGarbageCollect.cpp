// Copyright Epic Games, Inc. All Rights Reserved.

#include "Cooker/CookGarbageCollect.h"

#include "CookOnTheSide/CookOnTheFlyServer.h"
#include "Misc/EnumClassFlags.h"
#include "UObject/GarbageCollectionHistory.h"

namespace UE::Cook
{

FCookGCDiagnosticContext::~FCookGCDiagnosticContext()
{
	SetGCWithHistoryRequested(false);
}

bool FCookGCDiagnosticContext::NeedsDiagnosticSecondGC() const
{
	return bRequestGCWithHistory || bRequestFullGC;
}

bool FCookGCDiagnosticContext::CurrentGCHasHistory() const
{
	return bCurrentGCHasHistory;
}

bool FCookGCDiagnosticContext::TryRequestGCWithHistory()
{
#if ENABLE_GC_HISTORY
	if (!bRequestsAvailable || !bGCInProgress || bCurrentGCHasHistory)
	{
		return false;
	}
	SetGCWithHistoryRequested(true);
	return true;
#else
	return false;
#endif
}

bool FCookGCDiagnosticContext::TryRequestFullGC()
{
	if (!bRequestsAvailable || !bGCInProgress || bCurrentGCIsFull)
	{
		return false;
	}
	bRequestFullGC = true;
	return true;
}

void FCookGCDiagnosticContext::OnCookerStartCollectGarbage(UCookOnTheFlyServer& COTFS, uint32& ResultFlagsFromTick)
{
	bRequestsAvailable = true;

	bGCInProgress = true;
#if ENABLE_GC_HISTORY
	bCurrentGCHasHistory = FGCHistory::Get().GetHistorySize() > 0;
#else
	bCurrentGCHasHistory = false;
#endif
	if (bRequestFullGC)
	{
		COTFS.bGarbageCollectTypeSoft = false;
		ResultFlagsFromTick = ResultFlagsFromTick & ~UCookOnTheFlyServer::COSR_RequiresGC_Soft_OOM;
	}
	bCurrentGCIsFull = !COTFS.bGarbageCollectTypeSoft;
}

void FCookGCDiagnosticContext::OnCookerEndCollectGarbage(UCookOnTheFlyServer& COTFS, uint32& ResultFlagsFromTick)
{
	bGCInProgress = false;
	bCurrentGCHasHistory = false;
	bCurrentGCIsFull = false;
}

void FCookGCDiagnosticContext::OnEvaluateResultsComplete()
{
	SetGCWithHistoryRequested(false);
	bRequestFullGC = false;
}

void FCookGCDiagnosticContext::SetGCWithHistoryRequested(bool bValue)
{
#if ENABLE_GC_HISTORY
	if (bValue == bRequestGCWithHistory)
	{
		return;
	}

	if (bValue)
	{
		SavedGCHistorySize = FGCHistory::Get().GetHistorySize();
		if (SavedGCHistorySize < 1)
		{
			FGCHistory::Get().SetHistorySize(1);
		}
	}
	else
	{
		if (SavedGCHistorySize != FGCHistory::Get().GetHistorySize())
		{
			FGCHistory::Get().SetHistorySize(SavedGCHistorySize);
		}
		SavedGCHistorySize = 0;
	}
	bRequestGCWithHistory = bValue;
#endif
}

} // namespace UE::Cook