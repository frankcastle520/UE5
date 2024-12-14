// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "UObject/NameTypes.h"
#include "Features/IModularFeature.h"

// IRewindDebuggerRuntimeExtension 
//
// interface class for extensions which add functionality to the rewind debugger
// these get a callback on recording start/stop, to enable/disable trace channels and on clear to clean up any cached data
//

class REWINDDEBUGGERRUNTIMEINTERFACE_API IRewindDebuggerRuntimeExtension : public IModularFeature
{
public:
	virtual ~IRewindDebuggerRuntimeExtension() {}
	
	static const FName ModularFeatureName;

	// called when recording has started
	virtual void RecordingStarted() {};

	// called when recording has ended
	virtual void RecordingStopped() {};
	
	// called when recording is unloaded or a new one starts
	virtual void Clear() {};
};
