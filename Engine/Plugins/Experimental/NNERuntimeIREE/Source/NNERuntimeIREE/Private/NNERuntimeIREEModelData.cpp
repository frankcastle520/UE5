// Copyright Epic Games, Inc. All Rights Reserved.

#include "NNERuntimeIREEModelData.h"

#include "Containers/Array.h"
#include "NNE.h"
#include "NNERuntimeIREELog.h"
#include "Serialization/CustomVersion.h"

namespace UE::NNERuntimeIREE::ModelData::Private
{

enum Version : uint32
{
	V0 = 0, // Initial
	// New versions can be added above this line
	VersionPlusOne,
	Latest = VersionPlusOne - 1
};
const FGuid GUID(0x6dcb835d, 0x9ac64a1d, 0x8165d871, 0x6122dab7);
FCustomVersionRegistration Version(GUID, Version::Latest, TEXT("NNERuntimeIREEModelDataVersion"));// Always save with the latest version

} // UE::NNERuntimeIREE::ModelData::Private

void UNNERuntimeIREEModelData::Serialize(FArchive& Ar)
{
	// Store the asset version (no effect in load)
	Ar.UsingCustomVersion(UE::NNERuntimeIREE::ModelData::Private::GUID);

	if (Ar.IsSaving() || Ar.IsCountingMemory()) 
	{
		Ar << GUID;
		Ar << Version;
		Ar << FileId;
		Ar << ModuleMetaData;
		Ar << CompilerResult;
	}
	else
	{
		int32 NumArchitectures = 0;

		switch (Ar.CustomVer(UE::NNERuntimeIREE::ModelData::Private::GUID))
		{
		case UE::NNERuntimeIREE::ModelData::Private::Version::V0:
			Ar << GUID;
			Ar << Version;
			Ar << FileId;
			Ar << ModuleMetaData;
			Ar << CompilerResult;
			break;
		default:
			UE_LOG(LogNNERuntimeIREE, Error, TEXT("UNNERuntimeIREEModelData: Unknown asset version %d: Deserialisation failed, please reimport the original model."), Ar.CustomVer(UE::NNERuntimeIREE::ModelData::Private::GUID));
			break;
		}
	}
}

bool UNNERuntimeIREEModelData::IsSameGuidAndVersion(TConstArrayView64<uint8> Data, FGuid Guid, int32 Version)
{
	int32 GuidSize = sizeof(UNNERuntimeIREEModelData::GUID);
	int32 VersionSize = sizeof(UNNERuntimeIREEModelData::Version);
	if (Data.Num() <= GuidSize + VersionSize)
	{
		return false;
	}

	bool bResult = FGenericPlatformMemory::Memcmp(&(Data[0]), &Guid, GuidSize) == 0;
	bResult &= FGenericPlatformMemory::Memcmp(&(Data[GuidSize]), &Version, VersionSize) == 0;

	return bResult;
}