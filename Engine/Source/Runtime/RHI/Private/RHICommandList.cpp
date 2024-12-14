// Copyright Epic Games, Inc. All Rights Reserved.

#include "RHICommandList.h"
#include "Misc/App.h"
#include "Async/TaskGraphInterfaces.h"
#include "Containers/ConsumeAllMpmcQueue.h"
#include "RHI.h"
#include "ProfilingDebugging/CsvProfiler.h"
#include "GenericPlatform/GenericPlatformCrashContext.h"
#include "RHIBreadcrumbs.h"
#include "RHIResourceReplace.h"
#include "RHIContext.h"
#include "RHIFwd.h"
#include "RHITransition.h"
#include "Stats/StatsTrace.h"
#include "HAL/PlatformMisc.h"

CSV_DEFINE_CATEGORY_MODULE(RHI_API, RHITStalls, false);
CSV_DEFINE_CATEGORY_MODULE(RHI_API, RHITFlushes, false);

DECLARE_CYCLE_STAT(TEXT("Nonimmed. Command List Execute"), STAT_NonImmedCmdListExecuteTime, STATGROUP_RHICMDLIST);
DECLARE_DWORD_COUNTER_STAT(TEXT("Nonimmed. Command List memory"), STAT_NonImmedCmdListMemory, STATGROUP_RHICMDLIST);
DECLARE_DWORD_COUNTER_STAT(TEXT("Nonimmed. Command count"), STAT_NonImmedCmdListCount, STATGROUP_RHICMDLIST);

DECLARE_CYCLE_STAT(TEXT("All Command List Execute"), STAT_ImmedCmdListExecuteTime, STATGROUP_RHICMDLIST);
DECLARE_DWORD_COUNTER_STAT(TEXT("Immed. Command List memory"), STAT_ImmedCmdListMemory, STATGROUP_RHICMDLIST);
DECLARE_DWORD_COUNTER_STAT(TEXT("Immed. Command count"), STAT_ImmedCmdListCount, STATGROUP_RHICMDLIST);

UE_TRACE_CHANNEL_DEFINE(RHICommandsChannel);

#if VALIDATE_UNIFORM_BUFFER_STATIC_BINDINGS
static thread_local bool bScopedUniformBufferStaticBindingsRecursionGuard = false;
void FScopedUniformBufferStaticBindings::OnScopeEnter()
{
	checkf(!bScopedUniformBufferStaticBindingsRecursionGuard, TEXT("Uniform buffer global binding scope has been called recursively!"));
	bScopedUniformBufferStaticBindingsRecursionGuard = true;
}
void FScopedUniformBufferStaticBindings::OnScopeExit()
{
	bScopedUniformBufferStaticBindingsRecursionGuard = false;
}
#endif

#if !PLATFORM_USES_FIXED_RHI_CLASS
#include "RHICommandListCommandExecutes.inl"
#endif

static TAutoConsoleVariable<int32> CVarRHICmdBypass(
	TEXT("r.RHICmdBypass"),
	0,
	TEXT("Whether to bypass the rhi command list and send the rhi commands immediately.\n")
	TEXT("0: Disable (required for the multithreaded renderer)\n")
	TEXT("1: Enable (convenient for debugging low level graphics API calls, can suppress artifacts from multithreaded renderer code)"));

TAutoConsoleVariable<int32> CVarRHICmdWidth(
	TEXT("r.RHICmdWidth"), 
	8,
	TEXT("Controls the task granularity of a great number of things in the parallel renderer."));

TAutoConsoleVariable<bool> CVarRHICmdParallelTranslateEnable(
	TEXT("r.RHICmd.ParallelTranslate.Enable"),
	true,
	TEXT("When true, allows recorded RHI command lists to be translated in parallel, on supported platforms. ")
	TEXT("Setting this to false will make all command lists translate on the RHI thread."));

TAutoConsoleVariable<int32> CVarRHICmdParallelTranslateMaxCommandsPerTranslate(
	TEXT("r.RHICmd.ParallelTranslate.MaxCommandsPerTranslate"),
	256,
	TEXT("When parallel translation is enabled, specifies the target maximum number of RHI command list commands to allow in a single translation job. ")
	TEXT("If a newly submitted command list would cause an existing translate job to exceed this threshold, a new job will be started. ")
	TEXT("A value of 0 means 'no limit'. Translate jobs will not be split. ")
	TEXT("A value less than 0 means 'always split'. Command lists will never be batched together in the same translate job."));

TAutoConsoleVariable<bool> CVarRHICmdParallelTranslateCombineSingleAndParallel(
	TEXT("r.RHICmd.ParallelTranslate.CombineSingleAndParallel"),
	false,
	TEXT("When true, allows the batching of both parallel and single threaded RHI command lists into the same translate job. ")
	TEXT("Any parallel command lists that get batched with a single thread command list will fall back to translating on the RHI thread. ")
	TEXT("Enabling this may trade reduced parallelism for reduced dispatch overhead."));

static TAutoConsoleVariable<int32> CVarRHICmdBufferWriteLocks(
	TEXT("r.RHICmdBufferWriteLocks"),
	1,
	TEXT("Only relevant with an RHI thread. Debugging option to diagnose problems with buffered locks."));

static TAutoConsoleVariable<int32> CVarRHICmdMaxOutstandingMemoryBeforeFlush(
	TEXT("r.RHICmdMaxOutstandingMemoryBeforeFlush"),
	256,
	TEXT("In kilobytes. The amount of outstanding memory before the RHI will force a flush. This should generally be set high enough that it doesn't happen on typical frames."));

FAutoConsoleTaskPriority CPrio_SceneRenderingTask(
	TEXT("TaskGraph.TaskPriorities.SceneRenderingTask"),
	TEXT("Task and thread priority for various scene rendering tasks."),
	ENamedThreads::NormalThreadPriority,
	ENamedThreads::HighTaskPriority
);

DECLARE_CYCLE_STAT(TEXT("Parallel Translate"),                 STAT_ParallelTranslate,      STATGROUP_RHICMDLIST);
DECLARE_CYCLE_STAT(TEXT("RHI Thread Parallel Translate Wait"), STAT_ParallelTranslateWait,  STATGROUP_RHICMDLIST);
DECLARE_CYCLE_STAT(TEXT("Explicit wait for tasks"),            STAT_ExplicitWait,           STATGROUP_RHICMDLIST);
DECLARE_CYCLE_STAT(TEXT("Explicit wait for RHI thread"),       STAT_ExplicitWaitRHIThread,  STATGROUP_RHICMDLIST);
DECLARE_CYCLE_STAT(TEXT("Spin RHIThread wait for stall"),      STAT_SpinWaitRHIThreadStall, STATGROUP_RHICMDLIST);
DECLARE_CYCLE_STAT(TEXT("RHI Thread Execute"),                 STAT_RHIThreadExecute,       STATGROUP_RHICMDLIST);

RHI_API TOptional<ERHIThreadMode> GPendingRHIThreadMode;

/** Accumulates how many cycles the renderthread has been idle. */
uint32 GRenderThreadIdle[ERenderThreadIdleTypes::Num] = { 0 };

/** How many cycles the from sampling input to the frame being flipped. */
uint64 GInputLatencyTime = 0;

FRHICommandListExecutor GRHICommandList;

static FCriticalSection GRHIThreadOnTasksCritical;
static std::atomic<int32> GRHIThreadStallRequestCount;

FRHICOMMAND_MACRO(FRHICommandRHIThreadFence)
{
	FGraphEventRef Fence;
	FORCEINLINE_DEBUGGABLE FRHICommandRHIThreadFence(FGraphEventRef const& Fence)
		: Fence(Fence)
	{}

	void Execute(FRHICommandListBase&)
	{
		if (Fence)
		{
			Fence->DispatchSubsequents();
			Fence = nullptr;
		}
	}
};

FRHICommandListBase::FRHICommandListBase(FRHIGPUMask InGPUMask, bool bImmediate)
	: FRHICommandListBase(FPersistentState(InGPUMask, bImmediate))
{}

FRHICommandListBase::FRHICommandListBase(FPersistentState const& InPersistentState)
	: DispatchEvent(FGraphEvent::CreateGraphEvent())
	, PersistentState(InPersistentState)
{
	DispatchEvent->SetDebugName(TEXT("FRHICommandListBase::DispatchEvent"));
	CommandLink = &Root;
}

FRHICommandListBase::~FRHICommandListBase()
{
	// Some configurations enable checks in shipping/test, particularly server builds. Skip these checks explicitly in that case, as they can fire very late in
	// the shutdown process and crash in unexpected ways because the log output channel has already been destroyed. Also, having pending commands
	// on shutdown shouldn't really be a fatal error, it's a fairly harmless condition.
#if DO_CHECK && (!UE_BUILD_SHIPPING && !UE_BUILD_TEST)

	checkf(!HasCommands() || IsExecuting(), TEXT("FRHICommandListBase has been deleted while it still contained commands. The command list was not submitted."));

	for (void* Data : PersistentState.QueryBatchData_Occlusion)
	{
		check(Data == nullptr);
	}

#endif
}

const int32 FRHICommandListBase::GetUsedMemory() const
{
	return MemManager.GetByteCount();
}

void FRHICommandListBase::AddDispatchPrerequisite(const FGraphEventRef& Prereq)
{
	// Forward the prereq to a lambda on the command list and wait for it during translation.
	// No need to delay translating the earlier commands in this command list.
	EnqueueLambda(TEXT("AddDispatchPrerequisite"), [Prereq = Prereq](FRHICommandListBase& ExecutingCmdList)
	{
		if (!Prereq->IsComplete())
		{
			Prereq->Wait();
		}
	});
}

void FRHICommandListBase::FinishRecording()
{
	// "Complete" the dispatch event.
	DispatchEvent->DispatchSubsequents();
}

#if HAS_GPU_STATS
TOptional<FRHIDrawStatsCategory const*> FRHICommandListBase::SetDrawStatsCategory(TOptional<FRHIDrawStatsCategory const*> Category)
{
	check(!Category.IsSet() || ((*Category) == nullptr || (*Category)->ShouldCountDraws()));

	TOptional<FRHIDrawStatsCategory const*> Previous = PersistentState.CurrentDrawStatsCategory;

	if (Previous != Category)
	{
		PersistentState.CurrentDrawStatsCategory = Category;

		EnqueueLambda([Category](FRHICommandListBase& ExecutingCmdList)
		{
			// InitialDrawStatsCategory will be unset in Bypass() mode, but we shouldn't 
			// be using it as the Category should have already been determined.
			ExecutingCmdList.PersistentState.CurrentDrawStatsCategory = Category.IsSet()
				? Category.GetValue()
				: ExecutingCmdList.InitialDrawStatsCategory.GetValue();
		});
	}

	return Previous;
}
#endif

#if WITH_RHI_BREADCRUMBS
void FRHICommandListBase::AttachBreadcrumbSubTree(FRHIBreadcrumbAllocator& Allocator, FRHIBreadcrumbList& Nodes)
{
	for (FRHIBreadcrumbNode* Node : Nodes.IterateAndUnlink())
	{
		checkf(Node->Allocator == &Allocator, TEXT("All the nodes in a subtree must come from the same breadcrumb allocator."));
		if (Node->GetParent() == FRHIBreadcrumbNode::Sentinel)
		{
			Node->SetParent(GetCurrentBreadcrumbRef());
		}
	}

	// Switch the current breadcrumb allocator out for the subtree one.
	if (BreadcrumbAllocator.Get() != &Allocator)
	{
		if (BreadcrumbAllocator)
		{
			BreadcrumbAllocatorRefs.AddUnique(BreadcrumbAllocator.Get());
		}
		BreadcrumbAllocator = Allocator.AsShared();
	}
}
#endif

