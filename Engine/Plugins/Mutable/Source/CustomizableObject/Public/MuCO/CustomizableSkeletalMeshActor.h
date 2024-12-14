// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Animation/SkeletalMeshActor.h"

#include "CustomizableSkeletalMeshActor.generated.h"

class UCustomizableObjectInstance;
class UCustomizableSkeletalComponent;

class UObject;

UCLASS(ClassGroup = CustomizableObject, Blueprintable, ComponentWrapperClass, ConversionRoot, meta = (ChildCanTick))
class CUSTOMIZABLEOBJECT_API ACustomizableSkeletalMeshActor : public ASkeletalMeshActor
{
	GENERATED_UCLASS_BODY()
public:
	
	class UCustomizableSkeletalComponent* GetCustomizableSkeletalComponent(int32 Index = 0) const { return CustomizableSkeletalComponents[Index]; }
	
	class USkeletalMeshComponent* GetSkeletalMeshComponentAt(int32 Index = 0) const { return SkeletalMeshComponents[Index]; }

	void AttachNewComponent();

	int32 GetNumComponents() { return CustomizableSkeletalComponents.Num(); }

	/** Sets which material will be used for debugging purposes */
	UFUNCTION(BlueprintCallable, Category = CustomizableSkeletalMeshActor)
	void SetDebugMaterial(UMaterialInterface* InDebugMaterial);

	/** Enables the debug material to all the componets of the actor */
	UFUNCTION(BlueprintCallable, Category = CustomizableSkeletalMeshActor)
	void EnableDebugMaterial(bool bEnableDebugMaterial);

private:

	/** Changes materials to all actor componets */
	UFUNCTION()
	void SwitchComponentsMaterials(UCustomizableObjectInstance* Instance);

	/** return: the instance represented in all the actor components */
	UCustomizableObjectInstance* GetComponentsCommonInstance();

private:
	
	UPROPERTY(Category = CustomizableSkeletalMesh, VisibleAnywhere, BlueprintReadOnly, meta = (AllowPrivateAccess = "true"))
	TArray< TObjectPtr<class UCustomizableSkeletalComponent> > CustomizableSkeletalComponents;

	UPROPERTY(Category = CustomizableSkeletalMesh, VisibleAnywhere, BlueprintReadOnly, meta = (AllowPrivateAccess = "true"))
	TArray< TObjectPtr<class USkeletalMeshComponent>> SkeletalMeshComponents;

	// TODO: This is a temporary fix to not break the demos, we should update the demos to support the arrays of components
	UPROPERTY(Category = CustomizableSkeletalMesh, VisibleAnywhere, BlueprintReadOnly, meta = (AllowPrivateAccess = "true"))
	TObjectPtr<class UCustomizableSkeletalComponent> CustomizableSkeletalComponent;

	/** Material used for debugging */
	TObjectPtr<UMaterialInterface> DebugMaterial;

	/** bool that determines if the debug material has been enabled */
	bool bDebugMaterialEnabled = false;

	/** bool used to remove the debug material whet it has been disabled */
	bool bRemoveDebugMaterial = false;
};
