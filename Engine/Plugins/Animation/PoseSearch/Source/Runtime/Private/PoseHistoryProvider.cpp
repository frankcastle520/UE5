// Copyright Epic Games, Inc. All Rights Reserved.

#include "PoseSearch/PoseHistoryProvider.h"
#include "PoseSearch/AnimNode_PoseSearchHistoryCollector.h"

namespace UE::PoseSearch
{
	
const IPoseHistory& FPoseHistoryProvider::GetPoseHistory() const
{
	if (HistoryCollector)
	{
		return HistoryCollector->GetPoseHistory();
	}
	
	check(PoseHistory);
	return *PoseHistory;
}

} // namespace UE::PoseSearch