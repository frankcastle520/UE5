// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/Array.h"
#include "Containers/Map.h"
#include "Containers/StaticArray.h"
#include "Containers/UnrealString.h"
#include "CoreMinimal.h"
#include "CoreTypes.h"
#include "GpuProfilerTrace.h"
#include "HAL/CriticalSection.h"
#include "MultiGPU.h"
#include "ProfilingDebugging/CsvProfiler.h"
#include "ProfilingDebugging/CsvProfilerConfig.h"
#include "RHI.h"
#include "RHICommandList.h"
#include "RHIBreadcrumbs.h"
#include "RenderingThread.h"
#include "Stats/Stats.h"
#include "Stats/Stats2.h"
#include "UObject/NameTypes.h"
#include <tuple>

#define WANTS_DRAW_MESH_EVENTS (WITH_PROFILEGPU && WITH_RHI_BREADCRUMBS)

#if WITH_RHI_BREADCRUMBS

	struct FRHIBreadcrumbEvent_GameThread
	{
	private:
		TOptional<FRHIBreadcrumbEventScope>* Event;

	public:
		template<size_t N, typename... TArgs>
		FRHIBreadcrumbEvent_GameThread(bool bCondition, FRHIBreadcrumbData&& Data, TCHAR const(&FormatString)[N], TArgs&&... Args)
			: Event(bCondition ? new TOptional<FRHIBreadcrumbEventScope> : nullptr)
		{
			check(IsInGameThread());
			if (Event)
			{
				ENQUEUE_RENDER_COMMAND(FRHIBreadcrumbEvent_GameThread_Begin)(
				[
					Event = Event,
					Data = MoveTemp(Data),
					Values = std::make_tuple(std::forward<TArgs>(Args)...),
					&FormatString
				](FRHICommandListImmediate& RHICmdList) mutable
				{
					std::apply([Event, &RHICmdList, &Data, &FormatString](auto&&... InnerArgs)
					{
						Event->Emplace(RHICmdList, MoveTemp(Data), true, FormatString, std::forward<TArgs>(InnerArgs)...);
					}, Values);
				});
			}
		}

		~FRHIBreadcrumbEvent_GameThread()
		{
			check(IsInGameThread());
			if (Event)
			{
				ENQUEUE_RENDER_COMMAND(FRHIBreadcrumbEvent_GameThread_End)([Event = Event](FRHICommandListImmediate& RHICmdList)
				{
					delete Event;
				});
			}
		}
	};

	#define RHI_BREADCRUMB_EVENT_GAMETHREAD(                       Format, ...) FRHIBreadcrumbEvent_GameThread ANONYMOUS_VARIABLE(BreadcrumbEvent_GameThread)(true     , FRHIBreadcrumbData(__FILE__, __LINE__, TStatId(), NAME_None), TEXT(Format), ##__VA_ARGS__)
	#define RHI_BREADCRUMB_EVENT_CONDITIONAL_GAMETHREAD(Condition, Format, ...) FRHIBreadcrumbEvent_GameThread ANONYMOUS_VARIABLE(BreadcrumbEvent_GameThread)(Condition, FRHIBreadcrumbData(__FILE__, __LINE__, TStatId(), NAME_None), TEXT(Format), ##__VA_ARGS__)

	// Used only for back compat with SCOPED_DRAW_EVENTF_GAMETHREAD
	#define RHI_BREADCRUMB_EVENT_GAMETHREAD_STR_DEPRECATED(Format, ...) FRHIBreadcrumbEvent_GameThread ANONYMOUS_VARIABLE(BreadcrumbEvent_GameThread)(true, FRHIBreadcrumbData(__FILE__, __LINE__, TStatId(), NAME_None), Format, ##__VA_ARGS__)

#else

	#define RHI_BREADCRUMB_EVENT_GAMETHREAD(...)                do { } while(0)
	#define RHI_BREADCRUMB_EVENT_CONDITIONAL_GAMETHREAD(...)    do { } while(0)
	#define RHI_BREADCRUMB_EVENT_GAMETHREAD_STR_DEPRECATED(...) do { } while(0)

#endif

