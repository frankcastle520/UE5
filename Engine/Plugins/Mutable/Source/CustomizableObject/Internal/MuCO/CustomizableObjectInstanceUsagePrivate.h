// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Tickable.h"

#include "CustomizableObjectInstanceUsagePrivate.generated.h"

class UCustomizableObjectInstance;
class UCustomizableObjectInstanceUsage;
class UCustomizableSkeletalComponent;
class UPhysicsAsset;
class USkeletalMesh;
class USkeletalMeshComponent;

UCLASS()
class CUSTOMIZABLEOBJECT_API UCustomizableObjectInstanceUsagePrivate : public UObject, public FTickableGameObject
{
	GENERATED_BODY()

public:
	// FTickableGameObject interface
	virtual void Tick(float DeltaTime) override;
	virtual ETickableTickType GetTickableTickType() const override;
	virtual TStatId GetStatId() const override;
	virtual bool IsTickableWhenPaused() const override;
	virtual bool IsTickableInEditor() const override;
	virtual bool IsTickable() const override;
	
	// Own interface
	
	/** Common end point of all updates. Even those which failed. */
	void Callbacks() const;

	void SetSkeletalMeshAndOverrideMaterials(USkeletalMeshComponent& Parent, USkeletalMesh* SkeletalMesh, const UCustomizableObjectInstance& CustomizableObjectInstance, bool* bOutSkeletalMeshUpdated, bool* bOutMaterialsUpdated);

#if WITH_EDITOR
	// Used to generate instances outside the CustomizableObject editor and PIE
	void UpdateDistFromComponentToLevelEditorCamera(const FVector& CameraPosition);
	void EditorUpdateComponent();
#endif

	USkeletalMesh* GetSkeletalMesh() const;

	USkeletalMesh* GetAttachedSkeletalMesh() const;

	void SetSkeletalMesh(USkeletalMesh* SkeletalMesh, bool* bOutSkeletalMeshUpdated = nullptr, bool* bOutMaterialsUpdated = nullptr);

	void SetPhysicsAsset(UPhysicsAsset* PhysicsAsset, bool* bOutPhysicsAssetUpdated = nullptr);

	void UpdateDistFromComponentToPlayer(const AActor* const Pawn, bool bForceEvenIfNotBegunPlay = false);

	/** Set to true to replace the SkeletalMesh of the parent component by the ReferenceSkeletalMesh or the generated SkeletalMesh. */
	void SetPendingSetSkeletalMesh(bool bIsActive);
	
	bool GetPendingSetSkeletalMesh() const;

	UCustomizableSkeletalComponent* GetCustomizableSkeletalComponent();

	void SetCustomizableSkeletalComponent(UCustomizableSkeletalComponent* Component);
	
	UCustomizableObjectInstanceUsage* GetPublic();

	const UCustomizableObjectInstanceUsage* GetPublic() const;

	// Returns true if the NetMode of the associated UCustomizableSkeletalComponent (or the associated SkeletalMeshComponent if the former does not exist) is equal to InNetMode
	bool IsNetMode(ENetMode InNetMode) const;
};
