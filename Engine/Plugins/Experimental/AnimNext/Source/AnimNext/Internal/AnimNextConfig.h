// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AnimNextConfig.generated.h"

UCLASS(Config=AnimNext)
class ANIMNEXT_API UAnimNextConfig : public UObject
{
	GENERATED_BODY()

private:
	// UObject interface
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
};