// Macros to allow for scoping of draw events outside of RHI function implementations
// Render-thread event macros:
#define SCOPED_DRAW_EVENT(RHICmdList, Name)                                      RHI_BREADCRUMB_EVENT(RHICmdList, #Name);
#define SCOPED_DRAW_EVENTF(RHICmdList, Name, Format, ...)                        RHI_BREADCRUMB_EVENT_STR_DEPRECATED(RHICmdList, Format, ##__VA_ARGS__);
#define SCOPED_CONDITIONAL_DRAW_EVENT(RHICmdList, Name, Condition)               RHI_BREADCRUMB_EVENT_CONDITIONAL(RHICmdList, Condition, #Name);
#define SCOPED_CONDITIONAL_DRAW_EVENTF(RHICmdList, Name, Condition, Format, ...) RHI_BREADCRUMB_EVENT_CONDITIONAL_STR_DEPRECATED(RHICmdList, Condition, Format, ##__VA_ARGS__);

// Non-render-thread event macros:
#define SCOPED_DRAW_EVENT_GAMETHREAD(Name)                                       RHI_BREADCRUMB_EVENT_GAMETHREAD(#Name);
#define SCOPED_DRAW_EVENTF_GAMETHREAD(Name, Format, ...)                         RHI_BREADCRUMB_EVENT_GAMETHREAD_STR_DEPRECATED(Format, ##__VA_ARGS__);
#define SCOPED_CONDITIONAL_DRAW_EVENT_GAMETHREAD(Name, Condition)                RHI_BREADCRUMB_EVENT_CONDITIONAL_GAMETHREAD(Condition, #Name);
#define SCOPED_CONDITIONAL_DRAW_EVENTF_GAMETHREAD(Name, Condition, Format, ...)  RHI_BREADCRUMB_EVENT_CONDITIONAL_GAMETHREAD(Condition, Format, ##__VA_ARGS__);

