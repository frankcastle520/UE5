// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "Components/SceneComponent.h"

#include "HoldoutCompositeComponent.generated.h"

UCLASS(ClassGroup = Rendering, HideCategories=(Activation, Transform, Lighting, Rendering, Tags, Cooking, Physics, LOD, AssetUserData, Navigation), editinlinenew, meta = (BlueprintSpawnableComponent), MinimalAPI)
class UHoldoutCompositeComponent : public USceneComponent
{
	GENERATED_UCLASS_BODY()

public:
	//~ Begin UActorComponent interface
	HOLDOUTCOMPOSITE_API virtual void OnRegister() override;
	HOLDOUTCOMPOSITE_API virtual void OnUnregister() override;
	//~ End UActorComponent interface
	
	//~ Begin USceneComponent Interface.
	HOLDOUTCOMPOSITE_API virtual void OnAttachmentChanged() override;
	HOLDOUTCOMPOSITE_API virtual void DetachFromComponent(const FDetachmentTransformRules& DetachmentRules) override;
	//~ End USceneComponent Interface

	/* Get the enabled state of the component. */
	UFUNCTION(BlueprintGetter)
	bool IsEnabled() const;

	/* Set the enabled state of the component. */
	UFUNCTION(BlueprintSetter)
	void SetEnabled(bool bInEnabled);

private:

	/* Private implementation of the register method. */
	void RegisterCompositeImpl();

	/* Private implementation of the unregister method. */
	void UnregisterCompositeImpl();

private:

	/* Whether or not the component activates the composite. */
	UPROPERTY(EditAnywhere, BlueprintGetter = IsEnabled, BlueprintSetter = SetEnabled, Category = "HoldoutComposite", meta = (AllowPrivateAccess = true))
	bool bIsEnabled = true;
};

