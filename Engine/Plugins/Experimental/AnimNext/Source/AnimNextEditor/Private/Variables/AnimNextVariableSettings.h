// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UObject/Object.h"
#include "Param/ParamType.h"
#include "AnimNextVariableSettings.generated.h"

UCLASS(Config = EditorPerProjectUserSettings)
class UAnimNextVariableSettings : public UObject
{
	GENERATED_BODY()

	UAnimNextVariableSettings();

public:
	// Get the type of the last variable that we created
	const FAnimNextParamType& GetLastVariableType() const;

	// Set the type of the last variable that we created
	void SetLastVariableType(const FAnimNextParamType& InLastVariableType);

	// Get the name of the last variable that we created
	FName GetLastVariableName() const;

	// Set the name of the last variable that we created
	void SetLastVariableName(FName InLastVariableName);
private:
	UPROPERTY(Transient)
	FAnimNextParamType LastVariableType = FAnimNextParamType::GetType<bool>();

	UPROPERTY(Transient)
	FName LastVariableName = FName("NewVariable");
};