void FRHICommandListBase::ActivatePipelines(ERHIPipeline Pipelines)
{
	checkf(IsTopOfPipe() || Bypass(), TEXT("Cannot be called from the bottom of pipe."));
	checkf(Pipelines == ERHIPipeline::None || EnumHasAllFlags(AllowedPipelines, Pipelines), TEXT("At least one of the specified pipelinea are not allowed on this RHI command list."));

	if (ActivePipelines == Pipelines)
	{
		// Nothing to do.
		return;
	}

	ActivePipelines = Pipelines;

#if WITH_RHI_BREADCRUMBS
	FActivatePipelineCommand* Command = nullptr;
	FActivatePipelineCommand LocalFixup;

	if (ActivePipelines != ERHIPipeline::None)
	{
		LocalFixup.Target = CPUBreadcrumbState.Current;
		LocalFixup.Pipelines = ActivePipelines;

		for (ERHIPipeline Pipeline : MakeFlagsRange(ActivePipelines))
		{
			GPUBreadcrumbState[Pipeline].Latest.Reset();
		}

		if (IsTopOfPipe())
		{
			Command = new (Alloc<FActivatePipelineCommand>()) FActivatePipelineCommand(LocalFixup);

			// Link the commands together
			if (!ActivatePipelineCommands.First) { ActivatePipelineCommands.First = Command; }
			if ( ActivatePipelineCommands.Prev ) { ActivatePipelineCommands.Prev->Next = Command; }
			ActivatePipelineCommands.Prev = Command;
		}
		else
		{
			Command = &LocalFixup;
		}
	}
#endif

	EnqueueLambda([
		NewPipelines = ActivePipelines,
		bSinglePipeline = IsSingleRHIPipeline(ActivePipelines)
#if WITH_RHI_BREADCRUMBS
		, Command
#endif
	](FRHICommandListBase& ExecutingCmdList)
	{
		ExecutingCmdList.ActivePipelines = NewPipelines;

		if (!bSinglePipeline)
		{
			// Graphics/compute context handling is disabled in multi-pipe/none-pipe mode.
			ExecutingCmdList.GraphicsContext = nullptr;
			ExecutingCmdList.ComputeContext = nullptr;
		}

		//
		// Grab the appropriate command contexts from the RHI if we don't already have them.
		//
		for (ERHIPipeline Pipeline : MakeFlagsRange(NewPipelines))
		{
			IRHIComputeContext*& Context = ExecutingCmdList.Contexts[Pipeline];

			switch (Pipeline)
			{
			default:
				checkNoEntry();
				break;

			case ERHIPipeline::Graphics:
			{
				if (!Context)
				{
					// Need to handle the "immediate" context separately.
					Context = ExecutingCmdList.AllowParallelTranslate()
						? GDynamicRHI->RHIGetCommandContext(Pipeline, FRHIGPUMask::All()) // This mask argument specifies which contexts are included in an mGPU redirector (we always want all of them).
						: ::RHIGetDefaultContext();
				}

				if (bSinglePipeline)
				{
					ExecutingCmdList.GraphicsContext = static_cast<IRHICommandContext*>(Context);
					ExecutingCmdList.ComputeContext = Context;
				}
			}
			break;

			case ERHIPipeline::AsyncCompute:
			{
				if (!Context)
				{
					Context = GDynamicRHI->RHIGetCommandContext(Pipeline, FRHIGPUMask::All()); // This mask argument specifies which contexts are included in an mGPU redirector (we always want all of them).
					check(Context);
				}

				if (bSinglePipeline)
				{
					ExecutingCmdList.GraphicsContext = nullptr;
					ExecutingCmdList.ComputeContext = Context;
				}
			}
			break;
			}

			// (Re-)apply the current GPU mask.
			Context->RHISetGPUMask(ExecutingCmdList.PersistentState.CurrentGPUMask);
			Context->SetExecutingCommandList(&ExecutingCmdList);

#if WITH_RHI_BREADCRUMBS
			FRHIBreadcrumbNode* Target = Command->Target;
			check(EnumHasAllFlags(Command->Pipelines, Pipeline));
			check(Target != FRHIBreadcrumbNode::Sentinel);

			FRHIBreadcrumbNode*& Current = ExecutingCmdList.GPUBreadcrumbState[Pipeline].Current;
			check(Current != FRHIBreadcrumbNode::Sentinel);

			if (Current != Target)
			{
				//
				// The breadcrumb currently at the top of the new context's GPU stack is not the same as the current breadcrumb on the CPU stack.
				// This happens when we switch to a new pipeline after pushing breadcrumbs on a different one.
				//
				// Fix up the breadcrumbs by pushing/popping the difference (i.e. pop down to the common ancestor, then push up to the current GPU breadcrumb).
				// Use the RHI begin/end command directly to ensure breadcrumbs get appended to the GPU pipeline ranges etc.
				//

				FRHIBreadcrumbNode const* CommonAncestor = FRHIBreadcrumbNode::FindCommonAncestor(Current, Target);
				while (Current != CommonAncestor)
				{
					FRHIComputeCommandList::Get(ExecutingCmdList).EndBreadcrumbGPU(Current, Pipeline);
				}

				auto Recurse = [CommonAncestor, &ExecutingCmdList, Pipeline](FRHIBreadcrumbNode* Current, auto& Recurse) -> void
				{
					if (Current == CommonAncestor)
						return;

					Recurse(Current->GetParent(), Recurse);
					FRHIComputeCommandList::Get(ExecutingCmdList).BeginBreadcrumbGPU(Current, Pipeline);
				};
				Recurse(Target, Recurse);

				check(Target == Current);
			}

			ExecutingCmdList.GPUBreadcrumbState[Pipeline].Latest = Current;
#endif
		}
	});
}

ERHIPipeline FRHICommandListBase::SwitchPipeline(ERHIPipeline Pipeline)
{
	checkf(Pipeline == ERHIPipeline::None || FMath::IsPowerOfTwo(std::underlying_type_t<ERHIPipeline>(Pipeline)), TEXT("Only one pipeline may be active at a time."));
	ERHIPipeline Original = ActivePipelines;
	ActivatePipelines(Pipeline);
	return Original;
}

void FRHICommandListBase::Execute()
{
	check(!IsExecuting());
	bExecuting = true;

	PersistentState.CurrentGPUMask = PersistentState.InitialGPUMask;

#if WITH_RHI_BREADCRUMBS && WITH_ADDITIONAL_CRASH_CONTEXTS
	FScopedAdditionalCrashContextProvider CrashContext(
	[
		this,
		ThreadName = 
			  IsInRHIThread()             ? TEXT("RHIThread")
			: IsInActualRenderingThread() ? TEXT("RenderingThread")
			: IsInGameThread()            ? TEXT("GameThread")
			:                               TEXT("Parallel")
	](FCrashContextExtendedWriter& Writer)
	{
		if (PersistentState.LocalBreadcrumb)
		{
			PersistentState.LocalBreadcrumb->WriteCrashData(Writer, ThreadName);
		}
	});
#endif // WITH_ADDITIONAL_CRASH_CONTEXTS

	FRHICommandListIterator Iter(*this);
	while (Iter.HasCommandsLeft())
	{
		FRHICommandBase* Cmd = Iter.NextCommand();
		Cmd->ExecuteAndDestruct(*this);
	}
}

struct FRHICommandListExecutor::FTaskPipe::FTask
{
	TFunction<void()> Lambda;
	FGraphEventArray Prereqs;

	std::atomic<FTask*> Next{ nullptr };
	std::atomic<uint32> RefCount{ 2 }; // Tasks always start with 2 references: the producer and the consumer.

	ENamedThreads::Type LogicalThread;
	ENamedThreads::Type ActualThread;

	FTask(ENamedThreads::Type NamedThread, FGraphEventArray&& Prereqs, TFunction<void()>&& Lambda)
		: Lambda       (MoveTemp(Lambda))
		, Prereqs      (MoveTemp(Prereqs))
		, LogicalThread(NamedThread)
		, ActualThread (NamedThread)
	{
		if (GIsRunningRHIInTaskThread_InternalUseOnly && NamedThread == ENamedThreads::RHIThread)
		{
			// In RHI tasks mode, we don't have an actual RHI thread. Override the thread with any high priority parallel worker thread.
			// The task we execute gets tagged with ETaskTag::ERhiThread, and these tasks will run in-order due to task dependencies.
			ActualThread = ENamedThreads::AnyHiPriThreadNormalTask;
		}
	}

	void Release()
	{
		if (RefCount.fetch_sub(1) == 1)
		{
			delete this;
		}
	}

	// Memory pool for fast alloc of these FTask structs
	static TLockFreePointerListUnordered<void, PLATFORM_CACHE_LINE_SIZE> MemoryPool;

	void* operator new(size_t Size)
	{
		check(Size == sizeof(FTask));

		void* Memory = MemoryPool.Pop();
		if (!Memory)
		{
			Memory = FMemory::Malloc(sizeof(FTask), alignof(FTask));
		}
		return Memory;
	}

	void operator delete(void* Pointer)
	{
		MemoryPool.Push(Pointer);
	}
};

TLockFreePointerListUnordered<void, PLATFORM_CACHE_LINE_SIZE> FRHICommandListExecutor::FTaskPipe::FTask::MemoryPool;

FGraphEventRef FRHICommandListExecutor::FTaskPipe::LaunchTask(FTask* Task) const
{
	// Since we're adding a task to the TaskGraph, we can ask the TG to wait
	// for the prereqs rather than doing it ourselves in the task lambda.
	FGraphEventArray Prereqs = MoveTemp(Task->Prereqs);

	return FFunctionGraphTask::CreateAndDispatchWhenReady(
		[this, Task](ENamedThreads::Type NamedThread, FGraphEventRef const& CurrentEvent) mutable
		{
			check(NamedThread == Task->ActualThread);
			Execute(Task, CurrentEvent);
		}
		, QUICK_USE_CYCLE_STAT(RHITaskPipe, STATGROUP_TaskGraphTasks)
		, &Prereqs
		, Task->ActualThread
	);
}

void FRHICommandListExecutor::FTaskPipe::Enqueue(ENamedThreads::Type NamedThread, FGraphEventArray&& Prereqs, TFunction<void()>&& Lambda)
{
	if (LastThread != NamedThread)
	{
		// The target thread is changing. End the previous task chain and start a new one.
		FGraphEventRef PrevEvent = Close();
		if (PrevEvent)
		{
			Prereqs.Add(PrevEvent);
		}

		LastThread = NamedThread;
	}

	FTask* Existing = Current;
	Current = new FTask(NamedThread, MoveTemp(Prereqs), MoveTemp(Lambda));

	// Attempt to append the new task to an existing task.
	if (Existing)
	{
		FTask* Expected = nullptr;
		if (Existing->Next.compare_exchange_strong(Expected, Current))
		{
			// Appended task to existing one.
			check(Expected == nullptr);
			Existing->Release();

			return;
		}
		else
		{
			check(Expected == Existing);
			Existing->Release();
		}
	}

	// Failed to append, or no running task. Start a new one...
	LastEvent = LaunchTask(Current);
}

FGraphEventRef FRHICommandListExecutor::FTaskPipe::Close()
{
	// Split the task chain by releasing 'Current'.
	// The next Enqueue will start a new chain (i.e. a new TaskGraph task).
	if (Current)
	{
		Current->Release();
		Current = nullptr;
	}

	// This event will be signalled when the last task in the closed chain has completed.
	return LastEvent;
}

void FRHICommandListExecutor::FTaskPipe::Execute(FTask* Task, FGraphEventRef const& CurrentEvent) const
{
	struct FParallelThreadScope
	{
		FOptionalTaskTagScope TaskTag;
		FParallelThreadScope()
			: TaskTag(ETaskTag::EParallelRhiThread)
		{}
	};

	struct FRHIThreadScope
	{
		// This lock is used to implement StallRHIThread()
		FScopeLock StallCSLock;

		TOptional<FOptionalTaskTagScope> TaskTag;

		// Task threads acting as the RHI thread must take ownership of the RHI before calling platform APIs.
		TOptional<FScopedRHIThreadOwnership> ThreadOwnershipScope;

		FRHIThreadScope()
			: StallCSLock(&GRHIThreadOnTasksCritical) 
		{
			if (GIsRunningRHIInTaskThread_InternalUseOnly)
			{
				// Task threads must take ownership of the RHI before calling platform APIs.
				TaskTag.Emplace(ETaskTag::ERhiThread);
				ThreadOwnershipScope.Emplace(true);
			}
		}
	};

	TVariant<FEmptyVariantState, FParallelThreadScope, FRHIThreadScope> ThreadScope;
	if (Task->LogicalThread == ENamedThreads::RHIThread)
	{
		ThreadScope.Emplace<FRHIThreadScope>();
	}
	else if (Task->LogicalThread != ENamedThreads::GetRenderThread_Local())
	{
		ThreadScope.Emplace<FParallelThreadScope>();
	}

Restart:
	// Prereqs will be empty if we used the TaskGraph to await them.
	if (Task->Prereqs.Num())
	{
		// We'll only get here for chained tasks that haven't been processed by the TaskGraph.
		for (FGraphEventRef& Event : Task->Prereqs)
		{
			if (Event && !Event->IsComplete())
			{
				// There is at least one unresolved prerequisite.
				// Break from the loop and add a new task to continue once the prereqs are resolved.
				FGraphEventRef NewEvent = LaunchTask(Task);

				// Extend the lifetime of the current task event.
				CurrentEvent->DontCompleteUntil(NewEvent);
				return;
			}
		}
	}

	// All prereqs are resolved (if any). Run the task.
	Task->Lambda();

	// Attempt to close the command chain.
	FTask* Expected = nullptr;
	bool bClosed = Task->Next.compare_exchange_strong(Expected, Task);
	Task->Release();

	if (!bClosed)
	{
		// Another task was appended before we closed the chain.
		check(Expected != nullptr && Expected != Task);
		Task = Expected;

		// Execute the next task in the chain.
		goto Restart;
	}
}

