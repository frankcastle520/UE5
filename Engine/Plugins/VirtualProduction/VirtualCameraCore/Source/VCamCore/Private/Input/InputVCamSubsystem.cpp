// Copyright Epic Games, Inc. All Rights Reserved.

#include "Input/InputVCamSubsystem.h"

#include "EnhancedInputDeveloperSettings.h"
#include "Input/VCamPlayerInput.h"
#include "LogVCamCore.h"
#include "VCamComponent.h"
#include "VCamInputProcessor.h"

#include "Components/InputComponent.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/ConsoleManager.h"
#include "UserSettings/EnhancedInputUserSettings.h"
#if WITH_EDITOR
#include "Settings/LevelEditorViewportSettings.h"
#endif

namespace UE::VCamCore
{
#if WITH_EDITOR
	/** When positive, we override editor input behavior. Upon becoming zero, the editor behavior is restored. Never negative. */
	static int32 GVCamInputSubsystemCount = 0;
	/** Input settings that we override in order for VCam to function properly. Restored once all VCams shut down. */
	static struct FEditorBehaviorSnapshot
	{
		/** We set Slate.EnableGamepadEditorNavigation to false because it navigates through editor tabs using joystick. */
		bool bEnableGamepadEditorNavigation = true;
		/** We set ULevelEditorViewportSettings::bLevelEditorJoystickControls so viewport joystick controls do not override VCam. */
		bool bLevelEditorJoystickControls = true;
	} GEditorBehaviorSnapshot;

	static void IncrementAndOverrideEditorBehavior()
	{
		++GVCamInputSubsystemCount;
	
		// Use-case for the below cases: Person A using gamepad to drive VCam input while Person B clicks stuff in editor.
		
		// Gamepad may start navigating editor widgets. This CVar prevents that.
		if (IConsoleVariable* ConsoleVariable = IConsoleManager::Get().FindConsoleVariable(TEXT("Slate.EnableGamepadEditorNavigation")))
		{
			if (GVCamInputSubsystemCount == 1)
			{
				GEditorBehaviorSnapshot.bEnableGamepadEditorNavigation = ConsoleVariable->GetBool();
			}
		
			ConsoleVariable->Set(false);
		}

		// While viewport is focused, FEditorViewportClient::UpdateCameraMovementFromJoystick overrides changes VCam makes with the VCam.
		ULevelEditorViewportSettings* ViewportSettings = GetMutableDefault<ULevelEditorViewportSettings>();
		if (GVCamInputSubsystemCount == 1)
		{
			GEditorBehaviorSnapshot.bLevelEditorJoystickControls = ViewportSettings->bLevelEditorJoystickControls;
		}
		ViewportSettings->bLevelEditorJoystickControls = false;
	}

	static void DecrementAndRestoreEditorBehavior()
	{
		--GVCamInputSubsystemCount;

		if (GVCamInputSubsystemCount == 0)
		{
			if (IConsoleVariable* ConsoleVariable = IConsoleManager::Get().FindConsoleVariable(TEXT("Slate.EnableGamepadEditorNavigation")))
			{
				ConsoleVariable->Set(GEditorBehaviorSnapshot.bEnableGamepadEditorNavigation);
			}

			GetMutableDefault<ULevelEditorViewportSettings>()->bLevelEditorJoystickControls = GEditorBehaviorSnapshot.bLevelEditorJoystickControls;
		}
	}
#endif
}

void UInputVCamSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	UE_LOG(LogVCamCore, Log, TEXT("Initializing UInputVCamSubsystem..."));
	
	PlayerInput = NewObject<UVCamPlayerInput>(this);
	
	// Create and register the input preprocessor, this is what will call our "InputKey"
	// function to drive input instead of a player controller
	if (FSlateApplication::IsInitialized())
	{
		// It's dangerous to consume input in editor (imagine typing something into search boxes but all L keys were consumed by VCam input)
		// whereas probably expected by gameplay code.
		using namespace UE::VCamCore;
		InputPreprocessor = MakeShared<FVCamInputProcessor>(*this);
		FSlateApplication::Get().RegisterInputPreProcessor(InputPreprocessor, 0);

		// The below things should only be done in Slate applications. Slate is disabled e.g. in commandlets. It makes no sense to have VCam input in such cases.
#if WITH_EDITOR
		UE::VCamCore::IncrementAndOverrideEditorBehavior();
#endif
		
		if (GetDefault<UEnhancedInputDeveloperSettings>()->bEnableUserSettings)
		{
			InitalizeUserSettings();
		}
	}
}

