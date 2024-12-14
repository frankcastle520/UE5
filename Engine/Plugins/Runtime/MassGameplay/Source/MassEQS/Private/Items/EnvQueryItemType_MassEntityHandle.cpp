// Copyright Epic Games, Inc. All Rights Reserved.

#include "Items/EnvQueryItemType_MassEntityHandle.h"

#include "EnvironmentQuery/EnvQueryTypes.h"

#include "MassEntitySubsystem.h"

UEnvQueryItemType_MassEntityHandle::UEnvQueryItemType_MassEntityHandle()
{
	ValueSize = sizeof(FMassEnvQueryEntityInfo);
}

const FMassEnvQueryEntityInfo& UEnvQueryItemType_MassEntityHandle::GetValue(const uint8* RawData)
{
	return GetValueFromMemory<FMassEnvQueryEntityInfo>(RawData);
}

void UEnvQueryItemType_MassEntityHandle::SetValue(uint8* RawData, const FMassEnvQueryEntityInfo& Value)
{
	return SetValueInMemory<FMassEnvQueryEntityInfo>(RawData, Value);
}

FVector UEnvQueryItemType_MassEntityHandle::GetItemLocation(const uint8* RawData) const
{
	const FMassEnvQueryEntityInfo& EntityInfo = GetValue(RawData);	
	return EntityInfo.CachedTransform.GetLocation();
}

FRotator UEnvQueryItemType_MassEntityHandle::GetItemRotation(const uint8* RawData) const
{
	const FMassEnvQueryEntityInfo& EntityInfo = GetValue(RawData);
	return EntityInfo.CachedTransform.GetRotation().Rotator();
}
