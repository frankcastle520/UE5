// Copyright Epic Games, Inc. All Rights Reserved.

#include "Variables/RigUnit_CopyModuleProxyVariables.h"
#include "Module/AnimNextModuleInstance.h"

FRigUnit_CopyModuleProxyVariables_Execute()
{
	const FAnimNextModuleContextData& ContextData = ExecuteContext.GetContextData<FAnimNextModuleContextData>();
	ContextData.GetModuleInstance().CopyProxyVariables();
}