// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_VERSE_VM || defined(__INTELLISENSE__)
#include "AutoRTFM/AutoRTFM.h"
#include "Containers/StringConv.h"
#include "Containers/Utf8String.h"
#include "HAL/Platform.h"
#include "HAL/PlatformMisc.h"
#include "UObject/UnrealType.h"
#include "UObject/VerseValueProperty.h"
#include "VerseVM/Inline/VVMArrayBaseInline.h"
#include "VerseVM/Inline/VVMClassInline.h"
#include "VerseVM/Inline/VVMEqualInline.h"
#include "VerseVM/Inline/VVMIntInline.h"
#include "VerseVM/Inline/VVMMapInline.h"
#include "VerseVM/Inline/VVMMutableArrayInline.h"
#include "VerseVM/Inline/VVMScopeInline.h"
#include "VerseVM/Inline/VVMUniqueStringInline.h"
#include "VerseVM/Inline/VVMValueInline.h"
#include "VerseVM/Inline/VVMValueObjectInline.h"
#include "VerseVM/Inline/VVMVarInline.h"
#include "VerseVM/Inline/VVMVerseClassInline.h"
#include "VerseVM/VVMArray.h"
#include "VerseVM/VVMArrayBase.h"
#include "VerseVM/VVMBytecode.h"
#include "VerseVM/VVMBytecodeOps.h"
#include "VerseVM/VVMBytecodesAndCaptures.h"
#include "VerseVM/VVMCVars.h"
#include "VerseVM/VVMDebugger.h"
#include "VerseVM/VVMFailureContext.h"
#include "VerseVM/VVMFalse.h"
#include "VerseVM/VVMFloat.h"
#include "VerseVM/VVMFrame.h"
#include "VerseVM/VVMFunction.h"
#include "VerseVM/VVMGlobalHeapPtr.h"
#include "VerseVM/VVMInt.h"
#include "VerseVM/VVMLog.h"
#include "VerseVM/VVMMap.h"
#include "VerseVM/VVMMutableArray.h"
#include "VerseVM/VVMNativeFunction.h"
#include "VerseVM/VVMOpResult.h"
#include "VerseVM/VVMOption.h"
#include "VerseVM/VVMProcedure.h"
#include "VerseVM/VVMRational.h"
#include "VerseVM/VVMSuspension.h"
#include "VerseVM/VVMTask.h"
#include "VerseVM/VVMUniqueString.h"
#include "VerseVM/VVMUnreachable.h"
#include "VerseVM/VVMValue.h"
#include "VerseVM/VVMValuePrinting.h"
#include "VerseVM/VVMVar.h"
#include <stdio.h>

static_assert(UE_AUTORTFM, "New VM depends on AutoRTFM.");

namespace Verse
{

// The Interpreter is organized into two main execution loops: the main loop and the suspension loop.
// The main loop works like a normal interpreter loop. Control flow falls through from one bytecode
// to the next. We also have jump instructions which can divert control flow. However, since Verse
// also has failure, the bytecode has support for any bytecode that fails jumping to the current
// failure context's "on fail" bytecode destination. The way this works is that the BeginFailureContext
// and EndFailureContext bytecodes form a pair. The BeginFailureContext specifies where to jump to in
// the event of failure. Notably, if failure doesn't happen, the EndFailureContext bytecode must execute.
// This means that BeginFailureContext and EndFailureContext should be control equivalent -- we can't
// have jumps that jump over an EndFailureContext bytecode from within the failure context range.
//
// The bytecode also has builtin support for Verse's lenient execution model. This support is fundamental
// to the execution model of the bytecode. Bytecode instructions can suspend when a needed input
// operand is not concrete -- it's a placeholder -- and then resume execution when the input operand
// becomes concrete. Bytecode suspensions will capture their input operands and use the captured operands
// when they resume execution. When a placeholder becomes concrete unlocking a suspension, that suspension
// will execute in the suspension interpreter loop. The reason bytecode suspensions capture their input
// operands is so that those bytecode frame slots can be reused by the rest of the bytecode program.
// Because the operands aren't reloaded from the frame, and instead from the suspension, our bytecode
// generator can have a virtual register allocation algorithm that doesn't need to take into account
// liveness constraints dictated by leniency. This invariant has interesting implications executing a
// failure context leniently. In that scenario, we need to capture everything that's used both in the
// then/else branch. (For now, we implement this by just cloning the entire frame.) It's a goal to
// share as much code as we can between the main and suspension interpreter loops. That's why there
// are overloaded functions and interpreter-loop-specific macros that can handle both bytecode
// structs and suspension captures.
//
// Because of leniency, the interpreter needs to be careful about executing effects in program order. For
// example, if you have two effectful bytecodes one after the other, and the first one suspends, then the
// second one can't execute until the first one finishes. To handle this, we track an effect token that we
// thread through the program. Effectful operations will require the effect token to be concrete. They only
// execute after the token is concrete. Effectful operations always define a new non-concrete effect token.
// Only after the operation executes will it set the effect token to be concrete.
//
// Slots in the bytecode are all unification variables in support of Verse's general unification variable
// semantics. In our runtime, a unification variable is either a normal concrete value or a placeholder.
// A placeholder is used to support leniency. A placeholder can be used to unify two non-concrete variables.
// A placeholder can also point at a list of suspensions to fire when it becomes concrete. And finally, a
// placeholder can be mutated to point at a concrete value. When the runtime mutates a placeholder to
// point at a concrete value, it will fire its list of suspensions.
//
// Logically, a bytecode frame is initialized with empty placeholders. Every local variable in Verse is a
// unification variable. However, we really want to avoid this placeholder allocation for every local. After
// all, most locals will be defined before they're used. We optimize this by making these slots VRestValue
// instead of VPlaceholder. A VRestValue can be thought of a promise to produce a VPlaceholder if it's used
// before it has a concretely defined value. However, if we define a value in a bytecode slot before it's
// used, we can elide the allocation of the VPlaceholder altogether.

// This is used as a special PC to get the interpreter to break out of its loop.
FOpErr StopInterpreterSentry;

namespace
{
struct FExecutionState
{
	FOp* PC{nullptr};
	VFrame* Frame{nullptr};

	const TWriteBarrier<VValue>* Constants{nullptr};
	FValueOperand* Operands{nullptr};
	FLabelOffset* Labels{nullptr};

	FExecutionState(FOp* PC, VFrame* Frame)
		: PC(PC)
		, Frame(Frame)
		, Constants(Frame->Procedure->GetConstantsBegin())
		, Operands(Frame->Procedure->GetOperandsBegin())
		, Labels(Frame->Procedure->GetLabelsBegin())
	{
	}

	FExecutionState() = default;
	FExecutionState(const FExecutionState&) = default;
	FExecutionState(FExecutionState&&) = default;
	FExecutionState& operator=(const FExecutionState&) = default;
};

// In Verse, all functions conceptually take a single argument tuple
// To avoid unnecessary boxing and unboxing of VValues, we add an optimization where we try to avoid boxing/unboxing as much as possible
// This function reconciles the number of expected parameters with the number of provided arguments and boxes/unboxes only as needed
template <typename ArgFunction, typename StoreFunction, typename NamedArgFunction, typename NamedStoreFunction>
static void UnboxArguments(FAllocationContext Context, uint32 NumParams, uint32 NumNamedParams, uint32 NumArgs, FNamedParam* NamedParams, TArrayView<TWriteBarrier<VUniqueString>>* NamedArgs, ArgFunction GetArg, StoreFunction StoreArg, NamedArgFunction GetNamedArg, NamedStoreFunction StoreNamedArg)
{
	// --- Unnamed parameters -------------------------------
	if (NumArgs == NumParams)
	{
		/* direct passing */
		for (uint32 Arg = 0; Arg < NumArgs; ++Arg)
		{
			StoreArg(Arg, GetArg(Arg));
		}
	}
	else if (NumArgs == 1)
	{
		// Function wants loose arguments but a tuple is provided - unbox them
		VValue IncomingArg = GetArg(0);
		VArrayBase& Args = IncomingArg.StaticCast<VArrayBase>();

		V_DIE_UNLESS(NumParams == Args.Num());
		for (uint32 Param = 0; Param < NumParams; ++Param)
		{
			StoreArg(Param, Args.GetValue(Param));
		}
	}
	else if (NumParams == 1)
	{
		// Function wants loose arguments in a box, ie:
		// F(X:tuple(int, int)):int = X(0) + X(1)
		// F(3, 5) = 8 <-- we need to box these
		VArray& ArgArray = VArray::New(Context, NumArgs, GetArg);
		StoreArg(0, ArgArray);
	}
	else
	{
		V_DIE("Unexpected parameter/argument count mismatch");
	}

	// --- Named parameters ---------------------------------
	const uint32 NumNamedArgs = NamedArgs ? NamedArgs->Num() : 0;
	for (uint32 NamedParamIdx = 0; NamedParamIdx < NumNamedParams; ++NamedParamIdx)
	{
		VValue ValueToStore;
		for (uint32 NamedArgIdx = 0; NamedArgIdx < NumNamedArgs; ++NamedArgIdx)
		{
			if (NamedParams[NamedParamIdx].Name.Get() == (*NamedArgs)[NamedArgIdx].Get())
			{
				ValueToStore = GetNamedArg(NamedArgIdx);
				break;
			}
		}
		StoreNamedArg(NamedParamIdx, ValueToStore);
	}
}

template <typename ReturnSlotType, typename ArgFunction, typename NamedArgFunction>
static VFrame& MakeFrameForCallee(FRunningContext Context, FOp* CallerPC, VFrame* CallerFrame, ReturnSlotType ReturnSlot, VFunction& Function, uint32 NumArgs, TArrayView<TWriteBarrier<VUniqueString>>* NamedArgs, ArgFunction GetArg, NamedArgFunction GetNamedArg)
{
	VProcedure& Procedure = Function.GetProcedure();
	VFrame& Frame = VFrame::New(Context, CallerPC, CallerFrame, ReturnSlot, Procedure);

	check(FRegisterIndex::PARAMETER_START + Procedure.NumPositionalParameters + Procedure.NumNamedParameters <= Procedure.NumRegisters);

	Frame.Registers[FRegisterIndex::SELF].Set(Context, Function.Self.Get());
	if (VScope* LexicalScope = Function.ParentScope.Get())
	{
		Frame.Registers[FRegisterIndex::SCOPE].Set(Context, *LexicalScope);
	}

	UnboxArguments(
		Context, Procedure.NumPositionalParameters, Procedure.NumNamedParameters, NumArgs, Procedure.GetNamedParamsBegin(), NamedArgs,
		GetArg,
		[&](uint32 Param, VValue Value) {
			Frame.Registers[FRegisterIndex::PARAMETER_START + Param].Set(Context, Value);
		},
		GetNamedArg,
		[&](uint32 NamedParam, VValue Value) {
			Frame.Registers[Procedure.GetNamedParamsBegin()[NamedParam].Index.Index].Set(Context, Value);
		});

	return Frame;
}
} // namespace

class FInterpreter
{
	FRunningContext Context;

	FExecutionState State;
	VFailureContext* Failure;
	VTask* Task;
	VRestValue EffectToken{0};
	VSuspension* CurrentSuspension{nullptr};

	VFailureContext* const OutermostFailureContext;
	VTask* OutermostTask;
	FOp* OutermostStartPC;
	FOp* OutermostEndPC;

	FString ExecutionTrace;
	FExecutionState SavedStateForTracing;

	VValue GetOperand(FValueOperand Operand)
	{
		if (Operand.IsRegister())
		{
			return State.Frame->Registers[Operand.AsRegister().Index].Get(Context);
		}
		else if (Operand.IsConstant())
		{
			return State.Constants[Operand.AsConstant().Index].Get().Follow();
		}
		else
		{
			return VValue();
		}
	}

	static VValue GetOperand(const TWriteBarrier<VValue>& Value)
	{
		return Value.Get().Follow();
	}

	TArrayView<FValueOperand> GetOperands(TOperandRange<FValueOperand> Operands)
	{
		return TArrayView<FValueOperand>(State.Operands + Operands.Index, Operands.Num);
	}

	template <typename CellType>
	TArrayView<TWriteBarrier<CellType>> GetOperands(TOperandRange<TWriteBarrier<CellType>> Immediates)
	{
		TWriteBarrier<CellType>* Constants = BitCast<TWriteBarrier<CellType>*>(State.Constants);
		return TArrayView<TWriteBarrier<CellType>>{Constants + Immediates.Index, Immediates.Num};
	}

	static TArrayView<TWriteBarrier<VValue>> GetOperands(TArray<TWriteBarrier<VValue>>& Operands)
	{
		return TArrayView<TWriteBarrier<VValue>>(Operands);
	}

	TArrayView<FLabelOffset> GetConstants(FOp& PC, TOperandRange<FLabelOffset> Constants)
	{
		return TArrayView<FLabelOffset>(State.Labels + Constants.Index, Constants.Num);
	}

	template <typename OpType, typename = void>
	struct HasDest : std::false_type
	{
	};
	template <typename OpType>
	struct HasDest<OpType, std::void_t<decltype(OpType::Dest)>> : std::true_type
	{
	};

	// Construct a return slot for the "Dest" field of "Op" if it has one.
	template <typename OpType>
	auto MakeReturnSlot(OpType& Op)
	{
		return MakeReturnSlot(Op, HasDest<OpType>{});
	}

	template <typename OpType>
	VRestValue* MakeReturnSlot(OpType& Op, std::false_type)
	{
		return nullptr;
	}

	template <typename OpType>
	auto MakeReturnSlot(OpType& Op, std::true_type)
	{
		return MakeOperandReturnSlot(Op.Dest);
	}

	VRestValue* MakeOperandReturnSlot(FRegisterIndex Dest)
	{
		return &State.Frame->Registers[Dest.Index];
	}

	VValue MakeOperandReturnSlot(const TWriteBarrier<VValue>& Dest)
	{
		return GetOperand(Dest);
	}

