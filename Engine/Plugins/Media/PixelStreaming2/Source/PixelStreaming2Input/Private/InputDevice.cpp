// Copyright Epic Games, Inc. All Rights Reserved.

#include "InputDevice.h"
#include "Framework/Application/SlateApplication.h"

TSharedPtr<UE::PixelStreaming2Input::FInputDevice> UE::PixelStreaming2Input::FInputDevice::InputDevice;

namespace UE::PixelStreaming2Input
{
	TSharedPtr<FInputDevice> FInputDevice::GetInputDevice()
	{
		if (InputDevice)
		{
			return InputDevice;
		}

		TSharedPtr<FInputDevice> Device = TSharedPtr<FInputDevice>(new FInputDevice());
		if (Device)
		{
			InputDevice = Device;
		}
		return InputDevice;
	}

	FInputDevice::FInputDevice()
	{
		// This is imperative for editor streaming as when a modal is open or we've hit a BP breakpoint, the engine tick loop will not run, so instead we rely on this delegate to tick for us
		FSlateApplication::Get().OnPreTick().AddRaw(this, &FInputDevice::Tick);
	}

	void FInputDevice::AddInputHandler(TSharedPtr<IPixelStreaming2InputHandler> InputHandler)
	{
		InputHandlers.Add(InputHandler);
	}

	void FInputDevice::Tick(float DeltaTime)
	{
		for (TWeakPtr<IPixelStreaming2InputHandler> WeakHandler : InputHandlers)
		{
			if (TSharedPtr<IPixelStreaming2InputHandler> Handler = WeakHandler.Pin())
			{
				Handler->Tick(DeltaTime);
			}
		}
	}

	void FInputDevice::SetMessageHandler(const TSharedRef<FGenericApplicationMessageHandler>& InTargetHandler)
	{
		for (TWeakPtr<IPixelStreaming2InputHandler> WeakHandler : InputHandlers)
		{
			if (TSharedPtr<IPixelStreaming2InputHandler> Handler = WeakHandler.Pin())
			{
				Handler->SetMessageHandler(InTargetHandler);
			}
		}
	}

	bool FInputDevice::Exec(UWorld* InWorld, const TCHAR* Cmd, FOutputDevice& Ar)
	{
		bool bRetVal = true;
		for (TWeakPtr<IPixelStreaming2InputHandler> WeakHandler : InputHandlers)
		{
			if (TSharedPtr<IPixelStreaming2InputHandler> Handler = WeakHandler.Pin())
			{
				bRetVal &= Handler->Exec(InWorld, Cmd, Ar);
			}
		}
		return bRetVal;
	}

	void FInputDevice::SetChannelValue(int32 ControllerId, FForceFeedbackChannelType ChannelType, float Value)
	{
		for (TWeakPtr<IPixelStreaming2InputHandler> WeakHandler : InputHandlers)
		{
			if (TSharedPtr<IPixelStreaming2InputHandler> Handler = WeakHandler.Pin())
			{
				Handler->SetChannelValue(ControllerId, ChannelType, Value);
			}
		}
	}

	void FInputDevice::SetChannelValues(int32 ControllerId, const FForceFeedbackValues& Values)
	{
		for (TWeakPtr<IPixelStreaming2InputHandler> WeakHandler : InputHandlers)
		{
			if (TSharedPtr<IPixelStreaming2InputHandler> Handler = WeakHandler.Pin())
			{
				Handler->SetChannelValues(ControllerId, Values);
			}
		}
	}

	uint8 FInputDevice::OnControllerConnected()
	{
		uint8 NextControllerId = 0;
		while (ConnectedControllers.Contains(NextControllerId))
		{
			NextControllerId++;
		}

		ConnectedControllers.Add(NextControllerId);
		return NextControllerId;
	}

	void FInputDevice::OnControllerDisconnected(uint8 DeleteControllerId)
	{
		ConnectedControllers.Remove(DeleteControllerId);
	}
} // namespace UE::PixelStreaming2Input
