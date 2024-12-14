// Copyright Epic Games, Inc. All Rights Reserved.

#include "RigVMFunctions/Execution/RigVMFunction_Context.h"

FRigVMFunction_IsHostBeingDebugged_Execute()
{
#if WITH_EDITOR
	Result = ExecuteContext.IsHostBeingDebugged();
#else
	Result = false;
#endif
}


