// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "UObject/Object.h"
#include "UObject/ObjectMacros.h"
#include "Engine/DeveloperSettings.h"

#include "HoldoutCompositeSettings.generated.h"

/**
 * Settings for the HoldoutComposite module.
 */
UCLASS(config = HoldoutComposite, defaultconfig, meta = (DisplayName = "Holdout Composite"), MinimalAPI)
class UHoldoutCompositeSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UHoldoutCompositeSettings();

	//~ Begin UDeveloperSettings interface
	virtual FName GetCategoryName() const;
#if WITH_EDITOR
	virtual FText GetSectionText() const override;
	virtual FName GetSectionName() const override;
#endif
	//~ End UDeveloperSettings interface

public:
	/** When enabled, the view global exposure is applied onto the separate render when composited. */
	UPROPERTY(config, EditAnywhere, Category = General)
	bool bCompositeFollowsSceneExposure;

	/** When enabled, the separate composited render is also used to update screen-space reflections. */
	UPROPERTY(config, EditAnywhere, Category = General)
	bool bCompositeSupportsSSR;

	/** Composite (scene view extension) pass priority, which defaults to before OpenColorIO. */
	UPROPERTY(config, EditAnywhere, Category = General, AdvancedDisplay)
	int32 SceneViewExtensionPriority;

	/** Primitive component classes that do not support the holdout composite.*/
	UPROPERTY(config, EditAnywhere, Category = General)
	TArray<FSoftClassPath> DisabledPrimitiveClasses;
};

