// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "RigVMModel/RigVMSchema.h"
#include "AnimNextRigVMAssetSchema.generated.h"

UCLASS()
class UAnimNextRigVMAssetSchema : public URigVMSchema
{
	GENERATED_BODY()

	virtual bool SupportsNodeLayouts(const URigVMGraph* InGraph) const override
	{
		return true;
	}

protected:
	UAnimNextRigVMAssetSchema();
};

