// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Templates/PimplPtr.h"

#include "USDInfoCacheObject.generated.h"

class FUsdInfoCache;
namespace UE
{
	class FSdfPath;
}

/**
 * Minimal UObject wrapper around FUsdInfoCache, since we want this data to be
 * owned by an independently serializable UObject, but the implementation must be in
 * an RTTI-enabled module
 */
UCLASS()
class USDSTAGE_API UUsdInfoCache : public UObject
{
	GENERATED_BODY()

public:
	UUsdInfoCache();

	// Begin UObject interface
	virtual void Serialize(FArchive& Ar) override;
	// End UObject interface

	FUsdInfoCache& GetInner();
	const FUsdInfoCache& GetInner() const;

private:
	TPimplPtr<FUsdInfoCache> Impl;
};
