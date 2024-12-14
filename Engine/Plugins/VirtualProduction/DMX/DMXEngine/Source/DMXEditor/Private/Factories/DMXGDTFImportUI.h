// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "UObject/Object.h"
#include "DMXGDTFImportUI.generated.h"

UCLASS(config = Editor, HideCategories=Object, MinimalAPI)
class UDMXGDTFImportUI 
    : public UObject
{
	GENERATED_BODY()

public:
    UDMXGDTFImportUI();

	void ResetToDefault();

public:
    UPROPERTY(EditAnywhere, Config, Category = "DMX")
    bool bUseSubDirectory = true;

    UPROPERTY(EditAnywhere, Config, Category = "DMX")
    bool bImportXML = true;

    UPROPERTY(EditAnywhere, Config, Category = "DMX")
    bool bImportTextures = true;

    UPROPERTY(EditAnywhere, Config, Category = "DMX")
    bool bImportModels = true;
};
