// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "D3D12RHICommon.h"
#include "GPUProfiler.h"

class FD3D12SyncPoint;
using FD3D12SyncPointRef = TRefCountPtr<FD3D12SyncPoint>;

#if (RHI_NEW_GPU_PROFILER == 0)

// This class has multiple inheritance but really FGPUTiming is a static class
class FD3D12BufferedGPUTiming : public FGPUTiming, public FD3D12DeviceChild
{
public:
	FD3D12BufferedGPUTiming(FD3D12Device* InParent);
	~FD3D12BufferedGPUTiming();

	/**
	 * Start a GPU timing measurement.
	 */
	void StartTiming();

	/**
	 * End a GPU timing measurement.
	 * The timing for this particular measurement will be resolved at a later time by the GPU.
	 */
	void EndTiming();

	/**
	* Retrieves the most recently resolved timing measurement.
	* The unit is the same as for FPlatformTime::Cycles(). Returns 0 if there are no resolved measurements.
	*
	* @return	Value of the most recently resolved timing, or 0 if no measurements have been resolved by the GPU yet.
	*/
	uint64 GetTiming();

	static void CalibrateTimers(FD3D12Adapter* ParentAdapter);

	static void Initialize(FD3D12Adapter* ParentAdapter);

private:
	struct
	{
		uint64 Result = 0;
		FD3D12SyncPointRef SyncPoint;
	} Begin, End;

	/** Whether we are currently timing the GPU: between StartTiming() and EndTiming(). */
	bool bIsTiming = false;
	/** Whether stable power state is currently enabled */
	bool bStablePowerState = false;
};


/** A single perf event node, which tracks information about a appBeginDrawEvent/appEndDrawEvent range. */
class FD3D12EventNode : public FGPUProfilerEventNode, public FD3D12DeviceChild
{
public:
	FD3D12EventNode(const TCHAR* InName, FGPUProfilerEventNode* InParent, FD3D12Device* InParentDevice);
	virtual ~FD3D12EventNode();

	/**
	* Returns the time in ms that the GPU spent in this draw event.
	* This blocks the CPU if necessary, so can cause hitching.
	*/
	virtual float GetTiming() override;

	virtual void StartTiming() override
	{
		Timing.StartTiming();
	}

	virtual void StopTiming() override
	{
		Timing.EndTiming();
	}

	FD3D12BufferedGPUTiming Timing;
};

/** An entire frame of perf event nodes, including ancillary timers. */
class FD3D12EventNodeFrame : public FGPUProfilerEventNodeFrame, public FD3D12DeviceChild
{
public:
	FD3D12EventNodeFrame(FD3D12Device* InParent);
	virtual ~FD3D12EventNodeFrame();

	/** Start this frame of per tracking */
	virtual void StartFrame() override;

	/** End this frame of per tracking, but do not block yet */
	virtual void EndFrame() override;

	/** Calculates root timing base frequency (if needed by this RHI) */
	virtual float GetRootTimingResults() override;

	/** Timer tracking inclusive time spent in the root nodes. */
	FD3D12BufferedGPUTiming RootEventTiming;
};

/**
 * Encapsulates GPU profiling logic and data.
 * There's only one global instance of this struct so it should only contain global data, nothing specific to a frame.
 */
struct FD3D12GPUProfiler : public FGPUProfiler, public FD3D12DeviceChild
{
	/** GPU hitch profile histories */
	TIndirectArray<FD3D12EventNodeFrame> GPUHitchEventNodeFrames;

	FD3D12GPUProfiler(FD3D12Device* Parent)
		: FD3D12DeviceChild(Parent)
	{
		BeginFrame();
	}

	virtual FGPUProfilerEventNode* CreateEventNode(const TCHAR* InName, FGPUProfilerEventNode* InParent) override
	{
		FD3D12EventNode* EventNode = new FD3D12EventNode(InName, InParent, GetParentDevice());
		return EventNode;
	}

	void BeginFrame();
	void EndFrame();
};

#endif // (RHI_NEW_GPU_PROFILER == 0)