// Deprecated macros
#define BEGIN_DRAW_EVENTF(RHICmdList, Name, Event, Format, ...)                                      UE_DEPRECATED_MACRO(5.5, "BEGIN_DRAW_EVENTF has been deprecated. Equivalent functionality can be implemented by constructing / destructing an instance of FRHIBreadcrumbEventManual."                      )
#define STOP_DRAW_EVENT(Event)                                                                       UE_DEPRECATED_MACRO(5.5, "STOP_DRAW_EVENT has been deprecated. Equivalent functionality can be implemented by constructing / destructing an instance of FRHIBreadcrumbEventManual."                        )
#define STOP_DRAW_EVENT_GAMETHREAD(...)                                                              UE_DEPRECATED_MACRO(5.5, "STOP_DRAW_EVENT_GAMETHREAD has been deprecated. Equivalent functionality can be implemented by constructing / destructing an instance of FRHIBreadcrumbEvent_GameThread."        )
#define BEGIN_DRAW_EVENTF_GAMETHREAD(...)                                                            UE_DEPRECATED_MACRO(5.5, "BEGIN_DRAW_EVENTF_GAMETHREAD has been deprecated. Equivalent functionality can be implemented by constructing / destructing an instance of FRHIBreadcrumbEvent_GameThread."      )
#define BEGIN_DRAW_EVENTF_COLOR_GAMETHREAD(...)                                                      UE_DEPRECATED_MACRO(5.5, "BEGIN_DRAW_EVENTF_COLOR_GAMETHREAD has been deprecated. Equivalent functionality can be implemented by constructing / destructing an instance of FRHIBreadcrumbEvent_GameThread.")
#define SCOPED_DRAW_EVENT_COLOR(RHICmdList, Color, Name)                                             UE_DEPRECATED_MACRO(5.5, "SCOPED_DRAW_EVENT_COLOR has been deprecated. Use RHI_BREADCRUMB_EVENT instead."                                               ) RHI_BREADCRUMB_EVENT(RHICmdList, #Name)
#define SCOPED_GPU_EVENT(RHICmdList, Name)                                                           UE_DEPRECATED_MACRO(5.5, "SCOPED_GPU_EVENT has been deprecated. Use RHI_BREADCRUMB_EVENT instead."                                                      ) RHI_BREADCRUMB_EVENT(RHICmdList, #Name)
#define SCOPED_GPU_EVENT_COLOR(RHICmdList, Color, Name)                                              UE_DEPRECATED_MACRO(5.5, "SCOPED_GPU_EVENT_COLOR has been deprecated. Use RHI_BREADCRUMB_EVENT instead."                                                ) RHI_BREADCRUMB_EVENT(RHICmdList, #Name)
#define SCOPED_COMPUTE_EVENT(RHICmdList, Name)                                                       UE_DEPRECATED_MACRO(5.5, "SCOPED_COMPUTE_EVENT has been deprecated. Use RHI_BREADCRUMB_EVENT instead."                                                  ) RHI_BREADCRUMB_EVENT(RHICmdList, #Name)
#define SCOPED_COMPUTE_EVENT_COLOR(RHICmdList, Color, Name)                                          UE_DEPRECATED_MACRO(5.5, "SCOPED_COMPUTE_EVENT_COLOR has been deprecated. Use RHI_BREADCRUMB_EVENT instead."                                            ) RHI_BREADCRUMB_EVENT(RHICmdList, #Name)
#define SCOPED_DRAW_EVENTF_COLOR(RHICmdList, Color, Name, Format, ...)                               UE_DEPRECATED_MACRO(5.5, "SCOPED_DRAW_EVENTF_COLOR has been deprecated. Use RHI_BREADCRUMB_EVENT instead."                                              ) RHI_BREADCRUMB_EVENT(RHICmdList, Format, ##__VA_ARGS__)
#define SCOPED_GPU_EVENTF(RHICmdList, Name, Format, ...)                                             UE_DEPRECATED_MACRO(5.5, "SCOPED_GPU_EVENTF has been deprecated. Use RHI_BREADCRUMB_EVENT instead."                                                     ) RHI_BREADCRUMB_EVENT(RHICmdList, Format, ##__VA_ARGS__)
#define SCOPED_GPU_EVENTF_COLOR(RHICmdList, Color, Name, Format, ...)                                UE_DEPRECATED_MACRO(5.5, "SCOPED_GPU_EVENTF_COLOR has been deprecated. Use RHI_BREADCRUMB_EVENT instead."                                               ) RHI_BREADCRUMB_EVENT(RHICmdList, Format, ##__VA_ARGS__)
#define SCOPED_COMPUTE_EVENTF(RHICmdList, Name, Format, ...)                                         UE_DEPRECATED_MACRO(5.5, "SCOPED_COMPUTE_EVENTF has been deprecated. Use RHI_BREADCRUMB_EVENT instead."                                                 ) RHI_BREADCRUMB_EVENT(RHICmdList, Format, ##__VA_ARGS__)
#define SCOPED_COMPUTE_EVENTF_COLOR(RHICmdList, Color, Name, Format, ...)                            UE_DEPRECATED_MACRO(5.5, "SCOPED_COMPUTE_EVENTF_COLOR has been deprecated. Use RHI_BREADCRUMB_EVENT instead."                                           ) RHI_BREADCRUMB_EVENT(RHICmdList, Format, ##__VA_ARGS__)
#define SCOPED_CONDITIONAL_DRAW_EVENT_COLOR(RHICmdList, Name, Color, Condition)                      UE_DEPRECATED_MACRO(5.5, "SCOPED_CONDITIONAL_DRAW_EVENT_COLOR has been deprecated. Use RHI_BREADCRUMB_EVENT_CONDITIONAL instead."                       ) RHI_BREADCRUMB_EVENT_CONDITIONAL(RHICmdList, Condition, #Name)
#define SCOPED_CONDITIONAL_GPU_EVENT(RHICmdList, Name, Condition)                                    UE_DEPRECATED_MACRO(5.5, "SCOPED_CONDITIONAL_GPU_EVENT has been deprecated. Use RHI_BREADCRUMB_EVENT_CONDITIONAL instead."                              ) RHI_BREADCRUMB_EVENT_CONDITIONAL(RHICmdList, Condition, #Name)
#define SCOPED_CONDITIONAL_GPU_EVENT_COLOR(RHICmdList, Name, Color, Condition)                       UE_DEPRECATED_MACRO(5.5, "SCOPED_CONDITIONAL_GPU_EVENT_COLOR has been deprecated. Use RHI_BREADCRUMB_EVENT_CONDITIONAL instead."                        ) RHI_BREADCRUMB_EVENT_CONDITIONAL(RHICmdList, Condition, #Name)
#define SCOPED_CONDITIONAL_COMPUTE_EVENT(RHICmdList, Name, Condition)                                UE_DEPRECATED_MACRO(5.5, "SCOPED_CONDITIONAL_COMPUTE_EVENT has been deprecated. Use RHI_BREADCRUMB_EVENT_CONDITIONAL instead."                          ) RHI_BREADCRUMB_EVENT_CONDITIONAL(RHICmdList, Condition, #Name)
#define SCOPED_CONDITIONAL_COMPUTE_EVENT_COLOR(RHICmdList, Name, Color, Condition)                   UE_DEPRECATED_MACRO(5.5, "SCOPED_CONDITIONAL_COMPUTE_EVENT_COLOR has been deprecated. Use RHI_BREADCRUMB_EVENT_CONDITIONAL instead."                    ) RHI_BREADCRUMB_EVENT_CONDITIONAL(RHICmdList, Condition, #Name)
#define SCOPED_CONDITIONAL_DRAW_EVENTF_COLOR(RHICmdList, Color, Name, Condition, Format, ...)        UE_DEPRECATED_MACRO(5.5, "SCOPED_CONDITIONAL_DRAW_EVENTF_COLOR has been deprecated. Use RHI_BREADCRUMB_EVENT_CONDITIONAL instead."                      ) RHI_BREADCRUMB_EVENT_CONDITIONAL(RHICmdList, Condition, Format, ##__VA_ARGS__)
#define SCOPED_CONDITIONAL_GPU_EVENTF(RHICmdList, Name, Condition, Format, ...)                      UE_DEPRECATED_MACRO(5.5, "SCOPED_CONDITIONAL_GPU_EVENTF has been deprecated. Use RHI_BREADCRUMB_EVENT_CONDITIONAL instead."                             ) RHI_BREADCRUMB_EVENT_CONDITIONAL(RHICmdList, Condition, Format, ##__VA_ARGS__)
#define SCOPED_CONDITIONAL_GPU_EVENTF_COLOR(RHICmdList, Color, Name, Condition, Format, ...)         UE_DEPRECATED_MACRO(5.5, "SCOPED_CONDITIONAL_GPU_EVENTF_COLOR has been deprecated. Use RHI_BREADCRUMB_EVENT_CONDITIONAL instead."                       ) RHI_BREADCRUMB_EVENT_CONDITIONAL(RHICmdList, Condition, Format, ##__VA_ARGS__)
#define SCOPED_CONDITIONAL_COMPUTE_EVENTF(RHICmdList, Name, Condition, Format, ...)                  UE_DEPRECATED_MACRO(5.5, "SCOPED_CONDITIONAL_COMPUTE_EVENTF has been deprecated. Use RHI_BREADCRUMB_EVENT_CONDITIONAL instead."                         ) RHI_BREADCRUMB_EVENT_CONDITIONAL(RHICmdList, Condition, Format, ##__VA_ARGS__)
#define SCOPED_CONDITIONAL_COMPUTE_EVENTF_COLOR(RHICmdList, Color, Name, Condition, Format, ...)     UE_DEPRECATED_MACRO(5.5, "SCOPED_CONDITIONAL_COMPUTE_EVENTF_COLOR has been deprecated. Use RHI_BREADCRUMB_EVENT_CONDITIONAL instead."                   ) RHI_BREADCRUMB_EVENT_CONDITIONAL(RHICmdList, Condition, Format, ##__VA_ARGS__)
#define BEGIN_DRAW_EVENTF_COLOR(RHICmdList, Color, Name, Event, Format, ...)                         UE_DEPRECATED_MACRO(5.5, "BEGIN_DRAW_EVENTF_COLOR has been deprecated. Use BEGIN_DRAW_EVENTF instead."                                                  ) BEGIN_DRAW_EVENTF(RHICmdList, Name, Event, Format, ##__VA_ARGS__)
#define BEGIN_GPU_EVENTF(RHICmdList, Name, Event, Format, ...)                                       UE_DEPRECATED_MACRO(5.5, "BEGIN_GPU_EVENTF has been deprecated. Use BEGIN_DRAW_EVENTF instead."                                                         ) BEGIN_DRAW_EVENTF(RHICmdList, Name, Event, Format, ##__VA_ARGS__)
#define BEGIN_GPU_EVENTF_COLOR(RHICmdList, Color, Name, Event, Format, ...)                          UE_DEPRECATED_MACRO(5.5, "BEGIN_GPU_EVENTF_COLOR has been deprecated. Use BEGIN_DRAW_EVENTF instead."                                                   ) BEGIN_DRAW_EVENTF(RHICmdList, Name, Event, Format, ##__VA_ARGS__)
#define STOP_GPU_EVENT(Event)                                                                        UE_DEPRECATED_MACRO(5.5, "STOP_GPU_EVENT has been deprecated. Use STOP_DRAW_EVENT instead."                                                             ) STOP_DRAW_EVENT(Event)
#define SCOPED_DRAW_EVENT_COLOR_GAMETHREAD(Color, Name)                                              UE_DEPRECATED_MACRO(5.5, "SCOPED_DRAW_EVENT_COLOR_GAMETHREAD has been deprecated. Use RHI_BREADCRUMB_EVENT_GAMETHREAD instead."                         ) RHI_BREADCRUMB_EVENT_GAMETHREAD(#Name)
#define SCOPED_DRAW_EVENTF_COLOR_GAMETHREAD(Color, Name, Format, ...)                                UE_DEPRECATED_MACRO(5.5, "SCOPED_DRAW_EVENTF_COLOR_GAMETHREAD has been deprecated. Use RHI_BREADCRUMB_EVENT_GAMETHREAD instead."                        ) RHI_BREADCRUMB_EVENT_GAMETHREAD(Format, ##__VA_ARGS__)
#define SCOPED_CONDITIONAL_DRAW_EVENT_COLOR_GAMETHREAD(Name, Color, Condition)                       UE_DEPRECATED_MACRO(5.5, "SCOPED_CONDITIONAL_DRAW_EVENT_COLOR_GAMETHREAD has been deprecated. Use RHI_BREADCRUMB_EVENT_CONDITIONAL_GAMETHREAD instead." ) RHI_BREADCRUMB_EVENT_CONDITIONAL_GAMETHREAD(Condition, #Name)
#define SCOPED_CONDITIONAL_DRAW_EVENTF_COLOR_GAMETHREAD(Color, Name, Condition, Format, ...)         UE_DEPRECATED_MACRO(5.5, "SCOPED_CONDITIONAL_DRAW_EVENTF_COLOR_GAMETHREAD has been deprecated. Use RHI_BREADCRUMB_EVENT_CONDITIONAL_GAMETHREAD instead.") RHI_BREADCRUMB_EVENT_CONDITIONAL_GAMETHREAD(Condition, Format, ##__VA_ARGS__)
#define SCOPED_RHI_DRAW_EVENT(RHICmdContext, Name)                                                   UE_DEPRECATED_MACRO(5.5, "SCOPED_RHI_DRAW_EVENT has been deprecated. Use standard RHI breadcrumb events instead."                   )
#define SCOPED_RHI_DRAW_EVENTF(RHICmdContext, Name, Format, ...)                                     UE_DEPRECATED_MACRO(5.5, "SCOPED_RHI_DRAW_EVENTF has been deprecated. Use standard RHI breadcrumb events instead."                  )
#define SCOPED_RHI_CONDITIONAL_DRAW_EVENT(RHICmdContext, Name, Condition)                            UE_DEPRECATED_MACRO(5.5, "SCOPED_RHI_CONDITIONAL_DRAW_EVENT has been deprecated. Use standard RHI breadcrumb events instead."       )
#define SCOPED_RHI_CONDITIONAL_DRAW_EVENTF(RHICmdContext, Name, Condition, Format, ...)              UE_DEPRECATED_MACRO(5.5, "SCOPED_RHI_CONDITIONAL_DRAW_EVENTF has been deprecated. Use standard RHI breadcrumb events instead."      )
#define SCOPED_RHI_DRAW_EVENT_COLOR(RHICmdContext, Color, Name)                                      UE_DEPRECATED_MACRO(5.5, "SCOPED_RHI_DRAW_EVENT_COLOR has been deprecated. Use standard RHI breadcrumb events instead."             )
#define SCOPED_RHI_DRAW_EVENTF_COLOR(RHICmdContext, Color, Name, Format, ...)                        UE_DEPRECATED_MACRO(5.5, "SCOPED_RHI_DRAW_EVENTF_COLOR has been deprecated. Use standard RHI breadcrumb events instead."            )
#define SCOPED_RHI_CONDITIONAL_DRAW_EVENT_COLOR(RHICmdContext, Color, Name, Condition)               UE_DEPRECATED_MACRO(5.5, "SCOPED_RHI_CONDITIONAL_DRAW_EVENT_COLOR has been deprecated. Use standard RHI breadcrumb events instead." )
#define SCOPED_RHI_CONDITIONAL_DRAW_EVENTF_COLOR(RHICmdContext, Color, Name, Condition, Format, ...) UE_DEPRECATED_MACRO(5.5, "SCOPED_RHI_CONDITIONAL_DRAW_EVENTF_COLOR has been deprecated. Use standard RHI breadcrumb events instead.")