bool FRHICommandListExecutor::AllowParallel() const
{
	return !Bypass() && IsRunningRHIInSeparateThread();
}

void FRHICommandListExecutor::AddNextDispatchPrerequisite(FGraphEventRef Prereq)
{
	check(IsInRenderingThread());
	NextDispatchTaskPrerequisites.Add(MoveTemp(Prereq));
}

FRHICommandListExecutor::FTaskPipe* FRHICommandListExecutor::EnqueueDispatchTask(FGraphEventArray&& Prereqs, TFunction<void()>&& Lambda)
{
	check(IsInRenderingThread());
	ENamedThreads::Type NamedThread = !AllowParallel()
		? ENamedThreads::GetRenderThread_Local()
		: ENamedThreads::AnyHiPriThreadHiPriTask;

	// Append any additional dispatch prerequisites.
	NextDispatchTaskPrerequisites.Append(MoveTemp(Prereqs));

	DispatchPipe.Enqueue(NamedThread, MoveTemp(NextDispatchTaskPrerequisites), MoveTemp(Lambda));
	return &DispatchPipe;
}

FRHICommandListExecutor::FTaskPipe* FRHICommandListExecutor::FTranslateState::EnqueueTranslateTask(FGraphEventArray&& Prereqs, TFunction<void()>&& Lambda)
{
	// This is called on the dispatch thread

	ENamedThreads::Type NamedThread = ENamedThreads::AnyHiPriThreadHiPriTask;
	FTaskPipe* Pipe = &TranslatePipe;

	if (!GRHICommandList.AllowParallel())
	{
		NamedThread = ENamedThreads::GetRenderThread_Local();
		Pipe = &GRHICommandList.RHIThreadPipe;
	}
	else if (!bParallel)
	{
		NamedThread = ENamedThreads::RHIThread;
		Pipe = &GRHICommandList.RHIThreadPipe;
	}

	Pipe->Enqueue(NamedThread, MoveTemp(Prereqs), MoveTemp(Lambda));
	return Pipe;
}

FRHICommandListExecutor::FTaskPipe* FRHICommandListExecutor::EnqueueSubmitTask(FGraphEventArray&& Prereqs, TFunction<void()>&& Lambda)
{
	// This is called on the dispatch thread

	ENamedThreads::Type NamedThread = !AllowParallel()
		? ENamedThreads::GetRenderThread_Local()
		: ENamedThreads::RHIThread;

	RHIThreadPipe.Enqueue(NamedThread, MoveTemp(Prereqs), MoveTemp(Lambda));
	return &RHIThreadPipe;
}

void FRHICommandListExecutor::FSubmitState::FinalizeCurrent()
{
	TranslateEvents.Add(CurrentTranslateJob->Finalize());
	CurrentTranslateJob = nullptr;
}

void FRHICommandListExecutor::FSubmitState::Dispatch(FRHICommandListBase* CmdList)
{
	check(CmdList->DispatchEvent->IsComplete());
	CmdList->bAllowParallelTranslate = bAllowParallelTranslate;

#if WITH_RHI_BREADCRUMBS
	CmdList->CPUBreadcrumbState.bEmitBreadcrumbs = bEmitBreadcrumbs;
#endif

	if (CurrentTranslateJob)
	{
		//
		// Determine if the current translate batch should be closed, i.e.:
		//    - We've exceeded the threshold number of RHI commands.
		//    - The next command list requires single-threaded execution, but we're in a parallel batch.
		//
		if ((bAllowSingleParallelCombine
				? CurrentTranslateJob->bParallel && !CmdList->AllowParallelTranslate() // Only start a new translate job if we were parallel, but the new command list requires single thread.
				: CurrentTranslateJob->bParallel !=  CmdList->AllowParallelTranslate() // Always start a new translate job if bParallel is different. Avoids batching parallel work into single thread translates.
			) || (
				MaxCommandsPerTranslate  < 0 ? true  :
				MaxCommandsPerTranslate == 0 ? false :
				(CurrentTranslateJob->NumCommands + CmdList->NumCommands) > uint32(MaxCommandsPerTranslate)
			))
		{
			FinalizeCurrent();
		}
	}

	if (!CurrentTranslateJob)
	{
		// Start a new translate job
		CurrentTranslateJob = TranslateJobs.Emplace_GetRef(MakeUnique<FTranslateState>()).Get();
		CurrentTranslateJob->bParallel = CmdList->AllowParallelTranslate();
	}

	CurrentTranslateJob->NumCommands += CmdList->NumCommands;

	{
	#if WITH_RHI_BREADCRUMBS
		// Fixup unknown breadcrumb parents
		for (FRHIBreadcrumbNode* Node : CmdList->CPUBreadcrumbState.UnknownParentList.IterateAndUnlink())
		{
			check(Node->GetParent() == FRHIBreadcrumbNode::Sentinel);
			Node->SetParent(GRHICommandList.Breadcrumbs.CPU.Current);
		}

		{
			// Grab the first breadcrumb in both the CPU and GPU pipeline stacks, and add references to them.
			FRHIBreadcrumbNode* CPUFirst = GRHICommandList.Breadcrumbs.CPU.Current;
			check(CPUFirst != FRHIBreadcrumbNode::Sentinel);
			if (CPUFirst)
			{
				CmdList->BreadcrumbAllocatorRefs.AddUnique(CPUFirst->Allocator);
			}

			TRHIPipelineArray<FRHIBreadcrumbNode*> GPUFirst;
			for (ERHIPipeline Pipeline : MakeFlagsRange(ERHIPipeline::All))
			{
				FRHIBreadcrumbNode* Node = GRHICommandList.Breadcrumbs.GPU[Pipeline].Current;
				check(Node != FRHIBreadcrumbNode::Sentinel);

				GPUFirst[Pipeline] = Node;

				if (Node)
				{
					CmdList->BreadcrumbAllocatorRefs.AddUnique(Node->Allocator);
				}
			}

			// Walk the ActivatePipeline commands, resolve unknown targets, and update per-pipe pointers.
			for (FRHICommandListBase::FActivatePipelineCommand* Command = CmdList->ActivatePipelineCommands.First; Command; Command = Command->Next)
			{
				if (Command->Target == FRHIBreadcrumbNode::Sentinel)
				{
					Command->Target = GRHICommandList.Breadcrumbs.CPU.Current;
				}
				else
				{
					GRHICommandList.Breadcrumbs.CPU.Current = Command->Target;
				}

				for (ERHIPipeline Pipeline : MakeFlagsRange(Command->Pipelines))
				{
					GRHICommandList.Breadcrumbs.GPU[Pipeline].Current = GRHICommandList.Breadcrumbs.CPU.Current;
				}
			}

			for (ERHIPipeline Pipeline : MakeFlagsRange(ERHIPipeline::All))
			{
				if (CmdList->GPUBreadcrumbState[Pipeline].Latest.IsSet())
				{
					// A Begin/End happened on this pipeline after the last ActivatePipeline command.
					FRHIBreadcrumbNode* Node = CmdList->GPUBreadcrumbState[Pipeline].Latest.GetValue();
					if (Node != FRHIBreadcrumbNode::Sentinel)
					{
						GRHICommandList.Breadcrumbs.GPU[Pipeline].Current = Node;
					}
					else
					{
						GRHICommandList.Breadcrumbs.GPU[Pipeline].Current = GPUFirst[Pipeline];
					}
				}

				// Rewind GPU state
				CmdList->GPUBreadcrumbState[Pipeline].Current = GPUFirst[Pipeline];
			}

			if (CmdList->CPUBreadcrumbState.Current != FRHIBreadcrumbNode::Sentinel)
			{
				GRHICommandList.Breadcrumbs.CPU.Current = CmdList->CPUBreadcrumbState.Current;
			}
			else
			{
				GRHICommandList.Breadcrumbs.CPU.Current = CPUFirst;
			}

			// Rewind CPU state
			CmdList->CPUBreadcrumbState.Current = CPUFirst;
			CmdList->PersistentState.LocalBreadcrumb = CPUFirst;
		}
	#endif // WITH_RHI_BREADCRUMBS

	#if HAS_GPU_STATS
		CmdList->InitialDrawStatsCategory = GRHICommandList.CurrentDrawStatsCategory;

		if (CmdList->PersistentState.CurrentDrawStatsCategory.IsSet())
		{
			GRHICommandList.CurrentDrawStatsCategory = CmdList->PersistentState.CurrentDrawStatsCategory.GetValue();
		}

		CmdList->PersistentState.CurrentDrawStatsCategory = CmdList->InitialDrawStatsCategory;
	#endif

		FGraphEventArray Prereqs;
		if (!CmdList->AllowParallelTranslate())
		{
			// Wait for all previous translate jobs to complete
			Prereqs = MoveTemp(TranslateEvents);
		}

		// Also wait for the previous mutate fence (blocks future translates until the fence has signalled).
		Prereqs.Add(GRHICommandList.LastMutate);

		if (CmdList->LastLockFenceCommand)
		{
			check(CmdList->LastLockFenceCommand->Fence);
			GRHICommandList.LastMutate = CmdList->LastLockFenceCommand->Fence;
		}

		//
		// Don't start new translations until all prior submissions have been made.
		// This is because work inside RHIEndFrame needs to complete on the RHI thread before any new translates can start.
		// 
		// Also some RHI commands directly submit to GPU queues from within the RHI (during RHICmdList translation).
		// Not waiting for prior submits means these internal submissions can happen out-of-order with respect to other translations.
		// E.g. some platform implementations of EndDrawingViewport() submit directly to the GPU to perform the flip / present.
		//
		Prereqs.Add(GRHICommandList.LastSubmit);

		CurrentTranslateJob->EnqueueTranslateTask(MoveTemp(Prereqs),
			[TranslateJob = CurrentTranslateJob, CmdList]()
			{
				SCOPED_NAMED_EVENT(RHI_Translate, FColor::White);
				TranslateJob->Translate(CmdList);
			}
		);
	}
}

void FRHICommandListExecutor::FTranslateState::Translate(FRHICommandListBase* CmdList)
{
	// Apply the current translate job's contexts to the command list
	for (ERHIPipeline Pipeline : MakeFlagsRange(ERHIPipeline::All))
	{
		if (!CmdList->Contexts[Pipeline])
		{
			CmdList->Contexts[Pipeline] = PipelineStates[Pipeline].Context;
		}
	}

	if(!CmdList->UploadContext)
	{
		CmdList->UploadContext = UploadContextState;
	}
	
	CmdList->ActivePipelines = ERHIPipeline::None;

#if WITH_RHI_BREADCRUMBS
	// Walk into the breadcrumb tree to the first breadcrumb this RHI command list starts in.
	FRHIBreadcrumbNode::WalkIn(CmdList->CPUBreadcrumbState.Current);
#endif

	// Replay the recorded commands. The Contexts array accumulates any used
	// contexts depending on the ActivatePipeline commands that were recorded.
	CmdList->Execute();

#if WITH_RHI_BREADCRUMBS
	// Walk back out of the breadcrumb tree
	FRHIBreadcrumbNode::WalkOut(CmdList->CPUBreadcrumbState.Current);
#endif

	// Extract the contexts from the command list, so we can reuse them for future command lists.
	for (ERHIPipeline Pipeline : MakeFlagsRange(ERHIPipeline::All))
	{
		auto& TranslateContext = PipelineStates[Pipeline].Context;
		check(!TranslateContext || TranslateContext == CmdList->Contexts[Pipeline]);

		if (!TranslateContext)
		{
			TranslateContext = CmdList->Contexts[Pipeline];
		}

#if WITH_RHI_BREADCRUMBS
		// Link the command list's GPU breadcrumb range into the outer translate job's range.
		auto& CmdListState = CmdList->GPUBreadcrumbState[Pipeline];
		auto& TranslateState = PipelineStates[Pipeline];
		TranslateState.Range.InsertAfter(CmdListState.Range, TranslateState.Range.Last, Pipeline);
#endif // WITH_RHI_BREADCRUMBS
	}
	
	UploadContextState = CmdList->UploadContext;

#if WITH_RHI_BREADCRUMBS
	BreadcrumbAllocatorRefs.Append(MoveTemp(CmdList->BreadcrumbAllocatorRefs));
#endif
	DrawStats.Accumulate(CmdList->DrawStats);

	delete CmdList;
}

