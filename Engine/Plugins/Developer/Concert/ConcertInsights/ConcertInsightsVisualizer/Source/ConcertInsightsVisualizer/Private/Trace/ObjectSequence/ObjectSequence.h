// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Trace/Types/SequenceId.h"

namespace UE::ConcertInsightsVisualizer
{
	/**
	 * A sequence is a time range in which an object was processed across multiple endpoints.
	 * This data is displayed top-most event.
	 */
	struct FObjectSequence
	{
		/** ID for the object update. */
		FSequenceId SequenceId;
	};
}
