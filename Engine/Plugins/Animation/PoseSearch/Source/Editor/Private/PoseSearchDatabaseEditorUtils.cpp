// Copyright Epic Games, Inc. All Rights Reserved.

#include "PoseSearchDatabaseEditorUtils.h"

#include "PoseSearch/MultiAnimAsset.h"
#include "PoseSearch/PoseSearchDatabase.h"
#include "PoseSearch/PoseSearchSchema.h"

namespace UE::PoseSearch
{
	bool FPoseSearchEditorUtils::IsAssetCompatibleWithDatabase(const UPoseSearchDatabase* InDatabase, const FAssetData& InAssetData)
	{
		if (InDatabase)
		{
			if (InDatabase->Schema)
			{
				const TArray<FPoseSearchRoledSkeleton> RoledSkeletons = InDatabase->Schema->GetRoledSkeletons();
				if (RoledSkeletons.Num() == 1 )
				{
					if (InAssetData.GetClass()->IsChildOf(UAnimationAsset::StaticClass()) &&
						RoledSkeletons[0].Skeleton &&
						RoledSkeletons[0].Skeleton->IsCompatibleForEditor(InAssetData))
					{
						// We found a compatible skeleton in the schema.
						return true;
					}
				}
				else if (RoledSkeletons.Num() > 1)
				{
					if (const UMultiAnimAsset* MultiAnimAsset = Cast<UMultiAnimAsset>(InAssetData.GetAsset()))
					{
						if (MultiAnimAsset->GetNumRoles() != RoledSkeletons.Num())
						{
							return false;
						}
						
						for (const FPoseSearchRoledSkeleton& RoledSkeleton : RoledSkeletons)
						{
							if (const UAnimationAsset* RoledAnimationAsset = MultiAnimAsset->GetAnimationAsset(RoledSkeleton.Role))
							{
								if (!RoledAnimationAsset->GetSkeleton()->IsCompatibleForEditor(RoledSkeleton.Skeleton))
								{
									return false;
								}
							}
							else
							{
								// couldn't find a necessary asset for the RoledSkeleton
								return false;
							}
						}

						// we passed all the compatibility requirements!
						return true;
					}
				}
			}
		}
		
		return false;
	}
}
