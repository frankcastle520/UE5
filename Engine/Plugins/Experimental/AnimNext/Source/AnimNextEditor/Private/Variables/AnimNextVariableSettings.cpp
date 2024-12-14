// Copyright Epic Games, Inc. All Rights Reserved.

#include "AnimNextVariableSettings.h"

UAnimNextVariableSettings::UAnimNextVariableSettings()
{
}

const FAnimNextParamType& UAnimNextVariableSettings::GetLastVariableType() const
{
	return LastVariableType;
}

void UAnimNextVariableSettings::SetLastVariableType(const FAnimNextParamType& InLastVariableType)
{
	LastVariableType = InLastVariableType;
}

FName UAnimNextVariableSettings::GetLastVariableName() const
{
	return LastVariableName;
}

void UAnimNextVariableSettings::SetLastVariableName(FName InLastVariableName)
{
	LastVariableName = InLastVariableName;
}