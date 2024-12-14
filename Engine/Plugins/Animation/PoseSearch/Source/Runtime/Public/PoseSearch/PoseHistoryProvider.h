// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Animation/AnimNodeMessages.h"

struct FAnimNode_PoseSearchHistoryCollector_Base;

namespace UE::PoseSearch
{

struct IPoseHistory;

class POSESEARCH_API FPoseHistoryProvider : public UE::Anim::IGraphMessage
{
	DECLARE_ANIMGRAPH_MESSAGE(FPoseHistoryProvider);

public:
	UE_DEPRECATED(5.4, "Use FPoseHistoryProvider(const FAnimNode_PoseSearchHistoryCollector_Base* HistoryCollector) instead")
	FPoseHistoryProvider(const IPoseHistory& InPoseHistory) : PoseHistory(&InPoseHistory), HistoryCollector(nullptr) { }
	FPoseHistoryProvider(const FAnimNode_PoseSearchHistoryCollector_Base* InHistoryCollector) : PoseHistory(nullptr), HistoryCollector(InHistoryCollector) { }
	const IPoseHistory& GetPoseHistory() const;
	const FAnimNode_PoseSearchHistoryCollector_Base* GetHistoryCollector() const { return HistoryCollector; }

private:
	// delete after deleting FPoseHistoryProvider(const IPoseHistory& InPoseHistory)
	const IPoseHistory* PoseHistory = nullptr;
	const FAnimNode_PoseSearchHistoryCollector_Base* HistoryCollector = nullptr;
};

} // namespace UE::PoseSearch