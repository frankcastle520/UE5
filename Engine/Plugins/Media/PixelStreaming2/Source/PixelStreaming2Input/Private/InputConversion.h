// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/Map.h"
#include "InputCoreTypes.h"
#include "PixelStreaming2HMDEnums.h"
#include "PixelStreaming2InputEnums.h"
#include "GenericPlatform/GenericApplicationMessageHandler.h"

namespace UE::PixelStreaming2Input
{

	struct FInputConverter
	{
	public:
		static TMap<TTuple<EPixelStreaming2XRSystem, EControllerHand, uint8, EPixelStreaming2InputAction>, FKey> XRInputToFKey;
		static TMap<TTuple<uint8, EPixelStreaming2InputAction>, FKey>													 GamepadInputToFKey;
	};

} // namespace UE::PixelStreaming2Input