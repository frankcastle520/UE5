// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Templates/SharedPointer.h"
#include "UObject/ObjectMacros.h"
#include "UObject/ObjectPtr.h"
#include "UObject/UObjectGlobals.h"
#include "DataflowPreview.generated.h"

class UAnimationAsset;
class USkeletalMesh;

USTRUCT()
struct FDataflowPreviewCacheParams
{
	GENERATED_BODY()
public :

	/** Number of sampling frames per second for caching*/
	UPROPERTY(EditAnywhere, Category="Caching")
	int32 FrameRate = 30;

	/** Number of sampling frames per second for caching*/
	UPROPERTY(EditAnywhere, Category="Caching")
	FVector2f TimeRange = FVector2f(0.0f, 5.0f);

	/** Boolean to check if the caching will be done on an async thread (if yes no GT dependency) */
	UPROPERTY(EditAnywhere, AdvancedDisplay, Category = "Caching")
	bool bAsyncCaching = true;
};
