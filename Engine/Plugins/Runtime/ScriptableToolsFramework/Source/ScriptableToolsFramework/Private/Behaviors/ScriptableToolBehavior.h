// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "InputBehavior.h"

#include "ScriptableToolBehavior.generated.h"

UCLASS(Abstract)
class UScriptableToolBehavior : public UObject
{
	GENERATED_BODY()

public:

	UScriptableToolBehavior() {};

	void SetDefaultPriority(const FInputCapturePriority& Priority);

	virtual UInputBehavior* GetWrappedBehavior() PURE_VIRTUAL(UScriptableToolBehavior::GetWrappedBehavior, return nullptr;);

};