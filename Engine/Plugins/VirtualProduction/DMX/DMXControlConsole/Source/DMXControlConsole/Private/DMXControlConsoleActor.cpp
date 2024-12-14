// Copyright Epic Games, Inc. All Rights Reserved.

#include "DMXControlConsoleActor.h"

#include "Components/SceneComponent.h"
#include "DMXControlConsoleData.h"
#include "DMXControlConsoleFaderBase.h"
#include "DMXControlConsoleFaderGroup.h"
#include "Layouts/Controllers/DMXControlConsoleControllerBase.h"


#define LOCTEXT_NAMESPACE "DMXControlConsoleActor"

#if WITH_EDITORONLY_DATA
FSimpleMulticastDelegate ADMXControlConsoleActor::OnControlConsoleReset;
#endif // WITH_EDITORONLY_DATA

ADMXControlConsoleActor::ADMXControlConsoleActor()
{
	RootSceneComponent = CreateDefaultSubobject<USceneComponent>("SceneComponent");
	RootComponent = RootSceneComponent;
}

#if WITH_EDITOR
void ADMXControlConsoleActor::SetDMXControlConsoleData(UDMXControlConsoleData* InControlConsoleData)
{
	if (!ensureAlwaysMsgf(!ControlConsoleData, TEXT("Tried to set the DMXControlConsole for %s, but it already has one set. Changing the control console is not supported."), *GetName()))
	{
		return;
	}

	if (InControlConsoleData)
	{
		ControlConsoleData = InControlConsoleData;
	}
}
#endif // WITH_EDITOR

void ADMXControlConsoleActor::StartSendingDMX()
{
	if (ControlConsoleData)
	{
		ControlConsoleData->StartSendingDMX();
	}
}

void ADMXControlConsoleActor::StopSendingDMX()
{
	if (ControlConsoleData)
	{
		ControlConsoleData->StopSendingDMX();
	}
}

void ADMXControlConsoleActor::PauseSendingDMX()
{
	if (ControlConsoleData)
	{
		ControlConsoleData->PauseSendingDMX();
	}
}

void ADMXControlConsoleActor::ResetToDefault()
{
	if (!ControlConsoleData)
	{
		return;
	}

	const TArray<UDMXControlConsoleFaderGroup*> FaderGroups = ControlConsoleData->GetAllFaderGroups();
	for (const UDMXControlConsoleFaderGroup* FaderGroup : FaderGroups)
	{
		if (!FaderGroup)
		{
			continue;
		}

		const TArray<UDMXControlConsoleFaderBase*> Faders = FaderGroup->GetAllFaders();
		for (UDMXControlConsoleFaderBase* Fader : Faders)
		{
			if (!Fader)
			{
				continue;
			}

			Fader->ResetToDefault();
		}
	}

#if WITH_EDITOR
	OnControlConsoleReset.Broadcast();
#endif // WITH_EDITOR
}

void ADMXControlConsoleActor::ResetToZero()
{
	if (!ControlConsoleData)
	{
		return;
	}

	const TArray<UDMXControlConsoleFaderGroup*> FaderGroups = ControlConsoleData->GetAllFaderGroups();
	for (const UDMXControlConsoleFaderGroup* FaderGroup : FaderGroups)
	{
		if (!FaderGroup)
		{
			continue;
		}

		const TArray<UDMXControlConsoleFaderBase*> Faders = FaderGroup->GetAllFaders();
		for (UDMXControlConsoleFaderBase* Fader : Faders)
		{
			if (!Fader)
			{
				continue;
			}

			Fader->SetValue(0);
		}
	}

#if WITH_EDITOR
	OnControlConsoleReset.Broadcast();
#endif // WITH_EDITOR
}

void ADMXControlConsoleActor::PostLoad()
{
	Super::PostLoad();

#if WITH_EDITOR
	ApplySendDMXInEditorState();
#endif // WITH_EDITOR
}

void ADMXControlConsoleActor::BeginPlay()
{
	Super::BeginPlay();

	if (bAutoActivate)
	{
		StartSendingDMX();
	}

#if WITH_EDITOR
	bIsPlayInWorld = true;
#endif 
}

void ADMXControlConsoleActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Super::EndPlay(EndPlayReason);

#if WITH_EDITOR
	bIsPlayInWorld = false;
	ApplySendDMXInEditorState();
#else
	StopSendingDMX();
#endif
}

#if WITH_EDITOR
void ADMXControlConsoleActor::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	const FName PropertyName = PropertyChangedEvent.GetPropertyName();
	if (PropertyName == GET_MEMBER_NAME_CHECKED(ADMXControlConsoleActor, bSendDMXInEditor) ||
		PropertyName == GET_MEMBER_NAME_CHECKED(ADMXControlConsoleActor, bAutoActivate))
	{
		ApplySendDMXInEditorState();
	}
}
#endif // WITH_EDITOR

#if WITH_EDITOR
void ADMXControlConsoleActor::ApplySendDMXInEditorState()
{
	if (ControlConsoleData)
	{
		const bool bShouldSendDMX = bAutoActivate && (bIsPlayInWorld || bSendDMXInEditor);
		if (bShouldSendDMX)
		{
			ControlConsoleData->StartSendingDMX();
		}
		else
		{
			ControlConsoleData->StopSendingDMX();
		}
	}
}
#endif // WITH_EDITOR

#undef LOCTEXT_NAMESPACE