#if RHI_NEW_GPU_PROFILER

	#if HAS_GPU_STATS

		// @todo
		#define DECLARE_GPU_STAT(                StatName            ) DECLARE_FLOAT_COUNTER_STAT(TEXT(#StatName)       , Stat_GPU_##StatName, STATGROUP_GPU  ); CSV_DEFINE_STAT(GPU,StatName);
		#define DECLARE_GPU_STAT_NAMED(          StatName, NameString) DECLARE_FLOAT_COUNTER_STAT(NameString            , Stat_GPU_##StatName, STATGROUP_GPU  ); CSV_DEFINE_STAT(GPU,StatName);
		#define DECLARE_GPU_DRAWCALL_STAT(       StatName            ) DECLARE_FLOAT_COUNTER_STAT(TEXT(#StatName)       , Stat_GPU_##StatName, STATGROUP_GPU  ); CSV_DEFINE_STAT(GPU,StatName);
		#define DECLARE_GPU_DRAWCALL_STAT_NAMED( StatName, NameString) DECLARE_FLOAT_COUNTER_STAT(NameString            , Stat_GPU_##StatName, STATGROUP_GPU  ); CSV_DEFINE_STAT(GPU,StatName);
		#define DECLARE_GPU_DRAWCALL_STAT_EXTERN(StatName            ) DECLARE_FLOAT_COUNTER_STAT_EXTERN(TEXT(#StatName), Stat_GPU_##StatName, STATGROUP_GPU, ); CSV_DECLARE_STAT_EXTERN(GPU,StatName);

		// Extern GPU stats are needed where a stat is used in multiple CPPs. Use the DECLARE_GPU_STAT_NAMED_EXTERN in the header and DEFINE_GPU_STAT in the CPPs
		#define DECLARE_GPU_STAT_NAMED_EXTERN(StatName, NameString) DECLARE_FLOAT_COUNTER_STAT_EXTERN(NameString, Stat_GPU_##StatName, STATGROUP_GPU, ); CSV_DECLARE_STAT_EXTERN(GPU,StatName);
		#define DEFINE_GPU_STAT(              StatName            ) DEFINE_STAT(Stat_GPU_##StatName);                                                    CSV_DEFINE_STAT(GPU,StatName);
		#define DEFINE_GPU_DRAWCALL_STAT(     StatName            ) DEFINE_STAT(Stat_GPU_##StatName);                                                    CSV_DEFINE_STAT(GPU,StatName);

	#else

		#define DECLARE_GPU_STAT(...)
		#define DECLARE_GPU_STAT_NAMED(...)
		#define DECLARE_GPU_DRAWCALL_STAT(...)
		#define DECLARE_GPU_DRAWCALL_STAT_NAMED(...)
		#define DECLARE_GPU_DRAWCALL_STAT_EXTERN(...)
		#define DECLARE_GPU_STAT_NAMED_EXTERN(...)
		#define DEFINE_GPU_STAT(...)
		#define DEFINE_GPU_DRAWCALL_STAT(...)

	#endif

	// Empty when using the new RHI GPU profiler. GPU stats are handled via RHI_BREADCRUMB_EVENT_STAT etc.
	// @todo deprecate
	#define SCOPED_GPU_STAT_VERBOSE(...)
	#define SCOPED_GPU_STAT(...)
	#define GPU_STATS_BEGINFRAME(...) 
	#define GPU_STATS_ENDFRAME(...) 
	#define GPU_STATS_SUSPENDFRAME(...)

