// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "LowLevelNetTraceModule.h"

#if UE_LOW_LEVEL_NET_TRACE_ENABLED

class ILowLevelNetTrace
{
public:
	virtual ~ILowLevelNetTrace() = default;

	virtual bool UpdateSnapshot( FLowLevelNetTraceSnapshot& OutSnapshot ) = 0;
};

#endif // UE_LOW_LEVEL_NET_TRACE_ENABLED