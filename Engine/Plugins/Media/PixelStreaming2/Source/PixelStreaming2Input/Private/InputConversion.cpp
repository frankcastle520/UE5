// Copyright Epic Games, Inc. All Rights Reserved.

#include "InputConversion.h"

namespace UE::PixelStreaming2Input
{

	TMap<TTuple<EPixelStreaming2XRSystem, EControllerHand, uint8, EPixelStreaming2InputAction>, FKey> FInputConverter::XRInputToFKey;
	TMap<TTuple<uint8, EPixelStreaming2InputAction>, FKey>												  FInputConverter::GamepadInputToFKey;

} // namespace UE::PixelStreaming2Input
