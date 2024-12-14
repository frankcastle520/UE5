// Copyright Epic Games, Inc. All Rights Reserved.

#include "PoseSearch/PoseSearchAnimNotifies.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(PoseSearchAnimNotifies)

uint32 UAnimNotifyState_PoseSearchBranchIn::GetBranchInId() const
{
	const uint32 BranchInId = GetTypeHash(GetFullName());
	check(BranchInId != 0);
	return BranchInId;
}
