// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	GPUProfiler.h: Hierarchical GPU Profiler.
=============================================================================*/

#pragma once

#include "CoreMinimal.h"
#include "Misc/TVariant.h"
#include "ProfilingDebugging/CsvProfiler.h"

#include "RHIBreadcrumbs.h"

#if HAS_GPU_STATS
CSV_DECLARE_CATEGORY_MODULE_EXTERN(RHI_API, GPU);
#endif

#if RHI_NEW_GPU_PROFILER

namespace UE::RHI::GPUProfiler
{
	struct FQueue
	{
		enum class EType : uint8
		{
			Graphics,
			Compute,
			Copy,
			SwapChain
		};

		union
		{
			struct
			{
				EType Type;
				uint8 GPU;
				uint8 Index;
				uint8 Padding;
			};
			uint32 Value = 0;
		};

		FQueue() = default;

		constexpr FQueue(EType Type, uint8 GPU, uint8 Index)
			: Type   (Type)
			, GPU    (GPU)
			, Index  (Index)
			, Padding(0)
		{}

		constexpr bool operator == (FQueue const& RHS) const
		{
			return Value == RHS.Value;
		}

		constexpr bool operator != (FQueue const& RHS) const
		{
			return !(*this == RHS);
		}

		friend uint32 GetTypeHash(FQueue const& Queue)
		{
			return GetTypeHash(Queue.Value);
		}

		TCHAR const* GetTypeString() const
		{
			switch (Type)
			{
			case EType::Graphics:  return TEXT("Graphics");
			case EType::Compute:   return TEXT("Compute");
			case EType::Copy:      return TEXT("Copy");
			case EType::SwapChain: return TEXT("Swapchain");
			default:               return TEXT("<unknown>");
			}
		}
	};

	struct FEvent
	{
		//
		// All timestamps are relative to FPlatformTime::Cycles64().
		// TOP = Top of Pipe. Timestamps written by the GPU's command processor before work begins.
		// BOP = Bottom of Pipe. Timestamps written after the GPU completes work.
		//
		
		// Inserted on each call to RHIEndFrame. Marks the end of a profiler frame.
		struct FFrameBoundary
		{
			// The index of the frame that just ended.
			// Very first frame of the engine is frame 0 (from boot to first call to RHIEndFrame).
			uint32 FrameNumber;

		#if WITH_RHI_BREADCRUMBS
			// The RHI breadcrumb currently at the top of the stack at the frame boundary.
			FRHIBreadcrumbNode* Breadcrumb;
		#endif

			FFrameBoundary(uint32 FrameNumber
			#if WITH_RHI_BREADCRUMBS
				, FRHIBreadcrumbNode* Breadcrumb
			#endif
				)
				: FrameNumber(FrameNumber)
			#if WITH_RHI_BREADCRUMBS
				, Breadcrumb(Breadcrumb)
			#endif
			{}
		};

	#if WITH_RHI_BREADCRUMBS
		struct FBeginBreadcrumb
		{
			FRHIBreadcrumbNode* const Breadcrumb;
			uint64 GPUTimestampTOP;

			FBeginBreadcrumb(FRHIBreadcrumbNode* Breadcrumb, uint64 GPUTimestampTOP = 0)
				: Breadcrumb(Breadcrumb)
				, GPUTimestampTOP(GPUTimestampTOP)
			{}
		};

		struct FEndBreadcrumb
		{
			FRHIBreadcrumbNode* const Breadcrumb;
			uint64 GPUTimestampBOP = 0;

			FEndBreadcrumb(FRHIBreadcrumbNode* Breadcrumb, uint64 GPUTimestampBOP = 0)
				: Breadcrumb(Breadcrumb)
				, GPUTimestampBOP(GPUTimestampBOP)
			{}
		};
	#endif

		// Inserted when the GPU starts work on a queue.
		struct FBeginWork
		{
			// CPU timestamp of when the work was submitted to the driver for execution on the GPU.
			uint64 CPUTimestamp;