	// Include autogenerated functions to create captures
#include "VVMMakeCapturesFuncs.gen.h"

	void PrintOperandOrValue(FString& String, FRegisterIndex Operand)
	{
		if (Operand.Index == FRegisterIndex::UNINITIALIZED)
		{
			String += "(UNINITIALIZED)";
		}
		else
		{
			String += ToString(Context, FDefaultCellFormatter(), State.Frame->Registers[Operand.Index]);
		}
	}

	void PrintOperandOrValue(FString& String, FValueOperand Operand)
	{
		if (Operand.IsRegister())
		{
			String += ToString(Context, FDefaultCellFormatter(), State.Frame->Registers[Operand.AsRegister().Index]);
		}
		else if (Operand.IsConstant())
		{
			String += ToString(Context, FDefaultCellFormatter(), State.Constants[Operand.AsConstant().Index].Get());
		}
		else
		{
			String += "Empty";
		}
	}

	template <typename T>
	void PrintOperandOrValue(FString& String, TWriteBarrier<T>& Operand)
	{
		if constexpr (std::is_same_v<T, VValue>)
		{
			String += ToString(Context, FDefaultCellFormatter(), Operand.Get());
		}
		else
		{
			String += ToString(Context, FDefaultCellFormatter(), *Operand);
		}
	}

	void PrintOperandOrValue(FString& String, TOperandRange<FValueOperand> Operands)
	{
		String += TEXT("(");
		const TCHAR* Separator = TEXT("");
		for (int32 Index = 0; Index < Operands.Num; ++Index)
		{
			String += Separator;
			Separator = TEXT(", ");
			PrintOperandOrValue(String, State.Operands[Operands.Index + Index]);
		}
		String += TEXT(")");
	}

	template <typename T>
	void PrintOperandOrValue(FString& String, TOperandRange<TWriteBarrier<T>> Operands)
	{
		TWriteBarrier<T>* Constants = BitCast<TWriteBarrier<T>*>(State.Constants);
		String += TEXT("(");
		const TCHAR* Separator = TEXT("");
		for (int32 Index = 0; Index < Operands.Num; ++Index)
		{
			String += Separator;
			Separator = TEXT(", ");
			PrintOperandOrValue(String, Constants[Operands.Index + Index]);
		}
		String += TEXT(")");
	}

	template <typename T>
	void PrintOperandOrValue(FString& String, TArray<TWriteBarrier<T>>& Operands)
	{
		String += TEXT("(");
		const TCHAR* Separator = TEXT("");
		for (TWriteBarrier<T>& Operand : Operands)
		{
			String += Separator;
			Separator = TEXT(", ");
			PrintOperandOrValue(String, Operand);
		}
		String += TEXT(")");
	}

	template <typename OpOrCaptures>
	FString TraceOperandsImpl(OpOrCaptures& Op, TArray<EOperandRole> RolesToPrint)
	{
		FString String;
		const TCHAR* Separator = TEXT("");
		Op.ForEachOperand([&](EOperandRole Role, auto& OperandOrValue, const TCHAR* Name) {
			if (RolesToPrint.Find(Role) != INDEX_NONE)
			{
				String += Separator;
				Separator = TEXT(", ");
				String.Append(Name).Append("=");
				PrintOperandOrValue(String, OperandOrValue);
			}
		});
		return String;
	}

	template <typename OpOrCaptures>
	FString TraceInputs(OpOrCaptures& Op)
	{
		return TraceOperandsImpl(Op, {EOperandRole::Use, EOperandRole::Immediate});
	}

	template <typename OpOrCaptures>
	FString TraceOutputs(OpOrCaptures& Op)
	{
		return TraceOperandsImpl(Op, {EOperandRole::UnifyDef, EOperandRole::ClobberDef});
	}

	FString TracePrefix(VProcedure* Procedure, VRestValue* CurrentEffectToken, FOp* PC, bool bLenient)
	{
		FString String;
		String += FString::Printf(TEXT("0x%" PRIxPTR), Procedure);
		String += FString::Printf(TEXT("#%u|"), Procedure->BytecodeOffset(*PC));
		if (CurrentEffectToken)
		{
			String += TEXT("EffectToken=");
			String += ToString(Context, FDefaultCellFormatter(), *CurrentEffectToken);
			String += TEXT("|");
		}
		if (bLenient)
		{
			String += TEXT("Lenient|");
		}
		String += ToString(PC->Opcode);
		String += TEXT("(");
		return String;
	}

