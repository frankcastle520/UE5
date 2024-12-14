// Copyright Epic Games, Inc. All Rights Reserved.

#include "D3D12GPUProfiler.h"
#include "D3D12Adapter.h"
#include "D3D12Device.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"

int32 GD3D12RHIStablePowerState = 0;
static FAutoConsoleVariableRef CVarD3D12RHIStablePowerState(
	TEXT("D3D12.StablePowerState"),
	GD3D12RHIStablePowerState,
	TEXT("Enable stable power state. This increases GPU timing measurement accuracy but may decrease overall GPU clock rate.\n")
	TEXT("    0 (default): off\n")
	TEXT("    1          : set during profiling\n")
	TEXT("    2          : set on startup\n"),
	ECVF_Default
);

#if (RHI_NEW_GPU_PROFILER == 0)

FD3D12BufferedGPUTiming::FD3D12BufferedGPUTiming(FD3D12Device* InParent)
	: FD3D12DeviceChild(InParent)
{
}

FD3D12BufferedGPUTiming::~FD3D12BufferedGPUTiming() = default;

void FD3D12BufferedGPUTiming::Initialize(FD3D12Adapter* ParentAdapter)
{
	StaticInitialize(ParentAdapter, [](void* UserData)
	{
		// Are the static variables initialized?
		check(!GAreGlobalsInitialized);

		FD3D12Adapter* ParentAdapter = (FD3D12Adapter*)UserData;
		CalibrateTimers(ParentAdapter);
	});
}

void FD3D12BufferedGPUTiming::CalibrateTimers(FD3D12Adapter* ParentAdapter)
{
	for (uint32 GPUIndex : FRHIGPUMask::All())
	{
		FD3D12Device* Device = ParentAdapter->GetDevice(GPUIndex);

		uint64 TimingFrequency = Device->GetTimestampFrequency(ED3D12QueueType::Direct);
		SetTimingFrequency(TimingFrequency, GPUIndex);

		FGPUTimingCalibrationTimestamp CalibrationTimestamp = Device->GetCalibrationTimestamp(ED3D12QueueType::Direct);
		SetCalibrationTimestamp(CalibrationTimestamp, GPUIndex);
	}
}

void FD3D12BufferedGPUTiming::StartTiming()
{
	FD3D12Device* Device = GetParentDevice();
	ID3D12Device* D3DDevice = Device->GetDevice();

	// Issue a timestamp query for the 'start' time.
	if (GIsSupported && !bIsTiming)
	{
		// Check to see if stable power state cvar has changed
		const bool bStablePowerStateCVar = GD3D12RHIStablePowerState != 0;
		if (bStablePowerState != bStablePowerStateCVar)
		{
			if (SUCCEEDED(D3DDevice->SetStablePowerState(bStablePowerStateCVar)))
			{
				// SetStablePowerState succeeded. Update timing frequency.
				uint64 TimingFrequency = Device->GetTimestampFrequency(ED3D12QueueType::Direct);
				SetTimingFrequency(TimingFrequency, Device->GetGPUIndex());
				bStablePowerState = bStablePowerStateCVar;
			}
			else
			{
				// SetStablePowerState failed. This can occur if SDKLayers is not present on the system.
				CVarD3D12RHIStablePowerState->Set(0, ECVF_SetByConsole);
			}
		}

		FD3D12CommandContext& CmdContext = Device->GetDefaultCommandContext();
		CmdContext.InsertTimestamp(ED3D12Units::Raw, &Begin.Result);

		Begin.SyncPoint = CmdContext.GetContextSyncPoint();

		bIsTiming = true;
	}
}

void FD3D12BufferedGPUTiming::EndTiming()
{
	// Issue a timestamp query for the 'end' time.
	if (GIsSupported && bIsTiming)
	{
		FD3D12CommandContext& CmdContext = GetParentDevice()->GetDefaultCommandContext();
		CmdContext.InsertTimestamp(ED3D12Units::Raw, &End.Result);

		End.SyncPoint = CmdContext.GetContextSyncPoint();

		bIsTiming = false;
	}
}

uint64 FD3D12BufferedGPUTiming::GetTiming()
{
	if (End.SyncPoint)
		End.SyncPoint->Wait();

	if (Begin.SyncPoint)
		Begin.SyncPoint->Wait();

	return End.Result >= Begin.Result
		? End.Result - Begin.Result
		: 0;
}

