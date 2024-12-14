// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "RigVMCore/RigVMDispatchFactory.h"
#include "AnimNextExecuteContext.h"
#include "RigVMDispatch_CallObjectAccessorFunction.generated.h"

/** Synthetic dispatch injected by the compiler to get a value via an accessor UFunction, not user instantiated */
USTRUCT(meta = (Hidden, DisplayName = "Call Function", Category="Internal"))
struct ANIMNEXT_API FRigVMDispatch_CallObjectAccessorFunction : public FRigVMDispatchFactory
{
	GENERATED_BODY()

	FRigVMDispatch_CallObjectAccessorFunction();

	static inline const FLazyName ObjectName = FLazyName("Object");
	static inline const FLazyName FunctionName = FLazyName("Function");
	static inline const FLazyName ValueName = FLazyName("Value");

private:
	virtual UScriptStruct* GetExecuteContextStruct() const override { return FAnimNextExecuteContext::StaticStruct(); }
	virtual FName GetArgumentNameForOperandIndex(int32 InOperandIndex, int32 InTotalOperands) const override;
	virtual const TArray<FRigVMTemplateArgumentInfo>& GetArgumentInfos() const override;
	virtual FRigVMTemplateTypeMap OnNewArgumentType(const FName& InArgumentName, TRigVMTypeIndex InTypeIndex) const override;
	virtual bool IsSingleton() const override { return true; }

	virtual FRigVMFunctionPtr GetDispatchFunctionImpl(const FRigVMTemplateTypeMap& InTypes) const override
	{
		return &FRigVMDispatch_CallObjectAccessorFunction::Execute;
	}
	static void Execute(FRigVMExtendedExecuteContext& InContext, FRigVMMemoryHandleArray Handles, FRigVMPredicateBranchArray RigVMBranches);
};
