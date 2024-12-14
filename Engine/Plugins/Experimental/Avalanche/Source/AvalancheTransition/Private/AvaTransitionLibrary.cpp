// Copyright Epic Games, Inc. All Rights Reserved.

#include "AvaTransitionLibrary.h"
#include "AvaTransitionContext.h"
#include "AvaTransitionLayer.h"
#include "AvaTransitionLayerUtils.h"
#include "AvaTransitionSubsystem.h"
#include "Behavior/AvaTransitionBehaviorInstance.h"
#include "Behavior/AvaTransitionBehaviorInstanceCache.h"
#include "Engine/Level.h"
#include "Engine/World.h"
#include "IAvaTransitionNodeInterface.h"
#include "UObject/Package.h"

bool UAvaTransitionLibrary::IsTransitionActiveInLayer(UObject* InTransitionNode
	, EAvaTransitionComparisonResult InSceneComparisonType
	, EAvaTransitionLayerCompareType InLayerComparisonType
	, const FAvaTagHandleContainer& InSpecificLayers)
{
	const IAvaTransitionNodeInterface* NodeInterface = Cast<IAvaTransitionNodeInterface>(InTransitionNode);
	if (!NodeInterface)
	{
		return false;
	}

	const FAvaTransitionContext* TransitionContext = NodeInterface->GetBehaviorInstanceCache().GetTransitionContext();
	if (!TransitionContext)
	{
		return false;
	}

	const FAvaTransitionScene* TransitionScene = TransitionContext->GetTransitionScene();
	if (!TransitionScene)
	{
		return false;
	}

	ULevel* TransitionLevel = TransitionScene->GetLevel();
	if (!TransitionLevel || !TransitionLevel->OwningWorld)
	{
		return false;
	}

	UAvaTransitionSubsystem* TransitionSubsystem = TransitionLevel->OwningWorld->GetSubsystem<UAvaTransitionSubsystem>();
	if (!TransitionSubsystem)
	{
		return false;
	}

	const FAvaTransitionLayerComparator Comparator = FAvaTransitionLayerUtils::BuildComparator(*TransitionContext
		, InLayerComparisonType
		, InSpecificLayers);

	const TArray<const FAvaTransitionBehaviorInstance*> BehaviorInstances = FAvaTransitionLayerUtils::QueryBehaviorInstances(*TransitionSubsystem, Comparator);
	if (BehaviorInstances.IsEmpty())
	{
		return false;
	}

	bool bHasMatchingInstance = BehaviorInstances.ContainsByPredicate(
		[TransitionScene, InSceneComparisonType](const FAvaTransitionBehaviorInstance* InInstance)
		{
			const FAvaTransitionScene* OtherTransitionScene = InInstance->GetTransitionContext().GetTransitionScene();

			const EAvaTransitionComparisonResult ComparisonResult = OtherTransitionScene
				? TransitionScene->Compare(*OtherTransitionScene)
				: EAvaTransitionComparisonResult::None;

			return ComparisonResult == InSceneComparisonType;
		});

	return bHasMatchingInstance;
}

EAvaTransitionType UAvaTransitionLibrary::GetTransitionType(UObject* InTransitionNode)
{
	const IAvaTransitionNodeInterface* NodeInterface = Cast<IAvaTransitionNodeInterface>(InTransitionNode);
	if (!NodeInterface)
	{
		return EAvaTransitionType::None;
	}

	const FAvaTransitionContext* TransitionContext = NodeInterface->GetBehaviorInstanceCache().GetTransitionContext();
	if (!TransitionContext)
	{
		return EAvaTransitionType::None;
	}

	return TransitionContext->GetTransitionType();
}

bool UAvaTransitionLibrary::AreScenesTransitioning(UObject* InTransitionNode, const FAvaTagHandleContainer& InLayers, const TArray<TSoftObjectPtr<UWorld>>& InScenesToIgnore)
{
	const IAvaTransitionNodeInterface* NodeInterface = Cast<IAvaTransitionNodeInterface>(InTransitionNode);
	if (!NodeInterface)
	{
		return false;
	}

	const FAvaTransitionContext* TransitionContext = NodeInterface->GetBehaviorInstanceCache().GetTransitionContext();
	if (!TransitionContext)
	{
		return false;
	}

	const FAvaTransitionScene* TransitionScene = TransitionContext->GetTransitionScene();
	if (!TransitionScene)
	{
		return false;
	}

	ULevel* TransitionLevel = TransitionScene->GetLevel();
	if (!TransitionLevel || !TransitionLevel->OwningWorld)
	{
		return false;
	}

	UAvaTransitionSubsystem* TransitionSubsystem = TransitionLevel->OwningWorld->GetSubsystem<UAvaTransitionSubsystem>();
	if (!TransitionSubsystem)
	{
		return false;
	}

	const FAvaTransitionLayerComparator Comparator = FAvaTransitionLayerUtils::BuildComparator(*TransitionContext
		, EAvaTransitionLayerCompareType::Different
		, FAvaTagHandleContainer());

	TArray<const FAvaTransitionBehaviorInstance*> BehaviorInstances = FAvaTransitionLayerUtils::QueryBehaviorInstances(*TransitionSubsystem, Comparator);
	if (BehaviorInstances.IsEmpty())
	{
		return false;
	}

	TSet<FString> ScenesToIgnore;
	ScenesToIgnore.Reserve(InScenesToIgnore.Num());
	for (const TSoftObjectPtr<UWorld>& SceneToIgnore : InScenesToIgnore)
	{
		ScenesToIgnore.Add(SceneToIgnore.GetLongPackageName());
	}

	for (const FAvaTransitionBehaviorInstance* BehaviorInstance : BehaviorInstances)
	{
		if (!InLayers.ContainsTag(BehaviorInstance->GetTransitionLayer()))
		{
			continue;
		}

		const FAvaTransitionScene* const OtherTransitionScene = BehaviorInstance->GetTransitionContext().GetTransitionScene();

		// skip scenes marked as needing discard
		if (!OtherTransitionScene || OtherTransitionScene->HasAnyFlags(EAvaTransitionSceneFlags::NeedsDiscard))
		{
			continue;
		}

		const ULevel* const OtherLevel = OtherTransitionScene->GetLevel();
		if (!OtherLevel)
		{
			continue;
		}

		const UPackage* const OtherPackage = OtherLevel->GetPackage();
		if (!OtherPackage)
		{
			continue;
		}

		// Remove the /Temp from the Package Name
		FString PackageName = OtherPackage->GetName();
		if (PackageName.StartsWith(TEXT("/Temp")))
		{
			PackageName.RightChopInline(-1 + sizeof(TEXT("/Temp")) / sizeof(TCHAR));
		}

		// Remove the _LevelInstance_[Num] from the Package Name
		const int32 LevelInstancePosition = PackageName.Find(TEXT("_LevelInstance_"), ESearchCase::IgnoreCase, ESearchDir::FromEnd);
		if (LevelInstancePosition != INDEX_NONE)
		{
			PackageName.LeftChopInline(PackageName.Len() - LevelInstancePosition);
		}

		// If this scene isn't part of those to ignore, then it's a valid transitioning scene
		if (!ScenesToIgnore.Contains(PackageName))
		{
			return true;
		}
	}

	return false;
}

const UAvaTransitionTree* UAvaTransitionLibrary::GetTransitionTree(UObject* InTransitionNode)
{
	if (const IAvaTransitionNodeInterface* NodeInterface = Cast<IAvaTransitionNodeInterface>(InTransitionNode))
	{
		return NodeInterface->GetBehaviorInstanceCache().GetTransitionTree();
	}
	return nullptr;
}