	void BeginTrace()
	{
		if (CVarSingleStepTraceExecution.GetValueOnAnyThread())
		{
			getchar();
		}

		SavedStateForTracing = State;
		if (State.PC == &StopInterpreterSentry)
		{
			UE_LOG(LogVerseVM, Display, TEXT("StoppingExecution, encountered StopInterpreterSentry"));
			return;
		}

		ExecutionTrace = TracePrefix(State.Frame->Procedure.Get(), &EffectToken, State.PC, false);

#define VISIT_OP(Name)                                                     \
	case EOpcode::Name:                                                    \
	{                                                                      \
		ExecutionTrace += TraceInputs(*static_cast<FOp##Name*>(State.PC)); \
		break;                                                             \
	}

		switch (State.PC->Opcode)
		{
			VERSE_ENUM_OPS(VISIT_OP)
#undef VISIT_OP
		}

		ExecutionTrace += TEXT(")");
	}

	template <typename CaptureType>
	void BeginTrace(CaptureType& Captures, VBytecodeSuspension& Suspension)
	{
		if (CVarSingleStepTraceExecution.GetValueOnAnyThread())
		{
			getchar();
		}

		ExecutionTrace = TracePrefix(Suspension.Procedure.Get(), nullptr, Suspension.PC, true);
		ExecutionTrace += TraceInputs(Captures);
		ExecutionTrace += TEXT(")");
	}

	void EndTrace(bool bSuspended, bool bFailed)
	{
		FExecutionState CurrentState = State;
		State = SavedStateForTracing;

		FString Temp;

#define VISIT_OP(Name)                                           \
	case EOpcode::Name:                                          \
	{                                                            \
		Temp = TraceOutputs(*static_cast<FOp##Name*>(State.PC)); \
		break;                                                   \
	}

		switch (State.PC->Opcode)
		{
			VERSE_ENUM_OPS(VISIT_OP)
#undef VISIT_OP
		}

		if (!Temp.IsEmpty())
		{
			ExecutionTrace += TEXT("|");
			ExecutionTrace += Temp;
		}

		if (bSuspended)
		{
			ExecutionTrace += TEXT("|Suspending");
		}

		if (bFailed)
		{
			ExecutionTrace += TEXT("|Failed");
		}

		UE_LOG(LogVerseVM, Display, TEXT("%s"), *ExecutionTrace);

		State = CurrentState;
	}

	template <typename CaptureType>
	void EndTraceWithCaptures(CaptureType& Captures, bool bSuspended, bool bFailed)
	{
		ExecutionTrace += TEXT("|");
		ExecutionTrace += TraceOutputs(Captures);
		if (bSuspended)
		{
			ExecutionTrace += TEXT("|Suspending");
		}

		if (bFailed)
		{
			ExecutionTrace += TEXT("|Failed");
		}
		UE_LOG(LogVerseVM, Display, TEXT("%s"), *ExecutionTrace);
	}

	static bool Def(FRunningContext Context, VValue ResultSlot, VValue Value, VSuspension*& SuspensionsToFire)
	{
		// This returns true if we encounter a placeholder
		return VValue::Equal(Context, ResultSlot, Value, [Context, &SuspensionsToFire](VValue Left, VValue Right) {
			// Given how the interpreter is structured, we know these must be resolved
			// to placeholders. They can't be pointing to values or we should be using
			// the value they point to.
			checkSlow(!Left.IsPlaceholder() || Left.Follow().IsPlaceholder());
			checkSlow(!Right.IsPlaceholder() || Right.Follow().IsPlaceholder());

			if (Left.IsPlaceholder() && Right.IsPlaceholder())
			{
				Left.GetRootPlaceholder().Unify(Context, Right.GetRootPlaceholder());
				return;
			}

			VSuspension* NewSuspensionToFire;
			if (Left.IsPlaceholder())
			{
				NewSuspensionToFire = Left.GetRootPlaceholder().SetValue(Context, Right);
			}
			else
			{
				NewSuspensionToFire = Right.GetRootPlaceholder().SetValue(Context, Left);
			}

			if (!SuspensionsToFire)
			{
				SuspensionsToFire = NewSuspensionToFire;
			}
			else
			{
				SuspensionsToFire->Tail().Next.Set(Context, NewSuspensionToFire);
			}
		});
	}

	bool Def(VValue ResultSlot, VValue Value)
	{
		return Def(Context, ResultSlot, Value, CurrentSuspension);
	}

	bool Def(const TWriteBarrier<VValue>& ResultSlot, VValue Value)
	{
		return Def(GetOperand(ResultSlot), Value);
	}

	static bool Def(FRunningContext Context, VRestValue& ResultSlot, VValue Value, VSuspension*& SuspensionsToFire)
	{
		// TODO: This needs to consider split depth eventually.
		if (LIKELY(ResultSlot.CanDefQuickly()))
		{
			ResultSlot.Set(Context, Value);
			return true;
		}
		return Def(Context, ResultSlot.Get(Context), Value, SuspensionsToFire);
	}

	bool Def(VRestValue& ResultSlot, VValue Value)
	{
		return Def(Context, ResultSlot, Value, CurrentSuspension);
	}

	bool Def(FRegisterIndex ResultSlot, VValue Value)
	{
		return Def(State.Frame->Registers[ResultSlot.Index], Value);
	}

	static bool Def(FRunningContext Context, VReturnSlot& ReturnSlot, VValue Value, VSuspension*& SuspensionsToFire)
	{
		if (ReturnSlot.Kind == VReturnSlot::EReturnKind::RestValue)
		{
			if (ReturnSlot.RestValue)
			{
				return Def(Context, *ReturnSlot.RestValue, Value, SuspensionsToFire);
			}
			else
			{
				return true;
			}
		}
		else
		{
			checkSlow(ReturnSlot.Kind == VReturnSlot::EReturnKind::Value);
			return Def(Context, ReturnSlot.Value.Get(), Value, SuspensionsToFire);
		}
	}

	bool Def(VReturnSlot& ReturnSlot, VValue Value)
	{
		return Def(Context, ReturnSlot, Value, CurrentSuspension);
	}

	void BumpEffectEpoch()
	{
		EffectToken.Reset(0);
	}

	void FinishedExecutingFailureContextLeniently(VFailureContext& FailureContext, FOp* StartPC, FOp* EndPC, VValue NextEffectToken)
	{
		VFailureContext* ParentFailure = FailureContext.Parent.Get();
		VTask* ParentTask = FailureContext.Task.Get();

		if (StartPC < EndPC)
		{
			VFrame* Frame = FailureContext.Frame.Get();
			// When we cloned the frame for lenient execution, we guarantee the caller info
			// isn't set because when this is done executing, it should not return to the
			// caller at the time of creation of the failure context. It should return back here.
			V_DIE_IF(Frame->CallerFrame || Frame->CallerPC);

			FInterpreter Interpreter(
				Context,
				FExecutionState(StartPC, Frame),
				ParentFailure,
				ParentTask,
				NextEffectToken,
				StartPC, EndPC);
			Interpreter.Execute();

			// TODO: We need to think through exactly what control flow inside
			// of the then/else of a failure context means. For example, then/else
			// can contain a break/return, but we might already be executing past
			// that then/else leniently. So we need to somehow find a way to transfer
			// control of the non-lenient execution. This likely means the below
			// def of the effect token isn't always right.

			// This can't fail.
			Def(FailureContext.DoneEffectToken, Interpreter.EffectToken.Get(Context));
		}
		else
		{
			// This can't fail.
			Def(FailureContext.DoneEffectToken, NextEffectToken);
		}

		if (ParentFailure && !ParentFailure->bFailed)
		{
			// We increment the suspension count for our parent failure
			// context when this failure context sees lenient execution.
			// So this is the decrement to balance that out that increment.
			FinishedExecutingSuspensionIn(*ParentFailure);
		}
	}

	void Fail(VFailureContext& FailureContext)
	{
		V_DIE_IF(FailureContext.bFailed);
		V_DIE_UNLESS(Task == FailureContext.Task.Get());

		FailureContext.Fail(Context);
		FailureContext.FinishedExecuting(Context);

		if (LIKELY(!FailureContext.bExecutedEndFailureContextOpcode))
		{
			return;
		}

		FOp* StartPC = FailureContext.FailurePC;
		FOp* EndPC = FailureContext.DonePC;
		VValue NextEffectToken = FailureContext.IncomingEffectToken.Get();

		FinishedExecutingFailureContextLeniently(FailureContext, StartPC, EndPC, NextEffectToken);
	}

	void FinishedExecutingSuspensionIn(VFailureContext& FailureContext)
	{
		V_DIE_IF(FailureContext.bFailed);

		V_DIE_UNLESS(FailureContext.SuspensionCount);
		uint32 RemainingCount = --FailureContext.SuspensionCount;
		if (RemainingCount)
		{
			return;
		}

		if (LIKELY(!FailureContext.bExecutedEndFailureContextOpcode))
		{
			return;
		}

		FailureContext.FinishedExecuting(Context);
		FOp* StartPC = FailureContext.ThenPC;
		FOp* EndPC = FailureContext.FailurePC;
		// Since we finished executing all suspensions in this failure context without failure, we can now commit the transaction
		VValue NextEffectToken = FailureContext.BeforeThenEffectToken.Get(Context);
		if (NextEffectToken.IsPlaceholder())
		{
			VValue NewNextEffectToken = VValue::Placeholder(VPlaceholder::New(Context, 0));
			DoTransactionActionWhenEffectTokenIsConcrete<TransactAction::Commit>(FailureContext, *FailureContext.Task, NextEffectToken, NewNextEffectToken);
			NextEffectToken = NewNextEffectToken;
		}
		else
		{
			FailureContext.Transaction.Commit(Context);
		}

		FinishedExecutingFailureContextLeniently(FailureContext, StartPC, EndPC, NextEffectToken);
	}

	// Returns true if unwinding succeeded. False if we are trying to unwind past
	// the outermost frame of this Interpreter instance.
	bool UnwindIfNeeded()
	{
		if (!Failure->bFailed)
		{
			return true;
		}

		VFailureContext* FailedContext = Failure;
		while (true)
		{
			if (FailedContext == OutermostFailureContext)
			{
				return false;
			}

			VFailureContext* Parent = FailedContext->Parent.Get();
			if (!Parent->bFailed)
			{
				break;
			}
			FailedContext = Parent;
		}

		State = FExecutionState(FailedContext->FailurePC, FailedContext->Frame.Get());
		Failure = FailedContext->Parent.Get();
		EffectToken.Set(Context, FailedContext->IncomingEffectToken.Get());

		return true;
	}

	template <typename ReturnSlotType>
	void Suspend(VFailureContext& FailureContext, VTask& SuspendingTask, ReturnSlotType ResumeSlot)
	{
		V_DIE_UNLESS(&FailureContext == OutermostFailureContext);

		SuspendingTask.Suspend(Context);
		SuspendingTask.ResumeSlot.Set(Context, ResumeSlot);
	}

	// Returns true if yielding succeeded. False if we are trying to yield past
	// the outermost frame of this Interpreter instance.
	bool YieldIfNeeded(FOp* NextPC)
	{
		V_DIE_UNLESS(Failure == OutermostFailureContext);

		while (true)
		{
			if (Task->bRunning)
			{
				// The task is still active or already unwinding.
				if (Task->Phase != VTask::EPhase::CancelStarted)
				{
					return true;
				}

				if (Task->CancelChildren(Context))
				{
					BeginUnwind(NextPC);
					return true;
				}

				Task->Suspend(Context);
			}
			else
			{
				if (Task->Phase == VTask::EPhase::CancelRequested)
				{
					Task->Phase = VTask::EPhase::CancelStarted;
					if (Task->CancelChildren(Context))
					{
						Task->Resume(Context);
						BeginUnwind(NextPC);
						return true;
					}
				}
			}

			VTask* SuspendedTask = Task;

			// Save the current state for when the task is resumed.
			SuspendedTask->ResumePC = NextPC;
			SuspendedTask->ResumeFrame.Set(Context, State.Frame);

			// Switch back to the task that started or resumed this one.
			State = FExecutionState(SuspendedTask->YieldPC, SuspendedTask->YieldFrame.Get());
			Task = SuspendedTask->YieldTask.Get();

			// Detach the task from the stack.
			SuspendedTask->YieldPC = &StopInterpreterSentry;
			SuspendedTask->YieldTask.Reset();

			if (SuspendedTask == OutermostTask)
			{
				return false;
			}

			NextPC = State.PC;
		}
	}

	// Jump from PC to its associated unwind label, in the current function or some transitive caller.
	// There must always be some unwind label, because unwinding always terminates at EndTask.
	void BeginUnwind(FOp* PC)
	{
		V_DIE_UNLESS(Task->bRunning);

		Task->Phase = VTask::EPhase::CancelUnwind;

		if (Task->NativeDefer)
		{
			AutoRTFM::EContextStatus Status = AutoRTFM::Close([&] { Task->NativeDefer(Context, Task); });
			Task->NativeDefer.Reset();
			V_DIE_UNLESS(Status == AutoRTFM::EContextStatus::OnTrack);
		}

		for (VFrame* Frame = State.Frame; Frame != nullptr; PC = Frame->CallerPC, Frame = Frame->CallerFrame.Get())
		{
			VProcedure* Procedure = Frame->Procedure.Get();
			int32 Offset = Procedure->BytecodeOffset(PC);

			for (
				FUnwindEdge* UnwindEdge = Procedure->GetUnwindEdgesBegin();
				UnwindEdge != Procedure->GetUnwindEdgesEnd() && UnwindEdge->Begin < Offset;
				UnwindEdge++)
			{
				if (Offset <= UnwindEdge->End)
				{
					State = FExecutionState(UnwindEdge->OnUnwind.GetLabeledPC(), Frame);
					return;
				}
			}
		}

		VERSE_UNREACHABLE();
	}

	enum class TransactAction
	{
		Start,
		Commit
	};

	template <TransactAction Action>
	void DoTransactionActionWhenEffectTokenIsConcrete(VFailureContext& FailureContext, VTask& TaskContext, VValue IncomingEffectToken, VValue NextEffectToken)
	{
		VLambdaSuspension& Suspension = VLambdaSuspension::New(
			Context, FailureContext, TaskContext,
			[](FRunningContext TheContext, VLambdaSuspension& LambdaSuspension, VSuspension*& SuspensionsToFire) {
				if constexpr (Action == TransactAction::Start)
				{
					LambdaSuspension.FailureContext->Transaction.Start(TheContext);
				}
				else
				{
					LambdaSuspension.FailureContext->Transaction.Commit(TheContext);
				}
				VValue NextEffectToken = LambdaSuspension.Args()[0].Get();
				FInterpreter::Def(TheContext, NextEffectToken, VValue::EffectDoneMarker(), SuspensionsToFire);
			},
			NextEffectToken);

		IncomingEffectToken.EnqueueSuspension(Context, Suspension);
	}

	// Macros to be used both directly in the interpreter loops and impl functions.
	// Parameterized over the implementation of ENQUEUE_SUSPENSION, FAIL, and YIELD.

#define REQUIRE_CONCRETE(Value)          \
	if (UNLIKELY(Value.IsPlaceholder())) \
	{                                    \
		ENQUEUE_SUSPENSION(Value);       \
	}

#define DEF(Result, Value)   \
	if (!Def(Result, Value)) \
	{                        \
		FAIL();              \
	}

#define OP_RESULT_HELPER(Result)                                             \
	if (Result.Kind != FOpResult::Return)                                    \
	{                                                                        \
		if (Result.Kind == FOpResult::Block)                                 \
		{                                                                    \
			check(Result.Value.IsPlaceholder());                             \
			ENQUEUE_SUSPENSION(Result.Value);                                \
		}                                                                    \
		else if (Result.Kind == FOpResult::Fail)                             \
		{                                                                    \
			FAIL();                                                          \
		}                                                                    \
		else if (Result.Kind == FOpResult::Yield)                            \
		{                                                                    \
			YIELD();                                                         \
		}                                                                    \
		else                                                                 \
		{                                                                    \
			check(Result.Kind == FOpResult::Error);                          \
			/* TODO: SOL-4563 Implement proper handling of runtime errors */ \
			V_DIE("%s", *Result.Value.StaticCast<VArray>().AsString());      \
		}                                                                    \
	}

	// Macro definitions to be used in impl functions.

#define ENQUEUE_SUSPENSION(Value) \
	return                        \
	{                             \
		FOpResult::Block, Value   \
	}

#define FAIL()          \
	return              \
	{                   \
		FOpResult::Fail \
	}

#define YIELD()          \
	return               \
	{                    \
		FOpResult::Yield \
	}

	VRational& PrepareRationalSourceHelper(VValue& Source)
	{
		if (VRational* RationalSource = Source.DynamicCast<VRational>())
		{
			return *RationalSource;
		}

		if (!Source.IsInt())
		{
			V_DIE("Unsupported operands were passed to a Rational operation!");
		}

		return VRational::New(Context, Source.AsInt(), VInt(Context, 1));
	}

	template <typename OpType>
	FOpResult AddImpl(OpType& Op)
	{
		VValue LeftSource = GetOperand(Op.LeftSource);
		VValue RightSource = GetOperand(Op.RightSource);
		REQUIRE_CONCRETE(LeftSource);
		REQUIRE_CONCRETE(RightSource);

		if (LeftSource.IsInt() && RightSource.IsInt())
		{
			DEF(Op.Dest, VInt::Add(Context, LeftSource.AsInt(), RightSource.AsInt()));
		}
		else if (LeftSource.IsFloat() && RightSource.IsFloat())
		{
			DEF(Op.Dest, LeftSource.AsFloat() + RightSource.AsFloat());
		}
		else if (LeftSource.IsCellOfType<VRational>() || RightSource.IsCellOfType<VRational>())
		{
			VRational& LeftRational = PrepareRationalSourceHelper(LeftSource);
			VRational& RightRational = PrepareRationalSourceHelper(RightSource);

			DEF(Op.Dest, VRational::Add(Context, LeftRational, RightRational).StaticCast<VCell>());
		}
		else if (LeftSource.IsCellOfType<VArrayBase>() && RightSource.IsCellOfType<VArrayBase>())
		{
			// Array concatenation.
			VArrayBase& LeftArray = LeftSource.StaticCast<VArrayBase>();
			VArrayBase& RightArray = RightSource.StaticCast<VArrayBase>();

			DEF(Op.Dest, VArray::Concat(Context, LeftArray, RightArray));
		}
		else
		{
			V_DIE("Unsupported operands were passed to a `Add` operation!");
		}

		return {FOpResult::Return};
	}

	// TODO: Add the ability for bytecode instructions to have optional arguments so instead of having this bytecode
	//		 we can just have 'Add' which can take a boolean telling it whether the result should be mutable.
	template <typename OpType>
	FOpResult MutableAddImpl(OpType& Op)
	{
		VValue LeftSource = GetOperand(Op.LeftSource);
		VValue RightSource = GetOperand(Op.RightSource);
		REQUIRE_CONCRETE(LeftSource);
		REQUIRE_CONCRETE(RightSource);

		if (LeftSource.IsCellOfType<VArrayBase>() && RightSource.IsCellOfType<VArrayBase>())
		{
			// Array concatenation.
			VArrayBase& LeftArray = LeftSource.StaticCast<VArrayBase>();
			VArrayBase& RightArray = RightSource.StaticCast<VArrayBase>();

			DEF(Op.Dest, VMutableArray::Concat(Context, LeftArray, RightArray));
		}
		else
		{
			V_DIE("Unsupported operands were passed to a `MutableAdd` operation!");
		}

		return {FOpResult::Return};
	}

	template <typename OpType>
	FOpResult SubImpl(OpType& Op)
	{
		VValue LeftSource = GetOperand(Op.LeftSource);
		VValue RightSource = GetOperand(Op.RightSource);
		REQUIRE_CONCRETE(LeftSource);
		REQUIRE_CONCRETE(RightSource);

		if (LeftSource.IsInt() && RightSource.IsInt())
		{
			DEF(Op.Dest, VInt::Sub(Context, LeftSource.AsInt(), RightSource.AsInt()));
		}
		else if (LeftSource.IsFloat() && RightSource.IsFloat())
		{
			DEF(Op.Dest, LeftSource.AsFloat() - RightSource.AsFloat());
		}
		else if (LeftSource.IsCellOfType<VRational>() || RightSource.IsCellOfType<VRational>())
		{
			VRational& LeftRational = PrepareRationalSourceHelper(LeftSource);
			VRational& RightRational = PrepareRationalSourceHelper(RightSource);

			DEF(Op.Dest, VRational::Sub(Context, LeftRational, RightRational).StaticCast<VCell>());
		}
		else
		{
			V_DIE("Unsupported operands were passed to a `Sub` operation!");
		}

		return {FOpResult::Return};
	}

	template <typename OpType>
	FOpResult MulImpl(OpType& Op)
	{
		VValue LeftSource = GetOperand(Op.LeftSource);
		VValue RightSource = GetOperand(Op.RightSource);
		REQUIRE_CONCRETE(LeftSource);
		REQUIRE_CONCRETE(RightSource);

		if (LeftSource.IsInt())
		{
			if (RightSource.IsInt())
			{
				DEF(Op.Dest, VInt::Mul(Context, LeftSource.AsInt(), RightSource.AsInt()));
				return {FOpResult::Return};
			}
			else if (RightSource.IsFloat())
			{
				DEF(Op.Dest, LeftSource.AsInt().ConvertToFloat() * RightSource.AsFloat());
				return {FOpResult::Return};
			}
		}
		else if (LeftSource.IsFloat())
		{
			if (RightSource.IsInt())
			{
				DEF(Op.Dest, LeftSource.AsFloat() * RightSource.AsInt().ConvertToFloat());
				return {FOpResult::Return};
			}
			else if (RightSource.IsFloat())
			{
				DEF(Op.Dest, LeftSource.AsFloat() * RightSource.AsFloat());
				return {FOpResult::Return};
			}
		}

		if (LeftSource.IsCellOfType<VRational>() || RightSource.IsCellOfType<VRational>())
		{
			VRational& LeftRational = PrepareRationalSourceHelper(LeftSource);
			VRational& RightRational = PrepareRationalSourceHelper(RightSource);

			DEF(Op.Dest, VRational::Mul(Context, LeftRational, RightRational).StaticCast<VCell>());
			return {FOpResult::Return};
		}

		V_DIE("Unsupported operands were passed to a `Mul` operation!");
		VERSE_UNREACHABLE();
	}

	template <typename OpType>
	FOpResult DivImpl(OpType& Op)
	{
		VValue LeftSource = GetOperand(Op.LeftSource);
		VValue RightSource = GetOperand(Op.RightSource);
		REQUIRE_CONCRETE(LeftSource);
		REQUIRE_CONCRETE(RightSource);

		if (LeftSource.IsInt() && RightSource.IsInt())
		{
			if (RightSource.AsInt().IsZero())
			{
				FAIL();
			}

			DEF(Op.Dest, VRational::New(Context, LeftSource.AsInt(), RightSource.AsInt()).StaticCast<VCell>());
		}
		else if (LeftSource.IsFloat() && RightSource.IsFloat())
		{
			DEF(Op.Dest, LeftSource.AsFloat() / RightSource.AsFloat());
		}
		else if (LeftSource.IsCellOfType<VRational>() || RightSource.IsCellOfType<VRational>())
		{
			VRational& LeftRational = PrepareRationalSourceHelper(LeftSource);
			VRational& RightRational = PrepareRationalSourceHelper(RightSource);
			if (RightRational.IsZero())
			{
				FAIL();
			}

			DEF(Op.Dest, VRational::Div(Context, LeftRational, RightRational).StaticCast<VCell>());
		}
		else
		{
			V_DIE("Unsupported operands were passed to a `Div` operation!");
		}

		return {FOpResult::Return};
	}

	template <typename OpType>
	FOpResult ModImpl(OpType& Op)
	{
		VValue LeftSource = GetOperand(Op.LeftSource);
		VValue RightSource = GetOperand(Op.RightSource);
		REQUIRE_CONCRETE(LeftSource);
		REQUIRE_CONCRETE(RightSource);

		if (LeftSource.IsInt() && RightSource.IsInt())
		{
			if (RightSource.AsInt().IsZero())
			{
				FAIL();
			}

			DEF(Op.Dest, VInt::Mod(Context, LeftSource.AsInt(), RightSource.AsInt()));
		}
		// TODO: VRational could support Mod in limited circumstances
		else
		{
			V_DIE("Unsupported operands were passed to a `Mod` operation!");
		}

		return {FOpResult::Return};
	}

	template <typename OpType>
	FOpResult NegImpl(OpType& Op)
	{
		VValue Source = GetOperand(Op.Source);
		REQUIRE_CONCRETE(Source);

		if (Source.IsInt())
		{
			DEF(Op.Dest, VInt::Neg(Context, Source.AsInt()));
		}
		else if (Source.IsFloat())
		{
			DEF(Op.Dest, -(Source.AsFloat()));
		}
		else if (Source.IsCellOfType<VRational>())
		{
			DEF(Op.Dest, VRational::Neg(Context, Source.StaticCast<VRational>()));
		}
		else
		{
			V_DIE("Unimplemented type passed to VM `Neg` operation");
		}

		return {FOpResult::Return};
	}

	template <typename OpType>
	FOpResult QueryImpl(OpType& Op)
	{
		VValue Source = GetOperand(Op.Source);
		REQUIRE_CONCRETE(Source);

		if (Source.ExtractCell() == GlobalFalsePtr.Get())
		{
			FAIL();
		}
		else if (VOption* Option = Source.DynamicCast<VOption>()) // True = VOption(VFalse), which is handled by this case
		{
			DEF(Op.Dest, Option->GetValue());
		}
		else if (!Source.IsUObject())
		{
			V_DIE("Unimplemented type passed to VM `Query` operation");
		}

		return {FOpResult::Return};
	}

	template <typename OpType>
	FOpResult MapKeyImpl(OpType& Op)
	{
		VValue Map = GetOperand(Op.Map);
		VValue Index = GetOperand(Op.Index);
		REQUIRE_CONCRETE(Map);
		REQUIRE_CONCRETE(Index);

		if (Map.IsCellOfType<VMapBase>() && Index.IsInt())
		{
			DEF(Op.Dest, Map.StaticCast<VMapBase>().GetKey(Index.AsInt32()));
		}
		else
		{
			V_DIE("Unimplemented type passed to VM `MapKey` operation!");
		}
		return {FOpResult::Return};
	}

	template <typename OpType>
	FOpResult MapValueImpl(OpType& Op)
	{
		VValue Map = GetOperand(Op.Map);
		VValue Index = GetOperand(Op.Index);
		REQUIRE_CONCRETE(Map);
		REQUIRE_CONCRETE(Index);

		if (Map.IsCellOfType<VMapBase>() && Index.IsInt())
		{
			DEF(Op.Dest, Map.StaticCast<VMapBase>().GetValue(Index.AsInt32()));
		}
		else
		{
			V_DIE("Unimplemented type passed to VM `MapValue` operation!");
		}
		return {FOpResult::Return};
	}

	template <typename OpType>
	FOpResult LengthImpl(OpType& Op)
	{
		const VValue Container = GetOperand(Op.Container);
		// We need this to be concrete before we can attempt to get its size, even if the values in the container
		// might be placeholders.
		REQUIRE_CONCRETE(Container);
		if (const VArrayBase* Array = Container.DynamicCast<VArrayBase>())
		{
			DEF(Op.Dest, VInt{static_cast<int32>(Array->Num())});
		}
		else if (const VMapBase* Map = Container.DynamicCast<VMapBase>())
		{
			DEF(Op.Dest, VInt{static_cast<int32>(Map->Num())});
		}
		else
		{
			V_DIE("Unsupported container type passed!");
		}

		return {FOpResult::Return};
	}

	// TODO (SOL-5813) : Optimize melt to start at the value it suspended on rather
	// than re-doing the entire melt Op again which is what we do currently.
	template <typename OpType>
	FOpResult MeltImpl(OpType& Op)
	{
		VValue Value = GetOperand(Op.Value);
		VValue Result = VValue::Melt(Context, Value);
		REQUIRE_CONCRETE(Result);
		DEF(Op.Dest, Result);
		return {FOpResult::Return};
	}

	template <typename OpType>
	FOpResult FreezeImpl(OpType& Op)
	{
		VValue Value = GetOperand(Op.Value);
		VValue Result = VValue::Freeze(Context, Value);
		DEF(Op.Dest, Result);
		return {FOpResult::Return};
	}

	template <typename OpType>
	FOpResult VarGetImpl(OpType& Op)
	{
		VValue Var = GetOperand(Op.Var);
		REQUIRE_CONCRETE(Var);
		VValue Result;
		if (VVar* Ref = Var.DynamicCast<VVar>())
		{
			Result = Ref->Get(Context);
		}
		else if (VNativeRef* NativeRef = Var.DynamicCast<VNativeRef>())
		{
			Result = *NativeRef;
		}
		else
		{
			V_DIE("Unexpected ref type %s", *Var.AsCell().DebugName());
		}
		DEF(Op.Dest, Result);
		return {FOpResult::Return};
	}

	template <typename OpType>
	FOpResult VarSetImpl(OpType& Op)
	{
		VValue Var = GetOperand(Op.Var);
		VValue Value = GetOperand(Op.Value);
		REQUIRE_CONCRETE(Var);
		if (VVar* VarPtr = Var.DynamicCast<VVar>())
		{
			VarPtr->Set(Context, Value);
		}
		else if (VNativeRef* Ref = Var.DynamicCast<VNativeRef>())
		{
			FOpResult Result = Ref->Set(Context, Value);
			OP_RESULT_HELPER(Result);
		}
		else
		{
			V_DIE("Unexpected ref type %s", *Value.AsCell().DebugName());
		}
		return {FOpResult::Return};
	}

	template <typename OpType>
	FOpResult CallSetImpl(OpType& Op)
	{
		const VValue Container = GetOperand(Op.Container);
		const VValue Index = GetOperand(Op.Index);
		const VValue ValueToSet = GetOperand(Op.ValueToSet);
		REQUIRE_CONCRETE(Container);
		REQUIRE_CONCRETE(Index); // Must be an Int32 (although UInt32 is better)
		if (VMutableArray* Array = Container.DynamicCast<VMutableArray>())
		{
			// Bounds check since this index access in Verse is failable.
			if (Index.IsInt32() && Index.AsInt32() >= 0 && Array->IsInBounds(Index.AsInt32()))
			{
				Array->SetValueTransactionally(Context, static_cast<uint32>(Index.AsInt32()), ValueToSet);
			}
			else
			{
				FAIL();
			}
		}
		else if (VMutableMap* Map = Container.DynamicCast<VMutableMap>())
		{
			Map->AddTransactionally(Context, Index, ValueToSet);
		}
		else
		{
			V_DIE("Unsupported container type passed!");
		}

		return {FOpResult::Return};
	}

	template <typename OpType>
	FOpResult CallImpl(OpType& Op, VValue Callee, VTask* TaskContext, VValue IncomingEffectToken)
	{
		// Handles FOpCall for all cases except VFunction calls which
		// are handled differently for lenient and non-lenient calls.
		check(!Callee.IsPlaceholder());

		auto Arguments = GetOperands(Op.Arguments);
		if (VNativeFunction* NativeFunction = Callee.DynamicCast<VNativeFunction>())
		{
			// With leniency, the active failure contexts aren't 1:1 with the active transactions.
			// The active failure contexts form a tree. The active transactions form a path in that tree.
			// Right now, an active VM transaction is 1:1 with an RTFM transaction.
			// So, this begs the question: when calling a native function that has effects <= <computes>,
			// what do we do if that native call is inside a failure context that isn't part of the active transaction path.
			// What transaction do we run it in?
			// If we make it so that native functions suspend on the effect token, we never find ourselves in the
			// "what do we do if that native call is inside a failure context that isn't part of the active transaction path" problem.
			// But also, long term, this will make more programs stuck than we want.
			REQUIRE_CONCRETE(IncomingEffectToken);

			VFunction::Args Args;
			Args.AddUninitialized(NativeFunction->NumParameters);
			UnboxArguments(
				Context, NativeFunction->NumParameters, 0, Arguments.Num(), nullptr, nullptr,
				[&](uint32 Arg) {
					return GetOperand(Arguments[Arg]);
				},
				[&](uint32 Param, VValue Value) {
					Args[Param] = Value;
				},
				[](uint32 NamedArg) -> VValue { VERSE_UNREACHABLE(); }, // Named params not supported for native functions yet - #JIRA SOL-5954
				[](uint32 NamedParam, VValue Value) -> VValue { VERSE_UNREACHABLE(); });
			FNativeCallResult Result{FNativeCallResult::Error};
			Context.RunInNativeContext(Failure, TaskContext, [&] {
				Result = (*NativeFunction->Thunk)(Context, NativeFunction->Self.Get(), Args);
			});
			OP_RESULT_HELPER(Result);
			DEF(Op.Dest, Result.Value);
		}
		else
		{
			V_DIE_UNLESS(Arguments.Num() == 1);

			VValue Argument = GetOperand(Arguments[0]);
			// Special cases for known container types.
			if (VArrayBase* Array = Callee.DynamicCast<VArrayBase>())
			{
				REQUIRE_CONCRETE(Argument);
				// Bounds check since this index access in Verse is fallible.
				if (Argument.IsUint32() && Array->IsInBounds(Argument.AsUint32()))
				{
					DEF(Op.Dest, Array->GetValue(Argument.AsUint32()));
				}
				else
				{
					FAIL();
				}
			}
			else if (VMapBase* Map = Callee.DynamicCast<VMapBase>())
			{
				// TODO SOL-5621: We need to ensure the entire Key structure is concrete, not just the top-level.
				REQUIRE_CONCRETE(Argument);
				if (VValue Result = Map->Find(Context, Argument))
				{
					DEF(Op.Dest, Result);
				}
				else
				{
					FAIL();
				}
			}
			else if (VType* Type = Callee.DynamicCast<VType>())
			{
				REQUIRE_CONCRETE(Argument);
				if (Type->Subsumes(Context, Argument))
				{
					DEF(Op.Dest, Argument);
				}
				else
				{
					FAIL();
				}
			}
			else
			{
				V_DIE("Unknown callee");
			}
		}

		return {FOpResult::Return};
	}

	template <typename OpType>
	FOpResult NewArrayImpl(OpType& Op)
	{
		auto Values = GetOperands(Op.Values);
		const uint32 NumValues = Values.Num();
		VArray& NewArray = VArray::New(Context, NumValues, [this, &Values](uint32 Index) { return GetOperand(Values[Index]); });
		DEF(Op.Dest, NewArray);
		return {FOpResult::Return};
	}

	template <typename OpType>
	FOpResult NewMutableArrayImpl(OpType& Op)
	{
		auto Values = GetOperands(Op.Values);
		const uint32 NumValues = Values.Num();
		VMutableArray& NewArray = VMutableArray::New(Context, NumValues, [this, &Values](uint32 Index) { return GetOperand(Values[Index]); });
		DEF(Op.Dest, NewArray);
		return {FOpResult::Return};
	}

	template <typename OpType>
	FOpResult NewMutableArrayWithCapacityImpl(OpType& Op)
	{
		const VValue Size = GetOperand(Op.Size);
		REQUIRE_CONCRETE(Size); // Must be an Int32 (although UInt32 is better)
		// TODO: We should kill this opcode until we actually have a use for it.
		// Allocating this with None array type means we're not actually reserving a
		// capacity. The way to do this right in the future is to use profiling to
		// guide what array type we pick. This opcode is currently only being
		// used in our bytecode tests.
		DEF(Op.Dest, VMutableArray::New(Context, 0, static_cast<uint32>(Size.AsInt32()), EArrayType::None));

		return {FOpResult::Return};
	}

	template <typename OpType>
	FOpResult ArrayAddImpl(OpType& Op)
	{
		const VValue Container = GetOperand(Op.Container);
		const VValue ValueToAdd = GetOperand(Op.ValueToAdd);
		REQUIRE_CONCRETE(Container);
		if (VMutableArray* Array = Container.DynamicCast<VMutableArray>())
		{
			Array->AddValue(Context, ValueToAdd);
		}
		else
		{
			V_DIE("Unimplemented type passed to VM `ArrayAdd` operation!");
		}

		return {FOpResult::Return};
	}

	template <typename OpType>
	FOpResult InPlaceMakeImmutableImpl(OpType& Op)
	{
		const VValue Container = GetOperand(Op.Container);
		REQUIRE_CONCRETE(Container);
		if (Container.IsCellOfType<VMutableArray>())
		{
			Container.StaticCast<VMutableArray>().InPlaceMakeImmutable(Context);
			checkSlow(Container.IsCellOfType<VArray>() && !Container.IsCellOfType<VMutableArray>());
		}
		else
		{
			V_DIE("Unimplemented type passed to VM `InPlaceMakeImmutable` operation!");
		}

		return {FOpResult::Return};
	}

	template <typename OpType>
	FOpResult NewOptionImpl(OpType& Op)
	{
		VValue Value = GetOperand(Op.Value);

		DEF(Op.Dest, VOption::New(Context, Value));

		return {FOpResult::Return};
	}

	template <typename OpType>
	FOpResult NewMapImpl(OpType& Op)
	{
		auto Keys = GetOperands(Op.Keys);
		auto Values = GetOperands(Op.Values);

		const uint32 NumKeys = Keys.Num();
		V_DIE_UNLESS(NumKeys == static_cast<uint32>(Values.Num()));

		VMapBase& NewMap = VMapBase::New<VMap>(Context, NumKeys, [this, &Keys, &Values](uint32 Index) {
			return TPair<VValue, VValue>(GetOperand(Keys[Index]), GetOperand(Values[Index]));
		});

		DEF(Op.Dest, NewMap);

		return {FOpResult::Return};
	}

	template <typename OpType>
	FOpResult NewClassImpl(OpType& Op)
	{
		auto Inherited = GetOperands(Op.Inherited);

		TArray<VClass*> InheritedClasses = {};
		const uint32 NumInherited = Inherited.Num();
		InheritedClasses.Reserve(NumInherited);
		for (uint32 Index = 0; Index < NumInherited; ++Index)
		{
			const VValue CurrentArg = GetOperand(Inherited[Index]);
			REQUIRE_CONCRETE(CurrentArg);
			InheritedClasses.Add(&CurrentArg.StaticCast<VClass>());
		}
		VConstructor* Constructor = Op.Constructor.Get();
		UStruct* ImportStruct = Op.ImportStruct.Get() ? CastChecked<UStruct>(Op.ImportStruct.Get().AsUObject()) : nullptr;
		VClass& NewClass = VClass::New(Context, Op.Package.Get(), Op.Name.Get(), Op.UEMangledName.Get(), ImportStruct, Op.bNative, Op.ClassKind, InheritedClasses, *Constructor);
		DEF(Op.Dest, NewClass);
		return {FOpResult::Return};
	}

	template <typename OpType>
	FOpResult NewObjectImpl(OpType& Op, VClass& Class, VValue& NewObject, TArray<VFunction*>& Initializers)
	{
		auto Values = GetOperands(Op.Values);
		const uint32 NumFields = Op.Fields->Num();
		const uint32 NumValues = Values.Num();

		V_DIE_UNLESS(NumFields == NumValues);

		TArray<VValue> ArchetypeValues;
		ArchetypeValues.Reserve(NumValues);
		for (uint32 Index = 0; Index < NumValues; ++Index)
		{
			VValue CurrentValue = GetOperand(Values[Index]);
			REQUIRE_CONCRETE(CurrentValue);
			ArchetypeValues.Add(CurrentValue);
		}
		VUniqueStringSet& ArchetypeFields = *Op.Fields.Get();

		// UObject/VNativeStruct or VObject?
		bool bNative = Class.IsNative();
		if (!bNative && !Class.IsStruct())
		{
			const float UObjectProbability = CVarUObjectProbability.GetValueOnAnyThread();
			bNative = UObjectProbability > 0.0f && (UObjectProbability > RandomUObjectProbability.FRand());
		}
		if (bNative)
		{
			if (!Class.IsStruct())
			{
				V_RUNTIME_ERROR_IF(!verse::CanAllocateUObjects(), Context, FUtf8String::Printf("Ran out of memory for allocating `UObject`s while attempting to construct a Verse object of type %s!", *FString(Class.GetName())));

				NewObject = Class.NewUObject(Context, ArchetypeFields, ArchetypeValues, Initializers);
			}
			else
			{
				FOpResult Result = Class.NewNativeStruct(Context, ArchetypeFields, ArchetypeValues, Initializers);
				OP_RESULT_HELPER(Result);

				NewObject = Result.Value;
			}
		}
		else
		{
			NewObject = Class.NewVObject(Context, ArchetypeFields, ArchetypeValues, Initializers);
		}

		DEF(Op.Dest, NewObject);

		return {FOpResult::Return};
	}

	template <typename OpType>
	FOpResult LoadFieldImpl(OpType& Op)
	{
		const VValue ObjectOperand = GetOperand(Op.Object);
		REQUIRE_CONCRETE(ObjectOperand);
		VUniqueString& FieldName = *Op.Name.Get();
		VValue FieldValue;
		if (VObject* Object = ObjectOperand.DynamicCast<VObject>())
		{
			FieldValue = Object->LoadField(Context, FieldName);
		}
		else if (UObject* UeObject = ObjectOperand.ExtractUObject())
		{
			FieldValue = UVerseClass::LoadField(Context, UeObject, FieldName);
		}
		else
		{
			V_DIE("Unsupported operand to a `LoadField` operation!");
		}
		V_DIE_UNLESS(FieldValue);
		DEF(Op.Dest, FieldValue);
		return {FOpResult::Return};
	}

	template <typename OpType>
	FOpResult LoadFieldFromSuperImpl(OpType& Op)
	{
		const VValue ScopeOperand = GetOperand(Op.Scope);
		REQUIRE_CONCRETE(ScopeOperand);

		const VValue SelfOperand = GetOperand(Op.Self);
		REQUIRE_CONCRETE(SelfOperand);

		VUniqueString& FieldName = *Op.Name.Get();

		// Currently, we only allow object instances (of classes) to be referred to by `Self`.
		V_DIE_UNLESS(SelfOperand.IsCellOfType<VValueObject>() || SelfOperand.IsUObject());
		if (VValueObject* OperandValueObject = SelfOperand.DynamicCast<VValueObject>())
		{
			V_DIE_IF(OperandValueObject->IsStruct()); // Structs don't support inheritance or methods.
		}

		// NOTE: (yiliang.siew) We need to allocate a new function here for now in order to support passing methods around
		// as first-class values, since the method for each caller can't just be shared as the function from the
		// shape/constructor.
		VScope& Scope = ScopeOperand.StaticCast<VScope>();
		// NOTE: (yiliang.siew) For now, the scope can only store a superclass. In the future when scopes can handle the
		// captures for lambdas, this will have to be updated.
		V_DIE_UNLESS(Scope.SuperClass);
		VFunction* FunctionWithSelf = Scope.SuperClass->GetConstructor().LoadFunction(Context, FieldName, SelfOperand);
		V_DIE_UNLESS(FunctionWithSelf);

		DEF(Op.Dest, *FunctionWithSelf);

		return {FOpResult::Return};
	}

	template <typename OpType>
	FOpResult UnifyFieldImpl(OpType& Op)
	{
		const VValue ObjectOperand = GetOperand(Op.Object);
		REQUIRE_CONCRETE(ObjectOperand);
		VValue ValueOperand = GetOperand(Op.Value);
		REQUIRE_CONCRETE(ValueOperand);
		VUniqueString& FieldName = *Op.Name.Get();

		bool bSucceeded = false;
		if (VObject* Object = ObjectOperand.DynamicCast<VObject>())
		{
			const VEmergentType* EmergentType = Object->GetEmergentType();
			VShape* Shape = EmergentType->Shape.Get();
			V_DIE_UNLESS(Shape != nullptr);
			const VShape::VEntry* Field = Shape->GetField(FieldName);
			V_DIE_UNLESS(Field != nullptr);
			switch (Field->Type)
			{
				case EFieldType::Offset:
					bSucceeded = Def(Object->GetFieldData(*EmergentType->CppClassInfo)[Field->Index], ValueOperand);
					break;

				// NOTE: VNativeRef::Set only makes sense here because UnifyField is only used for initialization.
				case EFieldType::FProperty:
				{
					FOpResult Result = VNativeRef::Set<false>(Context, nullptr, Object->GetData(*EmergentType->CppClassInfo), Field->UProperty, ValueOperand);
					OP_RESULT_HELPER(Result);
					bSucceeded = true;
					break;
				}
				case EFieldType::FPropertyVar:
				{
					FOpResult Result = VNativeRef::Set<false>(Context, nullptr, Object->GetData(*EmergentType->CppClassInfo), Field->UProperty, ValueOperand.StaticCast<VVar>().Get(Context));
					OP_RESULT_HELPER(Result);
					bSucceeded = true;
					break;
				}

				case EFieldType::FVerseProperty:
					bSucceeded = Def(*Field->UProperty->ContainerPtrToValuePtr<VRestValue>(Object->GetData(*EmergentType->CppClassInfo)), ValueOperand);
					break;
				case EFieldType::Constant:
					bSucceeded = Def(Field->Value.Get(), ValueOperand);
					break;
				default:
					V_DIE("Field: %s has an unsupported type; cannot unify!", *Op.Name.Get()->AsString());
					break;
			}
		}
		else if (UObject* UeObject = ObjectOperand.ExtractUObject())
		{
			UVerseClass* Class = CastChecked<UVerseClass>(UeObject->GetClass());
			VShape* Shape = Class->Shape.Get();
			V_DIE_UNLESS(Shape != nullptr);
			const VShape::VEntry* Field = Shape->GetField(FieldName);
			V_DIE_UNLESS(Field != nullptr);
			switch (Field->Type)
			{
				// NOTE: VNativeRef::Set only makes sense here because UnifyField is only used for initialization.
				case EFieldType::FProperty:
				{
					FOpResult Result = VNativeRef::Set<false>(Context, nullptr, UeObject, Field->UProperty, ValueOperand);
					OP_RESULT_HELPER(Result);
					bSucceeded = true;
					break;
				}
				case EFieldType::FPropertyVar:
				{
					FOpResult Result = VNativeRef::Set<false>(Context, nullptr, UeObject, Field->UProperty, ValueOperand.StaticCast<VVar>().Get(Context));
					OP_RESULT_HELPER(Result);
					bSucceeded = true;
					break;
				}

				case EFieldType::FVerseProperty:
					bSucceeded = Def(*Field->UProperty->ContainerPtrToValuePtr<VRestValue>(UeObject), ValueOperand);
					break;
				case EFieldType::Constant:
					bSucceeded = Def(Field->Value.Get(), ValueOperand);
					break;
				default:
					V_DIE("Field: %s has an unsupported type; cannot unify!", *FieldName.AsString());
					break;
			}
		}
		else
		{
			V_DIE("Unsupported operand to a `UnifyField` operation!");
		}

		return bSucceeded ? FOpResult{FOpResult::Return} : FOpResult{FOpResult::Fail};
	}

	template <typename OpType>
	FOpResult SetFieldImpl(OpType& Op)
	{
		const VValue ObjectOperand = GetOperand(Op.Object);
		REQUIRE_CONCRETE(ObjectOperand);
		VValue Value = GetOperand(Op.Value);
		VUniqueString& FieldName = *Op.Name.Get();

		// This is only used for setting into a deeply mutable struct.
		// However, this code should just work for setting fields var
		// fields in a class when we stop boxing those fields in a VVar.

		bool bSucceeded = false;
		if (VObject* Object = ObjectOperand.DynamicCast<VObject>())
		{
			const VEmergentType* EmergentType = Object->GetEmergentType();
			VShape* Shape = EmergentType->Shape.Get();
			const VShape::VEntry* Field = Shape->GetField(FieldName);
			switch (Field->Type)
			{
				case EFieldType::Offset:
					Object->GetFieldData(*EmergentType->CppClassInfo)[Field->Index].SetTransactionally(Context, Object, Value);
					break;
				case EFieldType::FProperty:
				{
					FOpResult Result = VNativeRef::Set<true>(Context, Object->DynamicCast<VNativeStruct>(), Object->GetData(*EmergentType->CppClassInfo), Field->UProperty, Value);
					OP_RESULT_HELPER(Result);
					break;
				}
				case EFieldType::FVerseProperty:
					Field->UProperty->ContainerPtrToValuePtr<VRestValue>(Object->GetData(*EmergentType->CppClassInfo))->SetTransactionally(Context, Object, Value);
					break;
				case EFieldType::FPropertyVar:
				default:
					V_DIE("Field %s has an unsupported type; cannot set!", *FieldName.AsString());
					break;
			}
		}
		else if (ObjectOperand.IsUObject())
		{
			// TODO: Implement this when we stop boxing fields in VVars.
			VERSE_UNREACHABLE();
		}
		else
		{
			V_DIE("Unsupported operand to a `SetField` operation!");
		}

		return FOpResult{FOpResult::Return};
	}

	FOpResult NeqImplHelper(VValue LeftSource, VValue RightSource)
	{
		VValue ToSuspendOn;
		// This returns true for placeholders, so if we see any placeholders,
		// we're not yet done checking for inequality because we need to
		// check the concrete values.
		bool Result = VValue::Equal(Context, LeftSource, RightSource, [&](VValue Left, VValue Right) {
			checkSlow(Left.IsPlaceholder() || Right.IsPlaceholder());
			if (!ToSuspendOn)
			{
				ToSuspendOn = Left.IsPlaceholder() ? Left : Right;
			}
		});

		if (!Result)
		{
			return {FOpResult::Return};
		}
		REQUIRE_CONCRETE(ToSuspendOn);
		FAIL();
	}

	FOpResult LtImplHelper(VValue LeftSource, VValue RightSource)
	{
		REQUIRE_CONCRETE(LeftSource);
		REQUIRE_CONCRETE(RightSource);

		if (LeftSource.IsInt() && RightSource.IsInt())
		{
			if (!VInt::Lt(Context, LeftSource.AsInt(), RightSource.AsInt()))
			{
				FAIL();
			}
		}
		else if (LeftSource.IsFloat() && RightSource.IsFloat())
		{
			if (!(LeftSource.AsFloat() < RightSource.AsFloat()))
			{
				FAIL();
			}
		}
		else if (LeftSource.IsCellOfType<VRational>() && RightSource.IsCellOfType<VRational>())
		{
			VRational& LeftRational = LeftSource.StaticCast<VRational>();
			VRational& RightRational = RightSource.StaticCast<VRational>();
			if (!VRational::Lt(Context, LeftRational, RightRational))
			{
				FAIL();
			}
		}
		else
		{
			V_DIE("Unsupported operands were passed to a `Lt` operation!");
		}

		return {FOpResult::Return};
	}

	FOpResult LteImplHelper(VValue LeftSource, VValue RightSource)
	{
		REQUIRE_CONCRETE(LeftSource);
		REQUIRE_CONCRETE(RightSource);

		if (LeftSource.IsInt() && RightSource.IsInt())
		{
			if (!VInt::Lte(Context, LeftSource.AsInt(), RightSource.AsInt()))
			{
				FAIL();
			}
		}
		else if (LeftSource.IsFloat() && RightSource.IsFloat())
		{
			if (!(LeftSource.AsFloat() <= RightSource.AsFloat()))
			{
				FAIL();
			}
		}
		else if (LeftSource.IsCellOfType<VRational>() && RightSource.IsCellOfType<VRational>())
		{
			VRational& LeftRational = LeftSource.StaticCast<VRational>();
			VRational& RightRational = RightSource.StaticCast<VRational>();
			if (!VRational::Lte(Context, LeftRational, RightRational))
			{
				FAIL();
			}
		}
		else
		{
			V_DIE("Unsupported operands were passed to a `Lte` operation!");
		}

		return {FOpResult::Return};
	}

	FOpResult GtImplHelper(VValue LeftSource, VValue RightSource)
	{
		REQUIRE_CONCRETE(LeftSource);
		REQUIRE_CONCRETE(RightSource);

		if (LeftSource.IsInt() && RightSource.IsInt())
		{
			if (!VInt::Gt(Context, LeftSource.AsInt(), RightSource.AsInt()))
			{
				FAIL();
			}
		}
		else if (LeftSource.IsFloat() && RightSource.IsFloat())
		{
			if (!(LeftSource.AsFloat() > RightSource.AsFloat()))
			{
				FAIL();
			}
		}
		else if (LeftSource.IsCellOfType<VRational>() && RightSource.IsCellOfType<VRational>())
		{
			VRational& LeftRational = LeftSource.StaticCast<VRational>();
			VRational& RightRational = RightSource.StaticCast<VRational>();
			if (!VRational::Gt(Context, LeftRational, RightRational))
			{
				FAIL();
			}
		}
		else
		{
			V_DIE("Unsupported operands were passed to a `Gt` operation!");
		}

		return {FOpResult::Return};
	}

	FOpResult GteImplHelper(VValue LeftSource, VValue RightSource)
	{
		REQUIRE_CONCRETE(LeftSource);
		REQUIRE_CONCRETE(RightSource);

		if (LeftSource.IsInt() && RightSource.IsInt())
		{
			if (!VInt::Gte(Context, LeftSource.AsInt(), RightSource.AsInt()))
			{
				FAIL();
			}
		}
		else if (LeftSource.IsFloat() && RightSource.IsFloat())
		{
			if (!(LeftSource.AsFloat() >= RightSource.AsFloat()))
			{
				FAIL();
			}
		}
		else if (LeftSource.IsCellOfType<VRational>() && RightSource.IsCellOfType<VRational>())
		{
			VRational& LeftRational = LeftSource.StaticCast<VRational>();
			VRational& RightRational = RightSource.StaticCast<VRational>();
			if (!VRational::Gte(Context, LeftRational, RightRational))
			{
				FAIL();
			}
		}
		else
		{
			V_DIE("Unsupported operands were passed to a `Gte` operation!");
		}

		return {FOpResult::Return};
	}

#define DECLARE_COMPARISON_OP_IMPL(OpName)                              \
	template <typename OpType>                                          \
	FOpResult OpName##Impl(OpType& Op)                                  \
	{                                                                   \
		VValue LeftSource = GetOperand(Op.LeftSource);                  \
		VValue RightSource = GetOperand(Op.RightSource);                \
		FOpResult Result = OpName##ImplHelper(LeftSource, RightSource); \
		if (Result.Kind == FOpResult::Return)                           \
		{                                                               \
			/* success returns the left - value */                      \
			Def(Op.Dest, LeftSource);                                   \
		}                                                               \
		return Result;                                                  \
	}

	DECLARE_COMPARISON_OP_IMPL(Neq)
	DECLARE_COMPARISON_OP_IMPL(Lt)
	DECLARE_COMPARISON_OP_IMPL(Lte)
	DECLARE_COMPARISON_OP_IMPL(Gt)
	DECLARE_COMPARISON_OP_IMPL(Gte)

#undef ENQUEUE_SUSPENSION
#undef FAIL
#undef YIELD

	// NOTE: (yiliang.siew) We don't templat-ize `bHasOutermostPCBounds` since it would mean duplicating the codegen
	// where `ExecuteImpl` gets called. Since it's the interpreter loop and a really big function, it bloats compile times.
	template <bool bPrintTrace>
	FORCENOINLINE void ExecuteImpl(const bool bHasOutermostPCBounds)
	{
		// Macros to be used in both the interpreter loops.
		// Parameterized over the implementation of BEGIN/END_OP_CASE as well as ENQUEUE_SUSPENSION, FAIL, and YIELD

#define OP_IMPL_HELPER(OpName, ...)                     \
	FOpResult Result = OpName##Impl(Op, ##__VA_ARGS__); \
	OP_RESULT_HELPER(Result)

/// Define an opcode implementation that may suspend as part of execution.
#define OP_IMPL(OpName)    \
	BEGIN_OP_CASE(OpName){ \
		OP_IMPL_HELPER(OpName)} END_OP_CASE()

// Macro definitions to be used in the main interpreter loop.

// We REQUIRE_CONCRETE on the effect token first because it obviates the need to capture
// the incoming effect token. If the incoming effect token is a placeholder, we will
// suspend, and we'll only resume after it becomes concrete.
#define OP_IMPL_THREAD_EFFECTS(OpName)                         \
	BEGIN_OP_CASE(OpName)                                      \
	{                                                          \
		VValue IncomingEffectToken = EffectToken.Get(Context); \
		BumpEffectEpoch();                                     \
		REQUIRE_CONCRETE(IncomingEffectToken);                 \
		OP_IMPL_HELPER(OpName)                                 \
		DEF(EffectToken, VValue::EffectDoneMarker());          \
	}                                                          \
	END_OP_CASE()

#define NEXT_OP(bSuspended, bFailed)   \
	if constexpr (bPrintTrace)         \
	{                                  \
		EndTrace(bSuspended, bFailed); \
	}                                  \
	NextOp();                          \
	break

#define BEGIN_OP_CASE(Name)                                 \
	case EOpcode::Name:                                     \
	{                                                       \
		if constexpr (bPrintTrace)                          \
		{                                                   \
			BeginTrace();                                   \
		}                                                   \
		FOp##Name& Op = *static_cast<FOp##Name*>(State.PC); \
		NextPC = BitCast<FOp*>(&Op + 1);

#define END_OP_CASE()      \
	NEXT_OP(false, false); \
	}

#define ENQUEUE_SUSPENSION(Value)                                                                                                              \
	VBytecodeSuspension& Suspension = VBytecodeSuspension::New(Context, *Failure, *Task, *State.Frame->Procedure, State.PC, MakeCaptures(Op)); \
	Value.EnqueueSuspension(Context, Suspension);                                                                                              \
	++Failure->SuspensionCount;                                                                                                                \
	NEXT_OP(true, false)

#define FAIL()             \
	Fail(*Failure);        \
	if (!UnwindIfNeeded()) \
	{                      \
		return;            \
	}                      \
	NextPC = State.PC;     \
	NEXT_OP(false, true)

#define YIELD()                                   \
	Suspend(*Failure, *Task, MakeReturnSlot(Op)); \
	if (!YieldIfNeeded(NextPC))                   \
	{                                             \
		return;                                   \
	}                                             \
	NextPC = State.PC;                            \
	NEXT_OP(false, false)

		if (CurrentSuspension)
		{
			goto SuspensionInterpreterLoop;
		}

	MainInterpreterLoop:
		while (true)
		{
			FOp* NextPC = nullptr;

			auto UpdateExecutionState = [&](FOp* PC, VFrame* Frame) {
				State = FExecutionState(PC, Frame);
				NextPC = PC;
			};

			auto ReturnTo = [&](FOp* PC, VFrame* Frame) {
				if (Frame)
				{
					UpdateExecutionState(PC, Frame);
				}
				else
				{
					NextPC = &StopInterpreterSentry;
				}
			};

			auto NextOp = [&] {
				if (bHasOutermostPCBounds)
				{
					if (UNLIKELY(!State.Frame->CallerFrame
								 && (NextPC < OutermostStartPC || NextPC >= OutermostEndPC)))
					{
						NextPC = &StopInterpreterSentry;
					}
				}

				State.PC = NextPC;
			};

			Context.CheckForHandshake();

			if (FDebugger* Debugger = GetDebugger(); Debugger && State.PC != &StopInterpreterSentry)
			{
				Debugger->Notify(Context, *State.Frame, *State.PC);
			}

			switch (State.PC->Opcode)
			{
				OP_IMPL(Add)
				OP_IMPL(Sub)
				OP_IMPL(Mul)
				OP_IMPL(Div)
				OP_IMPL(Mod)
				OP_IMPL(Neg)

				OP_IMPL(MutableAdd)

				OP_IMPL(Neq)
				OP_IMPL(Lt)
				OP_IMPL(Lte)
				OP_IMPL(Gt)
				OP_IMPL(Gte)

				OP_IMPL(Query)

				OP_IMPL_THREAD_EFFECTS(Melt)
				OP_IMPL_THREAD_EFFECTS(Freeze)

				OP_IMPL_THREAD_EFFECTS(VarGet)
				OP_IMPL_THREAD_EFFECTS(VarSet)
				OP_IMPL_THREAD_EFFECTS(SetField)
				OP_IMPL_THREAD_EFFECTS(CallSet)

				OP_IMPL(NewOption)
				OP_IMPL(Length)
				OP_IMPL(NewArray)
				OP_IMPL(NewMutableArray)
				OP_IMPL(NewMutableArrayWithCapacity)
				OP_IMPL_THREAD_EFFECTS(ArrayAdd)
				OP_IMPL(InPlaceMakeImmutable)
				OP_IMPL(NewMap)
				OP_IMPL(MapKey)
				OP_IMPL(MapValue)
				OP_IMPL(NewClass)
				OP_IMPL(LoadField)
				OP_IMPL(LoadFieldFromSuper)
				OP_IMPL(UnifyField)

				BEGIN_OP_CASE(Err)
				{
					// If this is the stop interpreter sentry op, return.
					if (&Op == &StopInterpreterSentry)
					{
						return;
					}

					UE_LOG(LogVerseVM, Error, TEXT("Interpreted Err op"));
					return;
				}
				END_OP_CASE()

				BEGIN_OP_CASE(Move)
				{
					// TODO SOL-4459: This doesn't work with leniency and failure. For example,
					// if both Dest/Source are placeholders, failure will never be associated
					// to this Move, but that can't be right.
					DEF(Op.Dest, GetOperand(Op.Source));
				}
				END_OP_CASE()

				BEGIN_OP_CASE(Jump)
				{
					NextPC = Op.JumpOffset.GetLabeledPC();
				}
				END_OP_CASE()

				BEGIN_OP_CASE(JumpIfInitialized)
				{
					VValue Val = GetOperand(Op.Source);
					if (!Val.IsUninitialized())
					{
						NextPC = Op.JumpOffset.GetLabeledPC();
					}
				}
				END_OP_CASE();

				BEGIN_OP_CASE(Switch)
				{
					VValue Which = GetOperand(Op.Which);
					TArrayView<FLabelOffset> Offsets = GetConstants(Op, Op.JumpOffsets);
					NextPC = Offsets[Which.AsInt32()].GetLabeledPC();
				}
				END_OP_CASE()

				BEGIN_OP_CASE(BeginFailureContext)
				{
					Failure = &VFailureContext::New(Context, Task, Failure, *State.Frame, EffectToken.Get(Context), Op.OnFailure.GetLabeledPC());

					if (VValue IncomingEffectToken = EffectToken.Get(Context); IncomingEffectToken.IsPlaceholder())
					{
						BumpEffectEpoch();
						DoTransactionActionWhenEffectTokenIsConcrete<TransactAction::Start>(*Failure, *Task, IncomingEffectToken, EffectToken.Get(Context));
					}
					else
					{
						Failure->Transaction.Start(Context);
					}
				}
				END_OP_CASE()

				BEGIN_OP_CASE(EndFailureContext)
				{
					VFailureContext& FailureContext = *Failure;
					V_DIE_IF(FailureContext.bFailed);   // We shouldn't have failed and still made it here.
					V_DIE_UNLESS(FailureContext.Frame); // A null Frame indicates an artificial context from task resumption.

					FailureContext.bExecutedEndFailureContextOpcode = true;
					FailureContext.ThenPC = NextPC;
					FailureContext.DonePC = Op.Done.GetLabeledPC();

					if (FailureContext.SuspensionCount)
					{
						if (FailureContext.Parent)
						{
							++FailureContext.Parent->SuspensionCount;
						}
						FailureContext.BeforeThenEffectToken.Set(Context, EffectToken.Get(Context));
						EffectToken.Set(Context, FailureContext.DoneEffectToken.Get(Context));
						NextPC = Op.Done.GetLabeledPC();
						FailureContext.Frame.Set(Context, FailureContext.Frame->CloneWithoutCallerInfo(Context));
					}
					else
					{
						FailureContext.FinishedExecuting(Context);

						if (VValue IncomingEffectToken = EffectToken.Get(Context); IncomingEffectToken.IsPlaceholder())
						{
							BumpEffectEpoch();
							DoTransactionActionWhenEffectTokenIsConcrete<TransactAction::Commit>(FailureContext, *Task, IncomingEffectToken, EffectToken.Get(Context));
						}
						else
						{
							FailureContext.Transaction.Commit(Context);
						}
					}

					Failure = FailureContext.Parent.Get();
				}
				END_OP_CASE()

				BEGIN_OP_CASE(BeginTask)
				{
					V_DIE_UNLESS(Failure == OutermostFailureContext);

					VTask* Parent = Op.bAttached ? Task : nullptr;
					Task = &VTask::New(Context, Op.OnYield.GetLabeledPC(), State.Frame, Task, Parent);

					DEF(Op.Dest, *Task);
				}
				END_OP_CASE()

				BEGIN_OP_CASE(EndTask)
				{
					V_DIE_UNLESS(Task->bRunning);
					V_DIE_UNLESS(Failure == OutermostFailureContext);

					if (Task->Phase == VTask::EPhase::CancelRequested)
					{
						Task->Phase = VTask::EPhase::CancelStarted;
					}

					VValue Result;
					VTask* Awaiter;
					VTask* SignaledTask = nullptr;
					if (Task->Phase == VTask::EPhase::Active)
					{
						if (!Task->CancelChildren(Context))
						{
							VTask* Child = Task->LastChild.Get();
							Task->Park(Context, Child->LastCancel);

							V_DIE_IF(Task->NativeDefer);
							Task->NativeDefer = [Child](FAccessContext InContext, VTask* InTask) {
								AutoRTFM::Open([&] { InTask->Unpark(InContext, Child->LastCancel); });
							};

							NextPC = &Op;
							YIELD();
						}

						Result = GetOperand(Op.Value);
						Task->Result.Set(Context, Result);

						// Communicate the result to the parent task, if there is one.
						if (Op.Write.Index < FRegisterIndex::UNINITIALIZED)
						{
							if (State.Frame->Registers[Op.Write.Index].Get(Context).IsUninitialized())
							{
								State.Frame->Registers[Op.Write.Index].Set(Context, Result);
							}
						}
						if (Op.Signal.IsRegister())
						{
							VSemaphore& Semaphore = GetOperand(Op.Signal).StaticCast<VSemaphore>();
							Semaphore.Count += 1;

							if (Semaphore.Count == 0)
							{
								V_DIE_UNLESS(Semaphore.Await.Get());
								SignaledTask = Semaphore.Await.Get();
								Semaphore.Await.Reset();
							}
						}

						Awaiter = Task->LastAwait.Get();
						Task->LastAwait.Reset();
					}
					else
					{
						V_DIE_UNLESS(VTask::EPhase::CancelStarted <= Task->Phase && Task->Phase < VTask::EPhase::Canceled);

						if (!Task->CancelChildren(Context))
						{
							V_DIE_UNLESS(Task->Phase == VTask::EPhase::CancelStarted);

							NextPC = &Op;
							YIELD();
						}

						Task->Phase = VTask::EPhase::Canceled;
						Result = GlobalFalse();

						Awaiter = Task->LastCancel.Get();
						Task->LastCancel.Reset();

						if (VTask* Parent = Task->Parent.Get())
						{
							// A canceling parent is implicitly awaiting its last child.
							if (Parent->Phase == VTask::EPhase::CancelStarted && Parent->LastChild.Get() == Task)
							{
								SignaledTask = Parent;
							}
						}
					}

					Task->Suspend(Context);
					Task->Detach(Context);

					// This task may be resumed to run unblocked suspensions, but nothing remains to run after them.
					Task->ResumePC = &StopInterpreterSentry;
					Task->ResumeFrame.Set(Context, State.Frame);

					UpdateExecutionState(Task->YieldPC, Task->YieldFrame.Get());
					Task = Task->YieldTask.Get();

					auto ResumeAwaiter = [&](VTask* Awaiter) {
						Awaiter->YieldPC = NextPC;
						Awaiter->YieldFrame.Set(Context, State.Frame);
						Awaiter->YieldTask.Set(Context, Task);
						Awaiter->Resume(Context);

						UpdateExecutionState(Awaiter->ResumePC, Awaiter->ResumeFrame.Get());
						if (Task == nullptr)
						{
							OutermostTask = Awaiter;
						}
						Task = Awaiter;
					};

					// Resume any awaiting (or cancelling) tasks in the order they arrived.
					// The front of the list is the most recently-awaiting task, which should run last.
					if (SignaledTask && !SignaledTask->bRunning)
					{
						ResumeAwaiter(SignaledTask);
					}
					for (VTask* PrevTask; Awaiter != nullptr; Awaiter = PrevTask)
					{
						PrevTask = Awaiter->PrevTask.Get();

						// Normal resumption of a canceling task is a no-op.
						if (Awaiter->Phase != VTask::EPhase::Active)
						{
							continue;
						}

						ResumeAwaiter(Awaiter);
						if (Task->NativeDefer)
						{
							AutoRTFM::EContextStatus Status = AutoRTFM::Close([&] { Task->NativeDefer(Context, Task); });
							Task->NativeDefer.Reset();
							V_DIE_UNLESS(Status == AutoRTFM::EContextStatus::OnTrack);
						}
						if (!Def(Task->ResumeSlot, Result))
						{
							V_DIE("Failed unifying the result of `Await` or `Cancel`");
						}
					}

					// A resumed task may already have been re-suspended or canceled.
					if (Task == nullptr || !YieldIfNeeded(NextPC))
					{
						return;
					}
					NextPC = State.PC;
				}
				END_OP_CASE()

				BEGIN_OP_CASE(NewSemaphore)
				{
					VSemaphore& Semaphore = VSemaphore::New(Context);
					DEF(Op.Dest, Semaphore);
				}
				END_OP_CASE()

				BEGIN_OP_CASE(WaitSemaphore)
				{
					VSemaphore& Semaphore = GetOperand(Op.Source).StaticCast<VSemaphore>();
					Semaphore.Count -= Op.Count;

					if (Semaphore.Count < 0)
					{
						V_DIE_IF(Semaphore.Await.Get());
						Semaphore.Await.Set(Context, Task);
						YIELD();
					}
				}
				END_OP_CASE()

				// An indexed access (i.e. `B := A[10]`) is just the same as `Call(B, A, 10)`.
				BEGIN_OP_CASE(Call)
				{
					VValue Callee = GetOperand(Op.Callee);
					REQUIRE_CONCRETE(Callee);

					if (VFunction* Function = Callee.DynamicCast<VFunction>())
					{
						VRestValue* ReturnSlot = MakeReturnSlot(Op);
						TArrayView<FValueOperand> Arguments = GetOperands(Op.Arguments);
						VFrame& NewFrame = MakeFrameForCallee(
							Context, NextPC, State.Frame, ReturnSlot, *Function, Arguments.Num(), nullptr,
							[&](uint32 Arg) {
								return GetOperand(Arguments[Arg]);
							},
							[](uint32 NamedArg) -> VValue { VERSE_UNREACHABLE(); });
						UpdateExecutionState(Function->GetProcedure().GetOpsBegin(), &NewFrame);
					}
					else
					{
						OP_IMPL_HELPER(Call, Callee, Task, EffectToken.Get(Context));
					}
				}
				END_OP_CASE()

				BEGIN_OP_CASE(CallNamed)
				{
					VValue Callee = GetOperand(Op.Callee);
					REQUIRE_CONCRETE(Callee);

					if (VFunction* Function = Callee.DynamicCast<VFunction>())
					{
						VRestValue* ReturnSlot = &State.Frame->Registers[Op.Dest.Index];
						TArrayView<FValueOperand> Arguments = GetOperands(Op.Arguments);
						TArrayView<TWriteBarrier<VUniqueString>> NamedArguments = GetOperands(Op.NamedArguments);
						TArrayView<FValueOperand> NamedArgumentVals = GetOperands(Op.NamedArgumentVals);
						VFrame& NewFrame = MakeFrameForCallee(
							Context, NextPC, State.Frame, ReturnSlot, *Function, Arguments.Num(), &NamedArguments,
							[&](uint32 Arg, uint32* NamedArg = nullptr) {
								return GetOperand(Arguments[Arg]);
							},
							[&](uint32 NamedArg) {
								return GetOperand(NamedArgumentVals[NamedArg]);
							});
						UpdateExecutionState(Function->GetProcedure().GetOpsBegin(), &NewFrame);
					}
					else
					{
						OP_IMPL_HELPER(Call, Callee, Task, EffectToken.Get(Context));
					}
				}
				END_OP_CASE();

				BEGIN_OP_CASE(Return)
				{
					// TODO SOL-4461: Return should work with lenient execution of failure contexts.
					// We can't just logically execute the first Return we encounter during lenient
					// execution if the then/else when executed would've returned.
					//
					// We also need to figure out how to properly pop a frame off if the
					// failure context we're leniently executing returns. We could continue
					// to execute the current frame and just not thread through the effect
					// token, so no effects could happen. But that's inefficient.

					VValue IncomingEffectToken = EffectToken.Get(Context);
					DEF(State.Frame->ReturnSlot.EffectToken, IncomingEffectToken); // This can't fail.

					VValue Value = GetOperand(Op.Value);
					VFrame& Frame = *State.Frame;

					ReturnTo(Frame.CallerPC, Frame.CallerFrame.Get());

					// TODO: Add a test where this unification fails at the top level with no return continuation.
					DEF(Frame.ReturnSlot, Value);
				}
				END_OP_CASE()

				BEGIN_OP_CASE(ResumeUnwind)
				{
					BeginUnwind(NextPC);
					NextPC = State.PC;
				}
				END_OP_CASE()

				BEGIN_OP_CASE(NewObject)
				{
					VValue ClassOperand = GetOperand(Op.Class);
					REQUIRE_CONCRETE(ClassOperand);
					VClass& Class = ClassOperand.StaticCast<VClass>();

					VValue Object;
					TArray<VFunction*> Initializers;
					OP_IMPL_HELPER(NewObject, Class, Object, Initializers);

					// Push initializers onto the stack in reverse order to run them in forward order.
					while (Initializers.Num() > 0)
					{
						VFunction* Function = Initializers.Pop();
						Function = &Function->Bind(Context, Object);
						VRestValue* ReturnSlot = nullptr;
						VFrame& NewFrame = MakeFrameForCallee(
							Context, NextPC, State.Frame, ReturnSlot, *Function, 0, nullptr,
							[](uint32 Arg) -> VValue { VERSE_UNREACHABLE(); },
							[](uint32 NamedArg) -> VValue { VERSE_UNREACHABLE(); });
						UpdateExecutionState(Function->Procedure.Get()->GetOpsBegin(), &NewFrame);
					}
				}
				END_OP_CASE()

				BEGIN_OP_CASE(Reset)
				{
					State.Frame->Registers[Op.Dest.Index].Reset(0);
				}
				END_OP_CASE()

				BEGIN_OP_CASE(NewVar)
				{
					DEF(Op.Dest, VVar::New(Context));
				}
				END_OP_CASE()

				default:
					V_DIE("Invalid opcode: %u", static_cast<FOpcodeInt>(State.PC->Opcode));
			}

			if (CurrentSuspension)
			{
				goto SuspensionInterpreterLoop;
			}
		}

#undef OP_IMPL_THREAD_EFFECTS
#undef BEGIN_OP_CASE
#undef END_OP_CASE
#undef ENQUEUE_SUSPENSION
#undef FAIL
#undef YIELD

		// Macro definitions to be used in the suspension interpreter loop.

#define OP_IMPL_THREAD_EFFECTS(OpName)                   \
	BEGIN_OP_CASE(OpName)                                \
	{                                                    \
		OP_IMPL_HELPER(OpName)                           \
		DEF(Op.EffectToken, VValue::EffectDoneMarker()); \
	}                                                    \
	END_OP_CASE()

#define BEGIN_OP_CASE(Name)                                                                              \
	case EOpcode::Name:                                                                                  \
	{                                                                                                    \
		F##Name##SuspensionCaptures& Op = BytecodeSuspension.GetCaptures<F##Name##SuspensionCaptures>(); \
		if constexpr (bPrintTrace)                                                                       \
		{                                                                                                \
			BeginTrace(Op, BytecodeSuspension);                                                          \
		}

#define END_OP_CASE()                                                  \
	FinishedExecutingSuspensionIn(*BytecodeSuspension.FailureContext); \
	if constexpr (bPrintTrace)                                         \
	{                                                                  \
		EndTraceWithCaptures(Op, false, false);                        \
	}                                                                  \
	break;                                                             \
	}

#define ENQUEUE_SUSPENSION(Value)                         \
	Value.EnqueueSuspension(Context, *CurrentSuspension); \
	if constexpr (bPrintTrace)                            \
	{                                                     \
		EndTraceWithCaptures(Op, true, false);            \
	}                                                     \
	break

#define FAIL()                                 \
	if constexpr (bPrintTrace)                 \
	{                                          \
		EndTraceWithCaptures(Op, false, true); \
	}                                          \
	Fail(*BytecodeSuspension.FailureContext);  \
	break

#define YIELD()                                                                                \
	FinishedExecutingSuspensionIn(*BytecodeSuspension.FailureContext);                         \
	if constexpr (bPrintTrace)                                                                 \
	{                                                                                          \
		EndTraceWithCaptures(Op, false, false);                                                \
	}                                                                                          \
	Suspend(*BytecodeSuspension.FailureContext, *BytecodeSuspension.Task, MakeReturnSlot(Op)); \
	break

	SuspensionInterpreterLoop:
		do
		{
			check(!!CurrentSuspension);
			if (!CurrentSuspension->FailureContext->bFailed)
			{
				if (VLambdaSuspension* LambdaSuspension = CurrentSuspension->DynamicCast<VLambdaSuspension>())
				{
					LambdaSuspension->Callback(Context, *LambdaSuspension, CurrentSuspension);
				}
				else
				{
					VBytecodeSuspension& BytecodeSuspension = CurrentSuspension->StaticCast<VBytecodeSuspension>();

					switch (BytecodeSuspension.PC->Opcode)
					{
						OP_IMPL(Add)
						OP_IMPL(Sub)
						OP_IMPL(Mul)
						OP_IMPL(Div)
						OP_IMPL(Mod)
						OP_IMPL(Neg)

						OP_IMPL(MutableAdd)

						OP_IMPL(Neq)
						OP_IMPL(Lt)
						OP_IMPL(Lte)
						OP_IMPL(Gt)
						OP_IMPL(Gte)

						OP_IMPL(Query)

						OP_IMPL_THREAD_EFFECTS(Melt)
						OP_IMPL_THREAD_EFFECTS(Freeze)

						OP_IMPL_THREAD_EFFECTS(VarGet)
						OP_IMPL_THREAD_EFFECTS(VarSet)
						OP_IMPL_THREAD_EFFECTS(SetField)
						OP_IMPL_THREAD_EFFECTS(CallSet)

						OP_IMPL(Length)
						OP_IMPL(NewMutableArrayWithCapacity)
						OP_IMPL_THREAD_EFFECTS(ArrayAdd)
						OP_IMPL(InPlaceMakeImmutable)
						OP_IMPL(MapKey)
						OP_IMPL(MapValue)
						OP_IMPL(NewClass)
						OP_IMPL(LoadField)
						OP_IMPL(LoadFieldFromSuper)
						OP_IMPL(UnifyField)

						// An indexed access (i.e. `B := A[10]`) is just the same as `Call(B, A, 10)`.
						BEGIN_OP_CASE(Call)
						{
							VValue Callee = GetOperand(Op.Callee);
							REQUIRE_CONCRETE(Callee);

							if (VFunction* Function = Callee.DynamicCast<VFunction>())
							{
								FOp* CallerPC = nullptr;
								VFrame* CallerFrame = nullptr;

								VValue ReturnSlot = MakeReturnSlot(Op);
								TArrayView<TWriteBarrier<VValue>> Arguments = GetOperands(Op.Arguments);
								VFrame& NewFrame = MakeFrameForCallee(
									Context, CallerPC, CallerFrame, ReturnSlot, *Function, Arguments.Num(), nullptr,
									[&](uint32 Arg, uint32* NamedArg = nullptr) {
										return GetOperand(Arguments[Arg]);
									},
									[](uint32 NamedArg) -> VValue { VERSE_UNREACHABLE(); });
								NewFrame.ReturnSlot.EffectToken.Set(Context, GetOperand(Op.ReturnEffectToken));
								// TODO SOL-4435: Enact some recursion limit here since we're using the machine stack.
								VFailureContext& FailureContext = *CurrentSuspension->FailureContext;
								VTask& TaskContext = *CurrentSuspension->Task;

								FInterpreter Interpreter(
									Context,
									FExecutionState(Function->GetProcedure().GetOpsBegin(), &NewFrame),
									&FailureContext,
									Task,
									GetOperand(Op.EffectToken));
								Interpreter.Execute();
							}
							else
							{
								FOpResult Result = CallImpl(Op, Callee, CurrentSuspension->Task.Get(), GetOperand(Op.EffectToken));
								switch (Result.Kind)
								{
									case FOpResult::Return:
									case FOpResult::Yield:
										DEF(Op.ReturnEffectToken, GetOperand(Op.EffectToken));
										break;

									case FOpResult::Block:
									case FOpResult::Fail:
									case FOpResult::Error:
										break;
								}
								OP_RESULT_HELPER(Result);
							}
						}
						END_OP_CASE()

						BEGIN_OP_CASE(CallNamed)
						{
							VValue Callee = GetOperand(Op.Callee);
							REQUIRE_CONCRETE(Callee);
							if (VFunction* Function = Callee.DynamicCast<VFunction>())
							{
								FOp* CallerPC = nullptr;
								VFrame* CallerFrame = nullptr;
								VValue ReturnSlot = MakeReturnSlot(Op);
								TArrayView<TWriteBarrier<VValue>> Arguments = GetOperands(Op.Arguments);
								TArrayView<TWriteBarrier<VUniqueString>> NamedArguments(Op.NamedArguments);
								TArrayView<TWriteBarrier<VValue>> NamedArgumentVals = GetOperands(Op.NamedArgumentVals);
								VFrame& NewFrame = MakeFrameForCallee(
									Context, CallerPC, CallerFrame, ReturnSlot, *Function, Arguments.Num(), &NamedArguments,
									[&](uint32 Arg) {
										return GetOperand(Arguments[Arg]);
									},
									[&](uint32 NamedArg) {
										return GetOperand(NamedArgumentVals[NamedArg]);
									});
								NewFrame.ReturnSlot.EffectToken.Set(Context, GetOperand(Op.ReturnEffectToken));
								// TODO SOL-4435: Enact some recursion limit here since we're using the machine stack.
								VFailureContext& FailureContext = *CurrentSuspension->FailureContext;
								VTask& TaskContext = *CurrentSuspension->Task;
								FInterpreter Interpreter(
									Context,
									FExecutionState(Function->GetProcedure().GetOpsBegin(), &NewFrame),
									&FailureContext,
									Task,
									GetOperand(Op.EffectToken));
								Interpreter.Execute();
							}
							else
							{
								OP_IMPL_HELPER(Call, Callee, CurrentSuspension->Task.Get(), GetOperand(Op.EffectToken));
								DEF(Op.ReturnEffectToken, GetOperand(Op.EffectToken));
							}
						}
						END_OP_CASE()

						BEGIN_OP_CASE(NewObject)
						{
							V_DIE("Unblocked NewObject is unimplemented");
						}
						END_OP_CASE()

						default:
							V_DIE("Invalid opcode: %u", static_cast<FOpcodeInt>(State.PC->Opcode));
					}
				}
			}

			VSuspension* NextSuspension = CurrentSuspension->Next.Get();
			CurrentSuspension->Next.Set(Context, nullptr);
			CurrentSuspension = NextSuspension;
		}
		while (CurrentSuspension);

		if (!UnwindIfNeeded())
		{
			return;
		}
		if (!YieldIfNeeded(State.PC))
		{
			return;
		}

		goto MainInterpreterLoop;

#undef OP_IMPL_THREAD_EFFECTS
#undef BEGIN_OP_CASE
#undef END_OP_CASE
#undef ENQUEUE_SUSPENSION
#undef FAIL
	}

#undef OP_RESULT_HELPER

public:
	FInterpreter(FRunningContext Context, FExecutionState State, VFailureContext* FailureContext, VTask* Task, VValue IncomingEffectToken, FOp* StartPC = nullptr, FOp* EndPC = nullptr)
		: Context(Context)
		, State(State)
		, Failure(FailureContext)
		, Task(Task)
		, OutermostFailureContext(FailureContext)
		, OutermostTask(Task)
		, OutermostStartPC(StartPC)
		, OutermostEndPC(EndPC)
	{
		V_DIE_UNLESS(OutermostFailureContext);
		V_DIE_UNLESS(!!OutermostStartPC == !!OutermostEndPC);
		EffectToken.Set(Context, IncomingEffectToken);
	}

	void Execute()
	{
		if (CVarTraceExecution.GetValueOnAnyThread())
		{
			if (OutermostStartPC)
			{
				ExecuteImpl<true>(true);
			}
			else
			{
				ExecuteImpl<true>(false);
			}
		}
		else
		{
			if (OutermostStartPC)
			{
				ExecuteImpl<false>(true);
			}
			else
			{
				ExecuteImpl<false>(false);
			}
		}
	}

	// Upon failure, returns an uninitialized VValue
	static FOpResult Invoke(FRunningContext Context, VFunction::Args&& IncomingArguments, TArray<TWriteBarrier<VUniqueString>>* NamedArgs, VFunction::Args* NamedArgVals, VFunction& Function)
	{
		// This function expects to be run in the open
		check(!AutoRTFM::IsClosed());

		VRestValue ReturnSlot(0);

		VFunction::Args Arguments = MoveTemp(IncomingArguments);

		FOp* CallerPC = &StopInterpreterSentry;
		VFrame* CallerFrame = nullptr;
		TArrayView<TWriteBarrier<VUniqueString>> NamedArgsViewStorage;
		TArrayView<TWriteBarrier<VUniqueString>>* NamedArgsView = nullptr;
		if (NamedArgs)
		{
			NamedArgsViewStorage = *NamedArgs;
			NamedArgsView = &NamedArgsViewStorage;
		}
		VFrame& Frame = MakeFrameForCallee(
			Context, CallerPC, CallerFrame, &ReturnSlot, Function, Arguments.Num(), NamedArgsView,
			[&](uint32 Arg) {
				return Arguments[Arg];
			},
			[&](uint32 NamedArg) {
				return (*NamedArgVals)[NamedArg];
			});

		// Check if we're inside native C++ code that was invoked by Verse
		const FNativeContext& NativeContext = Context.NativeContext();
		V_DIE_UNLESS(NativeContext.IsValid());

		FInterpreter Interpreter(
			Context,
			FExecutionState(Function.GetProcedure().GetOpsBegin(), &Frame),
			NativeContext.FailureContext,
			NativeContext.Task,
			VValue::EffectDoneMarker());

		Interpreter.Execute();

		if (CVarTraceExecution.GetValueOnAnyThread())
		{
			UE_LOG(LogVerseVM, Display, TEXT("\n"));
		}

		return NativeContext.FailureContext->bFailed ? FOpResult(FOpResult::Fail) : FOpResult(FOpResult::Return, ReturnSlot.Get(Context));
	}

	static void ResumeInTransaction(FRunningContext Context, VValue ResumeArgument, VTask& Task)
	{
		// Normal resumption of a canceled task is a no-op.
		if (Task.Phase != VTask::EPhase::Active)
		{
			return;
		}

		if (CVarTraceExecution.GetValueOnAnyThread())
		{
			UE_LOG(LogVerseVM, Display, TEXT(""));
			UE_LOG(LogVerseVM, Display, TEXT("Resuming:"));
		}

		VFailureContext& FailureContext = VFailureContext::New(
			Context,
			/*Task*/ nullptr,
			/*Parent*/ nullptr,
			*Task.YieldFrame,
			VValue(),
			&StopInterpreterSentry);
		Task.Resume(Context);

		FInterpreter Interpreter(
			Context,
			FExecutionState(Task.ResumePC, Task.ResumeFrame.Get()),
			&FailureContext,
			&Task,
			VValue::EffectDoneMarker());
		AutoRTFM::TransactThenOpen([&] {
			FailureContext.Transaction.Start(Context);

			if (Task.NativeDefer)
			{
				AutoRTFM::EContextStatus Status = AutoRTFM::Close([&] { Task.NativeDefer(Context, &Task); });
				Task.NativeDefer.Reset();
				V_DIE_UNLESS(Status == AutoRTFM::EContextStatus::OnTrack);
			}

			bool bExecute = true;
			if (!FInterpreter::Def(Context, Task.ResumeSlot, ResumeArgument, Interpreter.CurrentSuspension))
			{
				Interpreter.Fail(*Interpreter.Failure);
				bExecute = Interpreter.UnwindIfNeeded();
			}

			if (bExecute)
			{
				Interpreter.Execute();
			}

			V_DIE_IF(FailureContext.bFailed || FailureContext.Transaction.bHasAborted);
			FailureContext.Transaction.Commit(Context);
		});
	}

	static void UnwindInTransaction(FRunningContext Context, VTask& Task)
	{
		V_DIE_UNLESS(Task.Phase == VTask::EPhase::CancelStarted && !Task.LastChild);

		if (CVarTraceExecution.GetValueOnAnyThread())
		{
			UE_LOG(LogVerseVM, Display, TEXT(""));
			UE_LOG(LogVerseVM, Display, TEXT("Unwinding:"));
		}

		VFailureContext& FailureContext = VFailureContext::New(
			Context,
			/*Task*/ nullptr,
			/*Parent*/ nullptr,
			*Task.YieldFrame,
			VValue(), // IncomingEffectToken doesn't matter here, since we bail out if we fail at the top level.
			&StopInterpreterSentry);
		Task.Resume(Context);

		FInterpreter Interpreter(
			Context,
			FExecutionState(Task.ResumePC, Task.ResumeFrame.Get()),
			&FailureContext,
			&Task,
			VValue::EffectDoneMarker());
		AutoRTFM::TransactThenOpen([&] {
			FailureContext.Transaction.Start(Context);

			Interpreter.BeginUnwind(Interpreter.State.PC);
			Interpreter.Execute();

			V_DIE_IF(FailureContext.bFailed || FailureContext.Transaction.bHasAborted);
			FailureContext.Transaction.Commit(Context);
		});
	}
};

FOpResult VFunction::Invoke(FRunningContext Context, Args&& Arguments, TArray<TWriteBarrier<VUniqueString>>* NamedArgs, Args* NamedArgVals)
{
	FOpResult Result = FInterpreter::Invoke(Context, MoveTemp(Arguments), NamedArgs, NamedArgVals, *this);
	check(Result.Kind != FOpResult::Return || !Result.Value.IsPlaceholder());
	return Result;
}

FOpResult VFunction::Invoke(FRunningContext Context, VValue Argument, TWriteBarrier<VUniqueString>* NamedArg)
{
	if (NamedArg)
	{
		TArray<TWriteBarrier<VUniqueString>> NamedArgs{*NamedArg};
		Args NamedArgVals{Argument};
		FOpResult Result = FInterpreter::Invoke(Context, VFunction::Args{Argument}, &NamedArgs, &NamedArgVals, *this);
		return Result;
	}
	FOpResult Result = FInterpreter::Invoke(Context, VFunction::Args{Argument}, nullptr, nullptr, *this);
	check(Result.Kind != FOpResult::Return || !Result.Value.IsPlaceholder());
	return Result;
}

void VTask::ResumeInTransaction(FRunningContext Context, VValue ResumeArgument)
{
	FInterpreter::ResumeInTransaction(Context, ResumeArgument, *this);
}

void VTask::UnwindInTransaction(FRunningContext Context)
{
	FInterpreter::UnwindInTransaction(Context, *this);
}

} // namespace Verse
#endif // WITH_VERSE_VM || defined(__INTELLISENSE__)