			// TOP timestamp of when the work actually started on the GPU.
			uint64 GPUTimestampTOP;

			FBeginWork(uint64 CPUTimestamp, uint64 GPUTimestampTOP = 0)
				: CPUTimestamp(CPUTimestamp)
				, GPUTimestampTOP(GPUTimestampTOP)
			{}
		};

		// Inserted when the GPU completes work on a queue and goes idle.
		struct FEndWork
		{
			uint64 GPUTimestampBOP;

			FEndWork(uint64 GPUTimestampBOP = 0)
				: GPUTimestampBOP(GPUTimestampBOP)
			{}
		};

		struct FStats
		{
			uint32 NumDraws;
			uint32 NumPrimitives;

			operator bool() const
			{
				return NumDraws > 0 || NumPrimitives > 0;
			}
		};

		// Can only be inserted when the GPU is marked "idle", i.e. after an FEndWork event.
		struct FSignalFence
		{
			//
			// Timestamp when the fence signal was enqueued to the GPU/driver.
			// 
			// The signal on the GPU doesn't happen until after the previous FEndWork
			// event's BOP timestamp, or this CPU timestamp, whichever is later.
			//
			uint64 CPUTimestamp;

			// Unique ID of the fence signaled.
			uint64 ID;

			// The fence value signaled.
			uint64 Value;

			FSignalFence(uint64 CPUTimestamp, uint64 ID, uint64 Value)
				: CPUTimestamp(CPUTimestamp)
				, ID(ID)
				, Value(Value)
			{}
		};

		// Can only be inserted when the GPU is marked "idle", i.e. after an FEndWork event.
		struct FWaitFence
		{
			// Timestamp when the fence wait was enqueued to the GPU/driver.
			uint64 CPUTimestamp;

			// Unique ID of the fence awaited.
			uint64 ID;

			// The fence value awaited.
			uint64 Value;

			FWaitFence(uint64 CPUTimestamp, uint64 ID, uint64 Value)
				: CPUTimestamp(CPUTimestamp)
				, ID(ID)
				, Value(Value)
			{}
		};

		struct FFlip
		{
			uint64 GPUTimestamp;
		};

		struct FVsync
		{
			uint64 GPUTimestamp;
		};
		
		using FStorage = TVariant<
			  FFrameBoundary
		#if WITH_RHI_BREADCRUMBS
			, FBeginBreadcrumb
			, FEndBreadcrumb
		#endif
			, FBeginWork
			, FEndWork
			, FStats
			, FSignalFence
			, FWaitFence
			, FFlip
			, FVsync
		>;

		enum class EType
		{
			FrameBoundary   = FStorage::IndexOfType<FFrameBoundary  >(),
		#if WITH_RHI_BREADCRUMBS
			BeginBreadcrumb = FStorage::IndexOfType<FBeginBreadcrumb>(),
			EndBreadcrumb   = FStorage::IndexOfType<FEndBreadcrumb  >(),
		#endif
			BeginWork       = FStorage::IndexOfType<FBeginWork      >(),
			EndWork         = FStorage::IndexOfType<FEndWork        >(),
			Stats           = FStorage::IndexOfType<FStats          >(),
			SignalFence     = FStorage::IndexOfType<FSignalFence    >(),
			WaitFence       = FStorage::IndexOfType<FWaitFence      >(),
			Flip            = FStorage::IndexOfType<FFlip           >(),
			VSync		    = FStorage::IndexOfType<FVsync          >()
		};

		FStorage Value;

		EType GetType() const
		{
			return static_cast<EType>(Value.GetIndex());
		}

		template <typename T>
		FEvent(T const& Value)
			: Value(TInPlaceType<T>(), Value)
		{}

		FEvent(FEvent const&) = delete;
		FEvent(FEvent&&) = delete;
	};

	class FEventStream
	{
	private:
		struct FChunk
		{
			struct FHeader
			{
				FChunk* Next = nullptr;
				uint32 Num = 0;