FGraphEventRef FRHICommandListExecutor::FTranslateState::Finalize()
{
	FTaskPipe* Pipe = EnqueueTranslateTask({},
		[this]()
		{
			SCOPED_NAMED_EVENT(RHI_Finalize, FColor::White);

			TRHIPipelineArray<IRHIPlatformCommandList*> PlatformCommandLists {InPlace, nullptr};
			FDynamicRHI::FRHIFinalizeContextArgs FinalizeArgs;
			for (auto& State : PipelineStates)
			{
				if (State.Context)
				{
					FinalizeArgs.Contexts.Add(State.Context);
				}
			}
		
			if(GDynamicRHI)
			{
				FinalizeArgs.UploadContext = UploadContextState;
				
				GDynamicRHI->RHIFinalizeContext(MoveTemp(FinalizeArgs), PlatformCommandLists);
				
				for (ERHIPipeline Pipeline : MakeFlagsRange(ERHIPipeline::All))
				{
					auto& State = PipelineStates[Pipeline]; 
					if (State.Context)
					{
						State.FinalizedCmdList = PlatformCommandLists[Pipeline];
					}
				}
			}
		}
	);

	if (Pipe == &TranslatePipe)
	{
		return Pipe->Close();
	}
	else
	{
		check(Pipe == &GRHICommandList.RHIThreadPipe);

		// Don't close the pipe if we got scheduled on the RHI thread pipe, to avoid splitting tasks.
		// Since the pipes guarantee FIFO order, we don't need an event for the submission to await.
		return {};
	}
}

void FRHICommandListExecutor::FSubmitState::Submit(const FSubmitArgs& Args)
{
	// Coalesce finalized platform command lists into a single array
	TArray<IRHIPlatformCommandList*> FinalizedCmdLists;
	for (TUniquePtr<FTranslateState> const& Job : TranslateJobs)
	{
		for (ERHIPipeline Pipeline : MakeFlagsRange(ERHIPipeline::All))
		{
			auto& TranslateState = Job->PipelineStates[Pipeline];
			if (TranslateState.FinalizedCmdList)
			{
#if WITH_RHI_BREADCRUMBS
				TranslateState.FinalizedCmdList->BreadcrumbAllocators = Job->BreadcrumbAllocatorRefs;

				auto& Allocators = TranslateState.FinalizedCmdList->BreadcrumbAllocators;

				auto& GlobalLast = GRHICommandList.Breadcrumbs.GPU[Pipeline].Last;
				// Link ranges of breadcrumbs together into depth-first list
				if (GlobalLast)
				{
					FRHIBreadcrumbNode*& Next = GlobalLast->GetNextPtr(Pipeline);
					check(!Next);
					Next = TranslateState.Range.First;
				}

				FRHIBreadcrumbRange Range{};
				Range.First = GlobalLast ? GlobalLast.Get() : TranslateState.Range.First;
				if (Range.First) { Allocators.AddUnique(Range.First->Allocator); }

				// Advance the global last breadcrumb forward
				if (TranslateState.Range.Last)
				{
					GlobalLast = TranslateState.Range.Last;
				}

				Range.Last = GlobalLast ? GlobalLast.Get() : Range.First;
				if (Range.Last) { Allocators.AddUnique(Range.Last->Allocator); }

				TranslateState.FinalizedCmdList->BreadcrumbRange = Range;
#endif

				FinalizedCmdLists.Add(TranslateState.FinalizedCmdList);
			}
		}

		GRHICommandList.FrameDrawStats.Accumulate(Job->DrawStats);
	}

	if (GDynamicRHI)
	{
		GDynamicRHI->RHISubmitCommandLists({ MoveTemp(FinalizedCmdLists) });
	}
	else
	{
		check(FinalizedCmdLists.IsEmpty());
	}

	{
		SCOPED_NAMED_EVENT(DeleteRHIResources, FColor::Magenta);
		while (true)
		{
			// If the RHI thread will be flushed, keep processing the RHI resource delete queue until it is empty.
			if (EnumHasAllFlags(SubmitFlags, ERHISubmitFlags::FlushRHIThread | ERHISubmitFlags::DeleteResources))
			{
				FRHIResource::GatherResourcesToDelete(ResourcesToDelete, bIncludeExtendedLifetimeResources);
			}

			if (!ResourcesToDelete.IsEmpty())
			{
				FRHIResource::DeleteResources(ResourcesToDelete);
				ResourcesToDelete.Reset();
			}
			else
			{
				break;
			}
		}
	}

	if (GDynamicRHI && EnumHasAllFlags(SubmitFlags, ERHISubmitFlags::DeleteResources))
	{
		GDynamicRHI->RHIProcessDeleteQueue();
	}

	if (EnumHasAnyFlags(SubmitFlags, ERHISubmitFlags::EndFrame))
	{
		FDynamicRHI::FRHIEndFrameArgs EndFrameArgs
		{
			.FrameNumber = GRHICommandList.FrameNumber++,
#if WITH_RHI_BREADCRUMBS
			.GPUBreadcrumbs = Args.GPUBreadcrumbs
#endif
		};

		GDynamicRHI->RHIEndFrame(EndFrameArgs);
		GRHICommandList.FrameDrawStats.ProcessAsFrameStats();
	}

	CompletionEvent->DispatchSubsequents();
	delete this;
}

RHI_API void FRHICommandListExecutor::Submit(TConstArrayView<FRHICommandListBase*> AdditionalCommandLists, ERHISubmitFlags SubmitFlags)
{
	check(IsInRenderingThread());
	SCOPED_NAMED_EVENT(RHICmdList_Submit, FColor::White);

	if (Bypass())
	{
		// Always submit to the GPU in Bypass mode. This allows us to wait for all translate tasks to 
		// complete before  returning from this function. ensuring commands are always executed in-order.
		EnumAddFlags(SubmitFlags, ERHISubmitFlags::SubmitToGPU);
	}

	// Commands may already be queued on the immediate command list. These need to be executed
	// first before any parallel commands can be inserted, otherwise commands will run out-of-order.
	FRHICommandListBase* ImmCmdList;
	{
		SCOPE_CYCLE_COUNTER(STAT_ImmedCmdListExecuteTime);
		INC_MEMORY_STAT_BY(STAT_ImmedCmdListMemory, CommandListImmediate.GetUsedMemory());
		INC_DWORD_STAT_BY(STAT_ImmedCmdListCount, CommandListImmediate.NumCommands);

		FRHIGPUMask Temp = CommandListImmediate.PersistentState.CurrentGPUMask;

		// Move the contents of the immediate command list into a new heap-allocated instance.
		ImmCmdList = new FRHICommandListBase(MoveTemp(static_cast<FRHICommandListBase&>(CommandListImmediate)));

		// Now reset the immediate command list.

		// Destruct and reconstruct the base type in-place to reset all members to
		// their defaults, taking a copy of the persistent state we just moved.
		static_cast<FRHICommandListBase&>(CommandListImmediate).~FRHICommandListBase();
		new (&CommandListImmediate) FRHICommandListBase(ImmCmdList->PersistentState);

		// The initial GPU mask must be updated here to preserve the last mask set on the immediate command list.
		// If we don't do this, the first set of commands recorded in the immediate command list after an
		// Execute/Reset will inherit the wrong mask.
		CommandListImmediate.PersistentState.InitialGPUMask = Temp;

		ImmCmdList->FinishRecording();
	}

	TArray<FRHICommandListBase*> AllCmdLists;
	AllCmdLists.Reserve(AdditionalCommandLists.Num() + 1);
	AllCmdLists.Add(ImmCmdList);
	AllCmdLists.Append(AdditionalCommandLists);

	//
	// Submission of RHI command lists involves a chain of "dispatch" tasks.
	// These tasks wait for FinishRecording() to be called on each RHI command list, then start translate tasks to replay those command lists into RHI contexts.
	// The dispatch tasks are chained together so that they run "single threaded", in the same order the corresponding RHI command lists were submitted in.
	// 
	// RHI contexts may have multiple RHI command lists replayed into them. The translate tasks leave the contexts open. They are eventually finalized
	// by a task in FTranslateState::Finalize() that runs after the batch of translates have completed for that context. Multiple translates for 
	// different contexts are allowed to run in parallel. Specific RHI command lists require single-threaded execution on the RHI thread.
	// 
	// Once all finalized tasks have completed, FSubmitState::Submit() called which hands the platform GPU command lists down to the RHI.
	// 
	// If threaded rendering is disabled, the tasks are enqueued to the render thread local queue, but the tasks and dependencies are the same.
	//

	if (!SubmitState)
	{
		SubmitState = new FSubmitState();
		FGraphEventRef NewCompletionEvent = FGraphEvent::CreateGraphEvent();
		NewCompletionEvent->SetDebugName(TEXT("FRHICommandListExecutor::Submit::CompletionEvent"));

		if (CompletionEvent)
		{
			NewCompletionEvent->DontCompleteUntil(CompletionEvent);
		}

		CompletionEvent = NewCompletionEvent;
		SubmitState->CompletionEvent = CompletionEvent;

#if WITH_RHI_BREADCRUMBS
		SubmitState->bEmitBreadcrumbs = bEmitBreadcrumbs;
#endif

		// Prevent use of parallel contexts if unsupported by the RHI, while the legacy 'profilegpu' command is active, or while disabled by the cvar.
		if (GRHISupportsParallelRHIExecute && !GTriggerGPUProfile && CVarRHICmdParallelTranslateEnable.GetValueOnRenderThread())
		{
			SubmitState->MaxCommandsPerTranslate     = CVarRHICmdParallelTranslateMaxCommandsPerTranslate.GetValueOnRenderThread();
			SubmitState->bAllowSingleParallelCombine = CVarRHICmdParallelTranslateCombineSingleAndParallel.GetValueOnRenderThread();
			SubmitState->bAllowParallelTranslate     = true;
		}
		else
		{
			// When parallel translate is disabled, allow translate chains to grow regardless of the total number
			// of recorded commands. There's no point splitting translate chains as there's no parallelism to gain.
			SubmitState->MaxCommandsPerTranslate     = 0;
			SubmitState->bAllowSingleParallelCombine = true;
			SubmitState->bAllowParallelTranslate     = false;
		}
	}
	
	if (EnumHasAnyFlags(SubmitFlags, ERHISubmitFlags::SubmitToGPU))
	{
		extern int32 GRHIResourceLifetimeRefCount;
		SubmitState->bIncludeExtendedLifetimeResources = GRHIResourceLifetimeRefCount == 0;
		SubmitState->SubmitFlags = SubmitFlags;

		if (EnumHasAnyFlags(SubmitFlags, ERHISubmitFlags::DeleteResources))
		{
			// If we'll be flushing the RHI thread, leave gathering resources to the RHI thread rather than doing it here.
			if (!EnumHasAnyFlags(SubmitFlags, ERHISubmitFlags::FlushRHIThread))
			{
				FRHIResource::GatherResourcesToDelete(SubmitState->ResourcesToDelete, SubmitState->bIncludeExtendedLifetimeResources);
			}
		}
	}

	// Dispatch each command list
	for (int32 Index = 0; Index < AllCmdLists.Num(); ++Index)
	{
		FRHICommandListBase* CmdList = AllCmdLists[Index];

		// Accumulate dispatch ready events into the WaitOutstandingTasks list.
		// This is used by FRHICommandListImmediate::WaitForTasks() when the render thread 
		// wants to block until all parallel RHICmdList recording tasks are completed.
		WaitOutstandingTasks.Add(CmdList->DispatchEvent);

		FGraphEventArray Prereqs;
		Prereqs.Add(CmdList->DispatchEvent);

		EnqueueDispatchTask(MoveTemp(Prereqs),
			[State = SubmitState, CmdList]()
			{
				SCOPED_NAMED_EVENT(RHI_Dispatch, FColor::White);
				State->Dispatch(CmdList);
			}
		);
	}

	if (EnumHasAnyFlags(SubmitFlags, ERHISubmitFlags::SubmitToGPU))
	{
		EnqueueDispatchTask({},
			[this, State = SubmitState]() mutable
			{
				SCOPED_NAMED_EVENT(RHI_FinalizeAndSubmit, FColor::White);

				// Finalize the last translate job
				State->FinalizeCurrent();

				FSubmitState::FSubmitArgs Args;
#if WITH_RHI_BREADCRUMBS
				for (ERHIPipeline Pipeline : MakeFlagsRange(ERHIPipeline::All))
				{
					Args.GPUBreadcrumbs[Pipeline] = GRHICommandList.Breadcrumbs.GPU[Pipeline].Current;
				}
#endif

				// Submission thread
				EnqueueSubmitTask(MoveTemp(State->TranslateEvents),
					[State, Args]() mutable
					{
						SCOPED_NAMED_EVENT(RHI_SubmitToGPU, FColor::White);
						State->Submit(Args);
					}
				);
				LastSubmit = RHIThreadPipe.Close();
			}
		);

		SubmitState = nullptr;
	}

	// Optionally wait for the RHI thread (and pipeline) to complete all outstanding work
	bool bWaitForCompletion = 
		(EnumHasAnyFlags(SubmitFlags, ERHISubmitFlags::FlushRHIThread)) ||
		(EnumHasAnyFlags(SubmitFlags, ERHISubmitFlags::SubmitToGPU) && !AllowParallel());

	if (bWaitForCompletion)
	{
		SCOPED_NAMED_EVENT(RHICmdList_FlushRHIThread, FColor::Red);

		// We've just submitted to the GPU above, so we only need to wait for the CompletionEvent event.
		if (CompletionEvent && !CompletionEvent->IsComplete())
		{
			CSV_SCOPED_TIMING_STAT(RHITFlushes, FlushRHIThreadTotal);
			SCOPE_CYCLE_COUNTER(STAT_ExplicitWaitRHIThread);

			FTaskGraphInterface::Get().WaitUntilTaskCompletes(CompletionEvent, ENamedThreads::GetRenderThread_Local());
		}

		// Call WaitForTasks to reset the array (these tasks should already be complete).
		WaitForTasks();
	}

#if CAN_TOGGLE_COMMAND_LIST_BYPASS
	if (EnumHasAnyFlags(SubmitFlags, ERHISubmitFlags::EnableBypass | ERHISubmitFlags::DisableBypass))
	{
		checkf(EnumHasAnyFlags(SubmitFlags, ERHISubmitFlags::FlushRHIThread), TEXT("Must flush the RHI thread when toggling Bypass."));

		bLatchedBypass = EnumHasAnyFlags(SubmitFlags, ERHISubmitFlags::EnableBypass);
	}
#endif

#if WITH_RHI_BREADCRUMBS
	if (EnumHasAnyFlags(SubmitFlags, ERHISubmitFlags::EnableDrawEvents | ERHISubmitFlags::DisableDrawEvents))
	{
		checkf(EnumHasAnyFlags(SubmitFlags, ERHISubmitFlags::FlushRHIThread), TEXT("Must flush the RHI thread when toggling draw events."));
		checkf(Breadcrumbs.CPU.Current == nullptr, TEXT("Draw events can only be toggled where there are no breadcrumbs on the stack"));

		bEmitBreadcrumbs = EnumHasAnyFlags(SubmitFlags, ERHISubmitFlags::EnableDrawEvents);
	}
#endif

	CommandListImmediate.InitializeImmediateContexts();
}

