// Copyright Epic Games, Inc. All Rights Reserved.

#include "Variables/RigUnit_ResolveUniversalObjectLocator.h"
#include "Module/AnimNextModuleInstance.h"

FRigUnit_ResolveUniversalObjectLocator_Execute()
{
	using namespace UE::UniversalObjectLocator;

	const FAnimNextModuleContextData& ContextData = ExecuteContext.GetContextData<FAnimNextModuleContextData>();

	FResolveParams ResolveParams;
	ResolveParams.Context = ContextData.GetModuleInstance().Object;
	FResolveResult Result = Locator.Resolve(ResolveParams);
	Object = Result.SyncGet().Object;
}