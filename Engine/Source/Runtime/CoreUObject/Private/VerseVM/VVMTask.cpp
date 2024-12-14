// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_VERSE_VM || defined(__INTELLISENSE__)
#include "VerseVM/VVMTask.h"
#include "VVMFailureContext.h"
#include "VerseVM/Inline/VVMAbstractVisitorInline.h"
#include "VerseVM/Inline/VVMCellInline.h"
#include "VerseVM/Inline/VVMMarkStackVisitorInline.h"
#include "VerseVM/Inline/VVMUniqueStringInline.h"
#include "VerseVM/VVMCppClassInfo.h"
#include "VerseVM/VVMFrame.h"

namespace Verse
{

DEFINE_DERIVED_VCPPCLASSINFO(VTask);
TGlobalHeapPtr<VEmergentType> VTask::EmergentType;

void VTask::BindStruct(FAllocationContext Context, VClass& TaskClass)
{
	Verse::VPackage* VerseNativePackage = TaskClass.GetScope();

	const FUtf8StringView VerseModulePath = "/Verse.org/Concurrency";
	const FUtf8StringView VerseScopeName = "task";

	TUtf8StringBuilder<32> VerseScopePath;
	VerseScopePath << VerseModulePath << "/" << VerseScopeName;

	Verse::VNativeFunction::SetThunk(VerseNativePackage, VerseScopePath, "(/Verse.org/Concurrency/task:)Active", &VTask::ActiveImpl);
	Verse::VNativeFunction::SetThunk(VerseNativePackage, VerseScopePath, "(/Verse.org/Concurrency/task:)Completed", &VTask::CompletedImpl);
	Verse::VNativeFunction::SetThunk(VerseNativePackage, VerseScopePath, "(/Verse.org/Concurrency/task:)Canceling", &VTask::CancelingImpl);
	Verse::VNativeFunction::SetThunk(VerseNativePackage, VerseScopePath, "(/Verse.org/Concurrency/task:)Canceled", &VTask::CanceledImpl);
	Verse::VNativeFunction::SetThunk(VerseNativePackage, VerseScopePath, "(/Verse.org/Concurrency/task:)Unsettled", &VTask::UnsettledImpl);
	Verse::VNativeFunction::SetThunk(VerseNativePackage, VerseScopePath, "(/Verse.org/Concurrency/task:)Settled", &VTask::SettledImpl);
	Verse::VNativeFunction::SetThunk(VerseNativePackage, VerseScopePath, "(/Verse.org/Concurrency/task:)Uninterrupted", &VTask::UninterruptedImpl);
	Verse::VNativeFunction::SetThunk(VerseNativePackage, VerseScopePath, "(/Verse.org/Concurrency/task:)Interrupted", &VTask::InterruptedImpl);

	Verse::VNativeFunction::SetThunk(VerseNativePackage, VerseScopePath, "Await", &VTask::AwaitImpl);
	Verse::VNativeFunction::SetThunk(VerseNativePackage, VerseScopePath, "Cancel", &VTask::CancelImpl);

	VEmergentType& NewEmergentType = TaskClass.GetOrCreateEmergentTypeForArchetype(Context, VUniqueStringSet::New(Context, {}), &VTask::StaticCppClassInfo);
	VTask::EmergentType.Set(Context, &NewEmergentType);
}

void VTask::BindStructTrivial(FAllocationContext Context)
{
	VEmergentType* NewEmergentType = VEmergentType::New(Context, VTrivialType::Singleton.Get(), &VTask::StaticCppClassInfo);
	NewEmergentType->Shape.Set(Context, VShape::New(Context, {}));
	VTask::EmergentType.Set(Context, NewEmergentType);
}

FOpResult VTask::ActiveImpl(FRunningContext Context, VValue Scope, VNativeFunction::Args Arguments)
{
	V_DIE_UNLESS(Scope.IsCellOfType<VTask>());
	VTask* Self = &Scope.StaticCast<VTask>();

	V_FAIL_UNLESS(Self->Phase < EPhase::CancelStarted && !Self->Result);
	V_RETURN(GlobalFalse());
}

FOpResult VTask::CompletedImpl(FRunningContext Context, VValue Scope, VNativeFunction::Args Arguments)
{
	V_DIE_UNLESS(Scope.IsCellOfType<VTask>());
	VTask* Self = &Scope.StaticCast<VTask>();

	V_FAIL_UNLESS(Self->Result);
	V_RETURN(GlobalFalse());
}

FOpResult VTask::CancelingImpl(FRunningContext Context, VValue Scope, VNativeFunction::Args Arguments)
{
	V_DIE_UNLESS(Scope.IsCellOfType<VTask>());
	VTask* Self = &Scope.StaticCast<VTask>();

	V_FAIL_UNLESS(EPhase::CancelStarted <= Self->Phase && Self->Phase < EPhase::Canceled);
	V_RETURN(GlobalFalse());
}

FOpResult VTask::CanceledImpl(FRunningContext Context, VValue Scope, VNativeFunction::Args Arguments)
{
	V_DIE_UNLESS(Scope.IsCellOfType<VTask>());
	VTask* Self = &Scope.StaticCast<VTask>();

	V_FAIL_UNLESS(Self->Phase == EPhase::Canceled);
	V_RETURN(GlobalFalse());
}

FOpResult VTask::UnsettledImpl(FRunningContext Context, VValue Scope, VNativeFunction::Args Arguments)
{
	V_DIE_UNLESS(Scope.IsCellOfType<VTask>());
	VTask* Self = &Scope.StaticCast<VTask>();

	V_FAIL_UNLESS(Self->Phase < EPhase::Canceled && !Self->Result);
	V_RETURN(GlobalFalse());
}

FOpResult VTask::SettledImpl(FRunningContext Context, VValue Scope, VNativeFunction::Args Arguments)
{
	V_DIE_UNLESS(Scope.IsCellOfType<VTask>());
	VTask* Self = &Scope.StaticCast<VTask>();

	V_FAIL_UNLESS(Self->Phase == EPhase::Canceled || Self->Result);
	V_RETURN(GlobalFalse());
}

FOpResult VTask::UninterruptedImpl(FRunningContext Context, VValue Scope, VNativeFunction::Args Arguments)
{
	V_DIE_UNLESS(Scope.IsCellOfType<VTask>());
	VTask* Self = &Scope.StaticCast<VTask>();

	V_FAIL_UNLESS(Self->Phase == EPhase::Active);
	V_RETURN(GlobalFalse());
}

FOpResult VTask::InterruptedImpl(FRunningContext Context, VValue Scope, VNativeFunction::Args Arguments)
{
	V_DIE_UNLESS(Scope.IsCellOfType<VTask>());
	VTask* Self = &Scope.StaticCast<VTask>();

	V_FAIL_UNLESS(Self->Phase != EPhase::Active);
	V_RETURN(GlobalFalse());
}

FOpResult VTask::AwaitImpl(FRunningContext Context, VValue Scope, VNativeFunction::Args Arguments)
{
	V_DIE_UNLESS(Scope.IsCellOfType<VTask>());
	VTask* Self = &Scope.StaticCast<VTask>();

	if (!Self->Result)
	{
		VTask* Task = Context.NativeContext().Task;

		Task->Park(Context, Self->LastAwait);

		V_DIE_IF(Task->NativeDefer);
		Task->NativeDefer = [Self](FAccessContext Context, VTask* Task) {
			AutoRTFM::Open([&] { Task->Unpark(Context, Self->LastAwait); });
		};

		V_YIELD();
	}

	V_RETURN(Self->Result.Get());
}

// When a task is canceled, it follows these phases, completing each one before starting the next.
// The implementation upholds and relies on these invariants throughout.
//
// 1) Reach a suspension point. The task is running during this phase. A call to a <suspends>
//    function is insufficient on its own, because cancellation cannot proceed until the task
//    actually suspends. (`EndTask` also functions as a last-chance suspension point.)
// 2) Cancel children in LIFO order. If a descendant is still running, the task must yield. At the
//    same time, it may still be registered for normal resumption, because de-registration happens
//    in a (native) defer block as part of unwinding. This has two consequences:
//    * If the task suspended in `Await` or `Cancel`, its `PrevTask`/`NextTask` links will still be
//      in use, so cancellation must resume via the child's `Parent` link instead.
//    * Something may try to resume the task. The task must not leave its suspension point, and it
//      may already be running (see `bRunning`), so normal resumption must become a no-op.
// 3) Unwind the stack and run `defer` blocks. After the previous phase, the task will no longer
//    yield for any reason, because any new children created during unwinding can always be
//    cancelled synchronously by the `EndTask` instruction at the end of unwinding.
// 4) Resume any cancelers, followed by the parent if it is in phase 2 and this is its last child.
//    The parent task's phase 2 guarantees that its last child does not change while it is waiting.
FOpResult VTask::CancelImpl(FRunningContext Context, VValue Scope, VNativeFunction::Args Arguments)
{
	V_DIE_UNLESS(Scope.IsCellOfType<VTask>());
	VTask* Self = &Scope.StaticCast<VTask>();

	if (Self->Phase < EPhase::Canceled && !Self->Result)
	{
		if (!Self->RequestCancel(Context))
		{
			VTask* Task = Context.NativeContext().Task;

			Task->Park(Context, Self->LastCancel);

			V_DIE_IF(Task->NativeDefer);
			Task->NativeDefer = [Self](FAccessContext Context, VTask* Task) {
				AutoRTFM::Open([&] { Task->Unpark(Context, Self->LastCancel); });
			};

			V_YIELD();
		}

		Self->UnwindInTransaction(Context);
	}

	V_RETURN(GlobalFalse());
}

// Call when initiating task cancellation. Returns true if the task is ready to unwind.
bool VTask::RequestCancel(FRunningContext Context)
{
	V_DIE_UNLESS(Phase < EPhase::Canceled && !Result);

	if (Phase < EPhase::CancelRequested)
	{
		Phase = EPhase::CancelRequested;
	}

	// The task is not yet at a suspension point, or is already unwinding.
	if (bRunning)
	{
		return false;
	}

	// The task is already waiting on a child's cancellation.
	if (Phase == EPhase::CancelStarted)
	{
		return false;
	}

	Phase = EPhase::CancelStarted;
	return CancelChildren(Context);
}

// Returns true if all children were canceled.
bool VTask::CancelChildren(FRunningContext Context)
{
	// Let unwinding children know not to resume this task.
	TGuardValue<bool> GuardRunning(bRunning, true);

	while (VTask* Child = LastChild.Get())
	{
		if (!Child->RequestCancel(Context))
		{
			return false;
		}

		V_DIE_UNLESS(Child == LastChild.Get());
		Child->UnwindInTransaction(Context);
	}

	return true;
}

template <typename TVisitor>
void VTask::VisitReferencesImpl(TVisitor& Visitor)
{
	TIntrusiveTree<VTask>::VisitReferencesImpl(Visitor);

	Visitor.Visit(ResumeFrame, TEXT("ResumeFrame"));
	ResumeSlot.Visit(Visitor);

	Visitor.Visit(YieldFrame, TEXT("YieldFrame"));
	Visitor.Visit(YieldTask, TEXT("YieldTask"));

	Visitor.Visit(Result, TEXT("Result"));
	Visitor.Visit(LastAwait, TEXT("LastAwait"));
	Visitor.Visit(LastCancel, TEXT("LastCancel"));

	Visitor.Visit(PrevTask, TEXT("PrevTask"));
	Visitor.Visit(NextTask, TEXT("NextTask"));
}

DEFINE_DERIVED_VCPPCLASSINFO(VSemaphore);
TGlobalTrivialEmergentTypePtr<&VSemaphore::StaticCppClassInfo> VSemaphore::GlobalTrivialEmergentType;

template <typename TVisitor>
void VSemaphore::VisitReferencesImpl(TVisitor& Visitor)
{
	Visitor.Visit(Await, TEXT("Await"));
}

} // namespace Verse
#endif // WITH_VERSE_VM || defined(__INTELLISENSE__)