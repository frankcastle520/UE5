// Copyright Epic Games, Inc. All Rights Reserved.

#include "RigLogicInstanceDataPool.h"
#include "RigLogicAnimNext.h"

namespace UE::AnimNext
{
	TSharedPtr<FRigLogicAnimNextInstanceData> FRigLogicInstanceDataPool::RequestData(const UE::AnimNext::FReferencePose* InReferencePose)
	{
		// Only lock while searching
		{
			FScopeLock Lock(&PoolAccessCriticalSection);

			TArray<TSharedPtr<FRigLogicAnimNextInstanceData>>* Found = Datas.Find(InReferencePose->SkeletalMesh);
			if (Found)
			{
				if (!Found->IsEmpty())
				{
					// We have pre-allocated data already, use one that is already there.
					return Found->Pop();
				}
			}
		}
		// We don't need to keep things locked here as we're cerating a new object
		// Initializing a new data is not a lightweight operation, so be sure to close the lock first.

		// We don't have any pool datas in the array yet. Create a new one.
		TSharedPtr<FRigLogicAnimNextInstanceData> NewObject = TSharedPtr<FRigLogicAnimNextInstanceData>(new FRigLogicAnimNextInstanceData());
		NewObject->Init(const_cast<USkeletalMesh*>(InReferencePose->SkeletalMesh.Get()));
		return NewObject;
	}

	void FRigLogicInstanceDataPool::FreeData(TWeakObjectPtr<const USkeletalMesh> SkeletalMesh, TSharedPtr<FRigLogicAnimNextInstanceData> InData)
	{
		FScopeLock Lock(&PoolAccessCriticalSection);

		// Do we have an element for the given skel mesh?
		TArray<TSharedPtr<FRigLogicAnimNextInstanceData>>* Found = Datas.Find(SkeletalMesh);
		if (!Found)
		{
			// We don't. Create a new one and add the instance data to it.
			TArray<TSharedPtr<FRigLogicAnimNextInstanceData>> NewArray;
			NewArray.Add(InData);
			Datas.Add(SkeletalMesh, NewArray);
		}
		else
		{
			// Found one, just add the instance data back in.
			Found->Add(InData);
		}
	}

	void FRigLogicInstanceDataPool::GarbageCollect()
	{
		FScopeLock Lock(&PoolAccessCriticalSection);

		for (auto Iterator = Datas.CreateIterator(); Iterator; ++Iterator)
		{
			// Remove cached instance datas for skeletal meshes that are not loaded anymore.
			if (!Iterator->Key.IsValid())
			{
				Iterator.RemoveCurrent();
			}
		}
	}

	void FRigLogicInstanceDataPool::Log()
	{
		FScopeLock Lock(&PoolAccessCriticalSection);

		UE_LOG(LogRigLogicAnimNext, Display, TEXT("Pool data:"));
		uint32 Counter = 0;
		for (auto Iterator = Datas.CreateIterator(); Iterator; ++Iterator)
		{
			if (Iterator->Key.IsValid())
			{
				const USkeletalMesh* SkeletalMesh = Iterator->Key.Get();
				UE_LOG(LogRigLogicAnimNext, Display, TEXT("   - Skeletal Mesh %s:"), *SkeletalMesh->GetPathName());

				const TArray<TSharedPtr<FRigLogicAnimNextInstanceData>>& InstanceDataArray = Iterator->Value;
				for (int32 i = 0; i < InstanceDataArray.Num(); ++i)
				{
					UE_LOG(LogRigLogicAnimNext, Display, TEXT("      + InstanceData %i (0x%d):"), i, InstanceDataArray[i].Get());
				}
			}
			else
			{
				UE_LOG(LogRigLogicAnimNext, Warning, TEXT("Entry %i linked to an invalid skeletal mesh."), Counter);
			}
			Counter++;
		}
	}
} // namespace UE::AnimNext