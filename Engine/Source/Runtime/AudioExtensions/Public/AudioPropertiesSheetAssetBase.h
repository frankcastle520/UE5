// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UObject/Object.h"
#include "UObject/ObjectPtr.h"

#include "AudioPropertiesSheetAssetBase.generated.h"

class UAudioPropertiesBindings;

UCLASS(Abstract)
class AUDIOEXTENSIONS_API UAudioPropertiesSheetAssetBase : public UObject
{
	GENERATED_BODY()

public: 
#if WITH_EDITOR
	UFUNCTION(BlueprintCallable, BlueprintPure=false, Category="AudioProperties")
	virtual bool CopyToObjectProperties(UObject* TargetObject) const { return false; };
#endif
};