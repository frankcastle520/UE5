// Copyright Epic Games, Inc. All Rights Reserved.

#include "WorldPartition/WorldPartitionPropertyOverrideSerialization.h"

#if WITH_EDITOR

#include "WorldPartition/WorldPartitionSettings.h"
#include "WorldPartition/WorldPartitionPropertyOverride.h"

FWorldPartitionPropertyOverrideArchive::FWorldPartitionPropertyOverrideArchive(FArchive& InArchive, FPropertyOverrideReferenceTable& InReferenceTable)
	: FNameAsStringProxyArchive(InArchive)
	, ReferenceTable(InReferenceTable)
{
	check(InArchive.IsPersistent());
	check(!InArchive.IsFilterEditorOnly());
	check(InArchive.ShouldSkipBulkData());
	check(!InArchive.WantBinaryPropertySerialization());

	SetIsLoading(InArchive.IsLoading());
	SetIsSaving(InArchive.IsSaving());
	SetIsTextFormat(InArchive.IsTextFormat());
	SetWantBinaryPropertySerialization(InArchive.WantBinaryPropertySerialization());
	SetIsPersistent(true);
	FArchiveProxy::SetFilterEditorOnly(InArchive.IsFilterEditorOnly());
	ArShouldSkipBulkData = true;
	PropertyOverridePolicy = UWorldPartitionSettings::Get()->GetPropertyOverridePolicy();
}

bool FWorldPartitionPropertyOverrideArchive::ShouldSkipProperty(const FProperty* InProperty) const
{
	if (PropertyOverridePolicy)
	{
		return !PropertyOverridePolicy->CanOverrideProperty(InProperty);
	}

	return true;
}

FSoftObjectPath FWorldPartitionPropertyOverrideArchive::ReadSoftObjectPath()
{
	if (ReferenceTable.bIsValid)
	{
		int32 Index = INDEX_NONE;
		*this << Index;
		if (ReferenceTable.SoftObjectPathTable.IsValidIndex(Index))
		{
			return ReferenceTable.SoftObjectPathTable[Index];
		}
		else
		{
			// Only invalid Index we can expect is INDEX_NONE if the serialized SoftObjectPath was null
			ensureMsgf(Index == INDEX_NONE, TEXT("Invalid Index (%d) was read from the SoftObjectPathTable"), Index);
			return FSoftObjectPath();
		}
	}
	else
	{
		// load the path name to the object
		FString LoadedString;
		InnerArchive << LoadedString;
		return FSoftObjectPath(LoadedString);
	}
}

void FWorldPartitionPropertyOverrideArchive::WriteSoftObjectPath(FSoftObjectPath SoftObjectPath)
{
	ReferenceTable.bIsValid = true;
	int32 Index = INDEX_NONE;
	if (SoftObjectPath.IsValid())
	{
		Index = ReferenceTable.SoftObjectPathTable.AddUnique(SoftObjectPath);
	}
	*this << Index;
}

FArchive& FWorldPartitionPropertyOverrideArchive::operator<<(FLazyObjectPtr& Value)
{ 
	return FArchiveUObject::SerializeLazyObjectPtr(*this, Value); 
}

FArchive& FWorldPartitionPropertyOverrideArchive::operator<<(UObject*& Obj)
{
	if (IsLoading())
	{
		FSoftObjectPath Value = ReadSoftObjectPath();
		Obj = Value.ResolveObject();

		// Previous data didn't have hard references so make sure to load object if it isn't already
		if (!Obj && !ReferenceTable.bIsValid)
		{
			Obj = Value.TryLoad();
		}
		return *this;
	}
	else
	{
		ReferenceTable.ObjectReferences.Add(Obj);
		WriteSoftObjectPath(FSoftObjectPath(Obj));
	}

	return *this;
}

FArchive& FWorldPartitionPropertyOverrideArchive::operator<<(FWeakObjectPtr& Obj)
{
	return FArchiveUObject::SerializeWeakObjectPtr(*this, Obj);
}

FArchive& FWorldPartitionPropertyOverrideArchive::operator<<(FSoftObjectPtr& Value)
{
	if (IsLoading())
	{
		// Reset before serializing to clear the internal weak pointer. 
		Value.ResetWeakPtr();
		Value = ReadSoftObjectPath();
	}
	else
	{
		WriteSoftObjectPath(Value.GetUniqueID());
	}
	return *this;
}

FArchive& FWorldPartitionPropertyOverrideArchive::operator<<(FSoftObjectPath& Value)
{
	if (IsLoading())
	{
		Value = ReadSoftObjectPath();
	}
	else
	{
		WriteSoftObjectPath(Value);
	}
	return *this;
}

FArchive& FWorldPartitionPropertyOverrideArchive::operator<<(FObjectPtr& Obj)
{
	return FArchiveUObject::SerializeObjectPtr(*this, Obj);
}


FWorldPartitionPropertyOverrideWriter::FWorldPartitionPropertyOverrideWriter(TArray<uint8, TSizedDefaultAllocator<32>>& InBytes)
	: FMemoryWriter(InBytes, true)
{
	SetFilterEditorOnly(false);
	ArShouldSkipBulkData = true;
	SetIsTextFormat(false);
	SetWantBinaryPropertySerialization(false);
}

FWorldPartitionPropertyOverrideReader::FWorldPartitionPropertyOverrideReader(const TArray<uint8>& InBytes)
	: FMemoryReader(InBytes, true)
{
	SetFilterEditorOnly(false);
	ArShouldSkipBulkData = true;
	SetIsTextFormat(false);
	SetWantBinaryPropertySerialization(false);
}

#endif

