// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ProfilingDebugging/CsvProfiler.h"
#include "Iris/IrisConfig.h"
#include "Iris/Core/IrisProfiler.h"

CSV_DECLARE_CATEGORY_EXTERN(Iris);

#if UE_NET_IRIS_CSV_STATS
	#define IRIS_CSV_PROFILER_SCOPE(CsvCategory, x) \
	CSV_SCOPED_TIMING_STAT(CsvCategory, x); \
	IRIS_PROFILER_SCOPE(x)
#else
	#define IRIS_CSV_PROFILER_SCOPE(CsvCategory, x) IRIS_PROFILER_SCOPE(x)
#endif

