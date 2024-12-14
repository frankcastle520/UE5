// Copyright Epic Games, Inc. All Rights Reserved.

#include "PhysicsControlRecord.h"
#include "PhysicsControlLog.h"
#include "PhysicsControlComponentHelpers.h"

#include "PhysicsEngine/ConstraintInstance.h"
#include "PhysicsEngine/BodyInstance.h"

//======================================================================================================================
void FPhysicsControlRecord::ResetConstraint()
{
	if (ConstraintInstance.IsValid())
	{
		ConstraintInstance->TermConstraint();
	}
	ConstraintInstance.Reset();
}

//======================================================================================================================
FVector FPhysicsControlRecord::GetControlPoint() const
{
	if (PhysicsControl.ControlData.bUseCustomControlPoint)
	{
		return PhysicsControl.ControlData.CustomControlPoint;
	}

	const FBodyInstance* ChildBodyInstance = UE::PhysicsControl::GetBodyInstance(
		ChildComponent.Get(), PhysicsControl.ChildBoneName);

	return ChildBodyInstance ? ChildBodyInstance->GetMassSpaceLocal().GetTranslation() : FVector::ZeroVector;
}

//======================================================================================================================
bool FPhysicsControlRecord::InitConstraint(UObject* ConstraintDebugOwner, FName ControlName)
{
	if (!ConstraintInstance.IsValid())
	{
		ConstraintInstance = MakeShared<FConstraintInstance>();
	}
	check(ConstraintInstance.IsValid());

	FBodyInstance* ParentBody = UE::PhysicsControl::GetBodyInstance(
		ParentComponent.Get(), PhysicsControl.ParentBoneName);
	FBodyInstance* ChildBody = UE::PhysicsControl::GetBodyInstance(
		ChildComponent.Get(), PhysicsControl.ChildBoneName);

	if (ParentComponent.IsValid() && !PhysicsControl.ParentBoneName.IsNone() && !ParentBody)
	{
		UE_LOG(LogPhysicsControl, Warning,
			TEXT("Failed to find expected parent body %s when making constraint for control %s"),
			*PhysicsControl.ParentBoneName.ToString(),
			*ControlName.ToString());
		return false;
	}
	if (ChildComponent.IsValid() && !PhysicsControl.ChildBoneName.IsNone() && !ChildBody)
	{
		UE_LOG(LogPhysicsControl, Warning,
			TEXT("Failed to find expected child body %s when making constraint for control %s"),
			*PhysicsControl.ChildBoneName.ToString(),
			*ControlName.ToString());
		return false;
	}

	ConstraintInstance->InitConstraint(ChildBody, ParentBody, 1.0f, ConstraintDebugOwner);
	ConstraintInstance->SetDisableCollision(PhysicsControl.ControlData.bDisableCollision);
	// These things won't change so set them once here
	ConstraintInstance->SetLinearXMotion(ELinearConstraintMotion::LCM_Free);
	ConstraintInstance->SetLinearYMotion(ELinearConstraintMotion::LCM_Free);
	ConstraintInstance->SetLinearZMotion(ELinearConstraintMotion::LCM_Free);
	ConstraintInstance->SetAngularSwing1Motion(EAngularConstraintMotion::ACM_Free);
	ConstraintInstance->SetAngularSwing2Motion(EAngularConstraintMotion::ACM_Free);
	ConstraintInstance->SetAngularTwistMotion(EAngularConstraintMotion::ACM_Free);
	ConstraintInstance->SetAngularDriveMode(EAngularDriveMode::SLERP);

	ConstraintInstance->SetOrientationDriveSLERP(true);
	ConstraintInstance->SetAngularVelocityDriveSLERP(true);
	ConstraintInstance->SetLinearPositionDrive(true, true, true);
	ConstraintInstance->SetLinearVelocityDrive(true, true, true);

	// Ensure the control point is set
	UpdateConstraintControlPoint();

	return true;
}

//======================================================================================================================
// Note that, by default, the constraint frames are simply identity. We only modify Frame1, which 
// corresponds to the child frame. Frame2 will always be identity, because we never change it.
void FPhysicsControlRecord::UpdateConstraintControlPoint()
{
	if (ConstraintInstance.IsValid())
	{
		// Constraints are child then parent
		FTransform Frame1 = ConstraintInstance->GetRefFrame(EConstraintFrame::Frame1);
		Frame1.SetTranslation(GetControlPoint());
		ConstraintInstance->SetRefFrame(EConstraintFrame::Frame1, Frame1);
	}
}

//======================================================================================================================
void FPhysicsControlRecord::ResetControlPoint()
{
	PhysicsControl.ControlData.bUseCustomControlPoint = false;
	UpdateConstraintControlPoint();
}

