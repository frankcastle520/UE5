// Copyright Epic Games, Inc. All Rights Reserved.

#include "RemoteControlDMXExposedEntityPatchWidgetFactory.h"

#include "IRemoteControlUIModule.h"
#include "RemoteControlProtocolDMX.h"
#include "UI/SRemoteControlDMXExposedEntityPatch.h"

namespace UE::RemoteControl::DMX
{
	void FRemoteControlDMXExposedEntityPatchWidgetFactory::Register()
	{
		IRemoteControlUIModule& RemoteControlUIModule = FModuleManager::LoadModuleChecked<IRemoteControlUIModule>("RemoteControlUI");
		RemoteControlUIModule.RegisterExposedEntityWidgetFactory(MakeShared<FRemoteControlDMXExposedEntityPatchWidgetFactory>());
	}

	FName FRemoteControlDMXExposedEntityPatchWidgetFactory::GetColumnName() const
	{
		return FRemoteControlProtocolDMX::PatchColumnName;
	}

	FName FRemoteControlDMXExposedEntityPatchWidgetFactory::GetProtocolName() const
	{
		return FRemoteControlProtocolDMX::ProtocolName;
	}

	TSharedRef<SWidget> FRemoteControlDMXExposedEntityPatchWidgetFactory::MakePropertyWidget(const FRCPanelExposedPropertyWidgetArgs& Args)
	{
		return SNew(SRemoteControlDMXExposedEntityPatch, Args.WeakPreset, Args.Property);
	}
}
