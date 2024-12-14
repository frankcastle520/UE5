// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if WITH_EDITOR

#include "UObject/WeakObjectPtr.h"
#include "Templates/SubclassOf.h"

class UWorld;
class UWorldPartition;
class IWorldPartitionCell;
class FWorldPartitionCookPackageContext;
struct FWorldPartitionStreamingQuerySource;
class AActor;

struct FWorldPartitionUtils
{
	struct FSimulateCookSessionParams
	{
		TArray<TSubclassOf<AActor>> FilteredClasses;
	};

	class ENGINE_API FSimulateCookedSession
	{
	public:

		FSimulateCookedSession(UWorld* InWorld, const FSimulateCookSessionParams& Params = FSimulateCookSessionParams());
		~FSimulateCookedSession();

		bool IsValid() const { return !!CookContext; }
		bool ForEachStreamingCells(TFunctionRef<void(const IWorldPartitionCell*)> Func);
		bool GetIntersectingCells(const TArray<FWorldPartitionStreamingQuerySource>& InSources, TArray<const IWorldPartitionCell*>& OutCells);

	private:
		bool SimulateCook(const FSimulateCookSessionParams& Params);

		FWorldPartitionCookPackageContext* CookContext;
		TWeakObjectPtr<UWorldPartition> WorldPartition;
	};
};

#endif