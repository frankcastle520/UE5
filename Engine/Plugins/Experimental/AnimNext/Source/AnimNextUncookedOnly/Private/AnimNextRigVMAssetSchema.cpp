// Copyright Epic Games, Inc. All Rights Reserved.

#include "AnimNextRigVMAssetSchema.h"
#include "AnimNextExecuteContext.h"

UAnimNextRigVMAssetSchema::UAnimNextRigVMAssetSchema()
{
	SetExecuteContextStruct(FAnimNextExecuteContext::StaticStruct());
}
