// Copyright Epic Games, Inc. All Rights Reserved.

#include "ChaosVDCoreSettings.h"

#include "Misc/ConfigContext.h"
#include "Widgets/SChaosVDPlaybackViewport.h"


UChaosVDSettingsObjectBase::UChaosVDSettingsObjectBase()
{
	
}

void UChaosVDSettingsObjectBase::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	UObject::PostEditChangeProperty(PropertyChangedEvent);

	BroadcastSettingsChanged();
}

void UChaosVDSettingsObjectBase::PostEditUndo()
{
	UObject::PostEditUndo();
	BroadcastSettingsChanged();
}

void UChaosVDSettingsObjectBase::OverridePerObjectConfigSection(FString& SectionName)
{
	if (OverrideConfigSectionName.IsEmpty())
	{
		OverrideConfigSectionName = GetClass()->GetPathName() + TEXT(" Instance");
	}

	SectionName = OverrideConfigSectionName;
}

void UChaosVDSettingsObjectBase::BroadcastSettingsChanged()
{
	SettingsChangedDelegate.Broadcast(this);

	constexpr bool bAllowCopyToDefaultObject = false;
	SaveConfig(CPF_Config,nullptr, GConfig, bAllowCopyToDefaultObject);
}

void UChaosVDVisualizationSettingsObjectBase::BroadcastSettingsChanged()
{
	Super::BroadcastSettingsChanged();
	SChaosVDPlaybackViewport::ExecuteExternalViewportInvalidateRequest();
}

