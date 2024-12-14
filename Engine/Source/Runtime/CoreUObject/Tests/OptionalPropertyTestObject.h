// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "OptionalPropertyTestObject.generated.h"

UCLASS()
class UOptionalPropertyTestObject : public UObject
{
    GENERATED_BODY()

public:
    UPROPERTY()
    TOptional<FString> OptionalString;

    UPROPERTY()
    TOptional<FText> OptionalText;

    UPROPERTY()
    TOptional<FName> OptionalName;

    UPROPERTY()
    TOptional<int32> OptionalInt;
};