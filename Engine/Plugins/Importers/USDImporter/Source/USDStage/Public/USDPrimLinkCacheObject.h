// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Templates/PimplPtr.h"

#include "USDPrimLinkCacheObject.generated.h"

class FUsdPrimLinkCache;
namespace UE
{
	class FSdfPath;
}

/**
 * Minimal UObject wrapper around FUsdPrimLinkCache, since we want this to be accessible
 * from USDSchemas which is an RTTI-enabled module, but also to be owned by an independently
 * serializable UObject
 */
UCLASS()
class USDSTAGE_API UUsdPrimLinkCache : public UObject
{
	GENERATED_BODY()

public:
	UUsdPrimLinkCache();

	// Begin UObject interface
	virtual void Serialize(FArchive& Ar) override;
	// End UObject interface

	FUsdPrimLinkCache& GetInner();
	const FUsdPrimLinkCache& GetInner() const;

private:
	TPimplPtr<FUsdPrimLinkCache> Impl;
};
