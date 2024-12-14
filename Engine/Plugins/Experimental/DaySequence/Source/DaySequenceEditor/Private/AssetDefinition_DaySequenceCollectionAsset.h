// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AssetDefinitionDefault.h"
#include "Factories/Factory.h"

#include "AssetDefinition_DaySequenceCollectionAsset.generated.h"

class UDaySequenceCollectionAsset;

UCLASS()
class UAssetDefinition_DaySequenceCollectionAsset : public UAssetDefinitionDefault
{
	GENERATED_BODY()

public:
	virtual FText GetAssetDisplayName() const override { return NSLOCTEXT("AssetTypeActions", "AssetTypeActions_DaySequenceCollectionAsset", "Day Sequence Collection"); }
	virtual FLinearColor GetAssetColor() const override { return FLinearColor(FColor(200, 80, 80)); }
	virtual TSoftClassPtr<UObject> GetAssetClass() const override;
};

UCLASS()
class DAYSEQUENCEEDITOR_API UDaySequenceCollectionAssetFactory : public UFactory
{
	GENERATED_BODY()
	
public:
	UDaySequenceCollectionAssetFactory(const class FObjectInitializer& Obj);
	
	virtual UObject* FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn) override;
};