// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "HAL/Platform.h"

namespace UE::VCamCore
{
/** Result of UVCamOutputProviderBase::PreReapplyViewport */
enum class EViewportChangeReply : uint8
{
	/**
	 * Returned by PreReapplyViewport that the subclass wants the entire output provider to be reinitialized.
	 * This could be returned e.g. because changing the viewport while outputting is not supported by this implementation.
	 * Do not call PostReapplyViewport after reinitialization is performed.
	 */
	Reinitialize,
	/** The viewport change will be processed by the implementation. Continue reapplying the output widget to the new target viewport and then call PostReapplyViewport.*/
	ApplyViewportChange
};
}