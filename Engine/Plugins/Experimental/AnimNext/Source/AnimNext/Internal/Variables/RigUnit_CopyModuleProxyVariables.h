// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Graph/RigUnit_AnimNextBase.h"
#include "RigUnit_CopyModuleProxyVariables.generated.h"

/** Synthetic node injected by the compiler to copy proxy variables to a module instance, not user instantiated */
USTRUCT(meta=(Hidden, DisplayName = "Copy Proxy Variables", Category="Internal"))
struct ANIMNEXT_API FRigUnit_CopyModuleProxyVariables : public FRigUnit_AnimNextBase
{
	GENERATED_BODY()

	FRigUnit_CopyModuleProxyVariables() = default;

	RIGVM_METHOD()
	virtual void Execute() override;

	// The execution result
	UPROPERTY(EditAnywhere, DisplayName = "Execute", Category = "Events", meta = (Input, Output))
	FAnimNextExecuteContext ExecuteContext;
};