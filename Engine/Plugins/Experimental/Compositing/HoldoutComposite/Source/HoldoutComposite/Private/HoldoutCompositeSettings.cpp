// Copyright Epic Games, Inc. All Rights Reserved.

#include "HoldoutCompositeSettings.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(HoldoutCompositeSettings)

UHoldoutCompositeSettings::UHoldoutCompositeSettings()
	: bCompositeFollowsSceneExposure(false)
	, bCompositeSupportsSSR(true)
	, SceneViewExtensionPriority(100 + 1 /* OPENCOLORIO_SCENE_VIEW_EXTENSION_PRIORITY + 1 */)
{ }

FName UHoldoutCompositeSettings::GetCategoryName() const
{
	return TEXT("Plugins");
}

#if WITH_EDITOR
FText UHoldoutCompositeSettings::GetSectionText() const
{
	return NSLOCTEXT("HoldoutCompositeSettings", "HoldoutCompositeSettingsSection", "Holdout Composite");
}

FName UHoldoutCompositeSettings::GetSectionName() const
{
	return TEXT("Holdout Composite");
}
#endif
