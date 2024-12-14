// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if WITH_EDITOR

#include "CoreFwd.h"
#include "UObject/ObjectMacros.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/Object.h"
#include "Serialization/NameAsStringProxyArchive.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include "WorldPartition/WorldPartitionPropertyOverride.h"

class UWorldPartitionPropertyOverridePolicy;

class FWorldPartitionPropertyOverrideArchive : public FNameAsStringProxyArchive
{
public:
	FWorldPartitionPropertyOverrideArchive(FArchive& InArchive, FPropertyOverrideReferenceTable& InReferenceTable);

	virtual bool ShouldSkipProperty(const FProperty* InProperty) const override;
	
	virtual FArchive& operator<<(FLazyObjectPtr& Value) override;
	virtual FArchive& operator<<(UObject*& Obj) override;
	virtual FArchive& operator<<(FWeakObjectPtr& Obj) override;
	virtual FArchive& operator<<(FSoftObjectPtr& Value) override;
	virtual FArchive& operator<<(FSoftObjectPath& Value) override;
	virtual FArchive& operator<<(FObjectPtr& Obj) override;
private:
	UWorldPartitionPropertyOverridePolicy* PropertyOverridePolicy = nullptr;
	FPropertyOverrideReferenceTable& ReferenceTable;

	FSoftObjectPath ReadSoftObjectPath();
	void WriteSoftObjectPath(FSoftObjectPath SoftObjectPath);
};

class FWorldPartitionPropertyOverrideWriter : public FMemoryWriter
{
public:
	FWorldPartitionPropertyOverrideWriter(TArray<uint8, TSizedDefaultAllocator<32>>& InBytes);
};

class FWorldPartitionPropertyOverrideReader : public FMemoryReader
{
public:
	FWorldPartitionPropertyOverrideReader(const TArray<uint8>& InBytes);
};

#endif