			#if WITH_RHI_BREADCRUMBS
				FRHIBreadcrumbAllocatorArray BreadcrumbAllocators;
			#endif
			} Header;

			static constexpr uint32 ChunkSizeInBytes = 16 * 1024;
			static constexpr uint32 RemainingBytes = ChunkSizeInBytes - Align<uint32>(sizeof(FHeader), alignof(FHeader));
			static constexpr uint32 MaxEventsPerChunk = RemainingBytes / Align<uint32>(sizeof(FEvent), alignof(FEvent));

			TStaticArray<TTypeCompatibleBytes<FEvent>, MaxEventsPerChunk> Elements;

			static RHI_API TLockFreePointerListUnordered<void, PLATFORM_CACHE_LINE_SIZE> MemoryPool;

			void* operator new(size_t Size)
			{
				check(Size == sizeof(FChunk));

				void* Memory = MemoryPool.Pop();
				if (!Memory)
				{
					Memory = FMemory::Malloc(sizeof(FChunk), alignof(FChunk));
				}
				return Memory;
			}

			void operator delete(void* Pointer)
			{
				MemoryPool.Push(Pointer);
			}

			FEvent* GetElement(uint32 Index)
			{
				return Elements[Index].GetTypedPtr();
			}
		};

		static_assert(sizeof(FChunk) <= FChunk::ChunkSizeInBytes, "Incorrect FChunk size.");

		FChunk* First = nullptr;
		FChunk* Current = nullptr;

	public:
		FEventStream() = default;
		FEventStream(FEventStream const&) = delete;

		FEventStream(FEventStream&& Other)
			: First(Other.First)
			, Current(Other.Current)
		{
			Other.First = nullptr;
			Other.Current = nullptr;
		}

		~FEventStream()
		{
			while (First)
			{
				FChunk* Next = First->Header.Next;
				delete First;
				First = Next;
			}
		}

		template <typename TEventType, typename... TArgs>
		TEventType& Emplace(TArgs&&... Args)
		{
			static_assert(std::is_trivially_destructible_v<TEventType>, "Destructors are not called on GPU profiler events, so the types must be trivially destructible.");

			if (!Current)
			{
				Current = new FChunk;
				if (!First)
				{
					First = Current;
				}
			}

			if (Current->Header.Num >= FChunk::MaxEventsPerChunk)
			{
				FChunk* NewChunk = new FChunk;
				Current->Header.Next = NewChunk;
				Current = NewChunk;
			}

			FEvent* Event = Current->GetElement(Current->Header.Num++);
			new (Event) FEvent(TEventType(Forward<TArgs>(Args)...));

			TEventType& Data = Event->Value.Get<TEventType>();

		#if WITH_RHI_BREADCRUMBS
			if constexpr (
				std::is_same_v<UE::RHI::GPUProfiler::FEvent::FBeginBreadcrumb, TEventType> ||
				std::is_same_v<UE::RHI::GPUProfiler::FEvent::FEndBreadcrumb  , TEventType>
				)
			{
				// Attach the breadcrumb allocator for begin/end breadcrumb events.
				// This keeps the breadcrumbs alive until the events have been consumed by the profilers.
				Current->Header.BreadcrumbAllocators.AddUnique(Data.Breadcrumb->Allocator);
			}
		#endif

			return Data;
		}

		bool IsEmpty() const
		{
			return First == nullptr;
		}

		void Append(FEventStream&& Other)
		{
			if (IsEmpty())
			{
				Current = Other.Current;
				First = Other.First;
			}
			else if (!Other.IsEmpty())
			{
				Current->Header.Next = Other.First;
				Current = Other.Current;
			}

			Other.Current = nullptr;
			Other.First = nullptr;
		}

