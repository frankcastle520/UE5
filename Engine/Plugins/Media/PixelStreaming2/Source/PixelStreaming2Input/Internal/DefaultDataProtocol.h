// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Templates/SharedPointer.h"
#include "IPixelStreaming2DataProtocol.h"

namespace UE::PixelStreaming2Input
{

	/**
	 * @return The default "ToStreamer" data protocol.
	 */
	PIXELSTREAMING2INPUT_API TSharedPtr<IPixelStreaming2DataProtocol> GetDefaultToStreamerProtocol();

	/**
	 * @return The default "FromStreamer" data protocol.
	 */
	PIXELSTREAMING2INPUT_API TSharedPtr<IPixelStreaming2DataProtocol> GetDefaultFromStreamerProtocol();

} // namespace UE::PixelStreaming2Input