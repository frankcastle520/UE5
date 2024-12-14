// Copyright Epic Games, Inc. All Rights Reserved.

#include "Utilities/CEEffectorLibrary.h"

#include "Containers/Set.h"
#include "Effector/Modes/CEEffectorModeBase.h"
#include "Effector/Types/CEEffectorTypeBase.h"
#include "Subsystems/CEEffectorSubsystem.h"

void UCEEffectorLibrary::GetEffectorModeClasses(TSet<TSubclassOf<UCEEffectorModeBase>>& OutModeClasses)
{
	OutModeClasses.Empty();

	if (const UCEEffectorSubsystem* EffectorSubsystem = UCEEffectorSubsystem::Get())
	{
		for (TSubclassOf<UCEEffectorExtensionBase> ModeClass : EffectorSubsystem->GetExtensionClasses<UCEEffectorModeBase>())
		{
			OutModeClasses.Add(ModeClass.Get());
		}
	}
}

void UCEEffectorLibrary::GetEffectorTypeClasses(TSet<TSubclassOf<UCEEffectorTypeBase>>& OutTypeClasses)
{
	OutTypeClasses.Empty();

	if (const UCEEffectorSubsystem* EffectorSubsystem = UCEEffectorSubsystem::Get())
	{
		for (TSubclassOf<UCEEffectorExtensionBase> TypeClass : EffectorSubsystem->GetExtensionClasses<UCEEffectorTypeBase>())
		{
			OutTypeClasses.Add(TypeClass.Get());
		}
	}
}

void UCEEffectorLibrary::GetEffectorExtensionClasses(TSet<TSubclassOf<UCEEffectorExtensionBase>>& OutExtensionClasses)
{
	OutExtensionClasses.Empty();

	if (const UCEEffectorSubsystem* EffectorSubsystem = UCEEffectorSubsystem::Get())
	{
		for (TSubclassOf<UCEEffectorExtensionBase> ExtensionClass : EffectorSubsystem->GetExtensionClasses<UCEEffectorExtensionBase>())
		{
			if (!ExtensionClass.Get()
				|| ExtensionClass->IsChildOf<UCEEffectorTypeBase>()
				|| ExtensionClass->IsChildOf<UCEEffectorModeBase>())
			{
				continue;
			}

			OutExtensionClasses.Add(ExtensionClass);
		}
	}
}
