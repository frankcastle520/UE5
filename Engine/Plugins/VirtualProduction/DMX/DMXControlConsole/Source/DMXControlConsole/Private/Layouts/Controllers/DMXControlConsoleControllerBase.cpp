// Copyright Epic Games, Inc. All Rights Reserved.

#include "Layouts/Controllers/DMXControlConsoleControllerBase.h"


#if WITH_EDITORONLY_DATA
FOnPropertiesChangedDelegate UDMXControlConsoleControllerBase::OnPropertiesChanged;
#endif // WITH_EDITORONLY_DATA

#if WITH_EDITOR
void UDMXControlConsoleControllerBase::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	if (PropertyChangedEvent.ChangeType != EPropertyChangeType::Interactive)
	{
		OnPropertiesChanged.Broadcast(PropertyChangedEvent);
	}
}
#endif // WITH_EDITOR