void FRHICommandListImmediate::InitializeImmediateContexts()
{
	check(Contexts[ERHIPipeline::Graphics    ] == nullptr);
	check(Contexts[ERHIPipeline::AsyncCompute] == nullptr);

	if (Bypass())
	{
#if WITH_RHI_BREADCRUMBS
		CPUBreadcrumbState.Current = GRHICommandList.Breadcrumbs.CPU.Current;
		CPUBreadcrumbState.bEmitBreadcrumbs = GRHICommandList.bEmitBreadcrumbs;
		for (ERHIPipeline Pipeline : MakeFlagsRange(ERHIPipeline::All))
		{
			GPUBreadcrumbState[Pipeline].Current = GRHICommandList.Breadcrumbs.GPU[Pipeline].Current;
			GPUBreadcrumbState[Pipeline].Latest = GRHICommandList.Breadcrumbs.GPU[Pipeline].Current;
		}
#endif

#if HAS_GPU_STATS
		InitialDrawStatsCategory = GRHICommandList.CurrentDrawStatsCategory;
#endif
	}

	// This can be called before the RHI is initialized, in which case
	// leave the immediate command list as default (contexts are nullptr).
	if (GDynamicRHI)
	{
		// The immediate command list always starts with Graphics as the active pipeline.
		SwitchPipeline(ERHIPipeline::Graphics);
	}
}

RHI_API void FRHICommandListImmediate::ImmediateFlush(EImmediateFlushType::Type FlushType, ERHISubmitFlags SubmitFlags)
{
	if (FlushType == EImmediateFlushType::WaitForOutstandingTasksOnly)
	{
		GRHICommandList.WaitForTasks();
	}
	else
	{
		if (FlushType >= EImmediateFlushType::FlushRHIThread)
		{
			EnumAddFlags(SubmitFlags, ERHISubmitFlags::FlushRHIThread);
		}

		if (FlushType >= EImmediateFlushType::FlushRHIThreadFlushResources)
		{
			EnumAddFlags(SubmitFlags, ERHISubmitFlags::DeleteResources);
		}

		EnumAddFlags(SubmitFlags, ERHISubmitFlags::SubmitToGPU);
		
		GRHICommandList.Submit({}, SubmitFlags);
	}
}

// @todo dev-pr : deprecate
RHI_API void FRHICommandListImmediate::QueueAsyncCommandListSubmit(TArrayView<FQueuedCommandList> CommandLists, ETranslatePriority /*unused ParallelTranslatePriority*/, int32 /*unused MinDrawsPerTranslate*/)
{
	TArray<FRHICommandListBase*> BaseCmdLists;
	BaseCmdLists.Reserve(CommandLists.Num());
	for (auto& CmdList : CommandLists)
	{
		BaseCmdLists.Add(CmdList.CmdList);
	}

	GRHICommandList.Submit(BaseCmdLists, ERHISubmitFlags::None);
}

FGraphEventRef FRHICommandListBase::RHIThreadFence(bool bSetLockFence)
{
	checkf(IsTopOfPipe() || Bypass(), TEXT("RHI thread fences only work when recording RHI commands (or in bypass mode)."));

	bUsesLockFence |= bSetLockFence;

	if (IsRunningRHIInSeparateThread())
	{
		FGraphEventRef Fence = nullptr;
		if (bSetLockFence && LastLockFenceCommand)
		{
			// Move the mutate event further along the command list timeline.
			Fence = MoveTemp(LastLockFenceCommand->Fence);
		}
		else
		{
			Fence = FGraphEvent::CreateGraphEvent();
			Fence->SetDebugName(TEXT("FRHICommandListBase::RHIThreadFence"));
		}

		FRHICommandRHIThreadFence* Cmd = ALLOC_COMMAND(FRHICommandRHIThreadFence)(Fence);

		if (bSetLockFence)
		{
			LastLockFenceCommand = Cmd;
		}

		return Fence;
	}

	return nullptr;
}

FRHICommandList_RecursiveHazardous::FRHICommandList_RecursiveHazardous(IRHICommandContext* Context)
	: FRHICommandList(Context->RHIGetGPUMask())
{
	ActivePipelines = ERHIPipeline::Graphics;
#if DO_CHECK
	AllowedPipelines = ActivePipelines;
#endif

	// Always grab the validation RHI context if active, so that the
	// validation RHI can see any RHI commands enqueued within the RHI itself.
	GraphicsContext = static_cast<IRHICommandContext*>(&Context->GetHighestLevelContext());
	ComputeContext = GraphicsContext;

	Contexts[ERHIPipeline::Graphics] = GraphicsContext;

	PersistentState.bRecursive = true;
}

FRHICommandList_RecursiveHazardous::~FRHICommandList_RecursiveHazardous()
{
	// @todo dev-pr remove DispatchEvent from recursive command lists so that calling FinishRecording() isn't necessary.
	FinishRecording();

	if (HasCommands())
	{
		Execute();
	}
}

FRHIComputeCommandList_RecursiveHazardous::FRHIComputeCommandList_RecursiveHazardous(IRHIComputeContext* Context)
	: FRHIComputeCommandList(Context->RHIGetGPUMask())
{
	ActivePipelines = Context->GetPipeline();
	check(IsSingleRHIPipeline(ActivePipelines));
#if DO_CHECK
	AllowedPipelines = ActivePipelines;
#endif

	// Always grab the validation RHI context if active, so that the
	// validation RHI can see any RHI commands enqueued within the RHI itself.
	GraphicsContext = nullptr;
	ComputeContext = &Context->GetHighestLevelContext();
	Contexts[ActivePipelines] = ComputeContext;

	PersistentState.bRecursive = true;
}

FRHIComputeCommandList_RecursiveHazardous::~FRHIComputeCommandList_RecursiveHazardous()
{
	// @todo dev-pr remove DispatchEvent from recursive command lists so that calling FinishRecording() isn't necessary.
	FinishRecording();

	if (HasCommands())
	{
		Execute();
	}
}

void FRHICommandListExecutor::LatchBypass()
{
	ERHISubmitFlags SubmitFlags = ERHISubmitFlags::None;

#if CAN_TOGGLE_COMMAND_LIST_BYPASS
	UE_CALL_ONCE([]()
	{
		if (FParse::Param(FCommandLine::Get(), TEXT("forcerhibypass")) && CVarRHICmdBypass.GetValueOnRenderThread() == 0)
		{
			IConsoleVariable* BypassVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.RHICmdBypass"));
			BypassVar->Set(1, ECVF_SetByCommandline);
		}
		else if (FParse::Param(FCommandLine::Get(), TEXT("parallelrendering")) && CVarRHICmdBypass.GetValueOnRenderThread() >= 1)
		{
			IConsoleVariable* BypassVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.RHICmdBypass"));
			BypassVar->Set(0, ECVF_SetByCommandline);
		}
	});

	{
		bool bNewBypass = (IsInGameThread() || (CVarRHICmdBypass.GetValueOnAnyThread() >= 1)) && !IsRunningRHIInSeparateThread();
		if (bLatchedBypass != bNewBypass)
		{
			SubmitFlags |= bNewBypass
				? ERHISubmitFlags::EnableBypass
				: ERHISubmitFlags::DisableBypass;
		}
	}
#endif

#if WITH_RHI_BREADCRUMBS
	{
		bool bNewValue = GetEmitDrawEvents();
		if (bEmitBreadcrumbs != bNewValue)
		{
			SubmitFlags |= bNewValue
				? ERHISubmitFlags::EnableDrawEvents
				: ERHISubmitFlags::DisableDrawEvents;
		}
	}
#endif // WITH_RHI_BREADCRUMBS

	if (SubmitFlags != ERHISubmitFlags::None)
	{
		CommandListImmediate.ImmediateFlush(EImmediateFlushType::FlushRHIThread, SubmitFlags);
	}

	if (bLatchedBypass || (!GSupportsParallelRenderingTasksWithSeparateRHIThread && IsRunningRHIInSeparateThread()))
	{
		bLatchedUseParallelAlgorithms = false;
	}
	else
	{
		bLatchedUseParallelAlgorithms = FApp::ShouldUseThreadingForPerformance();
	}
}

bool FRHICommandListExecutor::AreRHITasksActive()
{
	check(IsInRenderingThread());
	return GRHICommandList.CompletionEvent && !GRHICommandList.CompletionEvent->IsComplete();
}

void FRHICommandListExecutor::WaitOnRHIThreadFence(FGraphEventRef& Fence)
{
	check(IsInRenderingThread());

	// Exclude RHIT waits from the RT critical path stat (these waits simply get longer if the RT is running faster, so we don't get useful results)
	FThreadIdleStats::FScopeNonCriticalPath NonCriticalPathScope;

	if (Fence.GetReference() && !Fence->IsComplete())
	{
		{
			QUICK_SCOPE_CYCLE_COUNTER(STAT_WaitOnRHIThreadFence_Dispatch);
			GetImmediateCommandList().ImmediateFlush(EImmediateFlushType::DispatchToRHIThread); // necessary to prevent deadlock
		}
		check(IsRunningRHIInSeparateThread());
		QUICK_SCOPE_CYCLE_COUNTER(STAT_WaitOnRHIThreadFence_Wait);
		ENamedThreads::Type RenderThread_Local = ENamedThreads::GetRenderThread_Local();
		if (FTaskGraphInterface::Get().IsThreadProcessingTasks(RenderThread_Local))
		{
			// this is a deadlock. RT tasks must be done by now or they won't be done. We could add a third queue...
			UE_LOG(LogRHI, Fatal, TEXT("Deadlock in WaitOnRHIThreadFence."));
		}
		FTaskGraphInterface::Get().WaitUntilTaskCompletes(Fence, RenderThread_Local);
	}
}

void FRHICommandListExecutor::WaitForTasks(FGraphEventArray& OutstandingTasks)
{
	check(IsInRenderingThread());

	if (OutstandingTasks.Num())
	{
		bool bAny = false;
		for (int32 Index = 0; Index < OutstandingTasks.Num(); Index++)
		{
			if (!OutstandingTasks[Index]->IsComplete())
			{
				bAny = true;
				break;
			}
		}

		if (bAny)
		{
			SCOPE_CYCLE_COUNTER(STAT_ExplicitWait);
			ENamedThreads::Type RenderThread_Local = ENamedThreads::GetRenderThread_Local();
			check(!FTaskGraphInterface::Get().IsThreadProcessingTasks(RenderThread_Local));
			FTaskGraphInterface::Get().WaitUntilTasksComplete(OutstandingTasks, RenderThread_Local);
		}

		OutstandingTasks.Reset();
	}
}