		auto begin() const
		{
			class FIterator
			{
				friend FEventStream;

				FChunk* Current;
				uint32 Index = 0;

				FIterator(FChunk* Current)
					: Current(Current)
				{}

			public:
				FIterator& operator++()
				{
					++Index;

					while (Current && Index >= Current->Header.Num)
					{
						Current = Current->Header.Next;
						Index = 0;
					}

					return *this;
				}

				bool operator != (std::nullptr_t) const
				{
					return Current != nullptr;
				}

				FEvent const* operator*() const
				{
					return Current->GetElement(Index);
				}
			};

			return FIterator(First);
		}

		std::nullptr_t end() const
		{
			return nullptr;
		}
	};

	struct FEventSink
	{
	protected:
		RHI_API FEventSink();
		RHI_API ~FEventSink();

		FEventSink(FEventSink const&) = delete;
		FEventSink(FEventSink&&) = delete;

	public:
		virtual void ProcessEvents(FQueue Queue, FEventStream const& EventStream) = 0;
		virtual void InitializeQueues(TConstArrayView<FQueue> Queues) = 0;
	};

	RHI_API void ProcessEvents(FQueue Queue, FEventStream EventStream);
	RHI_API void InitializeQueues(TConstArrayView<FQueue> Queues);
}

#else

/** Stats for a single perf event node. */
class FGPUProfilerEventNodeStats : public FRefCountedObject
{
public:
	FGPUProfilerEventNodeStats() :
		NumDraws(0),
		NumPrimitives(0),
		NumVertices(0),
		NumDispatches(0),
		GroupCount(FIntVector(0, 0, 0)),
		NumTotalDispatches(0),
		NumTotalDraws(0),
		NumTotalPrimitives(0),
		NumTotalVertices(0),
		TimingResult(0),
		NumEvents(0)
	{
	}

	FGPUProfilerEventNodeStats(const FGPUProfilerEventNodeStats& rhs)
	{
		NumDraws = rhs.NumDraws;
		NumPrimitives = rhs.NumPrimitives;
		NumVertices = rhs.NumVertices;
		NumDispatches = rhs.NumDispatches;
		NumTotalDispatches = rhs.NumTotalDispatches;
		NumTotalDraws = rhs.NumDraws;
		NumTotalPrimitives = rhs.NumPrimitives;
		NumTotalVertices = rhs.NumVertices;
		TimingResult = rhs.TimingResult;
		NumEvents = rhs.NumEvents;
	}

	/** Exclusive number of draw calls rendered in this event. */
	uint32 NumDraws;

	/** Exclusive number of primitives rendered in this event. */
	uint32 NumPrimitives;

	/** Exclusive number of vertices rendered in this event. */
	uint32 NumVertices;

	/** Compute stats */
	uint32 NumDispatches;
	FIntVector GroupCount;
	uint32 NumTotalDispatches;

	/** Inclusive number of draw calls rendered in this event and children. */
	uint32 NumTotalDraws;

	/** Inclusive number of primitives rendered in this event and children. */
	uint32 NumTotalPrimitives;

	/** Inclusive number of vertices rendered in this event and children. */
	uint32 NumTotalVertices;

	/** GPU time spent inside the perf event's begin and end, in ms. */
	float TimingResult;

	/** Inclusive number of other perf events that this is the parent of. */
	uint32 NumEvents;

	const FGPUProfilerEventNodeStats operator+=(const FGPUProfilerEventNodeStats& rhs)
	{
		NumDraws += rhs.NumDraws;
		NumPrimitives += rhs.NumPrimitives;
		NumVertices += rhs.NumVertices;
		NumDispatches += rhs.NumDispatches;
		NumTotalDispatches += rhs.NumTotalDispatches;
		NumTotalDraws += rhs.NumDraws;
		NumTotalPrimitives += rhs.NumPrimitives;
		NumTotalVertices += rhs.NumVertices;
		TimingResult += rhs.TimingResult;
		NumEvents += rhs.NumEvents;

		return *this;
	}
};

/** Stats for a single perf event node. */
class FGPUProfilerEventNode : public FGPUProfilerEventNodeStats
{
public:
	FGPUProfilerEventNode(const TCHAR* InName, FGPUProfilerEventNode* InParent) :
		FGPUProfilerEventNodeStats(),
		Name(InName),
		Parent(InParent)
	{
	}

