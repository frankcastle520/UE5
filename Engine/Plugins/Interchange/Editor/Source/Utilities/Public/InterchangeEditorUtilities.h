// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "InterchangeEditorUtilitiesBase.h"

#include "InterchangeEditorUtilities.generated.h"

UCLASS()
class INTERCHANGEEDITORUTILITIES_API UInterchangeEditorUtilities : public UInterchangeEditorUtilitiesBase
{
	GENERATED_BODY()

public:

protected:

	virtual bool SaveAsset(UObject* Asset) override;

	virtual bool IsRuntimeOrPIE() override;
};
