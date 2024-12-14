// Copyright Epic Games, Inc. All Rights Reserved.

#include "LevelInstance/LevelInstanceSettings.h"
#include "WorldPartition/WorldPartitionSettings.h"

#if WITH_EDITOR
#include "Settings/EditorExperimentalSettings.h"
#endif

ULevelInstanceSettings::ULevelInstanceSettings()
{
}

#if WITH_EDITOR
bool ULevelInstanceSettings::IsPropertyOverrideEnabled() const
{
	return GetDefault<UEditorExperimentalSettings>()->bEnableLevelInstancePropertyOverrides && !!PropertyOverridePolicy;
}

void ULevelInstanceSettings::UpdatePropertyOverridePolicy()
{
	// Policy hasn't changed
	if (PropertyOverridePolicy && PropertyOverridePolicy->GetClass()->GetPathName() == PropertyOverridePolicyClass)
	{
		return;
	}

	// Policy set to null
	if (PropertyOverridePolicyClass.IsEmpty())
	{
		PropertyOverridePolicy = nullptr;
		UWorldPartitionSettings::Get()->SetPropertyOverridePolicy(nullptr);
	}
	else
	{
		UClass* PropertyOverrideClassPtr = LoadClass<ULevelInstancePropertyOverridePolicy>(nullptr, *PropertyOverridePolicyClass, nullptr, LOAD_NoWarn);
		if (PropertyOverrideClassPtr)
		{
			PropertyOverridePolicy = NewObject<ULevelInstancePropertyOverridePolicy>(GetTransientPackage(), PropertyOverrideClassPtr, NAME_None);
			UWorldPartitionSettings::Get()->SetPropertyOverridePolicy(PropertyOverridePolicy);
		}
		else
		{
			PropertyOverridePolicy = nullptr;
			UWorldPartitionSettings::Get()->SetPropertyOverridePolicy(nullptr);
		}
	}
}

#endif