void UInputVCamSubsystem::Deinitialize()
{
	Super::Deinitialize();
	UE_LOG(LogVCamCore, Log, TEXT("De-initializing UInputVCamSubsystem..."));

	if (FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().UnregisterInputPreProcessor(InputPreprocessor);
		InputPreprocessor.Reset(); // UObject will still around until GC'ed. No point in keeping the InputProcessor around.
		
		PlayerInput = nullptr;

#if WITH_EDITOR
		UE::VCamCore::DecrementAndRestoreEditorBehavior();
#endif
	}
}

void UInputVCamSubsystem::InitalizeUserSettings()
{
	UserSettings = NewObject<UEnhancedInputUserSettings>(this, TEXT("UserSettings"), RF_Transient);
	// UEnhancedInputUserSettings's API is designed to work with ULocalPlayers. However, we won't be making any calls to functions that internally call GetOwningPlayer().
	ULocalPlayer* LocalPlayerHack = GetMutableDefault<ULocalPlayer>();
	UserSettings->Initialize(LocalPlayerHack);
	BindUserSettingDelegates();
}

void UInputVCamSubsystem::OnUpdate(float DeltaTime)
{
	if (!ensure(PlayerInput))
	{
		return;
	}

	TArray<UInputComponent*> InputStack;
	for (auto It = CurrentInputStack.CreateIterator(); It; ++It)
	{
		if (UInputComponent* InputComponent = It->Get())
		{
			InputStack.Push(InputComponent);
		}
		else
		{
			It.RemoveCurrent();
		}
	}
	
	PlayerInput->Tick(DeltaTime);
	PlayerInput->ProcessInputStack(InputStack, DeltaTime, false);
}

bool UInputVCamSubsystem::InputKey(const FInputKeyParams& Params)
{
	// UVCamComponent::Update causes UInputVCamSubsystem::OnUpdate to be called.
	// If CanUpdate tells us that won't be called, no input should be enqueued.
	// If it was, then the next time an Update occurs, there would be an "explosion" of processed, accumulated, outdated inputs.
	return GetVCamComponent()->CanUpdate()
		&& PlayerInput->InputKey(Params);
}

void UInputVCamSubsystem::PushInputComponent(UInputComponent* InInputComponent)
{
	if (!ensureAlways(InInputComponent))
	{
		return;
	}
	
	bool bPushed = false;
	CurrentInputStack.RemoveSingle(InInputComponent);
	for (int32 Index = CurrentInputStack.Num() - 1; Index >= 0; --Index)
	{
		UInputComponent* IC = CurrentInputStack[Index].Get();
		if (IC == nullptr)
		{
			CurrentInputStack.RemoveAt(Index);
		}
		else if (IC->Priority <= InInputComponent->Priority)
		{
			CurrentInputStack.Insert(InInputComponent, Index + 1);
			bPushed = true;
			break;
		}
	}
	if (!bPushed)
	{
		CurrentInputStack.Insert(InInputComponent, 0);
		RequestRebuildControlMappings();
	}
}

bool UInputVCamSubsystem::PopInputComponent(UInputComponent* InInputComponent)
{
	if (ensure(InInputComponent) && CurrentInputStack.RemoveSingle(InInputComponent) > 0)
	{
		InInputComponent->ClearBindingValues();
		RequestRebuildControlMappings();
		return true;
	}
	return false;
}

const FVCamInputDeviceConfig& UInputVCamSubsystem::GetInputSettings() const
{
	// Undefined behaviour returning from dereferenced nullptr, let's make sure to assert.
	UE_CLOG(PlayerInput == nullptr, LogVCamCore, Fatal, TEXT("PlayerInput is designed to exist for the lifetime of UInputVCamSubsystem. Investigate!"));
	return PlayerInput->GetInputSettings();
}

void UInputVCamSubsystem::SetInputSettings(const FVCamInputDeviceConfig& Input)
{
	check(PlayerInput);
	PlayerInput->SetInputSettings(Input);
}

UEnhancedPlayerInput* UInputVCamSubsystem::GetPlayerInput() const
{
	return PlayerInput;
}
