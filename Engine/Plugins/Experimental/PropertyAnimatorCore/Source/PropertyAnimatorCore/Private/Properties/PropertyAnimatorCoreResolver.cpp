// Copyright Epic Games, Inc. All Rights Reserved.

#include "Properties/PropertyAnimatorCoreResolver.h"

#include "Presets/PropertyAnimatorCorePresetArchive.h"
#include "Presets/PropertyAnimatorCorePresetBase.h"

bool UPropertyAnimatorCoreResolver::ImportPreset(const UPropertyAnimatorCorePresetBase* InPreset, const TSharedRef<FPropertyAnimatorCorePresetArchive>& InValue)
{
	if (!InValue->IsObject())
	{
		return false;
	}

	return true;
}

bool UPropertyAnimatorCoreResolver::ExportPreset(const UPropertyAnimatorCorePresetBase* InPreset, TSharedPtr<FPropertyAnimatorCorePresetArchive>& OutValue) const
{
	const TSharedRef<FPropertyAnimatorCorePresetObjectArchive> TimeSourceArchive = InPreset->GetArchiveImplementation()->CreateObject();
	OutValue = TimeSourceArchive;
	return true;
}
