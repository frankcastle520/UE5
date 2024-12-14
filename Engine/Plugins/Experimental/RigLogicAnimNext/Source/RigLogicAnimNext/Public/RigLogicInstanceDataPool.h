// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "RigLogicInstanceData.h"

namespace UE::AnimNext
{
	class FRigLogicInstanceDataPool
	{
	public:
		TMap<TWeakObjectPtr<const USkeletalMesh>, TArray<TSharedPtr<FRigLogicAnimNextInstanceData>>> Datas;

		mutable FCriticalSection PoolAccessCriticalSection;

		TSharedPtr<FRigLogicAnimNextInstanceData> RequestData(const UE::AnimNext::FReferencePose* InReferencePose);
		void FreeData(TWeakObjectPtr<const USkeletalMesh> SkeletalMesh, TSharedPtr<FRigLogicAnimNextInstanceData> InData);

		void GarbageCollect();
		void Log();
	};
} // namespace UE::AnimNext