	~FGPUProfilerEventNode() {}

	FString Name;

	/** Pointer to parent node so we can walk up the tree on appEndDrawEvent. */
	FGPUProfilerEventNode* Parent;

	/** Children perf event nodes. */
	TArray<TRefCountPtr<FGPUProfilerEventNode> > Children;

	virtual float GetTiming() { return 0.0f; }
	virtual void StartTiming() {}
	virtual void StopTiming() {}
};

/** An entire frame of perf event nodes, including ancillary timers. */
struct FGPUProfilerEventNodeFrame
{
	virtual ~FGPUProfilerEventNodeFrame() {}

	/** Root nodes of the perf event tree. */
	TArray<TRefCountPtr<FGPUProfilerEventNode> > EventTree;

	/** Start this frame of per tracking */
	virtual void StartFrame() {}

	/** End this frame of per tracking, but do not block yet */
	virtual void EndFrame() {}

	/** Dumps perf event information, blocking on GPU. */
	RHI_API void DumpEventTree();

	/** Calculates root timing base frequency (if needed by this RHI) */
	virtual float GetRootTimingResults() { return 0.0f; }

	/** D3D11 Hack */
	virtual void LogDisjointQuery() {}

	virtual bool PlatformDisablesVSync() const { return false; }
};

/**
* Two timestamps performed on GPU and CPU at nearly the same time.
* This can be used to visualize GPU and CPU timing events on the same timeline.
*/
struct FGPUTimingCalibrationTimestamp
{
	uint64 GPUMicroseconds = 0;
	uint64 CPUMicroseconds = 0;
};

/**
 * Holds information if this platform's GPU allows timing
 */
struct FGPUTiming
{
public:
	/**
	 * Whether GPU timing measurements are supported by the driver.
	 *
	 * @return true if GPU timing measurements are supported by the driver.
	 */
	static bool IsSupported()
	{
		return GIsSupported;
	}

	/**
	 * Returns the frequency for the timing values, in number of ticks per seconds.
	 *
	 * @return Frequency for the timing values, in number of ticks per seconds, or 0 if the feature isn't supported.
	 */
	static uint64 GetTimingFrequency(uint32 GPUIndex = 0)
	{
		return GTimingFrequency[GPUIndex];
	}

	/**
	* Returns a pair of timestamps performed on GPU and CPU at nearly the same time, in microseconds.
	*
	* @return CPU and GPU timestamps, in microseconds. Both are 0 if feature isn't supported.
	*/
	static FGPUTimingCalibrationTimestamp GetCalibrationTimestamp(uint32 GPUIndex = 0)
	{
		return GCalibrationTimestamp[GPUIndex];
	}

	typedef void (PlatformStaticInitialize)(void*);
	static void StaticInitialize(void* UserData, PlatformStaticInitialize* PlatformFunction)
	{
		if (!GAreGlobalsInitialized && PlatformFunction)
		{
			(*PlatformFunction)(UserData);

			if (GetTimingFrequency() != 0)
			{
				GIsSupported = true;
			}
			else
			{
				GIsSupported = false;
			}

			GAreGlobalsInitialized = true;
		}
	}

protected:
	/** Whether the static variables have been initialized. */
	RHI_API static bool		GAreGlobalsInitialized;

	/** Whether GPU timing measurements are supported by the driver. */
	RHI_API static bool		GIsSupported;

	static void SetTimingFrequency(uint64 TimingFrequency, uint32 GPUIndex = 0)
	{
		GTimingFrequency[GPUIndex] = TimingFrequency;
	}

	static void SetCalibrationTimestamp(FGPUTimingCalibrationTimestamp CalibrationTimestamp, uint32 GPUIndex = 0)
	{
		GCalibrationTimestamp[GPUIndex] = CalibrationTimestamp;
	}

private:
	/** Frequency for the timing values, in number of ticks per seconds, or 0 if the feature isn't supported. */
	RHI_API static TStaticArray<uint64, MAX_NUM_GPUS>	GTimingFrequency;

