// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once


#include "Templates/SubclassOf.h"

#include "LiveLinkComponentSettings.generated.h"

class ULiveLinkControllerBase;
class ULiveLinkRole;


/**
 * Settings for LiveLink.
 */
UCLASS(config=Game, defaultconfig)
class LIVELINKCOMPONENTS_API ULiveLinkComponentSettings : public UObject
{
	GENERATED_BODY()

public:

	/** Default Controller class to use for the specified role */
	UPROPERTY(config, EditAnywhere, Category = "LiveLink", meta = (AllowAbstract = "false"))
	TMap<TSubclassOf<ULiveLinkRole>, TSubclassOf<ULiveLinkControllerBase>> DefaultControllerForRole;

	/** If true, all LiveLink subjects with pending frame data will be updated immediately before the LiveLink components execute (only applies to PIE) */
	UPROPERTY(config, EditAnywhere, Category = "LiveLink")
	bool bUpdateSubjectsImmediatelyInPIE = true;
};

#if UE_ENABLE_INCLUDE_ORDER_DEPRECATED_IN_5_2
#include "LiveLinkControllerBase.h"
#include "LiveLinkRole.h"
#endif
