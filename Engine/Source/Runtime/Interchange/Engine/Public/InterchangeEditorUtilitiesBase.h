// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UObject/Object.h"
#include "UObject/ObjectMacros.h"

#include "InterchangeEditorUtilitiesBase.generated.h"

UCLASS(MinimalAPI)
class UInterchangeEditorUtilitiesBase : public UObject
{
	GENERATED_BODY()

public:
	INTERCHANGEENGINE_API virtual bool SaveAsset(UObject* Asset)
	{
		return false;
	}

	INTERCHANGEENGINE_API virtual bool IsRuntimeOrPIE()
	{
#if WITH_EDITOR
		return false;
#else
		return true;
#endif
	}
};
