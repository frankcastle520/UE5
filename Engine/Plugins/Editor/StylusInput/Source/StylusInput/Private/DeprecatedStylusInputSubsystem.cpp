// Copyright Epic Games, Inc. All Rights Reserved.

#include <Framework/Docking/TabManager.h>
#include <HAL/IConsoleManager.h>
#include <Misc/App.h>

#include "IStylusInputModule.h"

PRAGMA_DISABLE_DEPRECATION_WARNINGS

#define LOCTEXT_NAMESPACE "StylusInputSubsystem"

namespace UE::StylusInput::Private
{
	static bool bTickStylusInputSubsystem = false;
	static FAutoConsoleVariableRef CVarEnableLegacySubsystem(
		TEXT("stylusinput.EnableLegacySubsystem"),
		bTickStylusInputSubsystem,
		TEXT("Enable the legacy stylus input subsystem, which will automatically create a tablet input context for any window on mouse over. This subsystem is deprecated for UE 5.5, and will be removed entirely in UE 5.7."),
		ECVF_Default
	);
}

// This is the function that all platform-specific implementations are required to implement.
TSharedPtr<IStylusInputInterfaceInternal> CreateStylusInputInterface();

#if !PLATFORM_WINDOWS
TSharedPtr<IStylusInputInterfaceInternal> CreateStylusInputInterface() { return TSharedPtr<IStylusInputInterfaceInternal>(); }
#endif

void UStylusInputSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	if (FApp::IsUnattended() || IsRunningCommandlet())
	{
		return;
	}

	Super::Initialize(Collection);

	UE_LOG(LogStylusInput, Log, TEXT("Initializing StylusInput subsystem."));

	InputInterface = CreateStylusInputInterface();

	if (!InputInterface.IsValid())
	{
		UE_LOG(LogStylusInput, Log, TEXT("StylusInput not supported on this platform."));
	}
}

void UStylusInputSubsystem::Deinitialize()
{
	Super::Deinitialize();

	InputInterface.Reset();

	UE_LOG(LogStylusInput, Log, TEXT("Shutting down StylusInput subsystem."));
}

int32 UStylusInputSubsystem::NumInputDevices() const
{
	if (InputInterface.IsValid())
	{
		return InputInterface->NumInputDevices();
	}
	return 0;
}

const IStylusInputDevice* UStylusInputSubsystem::GetInputDevice(const int32 Index) const
{
	if (InputInterface.IsValid())
	{
		return InputInterface->GetInputDevice(Index);
	}
	return nullptr;
}

void UStylusInputSubsystem::AddMessageHandler(IStylusMessageHandler& MessageHandler)
{
	MessageHandlers.AddUnique(&MessageHandler);
}

void UStylusInputSubsystem::RemoveMessageHandler(IStylusMessageHandler& MessageHandler)
{
	MessageHandlers.Remove(&MessageHandler);
}

bool UStylusInputSubsystem::IsTickable() const
{
	return UE::StylusInput::Private::bTickStylusInputSubsystem;
}

void UStylusInputSubsystem::Tick(float DeltaTime)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UStylusInputSubsystem::Tick);

	if (InputInterface.IsValid())
	{
		InputInterface->Tick();

		for (int32 DeviceIdx = 0; DeviceIdx < NumInputDevices(); ++DeviceIdx)
		{
			IStylusInputDevice* InputDevice = InputInterface->GetInputDevice(DeviceIdx);
			if (InputDevice->IsDirty())
			{
				InputDevice->Tick();

				for (IStylusMessageHandler* Handler : MessageHandlers)
				{
					Handler->OnStylusStateChanged(InputDevice->GetCurrentState(), DeviceIdx);
				}
			}
		}
	}
}

#undef LOCTEXT_NAMESPACE

PRAGMA_ENABLE_DEPRECATION_WARNINGS
