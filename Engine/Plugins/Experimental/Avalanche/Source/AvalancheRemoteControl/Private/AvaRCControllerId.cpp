// Copyright Epic Games, Inc. All Rights Reserved.

#include "AvaRCControllerId.h"
#include "RCVirtualProperty.h"
#include "RemoteControlPreset.h"

FAvaRCControllerId::FAvaRCControllerId(URCVirtualPropertyBase* InController)
	: Name(InController ? InController->DisplayName : NAME_None)
{
}

URCVirtualPropertyBase* FAvaRCControllerId::FindController(URemoteControlPreset* InPreset) const
{
	if (InPreset)
	{
		return InPreset->GetControllerByDisplayName(Name);
	}
	return nullptr;
}

FText FAvaRCControllerId::ToText() const
{
	return FText::FromName(Name);
}
