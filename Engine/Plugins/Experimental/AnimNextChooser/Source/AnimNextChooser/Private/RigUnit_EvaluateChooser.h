// Copyright Epic Games, Inc. All Rights Reserved.


#pragma once

#include "CoreMinimal.h"
#include "AnimNextExecuteContext.h"
#include "Chooser.h"
#include "ControlRigDefines.h"
#include "Units/RigUnit.h"

#include "RigUnit_EvaluateChooser.generated.h"

/*
 * Evaluates a Chooser Table and outputs the selected UObject
 */
USTRUCT(meta = (Abstract, Keywords="Evaluate Chooser", Category = "Chooser", Varying, NodeColor="0.737911 0.099899 0.099899"))
struct FRigUnit_EvaluateChooser : public FRigVMStruct
{
	GENERATED_BODY()

	UPROPERTY(meta = (Input))
	TObjectPtr<UObject> ContextObject;
	
	UPROPERTY(EditAnywhere, Category = "Chooser", meta = (Input, Constant))
	TObjectPtr<UChooserTable> Chooser;

	UPROPERTY(meta = (Output))
	TObjectPtr<UObject> Result;
};

/*
 * Evaluates a Chooser Table in the context of ControlRig
 */
USTRUCT(meta = (DisplayName = "Evaluate Chooser", Varying, NodeColor="0.737911 0.099899 0.099899", ExecuteContext="FControlRigExecuteContext"))
struct FRigUnit_EvaluateChooser_ControlRig : public FRigUnit_EvaluateChooser
{
	GENERATED_BODY()

	/** Execute logic for this rig unit */
	RIGVM_METHOD()
	virtual void Execute() override;
};

/*
 * Evaluates a Chooser Table in the context of AnimNext
 */
USTRUCT(meta = (DisplayName = "Evaluate Chooser", Varying, NodeColor="0.737911 0.099899 0.099899", ExecuteContext="FAnimNextExecuteContext"))
struct FRigUnit_EvaluateChooser_AnimNext : public FRigUnit_EvaluateChooser
{
	GENERATED_BODY()

	/** Execute logic for this rig unit */
	RIGVM_METHOD()
	virtual void Execute() override;
};