FD3D12EventNode::FD3D12EventNode(const TCHAR* InName, FGPUProfilerEventNode* InParent, FD3D12Device* InParentDevice)
	: FGPUProfilerEventNode(InName, InParent)
	, FD3D12DeviceChild(InParentDevice)
	, Timing(InParentDevice)
{
}

FD3D12EventNode::~FD3D12EventNode() = default;

float FD3D12EventNode::GetTiming()
{
	float Result = 0;

	if (Timing.IsSupported())
	{
		// Get the timing result and block the CPU until it is ready
		const uint64 GPUTiming = Timing.GetTiming();
		const uint64 GPUFreq = Timing.GetTimingFrequency(GetParentDevice()->GetGPUIndex());

		Result = double(GPUTiming) / double(GPUFreq);
	}

	return Result;
}

FD3D12EventNodeFrame::FD3D12EventNodeFrame(FD3D12Device* InParent)
	: FGPUProfilerEventNodeFrame()
	, FD3D12DeviceChild(InParent)
	, RootEventTiming(InParent)
{
}

FD3D12EventNodeFrame::~FD3D12EventNodeFrame() = default;

void FD3D12EventNodeFrame::StartFrame()
{
	EventTree.Reset();
	RootEventTiming.StartTiming();
}

void FD3D12EventNodeFrame::EndFrame()
{
	RootEventTiming.EndTiming();
}

float FD3D12EventNodeFrame::GetRootTimingResults()
{
	double RootResult = 0.0f;
	if (RootEventTiming.IsSupported())
	{
		const uint64 GPUTiming = RootEventTiming.GetTiming();
		const uint64 GPUFreq = RootEventTiming.GetTimingFrequency(GetParentDevice()->GetGPUIndex());

		RootResult = double(GPUTiming) / double(GPUFreq);
	}

	return (float)RootResult;
}

void FD3D12GPUProfiler::BeginFrame()
{
	CurrentEventNode = NULL;
	check(!bTrackingEvents);
	check(!CurrentEventNodeFrame); // this should have already been cleaned up and the end of the previous frame

	// latch the bools from the game thread into our private copy
	bLatchedGProfilingGPU = GTriggerGPUProfile;
	bLatchedGProfilingGPUHitches = GTriggerGPUHitchProfile;
	if (bLatchedGProfilingGPUHitches)
	{
		bLatchedGProfilingGPU = false; // we do NOT permit an ordinary GPU profile during hitch profiles
	}

	// if we are starting a hitch profile or this frame is a gpu profile, then save off the state of the draw events
	if (bLatchedGProfilingGPU || (!bPreviousLatchedGProfilingGPUHitches && bLatchedGProfilingGPUHitches))
	{
		bOriginalGEmitDrawEvents = GetEmitDrawEvents();
	}

	if (bLatchedGProfilingGPU || bLatchedGProfilingGPUHitches)
	{
		if (bLatchedGProfilingGPUHitches && GPUHitchDebounce)
		{
			// if we are doing hitches and we had a recent hitch, wait to recover
			// the reasoning is that collecting the hitch report may itself hitch the GPU
			GPUHitchDebounce--;
		}
		else
		{
			SetEmitDrawEvents(true);  // thwart an attempt to turn this off on the game side
			bTrackingEvents = true;

			CurrentEventNodeFrame = new FD3D12EventNodeFrame(GetParentDevice());
			CurrentEventNodeFrame->StartFrame();
		}
	}
	else if (bPreviousLatchedGProfilingGPUHitches)
	{
		// hitch profiler is turning off, clear history and restore draw events
		GPUHitchEventNodeFrames.Empty();
		SetEmitDrawEvents(bOriginalGEmitDrawEvents);
	}
	bPreviousLatchedGProfilingGPUHitches = bLatchedGProfilingGPUHitches;
}

