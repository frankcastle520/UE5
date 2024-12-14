// Copyright Epic Games, Inc. All Rights Reserved.

#include "EOSOverlayInputProviderPreProcessor.h"

#include "eos_sdk.h"
#include "eos_ui.h"

const TMap<FKey, EOS_UI_EInputStateButtonFlags>& FEOSOverlayInputProviderPreProcessor::GetUEKeyToEOSKeyMap()
{
	static const TMap<FKey, EOS_UI_EInputStateButtonFlags> UEKeyToEOSKeyMap = []()
		{
			return TMap<FKey, EOS_UI_EInputStateButtonFlags>(
				{
					{ EKeys::Gamepad_DPad_Down,			EOS_UI_EInputStateButtonFlags::EOS_UISBF_DPad_Down			},
					{ EKeys::Gamepad_DPad_Left,			EOS_UI_EInputStateButtonFlags::EOS_UISBF_DPad_Left			},
					{ EKeys::Gamepad_DPad_Right,		EOS_UI_EInputStateButtonFlags::EOS_UISBF_DPad_Right			},
					{ EKeys::Gamepad_DPad_Up,			EOS_UI_EInputStateButtonFlags::EOS_UISBF_DPad_Up			},
					{ EKeys::Gamepad_FaceButton_Bottom,	EOS_UI_EInputStateButtonFlags::EOS_UISBF_FaceButton_Bottom	},
					{ EKeys::Gamepad_FaceButton_Left,	EOS_UI_EInputStateButtonFlags::EOS_UISBF_FaceButton_Left	},
					{ EKeys::Gamepad_FaceButton_Right,	EOS_UI_EInputStateButtonFlags::EOS_UISBF_FaceButton_Right	},
					{ EKeys::Gamepad_FaceButton_Top,	EOS_UI_EInputStateButtonFlags::EOS_UISBF_FaceButton_Top		},
					{ EKeys::Gamepad_LeftShoulder,		EOS_UI_EInputStateButtonFlags::EOS_UISBF_LeftShoulder		},
					{ EKeys::Gamepad_LeftThumbstick,	EOS_UI_EInputStateButtonFlags::EOS_UISBF_LeftThumbstick		},
					{ EKeys::Gamepad_LeftTrigger,		EOS_UI_EInputStateButtonFlags::EOS_UISBF_LeftTrigger		},
					{ EKeys::Gamepad_RightShoulder,		EOS_UI_EInputStateButtonFlags::EOS_UISBF_RightShoulder		},
					{ EKeys::Gamepad_RightThumbstick,	EOS_UI_EInputStateButtonFlags::EOS_UISBF_RightThumbstick	},
					{ EKeys::Gamepad_RightTrigger,		EOS_UI_EInputStateButtonFlags::EOS_UISBF_RightTrigger		},
					{ EKeys::Gamepad_Special_Left,		EOS_UI_EInputStateButtonFlags::EOS_UISBF_Special_Left		},
					{ EKeys::Gamepad_Special_Right,		EOS_UI_EInputStateButtonFlags::EOS_UISBF_Special_Right		}
				});
		}();

	return UEKeyToEOSKeyMap;
}

void FEOSOverlayInputProviderPreProcessor::Initialize()
{
	// If we have not bound a DisplaySettingsUpdated delegate for each active platform, we do. It will tell us if we should be consuming input or not.
	TArray<IEOSPlatformHandlePtr> ActivePlatforms = IEOSSDKManager::Get()->GetActivePlatforms();
	for (const IEOSPlatformHandlePtr& ActivePlatform : ActivePlatforms)
	{
		OnPlatformCreated(ActivePlatform);
	}

	IEOSSDKManager::Get()->OnPlatformCreated.AddSP(this, &FEOSOverlayInputProviderPreProcessor::OnPlatformCreated);

	IEOSSDKManager::Get()->OnPreReleasePlatform.AddSP(this, &FEOSOverlayInputProviderPreProcessor::OnPreReleasePlatform);
}

