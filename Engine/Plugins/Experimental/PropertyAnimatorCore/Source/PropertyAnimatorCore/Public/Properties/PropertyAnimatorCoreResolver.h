// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Presets/PropertyAnimatorCorePresetable.h"
#include "Properties/PropertyAnimatorCoreData.h"
#include "UObject/NameTypes.h"
#include "UObject/Object.h"
#include "PropertyAnimatorCoreResolver.generated.h"

/**
 * Base class to find properties hidden or not reachable,
 * allows to discover resolvable properties for specific actors/components/objects
 * that we cannot reach or are transient, will be resolved when needed
 */
UCLASS(MinimalAPI, Abstract)
class UPropertyAnimatorCoreResolver : public UObject, public IPropertyAnimatorCorePresetable
{
	GENERATED_BODY()

public:
	UPropertyAnimatorCoreResolver()
		: UPropertyAnimatorCoreResolver(NAME_None)
	{}

	UPropertyAnimatorCoreResolver(FName InResolverName)
		: ResolverName(InResolverName)
	{}

	/** Get properties found inside parent property */
	virtual void GetResolvableProperties(const FPropertyAnimatorCoreData& InParentProperty, TSet<FPropertyAnimatorCoreData>& OutProperties) {}

	/** Called when we actually need the underlying properties */
	virtual void ResolveProperties(const FPropertyAnimatorCoreData& InTemplateProperty, TArray<FPropertyAnimatorCoreData>& OutProperties, bool bInForEvaluation) {}

	FName GetResolverName() const
	{
		return ResolverName;
	}

	//~ Begin IPropertyAnimatorCorePresetable
	PROPERTYANIMATORCORE_API virtual bool ImportPreset(const UPropertyAnimatorCorePresetBase* InPreset, const TSharedRef<FPropertyAnimatorCorePresetArchive>& InValue) override;
	PROPERTYANIMATORCORE_API virtual bool ExportPreset(const UPropertyAnimatorCorePresetBase* InPreset, TSharedPtr<FPropertyAnimatorCorePresetArchive>& OutValue) const override;
	//~ End IPropertyAnimatorCorePresetable

private:
	UPROPERTY()
	FName ResolverName = NAME_None;
};
