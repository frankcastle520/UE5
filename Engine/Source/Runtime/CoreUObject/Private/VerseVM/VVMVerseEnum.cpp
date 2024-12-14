// Copyright Epic Games, Inc. All Rights Reserved.

#include "VerseVM/VVMVerseEnum.h"
#include "Templates/SharedPointer.h"
#include "UObject/ObjectSaveContext.h"
#include "VerseVM/VVMEngineEnvironment.h"
#include "VerseVM/VVMVerse.h"

#if WITH_EDITOR
#include "UObject/CookedMetaData.h"
#endif

void UVerseEnum::Serialize(FArchive& Ar)
{
	Super::Serialize(Ar);

	if (Ar.IsLoading())
	{
		// Try to bind native enums to their C++/VNI definitions.
		if (!!(VerseEnumFlags & EVerseEnumFlags::NativeBound))
		{
			Verse::IEngineEnvironment* Environment = Verse::VerseVM::GetEngineEnvironment();
			ensure(Environment);
			Environment->TryBindVniEnum(this);
		}
	}
}

void UVerseEnum::PreSave(FObjectPreSaveContext ObjectSaveContext)
{
	Super::PreSave(ObjectSaveContext);

#if WITH_EDITOR
	// Note: We do this in PreSave rather than PreSaveRoot since Verse stores multiple generated types in the same package, and PreSaveRoot is only called for the main "asset" within each package
	if (ObjectSaveContext.IsCooking() && (ObjectSaveContext.GetSaveFlags() & SAVE_Optional))
	{
		if (!CachedCookedMetaDataPtr)
		{
			CachedCookedMetaDataPtr = CookedMetaDataUtil::NewCookedMetaData<UEnumCookedMetaData>(this, "CookedEnumMetaData");
		}

		CachedCookedMetaDataPtr->CacheMetaData(this);

		if (!CachedCookedMetaDataPtr->HasMetaData())
		{
			CookedMetaDataUtil::PurgeCookedMetaData<UEnumCookedMetaData>(CachedCookedMetaDataPtr);
		}
	}
	else if (CachedCookedMetaDataPtr)
	{
		CookedMetaDataUtil::PurgeCookedMetaData<UEnumCookedMetaData>(CachedCookedMetaDataPtr);
	}
#endif
}