void FEOSOverlayInputProviderPreProcessor::OnPlatformCreated(const IEOSPlatformHandlePtr& PlatformHandlePtr)
{
	if (EOS_HUI UIHandle = EOS_Platform_GetUIInterface(*PlatformHandlePtr))
	{
		EOS_HPlatform PlatformHandle = *PlatformHandlePtr;

		// Adding subscription to external ui display change event
		EOS_UI_AddNotifyDisplaySettingsUpdatedOptions Options = {};
		Options.ApiVersion = 1;
		UE_EOS_CHECK_API_MISMATCH(EOS_UI_ADDNOTIFYDISPLAYSETTINGSUPDATED_API_LATEST, 1);

		if (!DisplaySettingsUpdatedCallback.IsValid())
		{
			TUniquePtr<FOnDisplaySettingsUpdatedCallback> CallbackObj = MakeUnique<FOnDisplaySettingsUpdatedCallback>(AsWeak());
			CallbackObj->CallbackLambda = [this](const EOS_UI_OnDisplaySettingsUpdatedCallbackInfo* Data)
				{
					bIsExclusiveInput = (bool)Data->bIsExclusiveInput;
				};

			DisplaySettingsUpdatedCallback = MoveTemp(CallbackObj);
		}		

		DisplaySettingsUpdatedIdPerPlatform.Emplace(PlatformHandle, EOS_UI_AddNotifyDisplaySettingsUpdated(UIHandle, &Options, DisplaySettingsUpdatedCallback.Get(), DisplaySettingsUpdatedCallback->GetCallbackPtr()));
	}
}

void FEOSOverlayInputProviderPreProcessor::OnPreReleasePlatform(const EOS_HPlatform& PlatformHandle)
{
	DisplaySettingsUpdatedIdPerPlatform.Remove(PlatformHandle);
}

void FEOSOverlayInputProviderPreProcessor::Tick(const float DeltaTime, FSlateApplication& SlateApp, TSharedRef<ICursor> Cursor)
{
	
}

bool FEOSOverlayInputProviderPreProcessor::HandleKeyDownEvent(FSlateApplication& SlateApp, const FKeyEvent& InKeyEvent)
{
	FEOSInputState& CurrentInputState = GetCurrentInputState((uint32_t)InKeyEvent.GetUserIndex());

	FEOSInputState NewInputState = CurrentInputState;

	if (const EOS_UI_EInputStateButtonFlags* ButtonFlag = GetUEKeyToEOSKeyMap().Find(InKeyEvent.GetKey()))
	{
		NewInputState.ButtonDownFlags |= *ButtonFlag;
	}

	return ProcessInputEvent(NewInputState);
}

bool FEOSOverlayInputProviderPreProcessor::HandleKeyUpEvent(FSlateApplication& SlateApp, const FKeyEvent& InKeyEvent)
{
	FEOSInputState& CurrentInputState = GetCurrentInputState((uint32_t)InKeyEvent.GetUserIndex());

	FEOSInputState NewInputState = CurrentInputState;

	if (const EOS_UI_EInputStateButtonFlags* ButtonFlag = GetUEKeyToEOSKeyMap().Find(InKeyEvent.GetKey()))
	{
		NewInputState.ButtonDownFlags ^= *ButtonFlag;
	}

	return ProcessInputEvent(NewInputState);
}

bool FEOSOverlayInputProviderPreProcessor::HandleMouseButtonDownEvent(FSlateApplication& SlateApp, const FPointerEvent& MouseEvent)
{
	FEOSInputState& CurrentInputState = GetCurrentInputState((uint32_t)MouseEvent.GetUserIndex());

	FEOSInputState NewInputState = CurrentInputState;

	NewInputState
		.WithMouseButtonDown(true) // We hardcode this instead of using MouseEvent.IsMouseButtonDown(EKeys::LeftMouseButton) because it did not return false on the ButtonUpEvent
		.WithMousePosX((uint32_t)MouseEvent.GetScreenSpacePosition().X)
		.WithMousePosY((uint32_t)MouseEvent.GetScreenSpacePosition().Y);

	return ProcessInputEvent(NewInputState);
}

