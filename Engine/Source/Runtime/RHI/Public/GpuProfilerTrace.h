// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "Trace/Config.h"

#if !defined(GPUPROFILERTRACE_ENABLED)
#if UE_TRACE_ENABLED && !UE_BUILD_SHIPPING
#define GPUPROFILERTRACE_ENABLED 1
#else
#define GPUPROFILERTRACE_ENABLED 0
#endif
#endif

#if GPUPROFILERTRACE_ENABLED

#if RHI_NEW_GPU_PROFILER
// Define this structure here when the new GPU profiler is enabled so we can still build the old trace API.
// @todo - remove this. GPU timestamp calibration is no longer necessary with the new GPU profiler, as the
// platform RHIs are expected to translate timestamps from GPU to CPU clock domain before they reach the profiler.
struct FGPUTimingCalibrationTimestamp
{
	uint64 GPUMicroseconds = 0;
	uint64 CPUMicroseconds = 0;
};
#endif

class FName;

#if RHI_NEW_GPU_PROFILER
// Adds a GPUIndex argument to each function in the API without breaking back compat
#define GPU_TRACE_ARG , uint32 GPUIndex
#else
#define GPU_TRACE_ARG
#endif

struct FGpuProfilerTrace
{
	RHI_API static void BeginFrame(struct FGPUTimingCalibrationTimestamp& Calibration GPU_TRACE_ARG);
	RHI_API static void SpecifyEventByName(const FName& Name GPU_TRACE_ARG);
	RHI_API static void BeginEventByName(const FName& Name, uint32 FrameNumber, uint64 TimestampMicroseconds GPU_TRACE_ARG);
	RHI_API static void EndEvent(uint64 TimestampMicroseconds GPU_TRACE_ARG);
	RHI_API static void EndFrame(uint32 GPUIndex);
	RHI_API static void Deinitialize();
};

#undef GPU_TRACE_ARG

#define TRACE_GPUPROFILER_DEFINE_EVENT_TYPE(Name) \
	FGpuProfilerTrace::FEventType PREPROCESSOR_JOIN(__GGpuProfilerEventType, Name)(TEXT(#Name));

#define TRACE_GPUPROFILER_DECLARE_EVENT_TYPE_EXTERN(Name) \
	extern FGpuProfilerTrace::FEventType PREPROCESSOR_JOIN(__GGpuProfilerEventType, Name);

#define TRACE_GPUPROFILER_EVENT_TYPE(Name) \
	&PREPROCESSOR_JOIN(__GGpuProfilerEventType, Name)

#define TRACE_GPUPROFILER_BEGIN_FRAME() \
	FGpuProfilerTrace::BeginFrame();

#define TRACE_GPUPROFILER_BEGIN_EVENT(EventType, FrameNumber, TimestampMicroseconds) \
	FGpuProfilerTrace::BeginEvent(EventType, FrameNumber, TimestampMicroseconds);

#define TRACE_GPUPROFILER_END_EVENT(TimestampMicroseconds) \
	FGpuProfilerTrace::EndEvent(TimestampMicroseconds);

#define TRACE_GPUPROFILER_END_FRAME() \
	FGpuProfilerTrace::EndFrame();

#define TRACE_GPUPROFILER_DEINITIALIZE() \
	FGpuProfilerTrace::Deinitialize();

#else

struct FGpuProfilerTrace
{
	struct FEventType
	{
		FEventType(const TCHAR* Name) {};
	};
};

#define TRACE_GPUPROFILER_DEFINE_EVENT_TYPE(...)
#define TRACE_GPUPROFILER_DECLARE_EVENT_TYPE_EXTERN(...)
#define TRACE_GPUPROFILER_EVENT_TYPE(...) nullptr
#define TRACE_GPUPROFILER_BEGIN_FRAME(...)
#define TRACE_GPUPROFILER_BEGIN_EVENT(...)
#define TRACE_GPUPROFILER_END_EVENT(...)
#define TRACE_GPUPROFILER_END_FRAME(...)
#define TRACE_GPUPROFILER_DEINITIALIZE(...)

#endif
