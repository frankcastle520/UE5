// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Features/IPluginsEditorFeature.h"
#include "GameFeatureData.h"
#include "PluginDescriptor.h"

/**
 * Used to create custom templates for GameFeaturePlugins.
 */
struct GAMEFEATURESEDITOR_API FGameFeaturePluginTemplateDescription : public FPluginTemplateDescription
{
	FGameFeaturePluginTemplateDescription(FText InName, FText InDescription, FString InOnDiskPath, FString InDefaultSubfolder, FString InDefaultPluginName
		, TSubclassOf<UGameFeatureData> GameFeatureDataClassOverride, FString GameFeatureDataNameOverride, EPluginEnabledByDefault InEnabledByDefault);

	virtual bool ValidatePathForPlugin(const FString& ProposedAbsolutePluginPath, FText& OutErrorMessage) override;
	virtual void UpdatePathWhenTemplateSelected(FString& InOutPath) override;
	virtual void UpdatePathWhenTemplateUnselected(FString& InOutPath) override;

	virtual void UpdatePluginNameTextWhenTemplateSelected(FText& OutPluginNameText) override;
	virtual void UpdatePluginNameTextWhenTemplateUnselected(FText& OutPluginNameText) override;

	virtual void CustomizeDescriptorBeforeCreation(FPluginDescriptor& Descriptor) override;
	virtual void OnPluginCreated(TSharedPtr<IPlugin> NewPlugin) override;

	FString GetGameFeatureRoot() const;
	bool IsRootedInGameFeaturesRoot(const FString& InStr) const;

	FString DefaultSubfolder;
	FString DefaultPluginName;
	TSubclassOf<UGameFeatureData> GameFeatureDataClass;
	FString GameFeatureDataName;
	EPluginEnabledByDefault PluginEnabledByDefault = EPluginEnabledByDefault::Disabled;
};
