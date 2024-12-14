// Copyright Epic Games, Inc. All Rights Reserved.

#include "Effector/Modes/CEEffectorModeBase.h"

#include "CEClonerEffectorShared.h"
#include "Effector/CEEffectorComponent.h"

void UCEEffectorModeBase::OnExtensionParametersChanged(UCEEffectorComponent* InComponent)
{
	Super::OnExtensionParametersChanged(InComponent);

	FCEClonerEffectorChannelData& ChannelData = InComponent->GetChannelData();
	ChannelData.Mode = static_cast<ECEClonerEffectorMode>(ModeIdentifier);
}
