// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	TaskGraphFwd.h: TaskGraph library
=============================================================================*/

#pragma once

#include "TaskGraphDefinitions.h"
#include "Templates/RefCounting.h"

class FBaseGraphTask;

#if TASKGRAPH_NEW_FRONTEND

using FGraphEvent = FBaseGraphTask;
using FGraphEventRef = TRefCountPtr<FBaseGraphTask>;

#else

/** Convenience typedef for a reference counted pointer to a graph event **/
using FGraphEventRef = TRefCountPtr<class FGraphEvent>;

#endif
