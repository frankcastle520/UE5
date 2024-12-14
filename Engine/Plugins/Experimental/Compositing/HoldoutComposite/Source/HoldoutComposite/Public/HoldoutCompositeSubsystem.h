// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MaterialShared.h"
#include "Subsystems/WorldSubsystem.h"
#include "UObject/SoftObjectPtr.h"

#include "HoldoutCompositeSubsystem.generated.h"

class FHoldoutCompositeSceneViewExtension;
class SNotificationItem;
class URendererSettings;

/**
 * Composite subsytem used as an interface to the (private) scene view extension.
 */
UCLASS(BlueprintType, Transient)
class UHoldoutCompositeSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	UHoldoutCompositeSubsystem();

	// USubsystem implementation Begin
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	// USubsystem implementation End

	/* Register a single primitive for compositing. */
	UFUNCTION(BlueprintCallable, Category = "Holdout Composite")
	HOLDOUTCOMPOSITE_API void RegisterPrimitive(TSoftObjectPtr<UPrimitiveComponent> InPrimitiveComponent, bool bInHoldoutState=true);

	/* Register multiple primitives for compositing. */
	HOLDOUTCOMPOSITE_API void RegisterPrimitives(TArrayView<TSoftObjectPtr<UPrimitiveComponent>> InPrimitiveComponents, bool bInHoldoutState=true);

	/* Unregister a single primitive from compositing. */
	UFUNCTION(BlueprintCallable, Category = "Holdout Composite")
	HOLDOUTCOMPOSITE_API void UnregisterPrimitive(TSoftObjectPtr<UPrimitiveComponent> InPrimitiveComponent, bool bInHoldoutState=false);

	/* Unregister multiple primitives from compositing. */
	HOLDOUTCOMPOSITE_API void UnregisterPrimitives(TArrayView<TSoftObjectPtr<UPrimitiveComponent>> InPrimitiveComponents, bool bInHoldoutState=false);

private:

	/* Returns true if the (renderer) project settings are correctly enabled for the composite to be active. */
	bool ValidateProjectSettings();

#if WITH_EDITOR
	/* Toast notification to ask users to enable the missing project settings. */
	void PrimitiveHoldoutSettingsNotification(URendererSettings* RendererSettings);

	/* Toast notification item. */
	TWeakPtr<SNotificationItem> HoldoutNotificationItem;
#endif

	/* Owned scene view extension. */
	TSharedPtr<FHoldoutCompositeSceneViewExtension, ESPMode::ThreadSafe> HoldoutCompositeViewExtension;
};

