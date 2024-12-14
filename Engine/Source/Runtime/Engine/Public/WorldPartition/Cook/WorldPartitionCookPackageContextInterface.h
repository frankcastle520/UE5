// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#if WITH_EDITOR

#include "CoreMinimal.h"
#include "Templates/SubclassOf.h"

class IWorldPartitionCookPackageGenerator;
class IWorldPartitionCookPackageObject;
struct FWorldPartitionCookPackage;
class AActor;

struct FWorldPartitionCookPackageContextParams
{
	TArray<TSubclassOf<AActor>> FilteredClasses;
};

class IWorldPartitionCookPackageContext
{
public:
	virtual ~IWorldPartitionCookPackageContext() {};

	virtual void RegisterPackageCookPackageGenerator(IWorldPartitionCookPackageGenerator* Generator) = 0;
	virtual void UnregisterPackageCookPackageGenerator(IWorldPartitionCookPackageGenerator* Generator) = 0;

	UE_DEPRECATED(5.5, "Use AddPackageToGenerate instead.")
	virtual const FWorldPartitionCookPackage* AddLevelStreamingPackageToGenerate(IWorldPartitionCookPackageGenerator* Generator, const FString& Root, const FString& RelativePath) { return nullptr; }
	UE_DEPRECATED(5.5, "Use AddPackageToGenerate instead.")
	virtual const FWorldPartitionCookPackage* AddGenericPackageToGenerate(IWorldPartitionCookPackageGenerator* Generator, const FString& Root, const FString& RelativePath) { return nullptr; }

	virtual const FWorldPartitionCookPackage* AddPackageToGenerate(IWorldPartitionCookPackageGenerator* Generator, IWorldPartitionCookPackageObject* InCookPackageObject, const FString& Root, const FString& RelativePath) = 0;
	virtual FString GetGeneratedPackagePath(IWorldPartitionCookPackageObject* InCookPackageObject) const = 0;

	virtual bool GatherPackagesToCook(const FWorldPartitionCookPackageContextParams& Params = FWorldPartitionCookPackageContextParams()) = 0;
	virtual const FWorldPartitionCookPackageContextParams& GetParams() const = 0;
};

#endif