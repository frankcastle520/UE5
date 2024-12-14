// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "RigUnit_DynamicHierarchy.h"
#include "RigUnit_Physics.generated.h"

/**
 * Adds a new physics solver to the hierarchy
 * Note: This node only runs as part of the construction event.
 */
USTRUCT(meta=(DisplayName="Spawn Physics Solver", Keywords="Construction,Create,New,Simulation", Varying))
struct CONTROLRIG_API FRigUnit_HierarchyAddPhysicsSolver : public FRigUnit_DynamicHierarchyBaseMutable
{
	GENERATED_BODY()

	FRigUnit_HierarchyAddPhysicsSolver()
	{
		Name = TEXT("Solver");
		Solver = FRigPhysicsSolverID();
	}

	/*
	 * The name of the new solver to add
	 */
	UPROPERTY(meta = (Input))
	FName Name;

	/*
	 * The identifier of the solver
	 */
	UPROPERTY(meta = (Output))
	FRigPhysicsSolverID Solver;

	RIGVM_METHOD()
	virtual void Execute() override;
};

/**
 * Adds a new physics joint to the hierarchy
 * Note: This node only runs as part of the construction event.
 */
USTRUCT(meta=(DisplayName="Spawn Physics Joint", Keywords="Construction,Create,New,Joint,Skeleton", Varying))
struct CONTROLRIG_API FRigUnit_HierarchyAddPhysicsJoint : public FRigUnit_HierarchyAddElement
{
	GENERATED_BODY()

	FRigUnit_HierarchyAddPhysicsJoint()
	{
		Name = TEXT("NewPhysicsJoint");
		Parent = FRigElementKey(NAME_None, ERigElementType::Bone);
		Transform = FTransform::Identity;
		Solver = FRigPhysicsSolverID();
	}

	virtual ERigElementType GetElementTypeToSpawn() const override { return ERigElementType::Physics; }


	RIGVM_METHOD()
	virtual void Execute() override;

	/*
	 * The initial global transform of the spawned element
	 */
	UPROPERTY(meta = (Input))
	FTransform Transform;

	/*
	 * The solver to relate this new physics element to
	 */
	UPROPERTY(meta = (Input))
	FRigPhysicsSolverID Solver;

	/*
	 * The settings of the new physics element
	 */
	UPROPERTY(meta = (Input))
	FRigPhysicsSettings Settings;
};