	/**
	* Two timestamps performed on GPU and CPU at nearly the same time.
	* This can be used to visualize GPU and CPU timing events on the same timeline.
	* Both values may be 0 if timer calibration is not available on current platform.
	*/
	RHI_API static TStaticArray<FGPUTimingCalibrationTimestamp, MAX_NUM_GPUS> GCalibrationTimestamp;
};

/** 
 * Encapsulates GPU profiling logic and data. 
 * There's only one global instance of this struct so it should only contain global data, nothing specific to a frame.
 */
struct FGPUProfiler
{
	/** Whether we are currently tracking perf events or not. */
	bool bTrackingEvents;

	/** Whether we are currently tracking data for gpucrash debugging or not */
	bool bTrackingGPUCrashData;

	/** A latched version of GTriggerGPUProfile. This is a form of pseudo-thread safety. We read the value once a frame only. */
	bool bLatchedGProfilingGPU;

	/** A latched version of GTriggerGPUHitchProfile. This is a form of pseudo-thread safety. We read the value once a frame only. */
	bool bLatchedGProfilingGPUHitches;

	/** The previous latched version of GTriggerGPUHitchProfile.*/
	bool bPreviousLatchedGProfilingGPUHitches;

	/** Original state of GEmitDrawEvents before it was overridden for profiling. */
	bool bOriginalGEmitDrawEvents;

	/** GPU hitch profile history debounce...after a hitch, we just ignore frames for a while */
	int32 GPUHitchDebounce;

	/** scope depth to record crash data depth. to limit perf/mem requirements */
	int32 GPUCrashDataDepth;

	/** Current perf event node frame. */
	FGPUProfilerEventNodeFrame* CurrentEventNodeFrame = nullptr;

	/** Current perf event node. */
	FGPUProfilerEventNode* CurrentEventNode;

	int32 StackDepth;

	FGPUProfiler() :
		bTrackingEvents(false),
		bTrackingGPUCrashData(false),
		bLatchedGProfilingGPU(false),
		bLatchedGProfilingGPUHitches(false),
		bPreviousLatchedGProfilingGPUHitches(false),
		bOriginalGEmitDrawEvents(false),
		GPUHitchDebounce(0),
		GPUCrashDataDepth(-1),
		CurrentEventNodeFrame(NULL),
		CurrentEventNode(NULL),
		StackDepth(0)
	{
	}

	virtual ~FGPUProfiler()
	{
	}

	void RegisterGPUWork(uint32 NumDraws, uint32 NumPrimitives, uint32 NumVertices)
	{
		if (bTrackingEvents && CurrentEventNode)
		{
			check(IsInRenderingThread() || IsInRHIThread());
			CurrentEventNode->NumDraws += NumDraws;
			CurrentEventNode->NumPrimitives += NumPrimitives;
			CurrentEventNode->NumVertices += NumVertices;
		}
	}

	void RegisterGPUWork(uint32 NumPrimitives = 0, uint32 NumVertices = 0)
	{
		RegisterGPUWork(1, NumPrimitives, NumVertices);
	}

	void RegisterGPUDispatch(FIntVector GroupCount)
	{
		if (bTrackingEvents && CurrentEventNode)
		{
			check(IsInRenderingThread() || IsInRHIThread());
			CurrentEventNode->NumDispatches++;
			CurrentEventNode->GroupCount = GroupCount;
		}
	}

	virtual FGPUProfilerEventNode* CreateEventNode(const TCHAR* InName, FGPUProfilerEventNode* InParent)
	{
		return new FGPUProfilerEventNode(InName, InParent);
	}

	RHI_API virtual void PushEvent(const TCHAR* Name, FColor Color);
	RHI_API virtual void PopEvent();

	bool IsProfilingGPU() const { return bTrackingEvents; }
};

#endif
