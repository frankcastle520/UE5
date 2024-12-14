// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "PhysicsEngine/PhysicsHandleComponent.h"

#include "PhysicsControlAssetEditorPhysicsHandleComponent.generated.h"

class UPhysicsControlAssetEditorSkeletalMeshComponent;

/**
 * Extend the Physics Handle for PhAt. This adds support for manipulating the physics
 * if it is running in a RigidBody AnimNode (which is always is with Chaos at the moment).
 */
UCLASS()
class UPhysicsControlAssetEditorPhysicsHandleComponent : public UPhysicsHandleComponent
{
	GENERATED_UCLASS_BODY()

	bool bAnimInstanceMode;

public:
	void SetAnimInstanceMode(bool bInAnimInstanceMode);

	virtual void ReleaseComponent() override;

	void AddImpulseAtLocation(
		UPhysicsControlAssetEditorSkeletalMeshComponent* PhysicsControlAssetEditorSkeletalMeshComponent,
		FVector Impulse, FVector Location, FName BoneName);

protected:
	virtual void UpdateHandleTransform(const FTransform& NewTransform) override;
	virtual void UpdateDriveSettings() override;
	virtual void GrabComponentImp(
		class UPrimitiveComponent* Component, FName InBoneName, const FVector& Location, 
		const FRotator& Rotation, bool bRotationConstrained) override;
};
