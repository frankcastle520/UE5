// Copyright Epic Games, Inc. All Rights Reserved.

#include "RewindDebuggerChooserRuntime.h"

void FRewindDebuggerChooserRuntime::RecordingStarted()
{
	UE::Trace::ToggleChannel(TEXT("Chooser"), true);
}

void FRewindDebuggerChooserRuntime::RecordingStopped()
{
	UE::Trace::ToggleChannel(TEXT("Chooser"), false);
}