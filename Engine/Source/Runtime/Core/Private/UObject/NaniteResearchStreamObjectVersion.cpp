// Copyright Epic Games, Inc. All Rights Reserved.
#include "UObject/NaniteResearchStreamObjectVersion.h"
#include "UObject/DevObjectVersion.h"
#include "Containers/Map.h"
#include "Misc/Guid.h"


TMap<FGuid, FGuid> FNaniteResearchStreamObjectVersion::GetSystemGuids()
{
	TMap<FGuid, FGuid> SystemGuids;
	const FDevSystemGuids& DevGuids = FDevSystemGuids::Get();

	//SystemGuids.Add(DevGuids.NANITE_DERIVEDDATA_VER, FGuid("49D2EF76967340EFB22F2F398E4DDDDD"));

	return SystemGuids;
}