bool FRHICommandListImmediate::IsStalled()
{
	return GRHIThreadStallRequestCount.load() > 0;
}

bool FRHICommandListImmediate::StallRHIThread()
{
	check(IsInRenderingThread() && IsRunningRHIInSeparateThread());

	if (GRHIThreadStallRequestCount.load() > 0)
	{
		return false;
	}

	if (!FRHICommandListExecutor::AreRHITasksActive())
	{
		return false;
	}

	CSV_SCOPED_TIMING_STAT(RHITStalls, Total);
	SCOPED_NAMED_EVENT(StallRHIThread, FColor::Red);

	const int32 OldStallCount = GRHIThreadStallRequestCount.fetch_add(1);
	if (OldStallCount > 0)
	{
		return true;
	}
	{
		SCOPE_CYCLE_COUNTER(STAT_SpinWaitRHIThreadStall);

		{
			SCOPED_NAMED_EVENT(RHIThreadLock_Wait, FColor::Red);
#if PLATFORM_USES_UNFAIR_LOCKS
			// When we have unfair locks, we're not guaranteed to get the lock between the RHI tasks if our thread goes to sleep,
			// so we need to be more aggressive here as this is time critical.
			while (!GRHIThreadOnTasksCritical.TryLock())
			{
				FPlatformProcess::YieldThread();
			}
#else
			GRHIThreadOnTasksCritical.Lock();
#endif
		}
	}
	return true;
}

void FRHICommandListImmediate::UnStallRHIThread()
{
	check(IsInRenderingThread() && IsRunningRHIInSeparateThread());
	const int32 NewStallCount = GRHIThreadStallRequestCount.fetch_sub(1) - 1;
	check(NewStallCount >= 0);
	if (NewStallCount == 0)
	{
		GRHIThreadOnTasksCritical.Unlock();
	}
}

void FRHICommandListImmediate::BeginDrawingViewport(FRHIViewport* Viewport, FRHITexture* RenderTargetRHI)
{
	check(IsImmediate() && IsInRenderingThread());
	if (Bypass())
	{
		GetContext().RHIBeginDrawingViewport(Viewport, RenderTargetRHI);
		return;
	}
	ALLOC_COMMAND(FRHICommandBeginDrawingViewport)(Viewport, RenderTargetRHI);
	if (!IsRunningRHIInSeparateThread())
	{
		// if we aren't running an RHIThread, there is no good reason to buffer this frame advance stuff and that complicates state management, so flush everything out now
		QUICK_SCOPE_CYCLE_COUNTER(BeginDrawingViewport_Flush);
		CSV_SCOPED_TIMING_STAT(RHITFlushes, BeginDrawingViewport);
		ImmediateFlush(EImmediateFlushType::FlushRHIThread);
	}
}

void FRHICommandListImmediate::EndDrawingViewport(FRHIViewport* Viewport, bool bPresent, bool bLockToVsync)
{
	// Make sure all prior graphics and async compute work has been submitted.
	// This is necessary because platform RHIs often submit additional work on the graphics queue during present, and we need to ensure we won't deadlock on async work that wasn't yet submitted by the renderer.
	// In future, Present() itself should be an enqueued / recorded command, and platform RHIs should never implicitly submit graphics or async compute work.
	ImmediateFlush(EImmediateFlushType::DispatchToRHIThread);

	check(IsImmediate() && IsInRenderingThread());
	if (Bypass())
	{
		GetContext().RHIEndDrawingViewport(Viewport, bPresent, bLockToVsync);
	}
	else
	{
		ALLOC_COMMAND(FRHICommandEndDrawingViewport)(Viewport, bPresent, bLockToVsync);

		// if we aren't running an RHIThread, there is no good reason to buffer this frame advance stuff and that complicates state management, so flush everything out now
		{
			QUICK_SCOPE_CYCLE_COUNTER(STAT_EndDrawingViewport_Dispatch);
			ImmediateFlush(EImmediateFlushType::DispatchToRHIThread);
		}
	}

	RHIAdvanceFrameForGetViewportBackBuffer(Viewport);
}

void FRHICommandListImmediate::EndFrame()
{
	check(IsInRenderingThread());
	GDynamicRHI->RHIEndFrame_RenderThread(*this);
}

void FRHIComputeCommandList::Transition(TArrayView<const FRHITransitionInfo> Infos, ERHITransitionCreateFlags CreateFlags)
{
	const ERHIPipeline Pipeline = GetPipeline();

	if (Bypass())
	{
		// Stack allocate the transition
		FMemStack& MemStack = FMemStack::Get();
		FMemMark Mark(MemStack);
		FRHITransition* Transition = new (MemStack.Alloc(FRHITransition::GetTotalAllocationSize(), FRHITransition::GetAlignment())) FRHITransition(Pipeline, Pipeline);
		GDynamicRHI->RHICreateTransition(Transition, FRHITransitionCreateInfo(Pipeline, Pipeline, CreateFlags | ERHITransitionCreateFlags::NoSplit, Infos));

		GetComputeContext().RHIBeginTransitions(MakeArrayView((const FRHITransition**)&Transition, 1));
		GetComputeContext().RHIEndTransitions(MakeArrayView((const FRHITransition**)&Transition, 1));

		// Manual release
		GDynamicRHI->RHIReleaseTransition(Transition);
		Transition->~FRHITransition();
	}
	else
	{
		// Allocate the transition in the command list
		FRHITransition* Transition = new (Alloc(FRHITransition::GetTotalAllocationSize(), FRHITransition::GetAlignment())) FRHITransition(Pipeline, Pipeline);
		GDynamicRHI->RHICreateTransition(Transition, FRHITransitionCreateInfo(Pipeline, Pipeline, CreateFlags | ERHITransitionCreateFlags::NoSplit, Infos));

		ALLOC_COMMAND(FRHICommandResourceTransition)(Transition);
	}

	for (const FRHITransitionInfo& Info : Infos)
	{
		ensureMsgf(Info.IsWholeResource(), TEXT("The Transition method only supports whole resource transitions."));

		if (FRHIViewableResource* Resource = GetViewableResource(Info))
		{
			SetTrackedAccess({ FRHITrackedAccessInfo(Resource, Info.AccessAfter) });
		}
	}
}

void FRHIComputeCommandList::Transition(TArrayView<const FRHITransitionInfo> Infos, ERHIPipeline SrcPipelines, ERHIPipeline DstPipelines, ERHITransitionCreateFlags TransitionCreateFlags)
{
#if DO_CHECK
	for (const FRHITransitionInfo& Info : Infos)
	{
		checkf(Info.IsWholeResource(), TEXT("Only whole resource transitions are allowed in FRHIComputeCommandList::Transition."));
	}
#endif

	if (!GSupportsEfficientAsyncCompute)
	{
		SrcPipelines = ERHIPipeline::Graphics;
		DstPipelines = ERHIPipeline::Graphics;
	}

	const FRHITransition* Transition = RHICreateTransition({ SrcPipelines, DstPipelines, TransitionCreateFlags, Infos });

	for (ERHIPipeline Pipeline : MakeFlagsRange(SrcPipelines))
	{
		FRHICommandListScopedPipeline Scope(*this, Pipeline);
		BeginTransition(Transition);
	}

	for (ERHIPipeline Pipeline : MakeFlagsRange(DstPipelines))
	{
		FRHICommandListScopedPipeline Scope(*this, Pipeline);
		EndTransition(Transition);
	}

	{
		// Set the tracked access on only one of the destination pipes.
		FRHICommandListScopedPipeline Scope(*this, DstPipelines == ERHIPipeline::AsyncCompute ? ERHIPipeline::AsyncCompute : ERHIPipeline::Graphics);
		SetTrackedAccess(Infos);
	}
}

void FRHIComputeCommandList::BuildAccelerationStructure(FRHIRayTracingGeometry* Geometry)
{
	FRayTracingGeometryBuildParams Params;
	Params.Geometry = Geometry;
	Params.BuildMode = EAccelerationStructureBuildMode::Build;

	FRHIBufferRange ScratchBufferRange{};
	
	FRHIResourceCreateInfo ScratchBufferCreateInfo(TEXT("RHIScratchBuffer"));
	ScratchBufferRange.Buffer = CreateBuffer(Geometry->GetSizeInfo().BuildScratchSize, BUF_StructuredBuffer | BUF_RayTracingScratch, 0, ERHIAccess::UAVCompute, ScratchBufferCreateInfo);

	BuildAccelerationStructures(MakeArrayView(&Params, 1), ScratchBufferRange);
}

void FRHIComputeCommandList::BuildAccelerationStructures(const TArrayView<const FRayTracingGeometryBuildParams> Params)
{
	// Buffer size is limited to 2Gb, thus split acceleration structure building into pieces to accomodate this limitation
	const uint64 MaxScratchMemorySize = 2147483647u;
	const uint32 ParamTotalCount = Params.Num();

	uint32 ParamIt = 0;
	while (ParamIt < ParamTotalCount)
	{
		uint32 ParamBegin = ParamIt;
		uint32 ParamCount = 0;

		// Select a sub-range of input params which fits into MaxScratchMemorySize
		uint64 TotalRequiredScratchMemorySize = 0;
		for ( ;ParamIt < ParamTotalCount; ++ParamIt)
		{
			const FRayTracingGeometryBuildParams& P = Params[ParamIt];
			uint64 ScratchBufferRequiredSize = P.BuildMode == EAccelerationStructureBuildMode::Update ? P.Geometry->GetSizeInfo().UpdateScratchSize : P.Geometry->GetSizeInfo().BuildScratchSize;
			if (ScratchBufferRequiredSize + TotalRequiredScratchMemorySize < MaxScratchMemorySize)
			{
				TotalRequiredScratchMemorySize += ScratchBufferRequiredSize;
				ParamCount++;
			}
			else
			{
				break;
			}
		}

		// Allocate scratch buffer and build the acceleration structure for the selected sub-range
		if (ParamCount > 0)
		{
			const TArrayView<const FRayTracingGeometryBuildParams> EffectiveParams = MakeArrayView<const FRayTracingGeometryBuildParams>(&Params[ParamBegin], ParamCount);

			FRHIResourceCreateInfo ScratchBufferCreateInfo(TEXT("RHIScratchBuffer"));
			FRHIBufferRange ScratchBufferRange{};
			check(uint32(TotalRequiredScratchMemorySize) == TotalRequiredScratchMemorySize)
			ScratchBufferRange.Buffer = CreateBuffer(TotalRequiredScratchMemorySize, BUF_StructuredBuffer | BUF_RayTracingScratch, 0, ERHIAccess::UAVCompute, ScratchBufferCreateInfo);

			BuildAccelerationStructures(EffectiveParams, ScratchBufferRange);
		}
	}
}

static FLockTracker GLockTracker;

void* FDynamicRHI::RHILockBuffer(class FRHICommandListBase& RHICmdList, FRHIBuffer* Buffer, uint32 Offset, uint32 SizeRHI, EResourceLockMode LockMode)
{
	QUICK_SCOPE_CYCLE_COUNTER(STAT_FDynamicRHI_LockBuffer);

	void* Result;
	if (RHICmdList.IsTopOfPipe())
	{
		bool bBuffer = CVarRHICmdBufferWriteLocks.GetValueOnRenderThread() > 0;
		if (!bBuffer || LockMode != RLM_WriteOnly)
		{
			QUICK_SCOPE_CYCLE_COUNTER(STAT_RHIMETHOD_LockBuffer_FlushAndLock);
			CSV_SCOPED_TIMING_STAT(RHITFlushes, LockBuffer_BottomOfPipe);

			FRHICommandListScopedFlushAndExecute Flush(RHICmdList.GetAsImmediate());
			Result = GDynamicRHI->LockBuffer_BottomOfPipe(RHICmdList, Buffer, Offset, SizeRHI, LockMode);
		}
		else
		{
			QUICK_SCOPE_CYCLE_COUNTER(STAT_RHIMETHOD_LockBuffer_Malloc);
			Result = FMemory::Malloc(SizeRHI, 16);
		}

		// Only use the lock tracker at the top of the pipe. There's no need to track locks
		// at the bottom of the pipe, and doing so would require a critical section.
		GLockTracker.Lock(Buffer, Result, Offset, SizeRHI, LockMode);
	}
	else
	{
		Result = GDynamicRHI->LockBuffer_BottomOfPipe(RHICmdList, Buffer, Offset, SizeRHI, LockMode);
	}

	check(Result);
	return Result;
}

