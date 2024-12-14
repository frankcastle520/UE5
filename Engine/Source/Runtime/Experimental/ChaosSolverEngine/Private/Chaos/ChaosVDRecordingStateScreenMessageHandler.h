// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if WITH_CHAOS_VISUAL_DEBUGGER

#include "Delegates/IDelegateInstance.h"
#include "HAL/Platform.h"

class UGameInstance;

/** Helper object that adds a Recording in progress message in the viewport when a CVD recording is active and we are Playing or Simulation*/
class FChaosVDRecordingStateScreenMessageHandler
{
public:

	static FChaosVDRecordingStateScreenMessageHandler& Get();

	void Initialize();
	void TearDown();

private:
	void AddOnScreenRecordingMessage();
	void RemoveOnScreenRecordingMessage();
	void HandleCVDRecordingStarted();
	void HandleCVDRecordingStopped();
	void HandleCVDRecordingStartFailed(const FText& InFailureReason) const;
	void HandlePIEStarted(UGameInstance* GameInstance);

	void SerializeCollisionChannelsNames();

	FDelegateHandle RecordingStartedHandle;
	FDelegateHandle RecordingStoppedHandle;
	FDelegateHandle RecordingStartFailedHandle;
	uint64 CVDRecordingMessageKey = 0;

#if WITH_EDITOR
	FDelegateHandle PIEStartedHandle;
#endif
};

#endif
