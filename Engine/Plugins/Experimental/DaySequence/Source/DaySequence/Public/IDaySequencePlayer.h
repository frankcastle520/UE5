// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Misc/QualifiedFrameTime.h"
#include "Templates/SharedPointer.h"

namespace UE::DaySequence
{
class FOverrideUpdateIntervalHandle;
}

class DAYSEQUENCE_API IDaySequencePlayer
{
public:
	
	virtual void Pause() = 0;

	virtual FQualifiedFrameTime GetCurrentTime() const = 0;
	
	virtual FQualifiedFrameTime GetDuration() const = 0;
	
	virtual void SetIgnorePlaybackReplication(bool bState) = 0;

	virtual TSharedPtr<UE::DaySequence::FOverrideUpdateIntervalHandle> GetOverrideUpdateIntervalHandle() = 0;
};