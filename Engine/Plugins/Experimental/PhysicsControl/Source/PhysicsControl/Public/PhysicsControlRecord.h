// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "PhysicsControlData.h"
#include "PhysicsControlPoseData.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/EngineTypes.h"

struct FConstraintInstance;
struct FBodyInstance;
class UPrimitiveComponent;

/**
 * There will be a PhysicsControlRecord created at runtime for every Control that has been created
 */
struct FPhysicsControlRecord
{
	FPhysicsControlRecord(
		const FPhysicsControl&       InControl,
		const FPhysicsControlTarget& InControlTarget,
		UPrimitiveComponent*              InParentComponent,
		UPrimitiveComponent*              InChildComponent)
		: PhysicsControl(InControl)
		, ControlTarget(InControlTarget)
		, ParentComponent(InParentComponent)
		, ChildComponent(InChildComponent)
	{}

	/** Removes any constraint and resets the state */
	void ResetConstraint();

	/** Returns the control point, which may be custom or automatic (centre of mass) */
	FVector GetControlPoint() const;

	/**
	 * Creates the constraint if necessary and stores it. Then initializes the constraint with the bodies.
	 * Returns true/false on success/failure */
	bool InitConstraint(UObject* ConstraintDebugOwner, FName ControlName);

	/** Ensures the constraint frame matches the control point in the record. */
	void UpdateConstraintControlPoint();

	/** Sets the control point to the center of mass of the child mesh (or to zero if that fails). */
	void ResetControlPoint();

	/** The configuration data */
	FPhysicsControl PhysicsControl;

	/**
	 * The position/orientation etc targets for the controls. These are procedural/explicit control targets -
	 * skeletal meshes have the option to use skeletal animation as well, in which case these targets are 
	 * expressed as relative to that animation.
	 */
	FPhysicsControlTarget ControlTarget;

	// The previous skeletal control target. This will have been set at the end of a previous update
	// (but only if the control was enabled etc), so to check if it is valid, check the update
	// counter. Note that explicit control targets (which contain their own velocity) will be added
	// onto this.
	UE::PhysicsControl::FPosQuat PreviousSkeletalTargetTM;

	// Only use the previous target TM if the current counter is equal to this expected counter. The
	// expected update counter will be set when the control/previous target TM has just been updated.
	FGraphTraversalCounter ExpectedUpdateCounter;

	/**  The mesh that will be doing the driving. Blank/non-existent means it will happen in world space */
	TWeakObjectPtr<UPrimitiveComponent> ParentComponent;

	/** The mesh that the control will be driving. */
	TWeakObjectPtr<UPrimitiveComponent> ChildComponent;

	/** The underlying constraint used to implement the control. */
	TSharedPtr<FConstraintInstance> ConstraintInstance;
};

/**
 * There will be a PhysicsBodyModifier created at runtime for every BodyInstance involved in the component
 */
struct FPhysicsBodyModifierRecord
{
	FPhysicsBodyModifierRecord(
		TWeakObjectPtr<UPrimitiveComponent>  InComponent, 
		const FName&                    InBoneName, 
		FPhysicsControlModifierData     InBodyModifierData)
		: Component(InComponent)
		, BodyModifier(InBoneName, InBodyModifierData)
		, KinematicTargetPosition(FVector::ZeroVector)
		, KinematicTargetOrientation(FQuat::Identity)
		, bResetToCachedTarget(false)
	{}

	/**  The mesh that will be modified. */
	TWeakObjectPtr<UPrimitiveComponent> Component;

	// The core data
	FPhysicsBodyModifier BodyModifier;

	/** 
	 * The target position when kinematic. Note that this is applied on top of any animation 
	 * target if bUseSkeletalAnimation is set. 
	 */
	FVector KinematicTargetPosition = FVector::ZeroVector;

	/**
	 * The target orientation when kinematic. Note that this is applied on top of any animation
	 * target if bUseSkeletalAnimation is set.
	 */
	FQuat KinematicTargetOrientation = FQuat::Identity;

	/** 
	 * If true then the body will be set to the transform/velocity stored in any cached target (if that
	 * exists), and then this flag will be cleared.
	 */
	uint8 bResetToCachedTarget:1;
};

/**
 * Used internally/only at runtime to track when a SkeletalMeshComponent is being controlled through
 * a modifier, and to restore settings when that stops.
 */
struct FModifiedSkeletalMeshData
{
public: 
	FModifiedSkeletalMeshData() : ReferenceCount(0) {}

public:

	/** The original setting for restoration when we're deleted */
	uint8 bOriginalUpdateMeshWhenKinematic : 1;

	/** The original setting for restoration when we're deleted */
	EKinematicBonesUpdateToPhysics::Type OriginalKinematicBonesUpdateType;

	/**
	 * Track when skeletal meshes are going to be used so this entry can be removed
	 */
	int32 ReferenceCount;
};