bool FEOSOverlayInputProviderPreProcessor::HandleMouseButtonUpEvent(FSlateApplication& SlateApp, const FPointerEvent& MouseEvent)
{
	FEOSInputState& CurrentInputState = GetCurrentInputState((uint32_t)MouseEvent.GetUserIndex());

	FEOSInputState NewInputState = CurrentInputState;

	NewInputState
		.WithMouseButtonDown(false) // We hardcode this instead of using MouseEvent.IsMouseButtonDown(EKeys::LeftMouseButton) because it did not return false on the ButtonUpEvent
		.WithMousePosX((uint32_t)MouseEvent.GetScreenSpacePosition().X)
		.WithMousePosY((uint32_t)MouseEvent.GetScreenSpacePosition().Y);

	return ProcessInputEvent(NewInputState);
}

// We don't want any other type of input to be transmitted

bool FEOSOverlayInputProviderPreProcessor::HandleAnalogInputEvent(FSlateApplication& SlateApp, const FAnalogInputEvent& InAnalogInputEvent)
{
	return bIsExclusiveInput;
}

bool FEOSOverlayInputProviderPreProcessor::HandleMouseMoveEvent(FSlateApplication& SlateApp, const FPointerEvent& MouseEvent)
{
	return bIsExclusiveInput;
}

bool FEOSOverlayInputProviderPreProcessor::HandleMouseButtonDoubleClickEvent(FSlateApplication& SlateApp, const FPointerEvent& MouseEvent)
{
	return bIsExclusiveInput;
}

bool FEOSOverlayInputProviderPreProcessor::HandleMouseWheelOrGestureEvent(FSlateApplication& SlateApp, const FPointerEvent& InWheelEvent, const FPointerEvent* InGestureEvent)
{
	return bIsExclusiveInput;
}

bool FEOSOverlayInputProviderPreProcessor::HandleMotionDetectedEvent(FSlateApplication& SlateApp, const FMotionEvent& MotionEvent)
{
	return bIsExclusiveInput;
}

bool FEOSOverlayInputProviderPreProcessor::ProcessInputEvent(const FEOSInputState& NewInputState)
{
	HandleInput(NewInputState);

	return bIsExclusiveInput;
}

FEOSInputState& FEOSOverlayInputProviderPreProcessor::GetCurrentInputState(uint32_t GamepadIndex)
{
	if (FEOSInputState* CurrentInputState = CurrentInputStates.Find(GamepadIndex))
	{
		return *CurrentInputState;
	}
	else
	{
		return CurrentInputStates.Add(GamepadIndex).WithGamepadIndex(GamepadIndex);
	}
}

void FEOSOverlayInputProviderPreProcessor::HandleInput(const FEOSInputState& NewInputState)
{
	if (bIsReportInputStateSupported)
	{
		FEOSInputState& CurrentInputState = GetCurrentInputState(NewInputState.GamepadIndex);

		const bool bButtonEvent = (
			CurrentInputState.bAcceptIsFaceButtonRight != NewInputState.bAcceptIsFaceButtonRight ||
			CurrentInputState.ButtonDownFlags != NewInputState.ButtonDownFlags
			);

		const bool bMouseEvent = (
			CurrentInputState.bMouseButtonDown != NewInputState.bMouseButtonDown ||
			CurrentInputState.MousePosX != NewInputState.MousePosX ||
			CurrentInputState.MousePosY != NewInputState.MousePosY
			);

		// Only send if mouse or button changed
		if (bMouseEvent || bButtonEvent)
		{
			CurrentInputState = NewInputState;

			bool bIsReportInputStateSupportedInAnyPlatform = false;
			TArray<IEOSPlatformHandlePtr> ActivePlatforms = IEOSSDKManager::Get()->GetActivePlatforms();
			for (const IEOSPlatformHandlePtr& ActivePlatform : ActivePlatforms)
			{
				if (EOS_HUI UIHandle = EOS_Platform_GetUIInterface(*ActivePlatform))
				{
					bIsReportInputStateSupportedInAnyPlatform |= EOS_UI_ReportInputState(UIHandle, &NewInputState) != EOS_EResult::EOS_NotImplemented;
				}
			}
			bIsReportInputStateSupported = bIsReportInputStateSupportedInAnyPlatform;
		}
	}
}