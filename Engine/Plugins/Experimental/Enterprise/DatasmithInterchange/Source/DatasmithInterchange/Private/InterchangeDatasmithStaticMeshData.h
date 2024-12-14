// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "DatasmithAdditionalData.h"
#include "DatasmithPayload.h"
#include "Nodes/InterchangeBaseNode.h"

#include "InterchangeDatasmithStaticMeshData.generated.h"

UCLASS()
class UDatasmithInterchangeStaticMeshDataNode : public UInterchangeBaseNode
{
	GENERATED_BODY()

public:
	/**
	*/
	UPROPERTY()
	TMap<FString, TObjectPtr<UDatasmithAdditionalData>> AdditionalDataMap;
};
