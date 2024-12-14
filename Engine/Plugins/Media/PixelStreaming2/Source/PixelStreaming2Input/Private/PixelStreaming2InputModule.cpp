// Copyright Epic Games, Inc. All Rights Reserved.

#include "PixelStreaming2InputModule.h"

#include "ApplicationWrapper.h"
#include "CoreUtils.h"
#include "Framework/Application/SlateApplication.h"
#include "InputHandler.h"
#include "IPixelStreaming2HMDModule.h"

namespace UE::PixelStreaming2Input
{

	void FPixelStreaming2InputModule::StartupModule()
	{
		if (!UE::PixelStreaming2::IsStreamingSupported())
		{
			return;
		}

		if (!FSlateApplication::IsInitialized())
		{
			return;
		}

		InputDevice = FInputDevice::GetInputDevice();

		IModularFeatures::Get().RegisterModularFeature(GetModularFeatureName(), this);
	}

	void FPixelStreaming2InputModule::ShutdownModule()
	{
		if (!UE::PixelStreaming2::IsStreamingSupported())
		{
			return;
		}

		IModularFeatures::Get().UnregisterModularFeature(GetModularFeatureName(), this);
	}

	TSharedPtr<IPixelStreaming2InputHandler> FPixelStreaming2InputModule::CreateInputHandler()
	{
		TSharedPtr<FPixelStreaming2ApplicationWrapper> PixelStreamerApplicationWrapper = MakeShareable(new FPixelStreaming2ApplicationWrapper(FSlateApplication::Get().GetPlatformApplication()));
		TSharedPtr<FGenericApplicationMessageHandler>		 BaseHandler = FSlateApplication::Get().GetPlatformApplication()->GetMessageHandler();
		TSharedPtr<IPixelStreaming2InputHandler>		 InputHandler = MakeShared<FPixelStreaming2InputHandler>(PixelStreamerApplicationWrapper, BaseHandler);

		// Add this input handler to the input device's array of handlers. This ensures that it's ticked
		InputDevice->AddInputHandler(InputHandler);

		return InputHandler;
	}

	TSharedPtr<IInputDevice> FPixelStreaming2InputModule::CreateInputDevice(const TSharedRef<FGenericApplicationMessageHandler>& InMessageHandler)
	{
		return InputDevice;
	}
} // namespace UE::PixelStreaming2Input

IMPLEMENT_MODULE(UE::PixelStreaming2Input::FPixelStreaming2InputModule, PixelStreaming2Input)