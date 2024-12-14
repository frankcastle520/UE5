// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Components/ActorComponent.h"
#include "MoverComponent.h"
#include "GameFramework/NavMovementInterface.h"

#include "NavMoverComponent.generated.h"

/**
 * NavMoverComponent: Responsible for implementing INavMoveInterface with UMoverComponent so pathfinding and other forms of navigation movement work.
 * This component also caches the input given to it that is then consumed by the mover system.
 * Note: This component relies on the parent actor having a MoverComponent as well. By default this component only has a reference to MoverComponent meaning
 * we use other ways (such as gameplay tags for the active movement mode) to check for state rather than calling specific functions on the active MoverComponent.
 */
UCLASS(BlueprintType, meta = (BlueprintSpawnableComponent))
class MOVER_API UNavMoverComponent : public UActorComponent, public INavMovementInterface
{
	GENERATED_BODY()

public:
	UNavMoverComponent();

	FVector CachedNavMoveInputIntent = FVector::ZeroVector;
	FVector CachedNavMoveInputVelocity = FVector::ZeroVector;

	// TODO: Not sure if we need these
	FRotator CachedTurnInput = FRotator::ZeroRotator;
	FRotator CachedLookInput = FRotator::ZeroRotator;

	/** Properties that define how the component can move. */
	UPROPERTY(EditAnywhere, Category="Nav Movement", meta = (DisplayName = "Movement Capabilities", Keywords = "Nav Agent"))
	FNavAgentProperties NavAgentProps;

	/** Expresses runtime state of character's movement. Put all temporal changes to movement properties here */
	UPROPERTY()
	FMovementProperties MovementState;
	
	/** bool for keeping track of requested nav movement - is set to true when movement is requested, is false once nav movement was consumed */
	UPROPERTY(BlueprintReadOnly, Category="Nav Movement")
	bool bRequestedNavMovement = false;
	
protected:
	/** associated properties for nav movement */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Nav Movement")
	FNavMovementProperties NavMovementProperties;
	
private:
	/** object implementing IPathFollowingAgentInterface. Is private to control access to it.
	 *	@see SetPathFollowingAgent, GetPathFollowingAgent */
	UPROPERTY()
	TObjectPtr<UObject> PathFollowingComp;

	/** Associated Movement component that will actually move the actor */ 
	UPROPERTY()
	TObjectPtr<UMoverComponent> MoverComponent;
	
public:
	virtual void InitializeComponent() override;

	/** Get the owner of the object consuming nav movement */
	virtual UObject* GetOwnerAsObject() const override { return GetOwner(); }
	
	/** Get the component this movement component is updating */
	virtual TObjectPtr<UObject> GetUpdatedObject() const override { return MoverComponent->GetUpdatedComponent(); }

	/** Get axis-aligned cylinder around this actor, used for simple collision checks in nav movement */
	virtual void GetSimpleCollisionCylinder(float& CollisionRadius, float& CollisionHalfHeight) const override;

	/** Returns collision extents vector for this object, based on GetSimpleCollisionCylinder. */
	virtual FVector GetSimpleCollisionCylinderExtent() const override;
	
	/** Get forward vector of the object being driven by nav movement */
	virtual FVector GetForwardVector() const override;
	
	/** Get the current velocity of the movement component */
	UFUNCTION(BlueprintCallable, Category="AI|Components|NavMovement")
	virtual FVector GetVelocityForNavMovement() const override;

	/** Get the max speed of the movement component */
	UFUNCTION(BlueprintCallable, Category="AI|Components|NavMovement")
	virtual float GetMaxSpeedForNavMovement() const override;

	// Overridden to also call StopActiveMovement().
	virtual void StopMovementImmediately() override;

	/** Returns location of controlled actor - meaning center of collision bounding box */
	virtual FVector GetLocation() const override;
	/** Returns location of controlled actor's "feet" meaning center of bottom of collision bounding box */
	virtual FVector GetFeetLocation() const override;
	/** Returns based location of controlled actor */
	virtual FBasedPosition GetFeetLocationBased() const override;

	/** Set nav agent properties from an object */
	virtual void UpdateNavAgent(const UObject& ObjectToUpdateFrom) override;
	
	/** path following: request new velocity */
	virtual void RequestDirectMove(const FVector& MoveVelocity, bool bForceMaxSpeed) override;

	/** path following: request new move input (normal vector = full strength) */
	virtual void RequestPathMove(const FVector& MoveInput) override;

	/** check if current move target can be reached right now if positions are matching
	 *  (e.g. performing scripted move and can't stop) */
	virtual bool CanStopPathFollowing() const override;

	/** Get Nav movement props struct this component uses */
	virtual FNavMovementProperties* GetNavMovementProperties() override { return &NavMovementProperties; }
	/** Returns the NavMovementProps(const) */
	virtual const FNavMovementProperties& GetNavMovementProperties() const override{ return NavMovementProperties; } 
	
	virtual void SetPathFollowingAgent(IPathFollowingAgentInterface* InPathFollowingAgent) override;
	virtual IPathFollowingAgentInterface* GetPathFollowingAgent() override;
	virtual const IPathFollowingAgentInterface* GetPathFollowingAgent() const override;

	/** Returns the NavAgentProps(const) */
	virtual const FNavAgentProperties& GetNavAgentPropertiesRef() const override;
	/** Returns the NavAgentProps */
	virtual FNavAgentProperties& GetNavAgentPropertiesRef() override;

	/** Resets runtime movement state to character's movement capabilities */
	virtual void ResetMoveState() override;

	/** Returns true if path following can start */
	virtual bool CanStartPathFollowing() const override;

	/** Returns true if currently crouching */
	virtual bool IsCrouching() const override;
	
	/** Returns true if currently falling (not flying, in a non-fluid volume, and not on the ground) */ 
	virtual bool IsFalling() const override;

	/** Returns true if currently moving on the ground (e.g. walking or driving) */
	virtual bool IsMovingOnGround() const override;
	
	/** Returns true if currently swimming (moving through a fluid volume) */
	UFUNCTION(BlueprintCallable, Category="AI|Components|NavMovement")
	virtual bool IsSwimming() const override;

	/** Returns true if currently flying (moving through a non-fluid volume without resting on the ground) */
	virtual bool IsFlying() const override;
};