void FD3D12GPUProfiler::EndFrame()
{
	const uint32 GPUIndex = GetParentDevice()->GetGPUIndex();

	// if we have a frame open, close it now.
	if (CurrentEventNodeFrame)
	{
		CurrentEventNodeFrame->EndFrame();
		Parent->GetDefaultCommandContext().FlushCommands();
	}

	check(!bTrackingEvents || bLatchedGProfilingGPU || bLatchedGProfilingGPUHitches);
	check(!bTrackingEvents || CurrentEventNodeFrame);
	if (bLatchedGProfilingGPU)
	{
		if (bTrackingEvents)
		{
			SetEmitDrawEvents(bOriginalGEmitDrawEvents);

			UE_LOG(LogD3D12RHI, Log, TEXT(""));
			UE_LOG(LogD3D12RHI, Log, TEXT(""));
			GTriggerGPUProfile = false;
			bLatchedGProfilingGPU = false;

			// Only dump the event tree and generate the screenshot for the first GPU.  Eventually, we may want to collate
			// profiling data for all GPUs into a single tree, but the short term goal is to make profiling in the editor
			// functional at all with "-MaxGPUCount=2" (required to enable multiple GPUs for GPU Lightmass).  In the editor,
			// we don't actually render anything on the additional GPUs, but the editor's profile visualizer will pick up
			// whatever event tree we dumped last, which will be the empty one from the last GPU, making the results
			// useless without this code fix.  Unreal Insights would be preferred for multi-GPU profiling outside the editor.
			if (GPUIndex == 0)
			{
				CurrentEventNodeFrame->DumpEventTree();

				if (RHIConfig::ShouldSaveScreenshotAfterProfilingGPU()
					&& GEngine->GameViewport)
				{
					GEngine->GameViewport->Exec(NULL, TEXT("SCREENSHOT"), *GLog);
				}
			}
		}
	}
	else if (bLatchedGProfilingGPUHitches)
	{
		//@todo this really detects any hitch, even one on the game thread.
		// it would be nice to restrict the test to stalls on D3D, but for now...
		// this needs to be out here because bTrackingEvents is false during the hitch debounce
		static double LastTime = -1.0;
		double Now = FPlatformTime::Seconds();
		if (bTrackingEvents)
		{
			/** How long, in seconds a frame much be to be considered a hitch **/
			const float HitchThreshold = RHIConfig::GetGPUHitchThreshold();
			float ThisTime = Now - LastTime;
			bool bHitched = (ThisTime > HitchThreshold) && LastTime > 0.0 && CurrentEventNodeFrame;
			if (bHitched)
			{
				UE_LOG(LogD3D12RHI, Warning, TEXT("*******************************************************************************"));
				UE_LOG(LogD3D12RHI, Warning, TEXT("********** Hitch detected on CPU, frametime = %6.1fms"), ThisTime * 1000.0f);
				UE_LOG(LogD3D12RHI, Warning, TEXT("*******************************************************************************"));

				for (int32 Frame = 0; Frame < GPUHitchEventNodeFrames.Num(); Frame++)
				{
					UE_LOG(LogD3D12RHI, Warning, TEXT(""));
					UE_LOG(LogD3D12RHI, Warning, TEXT(""));
					UE_LOG(LogD3D12RHI, Warning, TEXT("********** GPU Frame: Current - %d"), GPUHitchEventNodeFrames.Num() - Frame);
					GPUHitchEventNodeFrames[Frame].DumpEventTree();
				}
				UE_LOG(LogD3D12RHI, Warning, TEXT(""));
				UE_LOG(LogD3D12RHI, Warning, TEXT(""));
				UE_LOG(LogD3D12RHI, Warning, TEXT("********** GPU Frame: Current"));
				CurrentEventNodeFrame->DumpEventTree();

				UE_LOG(LogD3D12RHI, Warning, TEXT("*******************************************************************************"));
				UE_LOG(LogD3D12RHI, Warning, TEXT("********** End Hitch GPU Profile"));
				UE_LOG(LogD3D12RHI, Warning, TEXT("*******************************************************************************"));
				if (GEngine->GameViewport)
				{
					GEngine->GameViewport->Exec(NULL, TEXT("SCREENSHOT"), *GLog);
				}

				GPUHitchDebounce = 5; // don't trigger this again for a while
				GPUHitchEventNodeFrames.Empty(); // clear history
			}
			else if (CurrentEventNodeFrame) // this will be null for discarded frames while recovering from a recent hitch
			{
				/** How many old frames to buffer for hitch reports **/
				static const int32 HitchHistorySize = 4;

				if (GPUHitchEventNodeFrames.Num() >= HitchHistorySize)
				{
					GPUHitchEventNodeFrames.RemoveAt(0);
				}
				GPUHitchEventNodeFrames.Add((FD3D12EventNodeFrame*)CurrentEventNodeFrame);
				CurrentEventNodeFrame = NULL;  // prevent deletion of this below; ke kept it in the history
			}
		}
		LastTime = Now;
	}
	bTrackingEvents = false;
	delete CurrentEventNodeFrame;
	CurrentEventNodeFrame = NULL;
}

#endif // (RHI_NEW_GPU_PROFILER == 0)