void FDynamicRHI::RHIUnlockBuffer(class FRHICommandListBase& RHICmdList, FRHIBuffer* Buffer)
{
	QUICK_SCOPE_CYCLE_COUNTER(STAT_FDynamicRHI_UnlockBuffer_RenderThread);

	if (RHICmdList.IsTopOfPipe())
	{
		FLockTracker::FLockParams Params = GLockTracker.Unlock(Buffer);

		bool bBuffer = CVarRHICmdBufferWriteLocks.GetValueOnRenderThread() > 0;
		if (!bBuffer || Params.LockMode != RLM_WriteOnly)
		{
			QUICK_SCOPE_CYCLE_COUNTER(STAT_RHIMETHOD_UnlockBuffer_FlushAndUnlock);
			CSV_SCOPED_TIMING_STAT(RHITFlushes, UnlockBuffer_BottomOfPipe);

			FRHICommandListScopedFlushAndExecute Flush(RHICmdList.GetAsImmediate());
			GDynamicRHI->UnlockBuffer_BottomOfPipe(RHICmdList, Buffer);
			GLockTracker.TotalMemoryOutstanding = 0;
		}
		else
		{
			RHICmdList.EnqueueLambda(TEXT("RHIUnlockBuffer"), [Buffer, Params](FRHICommandListBase& InRHICmdList)
			{
				QUICK_SCOPE_CYCLE_COUNTER(STAT_FRHICommandUpdateBuffer_Execute);
				void* Data = GDynamicRHI->LockBuffer_BottomOfPipe(InRHICmdList, Buffer, Params.Offset, Params.BufferSize, RLM_WriteOnly);
				{
					// If we spend a long time doing this memcpy, it means we got freshly allocated memory from the OS that has never been
					// initialized and is causing pagefault to bring zeroed pages into our process.
					TRACE_CPUPROFILER_EVENT_SCOPE(RHIUnlockBuffer_Memcpy);
					FMemory::Memcpy(Data, Params.Buffer, Params.BufferSize);
				}
				FMemory::Free(Params.Buffer);
				GDynamicRHI->UnlockBuffer_BottomOfPipe(InRHICmdList, Buffer);
			});
			RHICmdList.RHIThreadFence(true);
		}

		if (RHICmdList.IsImmediate() && GLockTracker.TotalMemoryOutstanding > uint32(CVarRHICmdMaxOutstandingMemoryBeforeFlush.GetValueOnRenderThread()) * 1024u)
		{
			QUICK_SCOPE_CYCLE_COUNTER(STAT_RHIMETHOD_UnlockBuffer_FlushForMem);
			// we could be loading a level or something, lets get this stuff going
			RHICmdList.GetAsImmediate().ImmediateFlush(EImmediateFlushType::DispatchToRHIThread); 
			GLockTracker.TotalMemoryOutstanding = 0;
		}
	}
	else
	{
		GDynamicRHI->UnlockBuffer_BottomOfPipe(RHICmdList, Buffer);
	}
}

void FDynamicRHI::RHIWriteGPUFence_TopOfPipe(FRHICommandListBase& RHICmdList, FRHIGPUFence* Fence)
{
	if (RHICmdList.Bypass())
	{
		RHICmdList.GetComputeContext().RHIWriteGPUFence(Fence);
		return;
	}
	ALLOC_COMMAND_CL(RHICmdList, FRHICommandWriteGPUFence)(Fence);
}

void FDynamicRHI::RHIBeginRenderQuery_TopOfPipe(FRHICommandListBase& RHICmdList, FRHIRenderQuery* RenderQueryRHI)
{
	if (RHICmdList.Bypass())
	{
		RHICmdList.GetContext().RHIBeginRenderQuery(RenderQueryRHI);
		return;
	}
	ALLOC_COMMAND_CL(RHICmdList, FRHICommandBeginRenderQuery)(RenderQueryRHI);
}

void FDynamicRHI::RHIEndRenderQuery_TopOfPipe(FRHICommandListBase& RHICmdList, FRHIRenderQuery* RenderQueryRHI)
{
	if (RHICmdList.Bypass())
	{
		RHICmdList.GetContext().RHIEndRenderQuery(RenderQueryRHI);
		return;
	}
	ALLOC_COMMAND_CL(RHICmdList, FRHICommandEndRenderQuery)(RenderQueryRHI);
}

// @todo-mattc-staging Default implementation
void* FDynamicRHI::RHILockStagingBuffer(FRHIStagingBuffer* StagingBuffer, FRHIGPUFence* Fence, uint32 Offset, uint32 SizeRHI)
{
	check(false);
	return nullptr;
	//return GDynamicRHI->RHILockVertexBuffer(StagingBuffer->GetSourceBuffer(), Offset, SizeRHI, RLM_ReadOnly);
}
void FDynamicRHI::RHIUnlockStagingBuffer(FRHIStagingBuffer* StagingBuffer)
{
	check(false);
	//GDynamicRHI->RHIUnlockVertexBuffer(StagingBuffer->GetSourceBuffer());
}

void* FDynamicRHI::LockStagingBuffer_RenderThread(class FRHICommandListImmediate& RHICmdList, FRHIStagingBuffer* StagingBuffer, FRHIGPUFence* Fence, uint32 Offset, uint32 SizeRHI)
{
	check(IsInRenderingThread());
	if (!Fence || !Fence->Poll() || Fence->NumPendingWriteCommands.GetValue() != 0)
	{
		QUICK_SCOPE_CYCLE_COUNTER(STAT_FDynamicRHI_LockStagingBuffer_Flush);
		RHICmdList.ImmediateFlush(EImmediateFlushType::FlushRHIThread);
	}
	{
		QUICK_SCOPE_CYCLE_COUNTER(STAT_FDynamicRHI_LockStagingBuffer_RenderThread);
		if (GRHISupportsMultithreading)
		{
			return GDynamicRHI->RHILockStagingBuffer(StagingBuffer, Fence, Offset, SizeRHI);
		}
		else
		{
			FScopedRHIThreadStaller StallRHIThread(RHICmdList);
			return GDynamicRHI->RHILockStagingBuffer(StagingBuffer, Fence, Offset, SizeRHI);
		}
	}
}

void FDynamicRHI::UnlockStagingBuffer_RenderThread(class FRHICommandListImmediate& RHICmdList, FRHIStagingBuffer* StagingBuffer)
{
	QUICK_SCOPE_CYCLE_COUNTER(STAT_FDynamicRHI_UnlockStagingBuffer_RenderThread);
	check(IsInRenderingThread());
	if (GRHISupportsMultithreading)
	{
		GDynamicRHI->RHIUnlockStagingBuffer(StagingBuffer);
	}
	else
	{
		FScopedRHIThreadStaller StallRHIThread(RHICmdList);
		GDynamicRHI->RHIUnlockStagingBuffer(StagingBuffer);
	}
}

FTextureRHIRef FDynamicRHI::AsyncReallocateTexture2D_RenderThread(class FRHICommandListImmediate& RHICmdList, FRHITexture* Texture2D, int32 NewMipCount, int32 NewSizeX, int32 NewSizeY, FThreadSafeCounter* RequestStatus)
{
	QUICK_SCOPE_CYCLE_COUNTER(STAT_RHIMETHOD_AsyncReallocateTexture2D_Flush);
	CSV_SCOPED_TIMING_STAT(RHITFlushes, AsyncReallocateTexture2D_RenderThread);
	RHICmdList.ImmediateFlush(EImmediateFlushType::FlushRHIThread);  
	return GDynamicRHI->RHIAsyncReallocateTexture2D(Texture2D, NewMipCount, NewSizeX, NewSizeY, RequestStatus);
}

ETextureReallocationStatus FDynamicRHI::FinalizeAsyncReallocateTexture2D_RenderThread(class FRHICommandListImmediate& RHICmdList, FRHITexture* Texture2D, bool bBlockUntilCompleted)
{
	CSV_SCOPED_TIMING_STAT(RHITStalls, FinalizeAsyncReallocateTexture2D_RenderThread);
	FScopedRHIThreadStaller StallRHIThread(RHICmdList);
	return GDynamicRHI->RHIFinalizeAsyncReallocateTexture2D(Texture2D, bBlockUntilCompleted);
}

ETextureReallocationStatus FDynamicRHI::CancelAsyncReallocateTexture2D_RenderThread(class FRHICommandListImmediate& RHICmdList, FRHITexture* Texture2D, bool bBlockUntilCompleted)
{
	CSV_SCOPED_TIMING_STAT(RHITStalls, CancelAsyncReallocateTexture2D_RenderThread);
	FScopedRHIThreadStaller StallRHIThread(RHICmdList);
	return GDynamicRHI->RHICancelAsyncReallocateTexture2D(Texture2D, bBlockUntilCompleted);
}

FUpdateTexture3DData FDynamicRHI::RHIBeginUpdateTexture3D(FRHICommandListBase& RHICmdList, FRHITexture* Texture, uint32 MipIndex, const struct FUpdateTextureRegion3D& UpdateRegion)
{
	check(IsInParallelRenderingThread());

	const int32 FormatSize = PixelFormatBlockBytes[Texture->GetFormat()];
	const int32 RowPitch = UpdateRegion.Width * FormatSize;
	const int32 DepthPitch = UpdateRegion.Width * UpdateRegion.Height * FormatSize;

	SIZE_T MemorySize = static_cast<SIZE_T>(DepthPitch) * UpdateRegion.Depth;
	uint8* Data = (uint8*)FMemory::Malloc(MemorySize);	

	return FUpdateTexture3DData(Texture, MipIndex, UpdateRegion, RowPitch, DepthPitch, Data, MemorySize, GFrameNumberRenderThread);
}

void FDynamicRHI::RHIEndUpdateTexture3D(FRHICommandListBase& RHICmdList, FUpdateTexture3DData& UpdateData)
{
	check(IsInParallelRenderingThread());
	check(GFrameNumberRenderThread == UpdateData.FrameNumber); 
	GDynamicRHI->RHIUpdateTexture3D(RHICmdList, UpdateData.Texture, UpdateData.MipIndex, UpdateData.UpdateRegion, UpdateData.RowPitch, UpdateData.DepthPitch, UpdateData.Data);
	FMemory::Free(UpdateData.Data);
	UpdateData.Data = nullptr;
}

void FDynamicRHI::RHIEndMultiUpdateTexture3D(FRHICommandListBase& RHICmdList, TArray<FUpdateTexture3DData>& UpdateDataArray)
{
	for (int32 Idx = 0; Idx < UpdateDataArray.Num(); ++Idx)
	{
		GDynamicRHI->RHIEndUpdateTexture3D(RHICmdList, UpdateDataArray[Idx]);
	}
}

void* FDynamicRHI::LockTexture2D_RenderThread(class FRHICommandListImmediate& RHICmdList, FRHITexture* Texture, uint32 MipIndex, EResourceLockMode LockMode, uint32& DestStride, bool bLockWithinMiptail, bool bNeedsDefaultRHIFlush, uint64* OutLockedByteCount)
{
	if (bNeedsDefaultRHIFlush) 
	{
		QUICK_SCOPE_CYCLE_COUNTER(STAT_RHIMETHOD_LockTexture2D_Flush);
		CSV_SCOPED_TIMING_STAT(RHITFlushes, LockTexture2D_RenderThread);
		RHICmdList.ImmediateFlush(EImmediateFlushType::FlushRHIThread);
		return GDynamicRHI->RHILockTexture2D(Texture, MipIndex, LockMode, DestStride, bLockWithinMiptail, OutLockedByteCount);
	}
	CSV_SCOPED_TIMING_STAT(RHITStalls, LockTexture2D_RenderThread);
	FScopedRHIThreadStaller StallRHIThread(RHICmdList);
	return GDynamicRHI->RHILockTexture2D(Texture, MipIndex, LockMode, DestStride, bLockWithinMiptail, OutLockedByteCount);
}

void FDynamicRHI::UnlockTexture2D_RenderThread(class FRHICommandListImmediate& RHICmdList, FRHITexture* Texture, uint32 MipIndex, bool bLockWithinMiptail, bool bNeedsDefaultRHIFlush)
{
	if (bNeedsDefaultRHIFlush)
	{
		QUICK_SCOPE_CYCLE_COUNTER(STAT_RHIMETHOD_UnlockTexture2D_Flush);
		CSV_SCOPED_TIMING_STAT(RHITFlushes, UnlockTexture2D_RenderThread);
		RHICmdList.ImmediateFlush(EImmediateFlushType::FlushRHIThread);
		GDynamicRHI->RHIUnlockTexture2D(Texture, MipIndex, bLockWithinMiptail);
		return;
	}
	CSV_SCOPED_TIMING_STAT(RHITStalls, UnlockTexture2D_RenderThread);
	FScopedRHIThreadStaller StallRHIThread(RHICmdList);
	GDynamicRHI->RHIUnlockTexture2D(Texture, MipIndex, bLockWithinMiptail);
}

