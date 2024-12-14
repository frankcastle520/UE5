// Copyright Epic Games, Inc. All Rights Reserved.

#include "AssetDefinition_DaySequenceCollectionAsset.h"

#include "DaySequenceCollectionAsset.h"

#define LOCTEXT_NAMESPACE "AssetTypeActions"

TSoftClassPtr<UObject> UAssetDefinition_DaySequenceCollectionAsset::GetAssetClass() const
{
	return UDaySequenceCollectionAsset::StaticClass();
}

UDaySequenceCollectionAssetFactory::UDaySequenceCollectionAssetFactory(const class FObjectInitializer& Obj)
: Super(Obj)
{
	SupportedClass = UDaySequenceCollectionAsset::StaticClass();
	bEditAfterNew = true;
	bCreateNew = true;
}

UObject* UDaySequenceCollectionAssetFactory::FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn)
{
	return NewObject<UDaySequenceCollectionAsset>(InParent, Class, Name, Flags | RF_Transactional, Context);
}

#undef LOCTEXT_NAMESPACE
