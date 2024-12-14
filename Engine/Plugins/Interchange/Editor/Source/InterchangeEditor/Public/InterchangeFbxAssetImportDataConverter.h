// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "InterchangeAssetImportData.h"
#include "UObject/Class.h"
#include "UObject/Object.h"
#include "UObject/ObjectMacros.h"

#include "InterchangeFbxAssetImportDataConverter.generated.h"

UCLASS()
class INTERCHANGEEDITOR_API UInterchangeFbxAssetImportDataConverter : public UInterchangeAssetImportDataConverterBase
{
	GENERATED_BODY()

public:
	virtual bool CanConvertClass(const UClass* SourceClass, const UClass* DestinationClass) const override;

	virtual bool ConvertImportData(UObject* Asset, const FString& ToExtension) const override;
	virtual bool ConvertImportData(const UObject* SourceImportData, const UClass* DestinationClass, UObject** DestinationImportData) const override;
};