void* FDynamicRHI::LockTexture2DArray_RenderThread(class FRHICommandListImmediate& RHICmdList, FRHITexture* Texture, uint32 ArrayIndex, uint32 MipIndex, EResourceLockMode LockMode, uint32& DestStride, bool bLockWithinMiptail)
{
	QUICK_SCOPE_CYCLE_COUNTER(STAT_RHIMETHOD_LockTexture2DArray_Flush);
	CSV_SCOPED_TIMING_STAT(RHITFlushes, LockTexture2DArray_RenderThread);
	RHICmdList.ImmediateFlush(EImmediateFlushType::FlushRHIThread);
	return GDynamicRHI->RHILockTexture2DArray(Texture, ArrayIndex, MipIndex, LockMode, DestStride, bLockWithinMiptail);
}

void FDynamicRHI::UnlockTexture2DArray_RenderThread(class FRHICommandListImmediate& RHICmdList, FRHITexture* Texture, uint32 ArrayIndex, uint32 MipIndex, bool bLockWithinMiptail)
{
	QUICK_SCOPE_CYCLE_COUNTER(STAT_RHIMETHOD_UnlockTexture2DArray_Flush);
	CSV_SCOPED_TIMING_STAT(RHITFlushes, UnlockTexture2DArray_RenderThread);
	RHICmdList.ImmediateFlush(EImmediateFlushType::FlushRHIThread);
	GDynamicRHI->RHIUnlockTexture2DArray(Texture, ArrayIndex, MipIndex, bLockWithinMiptail);
}

FRHIShaderLibraryRef FDynamicRHI::RHICreateShaderLibrary_RenderThread(class FRHICommandListImmediate& RHICmdList, EShaderPlatform Platform, FString FilePath, FString Name)
{
	FScopedRHIThreadStaller StallRHIThread(RHICmdList);
	return GDynamicRHI->RHICreateShaderLibrary(Platform, FilePath, Name);
}

void* FDynamicRHI::RHILockTextureCubeFace_RenderThread(class FRHICommandListImmediate& RHICmdList, FRHITexture* Texture, uint32 FaceIndex, uint32 ArrayIndex, uint32 MipIndex, EResourceLockMode LockMode, uint32& DestStride, bool bLockWithinMiptail)
{
	QUICK_SCOPE_CYCLE_COUNTER(STAT_RHIMETHOD_LockTextureCubeFace_Flush);
	CSV_SCOPED_TIMING_STAT(RHITFlushes, RHILockTextureCubeFace_RenderThread);
	RHICmdList.ImmediateFlush(EImmediateFlushType::FlushRHIThread);
	return GDynamicRHI->RHILockTextureCubeFace(Texture, FaceIndex, ArrayIndex, MipIndex, LockMode, DestStride, bLockWithinMiptail);
}

void FDynamicRHI::RHIUnlockTextureCubeFace_RenderThread(class FRHICommandListImmediate& RHICmdList, FRHITexture* Texture, uint32 FaceIndex, uint32 ArrayIndex, uint32 MipIndex, bool bLockWithinMiptail)
{
	QUICK_SCOPE_CYCLE_COUNTER(STAT_RHIMETHOD_UnlockTextureCubeFace_Flush);
	CSV_SCOPED_TIMING_STAT(RHITFlushes, RHIUnlockTextureCubeFace_RenderThread);
	RHICmdList.ImmediateFlush(EImmediateFlushType::FlushRHIThread);
	GDynamicRHI->RHIUnlockTextureCubeFace(Texture, FaceIndex, ArrayIndex, MipIndex, bLockWithinMiptail);
}

void FDynamicRHI::RHIMapStagingSurface_RenderThread(class FRHICommandListImmediate& RHICmdList, FRHITexture* Texture, uint32 GPUIndex, FRHIGPUFence* Fence, void*& OutData, int32& OutWidth, int32& OutHeight)
{
	if (Fence == nullptr || !Fence->Poll() || Fence->NumPendingWriteCommands.GetValue() != 0)
	{
		QUICK_SCOPE_CYCLE_COUNTER(STAT_RHIMETHOD_MapStagingSurface_Flush);
		RHICmdList.ImmediateFlush(EImmediateFlushType::FlushRHIThread);
	}
	{
		QUICK_SCOPE_CYCLE_COUNTER(STAT_RHIMETHOD_MapStagingSurface_RenderThread);
		if (GRHISupportsMultithreading)
		{
			GDynamicRHI->RHIMapStagingSurface(Texture, Fence, OutData, OutWidth, OutHeight, GPUIndex != INDEX_NONE ? GPUIndex : RHICmdList.GetGPUMask().ToIndex());
		}
		else
		{
			FScopedRHIThreadStaller StallRHIThread(RHICmdList);
			GDynamicRHI->RHIMapStagingSurface(Texture, Fence, OutData, OutWidth, OutHeight, GPUIndex != INDEX_NONE ? GPUIndex : RHICmdList.GetGPUMask().ToIndex());
		}
	}
}

void FDynamicRHI::RHIUnmapStagingSurface_RenderThread(class FRHICommandListImmediate& RHICmdList, FRHITexture* Texture, uint32 GPUIndex)
{
	if (GRHISupportsMultithreading)
	{
		GDynamicRHI->RHIUnmapStagingSurface(Texture, GPUIndex != INDEX_NONE ? GPUIndex : RHICmdList.GetGPUMask().ToIndex());
	}
	else
	{
		FScopedRHIThreadStaller StallRHIThread(RHICmdList);
		GDynamicRHI->RHIUnmapStagingSurface(Texture, GPUIndex != INDEX_NONE ? GPUIndex : RHICmdList.GetGPUMask().ToIndex());
	}
}

void FDynamicRHI::RHIReadSurfaceFloatData_RenderThread(class FRHICommandListImmediate& RHICmdList, FRHITexture* Texture, FIntRect Rect, TArray<FFloat16Color>& OutData, ECubeFace CubeFace, int32 ArrayIndex, int32 MipIndex)
{
	QUICK_SCOPE_CYCLE_COUNTER(STAT_RHIMETHOD_ReadSurfaceFloatData_Flush);
	CSV_SCOPED_TIMING_STAT(RHITFlushes, RHIReadSurfaceFloatData_RenderThread);
	RHICmdList.ImmediateFlush(EImmediateFlushType::FlushRHIThread);
	GDynamicRHI->RHIReadSurfaceFloatData(Texture, Rect, OutData, CubeFace, ArrayIndex, MipIndex);
}

void FDynamicRHI::RHIReadSurfaceFloatData_RenderThread(class FRHICommandListImmediate& RHICmdList, FRHITexture* Texture, FIntRect Rect, TArray<FFloat16Color>& OutData, FReadSurfaceDataFlags Flags)
{
	QUICK_SCOPE_CYCLE_COUNTER(STAT_RHIMETHOD_ReadSurfaceFloatData_Flush);
	CSV_SCOPED_TIMING_STAT(RHITFlushes, RHIReadSurfaceFloatData_RenderThread);
	RHICmdList.ImmediateFlush(EImmediateFlushType::FlushRHIThread);
	GDynamicRHI->RHIReadSurfaceFloatData(Texture, Rect, OutData, Flags);
}

void FRHICommandListBase::UpdateTextureReference(FRHITextureReference* TextureRef, FRHITexture* NewTexture)
{
	if (TextureRef == nullptr)
	{
		return;
	}
	
	GDynamicRHI->RHIUpdateTextureReference(*this, TextureRef, NewTexture);
}

void FRHICommandListExecutor::CleanupGraphEvents()
{
	check(!SubmitState);

	WaitOutstandingTasks.Reset();

	LastMutate.SafeRelease();
	LastSubmit.SafeRelease();
	CompletionEvent.SafeRelease();

	DispatchPipe.CleanupGraphEvents();
	RHIThreadPipe.CleanupGraphEvents();
}

UE_DEPRECATED(5.5, "GenerateMips on RHI command lists is deprecated and no longer functions. Use the FGenerateMips helper class from the RenderCore module to generate mips on textures.")
void FRHICommandList::GenerateMips(FRHITexture*)
{
	UE_LOG(LogRHI, Fatal, TEXT("GenerateMips on RHI command lists is deprecated and no longer functions. Use the FGenerateMips helper class from the RenderCore module to generate mips on textures."));
}

void FRHICommandListBase::InvalidBufferFatalError(const FRHIResourceCreateInfo& CreateInfo, EBufferUsageFlags Usage, uint32 Stride)
{
	UE_LOG(LogRHI, Fatal, TEXT("Attempt to create zero-sized buffer '%s', owner '%s', usage 0x%x, stride %u."), CreateInfo.DebugName ? CreateInfo.DebugName : TEXT("(nullptr)"), *CreateInfo.OwnerName.ToString(), (uint32)Usage, Stride);
}

FRayTracingShaderBindings UE::RHI::ConvertRayTracingShaderBindings(const FRHIBatchedShaderParameters& BatchedParameters)
{
	FRayTracingShaderBindings Result;

#if RHI_RAYTRACING

	// Use array views for bounds checking
	TArrayView<FRHITexture*> Textures = Result.Textures;
	TArrayView<FRHIShaderResourceView*> SRVs = Result.SRVs;
	TArrayView<FRHIUniformBuffer*> UniformBuffers = Result.UniformBuffers;
	TArrayView<FRHISamplerState*> Samplers = Result.Samplers;
	TArrayView<FRHIUnorderedAccessView*> UAVs = Result.UAVs;

	checkf(BatchedParameters.Parameters.IsEmpty(), TEXT("FRHIShaderParameter is not supported by FRayTracingShaderBindings"));

	// TODO: Handle FRHIBatchedShaderParameters::BindlessParameters once supported in FRayTracingShaderBindings

	for (const FRHIShaderParameterResource& It : BatchedParameters.ResourceParameters)
	{
		using EType = FRHIShaderParameterResource::EType;
		switch (It.Type)
		{
		case EType::Texture:
			Textures[It.Index] = static_cast<FRHITexture*>(It.Resource);
			break;
		case EType::ResourceView:
			SRVs[It.Index] = static_cast<FRHIShaderResourceView*>(It.Resource);
			break;
		case EType::UnorderedAccessView:
			UAVs[It.Index] = static_cast<FRHIUnorderedAccessView*>(It.Resource);
			break;
		case EType::Sampler:
			Samplers[It.Index] = static_cast<FRHISamplerState*>(It.Resource);
			break;
		case EType::UniformBuffer:
			UniformBuffers[It.Index] = static_cast<FRHIUniformBuffer*>(It.Resource);
			break;
		case EType::ResourceCollection:
			checkNoEntry(); // not supported
			break;
		default:
			checkNoEntry();
		}
	}

	Result.BindlessParameters = BatchedParameters.BindlessParameters;

#else // // RHI_RAYTRACING

	checkNoEntry();

#endif // RHI_RAYTRACING

	return Result;
}

void FRHICommandList::RayTraceDispatch(
	FRayTracingPipelineState* Pipeline,
	FRHIRayTracingShader* RayGenShader,
	FRHIShaderBindingTable* SBT,
	FRHIBatchedShaderParameters& GlobalResourceBindings,
	uint32 Width, uint32 Height)
{
#if RHI_RAYTRACING

	FRayTracingShaderBindings LegacyBindings = UE::RHI::ConvertRayTracingShaderBindings(GlobalResourceBindings);
	RayTraceDispatch(Pipeline, RayGenShader, SBT, LegacyBindings, Width, Height);

#else // RHI_RAYTRACING

	checkNoEntry();

#endif // RHI_RAYTRACING

	GlobalResourceBindings.Reset();
}

void FRHICommandList::RayTraceDispatchIndirect(
	FRayTracingPipelineState* Pipeline,
	FRHIRayTracingShader* RayGenShader,
	FRHIShaderBindingTable* SBT,
	FRHIBatchedShaderParameters& GlobalResourceBindings,
	FRHIBuffer* ArgumentBuffer, uint32 ArgumentOffset)
{
#if RHI_RAYTRACING

	FRayTracingShaderBindings LegacyBindings = UE::RHI::ConvertRayTracingShaderBindings(GlobalResourceBindings);
	RayTraceDispatchIndirect(Pipeline, RayGenShader, SBT, LegacyBindings, ArgumentBuffer, ArgumentOffset);

#else // RHI_RAYTRACING

	checkNoEntry();

#endif // RHI_RAYTRACING

	GlobalResourceBindings.Reset();
}

