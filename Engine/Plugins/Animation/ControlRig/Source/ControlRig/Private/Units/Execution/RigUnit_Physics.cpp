// Copyright Epic Games, Inc. All Rights Reserved.

#include "Units/Execution/RigUnit_Physics.h"
#include "Rigs/RigHierarchyController.h"
#include "Units/RigUnitContext.h"
#include "ControlRig.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(RigUnit_Physics)

FRigUnit_HierarchyAddPhysicsSolver_Execute()
{
	FString ErrorMessage;
	if(!FRigUnit_DynamicHierarchyBase::IsValidToRunInContext(ExecuteContext, true, &ErrorMessage))
	{
		if(!ErrorMessage.IsEmpty())
		{
			UE_CONTROLRIG_RIGUNIT_REPORT_ERROR(TEXT("%s"), *ErrorMessage);
		}
		return;
	}

	Solver = FRigPhysicsSolverID();

	if(UControlRig* ControlRig = Cast<UControlRig>(ExecuteContext.Hierarchy->GetOuter()))
	{
		Solver = ControlRig->AddPhysicsSolver(Name, false, false);
	}
}

FRigUnit_HierarchyAddPhysicsJoint_Execute()
{
	FString ErrorMessage;
	if(!FRigUnit_DynamicHierarchyBase::IsValidToRunInContext(ExecuteContext, true, &ErrorMessage))
	{
		if(!ErrorMessage.IsEmpty())
		{
			UE_CONTROLRIG_RIGUNIT_REPORT_ERROR(TEXT("%s"), *ErrorMessage);
		}
		return;
	}

	Item.Reset();

	if(URigHierarchyController* Controller = ExecuteContext.Hierarchy->GetController(true))
	{
		FRigHierarchyControllerInstructionBracket InstructionBracket(Controller, ExecuteContext.GetInstructionIndex());
		Item = Controller->AddPhysicsElement(Name, Parent, Solver, Settings, Transform, false, false);
	}
}
