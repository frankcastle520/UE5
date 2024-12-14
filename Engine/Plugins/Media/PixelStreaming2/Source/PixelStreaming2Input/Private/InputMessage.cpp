// Copyright Epic Games, Inc. All Rights Reserved.

#include "InputMessage.h"
#include "PixelStreaming2InputEnums.h"

namespace UE::PixelStreaming2Input
{

	FInputMessage::FInputMessage(uint8 InId)
		: Id(InId)
		, Structure({})
	{
	}

	FInputMessage::FInputMessage(uint8 InId, TArray<EPixelStreaming2MessageTypes> InStructure)
		: Id(InId)
		, Structure(InStructure)
	{
	}

} // namespace UE::PixelStreaming2Input