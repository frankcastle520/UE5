// Copyright Epic Games, Inc. All Rights Reserved.

#include "ConsoleVariables.h"

namespace UE::Sequencer
{
	TAutoConsoleVariable<bool> CVarEnableRelevantThumbnails(
		TEXT("Sequencer.EnableRelevantThumbnails"),
		false,
		TEXT("If enabled, upon saving a sequence, tries to find a suitable frame, scrubs to it, renders a thumbnail, and saves it as thumbnail.")
	);
}