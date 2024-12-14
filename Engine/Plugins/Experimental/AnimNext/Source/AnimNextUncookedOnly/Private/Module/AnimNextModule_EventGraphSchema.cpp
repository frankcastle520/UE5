// Copyright Epic Games, Inc. All Rights Reserved.

#include "AnimNextEventGraphSchema.h"

#include "AnimNextExecuteContext.h"
#include "Graph/RigUnit_AnimNextBase.h"
#include "Graph/RigUnit_AnimNextTraitStack.h"

bool UAnimNextEventGraphSchema::SupportsUnitFunction(URigVMController* InController, const FRigVMFunction* InUnitFunction) const
{
	if(const UScriptStruct* FunctionExecuteContextStruct = InUnitFunction->GetExecuteContextStruct())
	{
		if(FunctionExecuteContextStruct == FAnimNextExecuteContext::StaticStruct())
		{
			// Disallow trait stacks in event graphs 
			if(InUnitFunction->Struct && InUnitFunction->Struct->IsChildOf(FRigUnit_AnimNextTraitStack::StaticStruct()))
			{
				return false;
			}
		}
	}

	return Super::SupportsUnitFunction(InController, InUnitFunction);
}