#else

class FRealtimeGPUProfiler;
class FRealtimeGPUProfilerEvent;
class FRealtimeGPUProfilerFrame;

#if HAS_GPU_STATS

	// The DECLARE_GPU_STAT macros both declare and define a stat (for use in a single CPP)
	#define DECLARE_GPU_STAT(StatName)                            DECLARE_FLOAT_COUNTER_STAT(TEXT(#StatName)       , Stat_GPU_##StatName, STATGROUP_GPU  ); CSV_DEFINE_STAT(GPU,StatName);         static FRHIDrawStatsCategory DrawcallCountCategory_##StatName;
	#define DECLARE_GPU_STAT_NAMED(StatName, NameString)          DECLARE_FLOAT_COUNTER_STAT(NameString            , Stat_GPU_##StatName, STATGROUP_GPU  ); CSV_DEFINE_STAT(GPU,StatName);         static FRHIDrawStatsCategory DrawcallCountCategory_##StatName;
	#define DECLARE_GPU_DRAWCALL_STAT(StatName)                   DECLARE_FLOAT_COUNTER_STAT(TEXT(#StatName)       , Stat_GPU_##StatName, STATGROUP_GPU  ); CSV_DEFINE_STAT(GPU,StatName);         static FRHIDrawStatsCategory DrawcallCountCategory_##StatName((TCHAR*)TEXT(#StatName));
	#define DECLARE_GPU_DRAWCALL_STAT_NAMED(StatName, NameString) DECLARE_FLOAT_COUNTER_STAT(NameString            , Stat_GPU_##StatName, STATGROUP_GPU  ); CSV_DEFINE_STAT(GPU,StatName);         static FRHIDrawStatsCategory DrawcallCountCategory_##StatName((TCHAR*)TEXT(#StatName));
	#define DECLARE_GPU_DRAWCALL_STAT_EXTERN(StatName)            DECLARE_FLOAT_COUNTER_STAT_EXTERN(TEXT(#StatName), Stat_GPU_##StatName, STATGROUP_GPU, ); CSV_DECLARE_STAT_EXTERN(GPU,StatName); extern FRHIDrawStatsCategory DrawcallCountCategory_##StatName;

	// Extern GPU stats are needed where a stat is used in multiple CPPs. Use the DECLARE_GPU_STAT_NAMED_EXTERN in the header and DEFINE_GPU_STAT in the CPPs
	#define DECLARE_GPU_STAT_NAMED_EXTERN(StatName, NameString) DECLARE_FLOAT_COUNTER_STAT_EXTERN(NameString, Stat_GPU_##StatName, STATGROUP_GPU, ); CSV_DECLARE_STAT_EXTERN(GPU,StatName); extern FRHIDrawStatsCategory DrawcallCountCategory_##StatName;
	#define DEFINE_GPU_STAT(StatName)                           DEFINE_STAT(Stat_GPU_##StatName);                                                    CSV_DEFINE_STAT(GPU,StatName);                FRHIDrawStatsCategory DrawcallCountCategory_##StatName;
	#define DEFINE_GPU_DRAWCALL_STAT(StatName)                  DEFINE_STAT(Stat_GPU_##StatName);                                                    CSV_DEFINE_STAT(GPU,StatName);                FRHIDrawStatsCategory DrawcallCountCategory_##StatName((TCHAR*)TEXT(#StatName));

	#define SCOPED_GPU_STAT_VERBOSE(RHICmdList, StatName, Description)                 \
		FScopedGPUStatEvent PREPROCESSOR_JOIN(GPUStatEvent_##StatName,__LINE__)(       \
			  RHICmdList														       \
			, CSV_STAT_FNAME(StatName)											       \
			, GET_STATID(Stat_GPU_##StatName)                                          \
			, Description														       \
		);																			   \
		FScopedDrawStatCategory PREPROCESSOR_JOIN(DrawCallScope_##StatName, __LINE__)( \
			RHICmdList, DrawcallCountCategory_##StatName							   \
		);

	#define SCOPED_GPU_STAT(RHICmdList, StatName) SCOPED_GPU_STAT_VERBOSE(RHICmdList, StatName, nullptr)

	#define GPU_STATS_BEGINFRAME(RHICmdList) FRealtimeGPUProfiler::Get()->BeginFrame(RHICmdList);
	#define GPU_STATS_ENDFRAME(RHICmdList)   FRealtimeGPUProfiler::Get()->EndFrame(RHICmdList);
	#define GPU_STATS_SUSPENDFRAME()         FRealtimeGPUProfiler::Get()->SuspendFrame();

#else

	#define DECLARE_GPU_STAT(StatName)
	#define DECLARE_GPU_STAT_NAMED(StatName, NameString)
	#define DECLARE_GPU_DRAWCALL_STAT(StatName)
	#define DECLARE_GPU_DRAWCALL_STAT_NAMED(StatName, NameString)
	#define DECLARE_GPU_DRAWCALL_STAT_EXTERN(StatName)

	#define DECLARE_GPU_STAT_NAMED_EXTERN(StatName, NameString)
	#define DEFINE_GPU_STAT(StatName)
	#define DEFINE_GPU_DRAWCALL_STAT(StatName)

	#define SCOPED_GPU_STAT_VERBOSE(RHICmdList, StatName, Description)
	#define SCOPED_GPU_STAT(RHICmdList, StatName) 

	#define GPU_STATS_BEGINFRAME(RHICmdList) 
	#define GPU_STATS_ENDFRAME(RHICmdList) 
	#define GPU_STATS_SUSPENDFRAME()

#endif

RENDERCORE_API bool AreGPUStatsEnabled();

#if HAS_GPU_STATS

class FRealtimeGPUProfilerQuery
{
public:
	FRealtimeGPUProfilerQuery() = default;
	FRealtimeGPUProfilerQuery(FRHIGPUMask InGPUMask, FRHIRenderQuery* InQuery, FRealtimeGPUProfilerEvent* Parent)
		: GPUMask(InGPUMask)
		, Query(InQuery)
		, Parent(Parent)
	{}

	RENDERCORE_API void Submit(FRHICommandList& RHICmdList, bool bBegin) const;

	// RDG might create profiler events that are never submitted due to pass culling etc.
	// This is called when FRDGScope_GPU instances are destructed, and will mark this query as discarded if it was never submitted.
	RENDERCORE_API void Discard(bool bBegin);

	operator bool() const { return Query != nullptr; }

private:
	FRHIGPUMask GPUMask;
	FRHIRenderQuery* Query{};
	FRealtimeGPUProfilerEvent* Parent{};
};

#if GPUPROFILERTRACE_ENABLED
struct FRealtimeGPUProfilerHistoryItem
{
	FRealtimeGPUProfilerHistoryItem();

	static const uint64 HistoryCount = 64;

	// Constructor memsets everything to zero, assuming structure is Plain Old Data.  If any dynamic structures are
	// added, you'll need a more generalized constructor that zeroes out all the uninitialized data.
	bool UpdatedThisFrame;
	FRHIGPUMask LastGPUMask;
	uint64 NextWriteIndex;
	uint64 AccumulatedTime;				// Accumulated time could be computed, but may also be useful to inspect in the debugger
	TStaticArray<uint64, HistoryCount> Times;
};

struct FRealtimeGPUProfilerHistoryByDescription
{
	TMap<FString, FRealtimeGPUProfilerHistoryItem> History;
	mutable FRWLock Mutex;
};

struct FRealtimeGPUProfilerDescriptionResult
{
	// Times are in microseconds
	FString Description;
	FRHIGPUMask GPUMask;
	uint64 AverageTime;
	uint64 MinTime;
	uint64 MaxTime;
};
#endif  // GPUPROFILERTRACE_ENABLED

/**
* FRealtimeGPUProfiler class. This manages recording and reporting all for GPU stats
*/
class FRealtimeGPUProfiler
{
	static FRealtimeGPUProfiler* Instance;
public:
	// Singleton interface
	static RENDERCORE_API FRealtimeGPUProfiler* Get();

	/** *Safe release of the singleton */
	static RENDERCORE_API void SafeRelease();

	/** Per-frame update */
	RENDERCORE_API void BeginFrame(FRHICommandListImmediate& RHICmdList);
	RENDERCORE_API void EndFrame(FRHICommandListImmediate& RHICmdList);
	RENDERCORE_API void SuspendFrame();

	/** Push/pop events */
	RENDERCORE_API FRealtimeGPUProfilerQuery PushEvent(FRHIGPUMask GPUMask, const FName& Name, const TStatId& Stat, const TCHAR* Description);
	RENDERCORE_API FRealtimeGPUProfilerQuery PopEvent();

	/** Push/pop stats which do additional draw call tracking on top of events. */
	RENDERCORE_API void PushStat(FRHICommandListImmediate& RHICmdList, const FName& Name, const TStatId& Stat, const TCHAR* Description);
	RENDERCORE_API void PopStat(FRHICommandListImmediate& RHICmdList);

#if GPUPROFILERTRACE_ENABLED
	RENDERCORE_API void FetchPerfByDescription(TArray<FRealtimeGPUProfilerDescriptionResult> & OutResults) const;
#endif

private:
	FRealtimeGPUProfiler();

	/** Deinitialize of the object*/
	void Cleanup();

	TUniquePtr<FRealtimeGPUProfilerFrame> ActiveFrame;
	TQueue<TUniquePtr<FRealtimeGPUProfilerFrame>> PendingFrames;

	FRenderQueryPoolRHIRef RenderQueryPool;
	bool bLocked = false;

#if GPUPROFILERTRACE_ENABLED
	FRealtimeGPUProfilerHistoryByDescription HistoryByDescription;
#endif
};

/**
* Class that logs GPU Stat events for the realtime GPU profiler
*/
class FScopedGPUStatEvent
{
	/** Cmdlist to push onto. */
	FRHICommandListBase* RHICmdList = nullptr;

public:
	RENDERCORE_API FScopedGPUStatEvent(FRHICommandListBase& InRHICmdList, const FName& Name, const TStatId& StatId, const TCHAR* Description);
	RENDERCORE_API ~FScopedGPUStatEvent();
};

class FScopedDrawStatCategory
{
	FRHICommandListBase* const RHICmdList = nullptr;
	TOptional<FRHIDrawStatsCategory const*> Previous {};

public:
	RENDERCORE_API FScopedDrawStatCategory(FRHICommandListBase& RHICmdList, FRHIDrawStatsCategory const& Category);
	RENDERCORE_API ~FScopedDrawStatCategory();
};

#endif // HAS_GPU_STATS

#endif // (RHI_NEW_GPU_PROFILER == 0)
