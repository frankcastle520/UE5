// Copyright Epic Games, Inc. All Rights Reserved.

#include "AutoRTFM/AutoRTFM.h"
#include "CoreGlobals.h"
#include "HAL/IConsoleManager.h"
#include "GenericPlatform/GenericPlatformCrashContext.h"

#if UE_AUTORTFM
static_assert(UE_AUTORTFM_ENABLED, "AutoRTFM/API.cpp requires the compiler flag '-fautortfm'");
#endif

namespace
{
	// Move this to a local only and use functions to access this
#if UE_AUTORTFM_ENABLED_RUNTIME_BY_DEFAULT
	int GAutoRTFMRuntimeEnabled = AutoRTFM::ForTheRuntime::EAutoRTFMEnabledState::AutoRTFM_Enabled;
#else
	int GAutoRTFMRuntimeEnabled = AutoRTFM::ForTheRuntime::EAutoRTFMEnabledState::AutoRTFM_Disabled;
#endif // UE_AUTORTFM_ENABLED_RUNTIME_BY_DEFAULT

	void UpdateAutoRTFMRuntimeCrashData()
	{
		FGenericCrashContext::SetGameData(TEXT("IsAutoRTFMRuntimeEnabled"), AutoRTFM::ForTheRuntime::IsAutoRTFMRuntimeEnabled() ? TEXT("true") : TEXT("false"));
	}

	bool GAutoRTFMEnsureOnAbortByLanguage = true;

	int GAutoRTFMRetryTransactions = AutoRTFM::ForTheRuntime::EAutoRTFMRetryTransactionState::NoRetry;

	void UpdateAutoRTFMRetryTransactionsData()
	{
		switch (GAutoRTFMRetryTransactions)
		{
		case AutoRTFM::ForTheRuntime::EAutoRTFMRetryTransactionState::NoRetry:
			return FGenericCrashContext::SetGameData(TEXT("AutoRTFMRetryTransactionState"), TEXT("NoRetry"));
		case AutoRTFM::ForTheRuntime::EAutoRTFMRetryTransactionState::RetryNonNested:
			return FGenericCrashContext::SetGameData(TEXT("AutoRTFMRetryTransactionState"), TEXT("RetryNonNested"));
		case AutoRTFM::ForTheRuntime::EAutoRTFMRetryTransactionState::RetryNestedToo:
			return FGenericCrashContext::SetGameData(TEXT("AutoRTFMRetryTransactionState"), TEXT("RetryNestedToo"));
		}
	}
}

#if UE_AUTORTFM
static FAutoConsoleVariableRef CVarAutoRTFMRuntimeEnabled(
	TEXT("AutoRTFMRuntimeEnabled"),
	GAutoRTFMRuntimeEnabled,
	TEXT("Enables the AutoRTFM runtime"),
	FConsoleVariableDelegate::CreateLambda([] (IConsoleVariable*) { UpdateAutoRTFMRuntimeCrashData(); }),
	ECVF_Default
);

static FAutoConsoleVariableRef CVarAutoRTFMRetryTransactions(
	TEXT("AutoRTFMRetryTransactions"),
	GAutoRTFMRetryTransactions,
	TEXT("Enables the AutoRTFM sanitizer-like mode where we can force an abort-and-retry on transactions (useful to test abort codepaths work as intended)"),
	FConsoleVariableDelegate::CreateLambda([](IConsoleVariable*) { UpdateAutoRTFMRetryTransactionsData(); }),
	ECVF_Default
);

static FDelayedAutoRegisterHelper DelayedAutoRegister(EDelayedRegisterRunPhase::EndOfEngineInit, []
	{
		UpdateAutoRTFMRuntimeCrashData();
		UpdateAutoRTFMRetryTransactionsData();
	});
#endif

namespace AutoRTFM
{
	namespace ForTheRuntime
	{
		bool SetAutoRTFMRuntime(EAutoRTFMEnabledState State)
		{
			// #noop if AutoRTFM is not compiled in
#if UE_AUTORTFM
			switch (GAutoRTFMRuntimeEnabled)
			{
			default:
				break;
			case EAutoRTFMEnabledState::AutoRTFM_ForcedDisabled:
				UE_LOG(LogCore, Log, TEXT("Ignoring changing AutoRTFM runtime state due to GAutoRTFMRuntimeEnabled being set to forced disabled."));
				return false;
			case EAutoRTFMEnabledState::AutoRTFM_ForcedEnabled:
				UE_LOG(LogCore, Log, TEXT("Ignoring changing AutoRTFM runtime state due to GAutoRTFMRuntimeEnabled being set to forced enabled."));
				return false;
			}

			GAutoRTFMRuntimeEnabled = State;

			UpdateAutoRTFMRuntimeCrashData();

			return true;
#else
			return false;
#endif
		}

		bool IsAutoRTFMRuntimeEnabled()
		{
			// #noop if AutoRTFM is not compiled in
#if UE_AUTORTFM
			switch (GAutoRTFMRuntimeEnabled)
			{
			default:
				return false;
			case EAutoRTFMEnabledState::AutoRTFM_Enabled:
			case EAutoRTFMEnabledState::AutoRTFM_ForcedEnabled:
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
			case EAutoRTFMEnabledState::AutoRTFM_EnabledForAllVerse:
#pragma clang diagnostic pop
				return true;
			}
#else
			return false;
#endif
		}

		bool IsAutoRTFMRuntimeEnabledForAllVerse()
		{
			return IsAutoRTFMRuntimeEnabled();
		}

		void SetEnsureOnAbortByLanguage(bool bEnabled)
		{
#if UE_AUTORTFM
			GAutoRTFMEnsureOnAbortByLanguage = bEnabled;
#endif
		}

		bool IsEnsureOnAbortByLanguageEnabled()
		{
#if UE_AUTORTFM
			return GAutoRTFMEnsureOnAbortByLanguage;
#else
			return false;
#endif
		}

		void SetRetryTransaction(EAutoRTFMRetryTransactionState State)
		{
#if UE_AUTORTFM
			GAutoRTFMRetryTransactions = State;
			UpdateAutoRTFMRetryTransactionsData();
#endif
		}

		EAutoRTFMRetryTransactionState GetRetryTransaction()
		{
#if UE_AUTORTFM
			return static_cast<EAutoRTFMRetryTransactionState>(GAutoRTFMRetryTransactions);
#else
			return NoRetry;
#endif
		}

		bool ShouldRetryNonNestedTransactions()
		{
#if UE_AUTORTFM
			switch (GAutoRTFMRetryTransactions)
			{
			default:
				return false;
			case EAutoRTFMRetryTransactionState::RetryNonNested:
			case EAutoRTFMRetryTransactionState::RetryNestedToo:
				return true;
			}
#else
			return false;
#endif
		}

		bool ShouldRetryNestedTransactionsToo()
		{
#if UE_AUTORTFM
			switch (GAutoRTFMRetryTransactions)
			{
			default:
				return false;
			case EAutoRTFMRetryTransactionState::RetryNestedToo:
				return true;
			}
#else
			return false;
#endif
		}
	}
}

#if (defined(__AUTORTFM_ENABLED) && __AUTORTFM_ENABLED)
#include "AutoRTFM/AutoRTFMConstants.h"
#include "CallNest.h"
#include "Context.h"
#include "ContextInlines.h"
#include "ContextStatus.h"
#include "FunctionMapInlines.h"
#include "TransactionInlines.h"
#include "Toggles.h"
#include "Utils.h"

#include "Templates/Tuple.h"

// This is the implementation of the AutoRTFM.h API. Ideally, functions here should just delegate to some internal API.
// For now, I have these functions also perform some error checking.

namespace AutoRTFM
{

namespace
{

// Internal closed-variant implementations.
UE_AUTORTFM_ALWAYS_OPEN bool RTFM_autortfm_is_transactional()
{
	return true;
}

UE_AUTORTFM_ALWAYS_OPEN autortfm_result RTFM_autortfm_transact(void (*UninstrumentedWork)(void*), void (*InstrumentedWork)(void*), void* Arg)
{
	FContext* Context = FContext::Get();
	return static_cast<autortfm_result>(Context->Transact(InstrumentedWork, Arg));
}

UE_AUTORTFM_FORCEINLINE autortfm_result TransactThenOpenImpl(void (*UninstrumentedWork)(void*), void* Arg)
{
	return static_cast<autortfm_result>(
		AutoRTFM::Transact([&]
		{
			autortfm_open(UninstrumentedWork, Arg);
		}));
}

UE_AUTORTFM_ALWAYS_OPEN autortfm_result RTFM_autortfm_transact_then_open(void (*UninstrumentedWork)(void*), void (*InstrumentedWork)(void*), void* Arg)
{
	return TransactThenOpenImpl(UninstrumentedWork, Arg);
}

UE_AUTORTFM_ALWAYS_OPEN void RTFM_autortfm_commit(void (*UninstrumentedWork)(void*), void (*InstrumentedWork)(void*), void* Arg)
{
	autortfm_result Result = autortfm_transact(UninstrumentedWork, InstrumentedWork, Arg);
	UE_CLOG(Result != autortfm_committed, LogAutoRTFM, Fatal, TEXT("Unexpected transaction result: %u."), Result);
}

UE_AUTORTFM_ALWAYS_OPEN void RTFM_autortfm_abort()
{
	FContext* Context = FContext::Get();
	Context->AbortByRequestAndThrow();
}

UE_AUTORTFM_ALWAYS_OPEN void RTFM_autortfm_start_transaction()
{
	UE_LOG(LogAutoRTFM, Fatal, TEXT("The function `autortfm_start_transaction` was called from closed code."));
}

UE_AUTORTFM_ALWAYS_OPEN void RTFM_autortfm_commit_transaction()
{
	UE_LOG(LogAutoRTFM, Fatal, TEXT("The function `RTFM_autortfm_commit_transaction` was called from closed code."));
}

UE_AUTORTFM_ALWAYS_OPEN autortfm_result RTFM_autortfm_abort_transaction()
{
	FContext* const Context = FContext::Get();
	return static_cast<autortfm_result>(Context->AbortTransaction(/* bIsClosed */ true, /* bIsCascading */ false));
}

UE_AUTORTFM_ALWAYS_OPEN autortfm_result RTFM_autortfm_cascading_abort_transaction()
{
	FContext* const Context = FContext::Get();
	return static_cast<autortfm_result>(Context->AbortTransaction(/* bIsClosed */ true, /* bIsCascading */ true));
}

UE_AUTORTFM_ALWAYS_OPEN void RTFM_autortfm_clear_transaction_status()
{
	UE_LOG(LogAutoRTFM, Fatal, TEXT("The function `autortfm_clear_transaction_status` was called from closed code."));
	AutoRTFM::Unreachable();
}

UE_AUTORTFM_ALWAYS_OPEN void RTFM_autortfm_open(void (*Work)(void*), void* Arg)
{
	Work(Arg);

	FContext* Context = FContext::Get();
	if (Context->IsAborting())
	{
		Context->Throw();
	}
}

UE_AUTORTFM_ALWAYS_OPEN autortfm_status RTFM_autortfm_close(void (*UninstrumentedWork)(void*), void (*InstrumentedWork)(void*), void* Arg)
{
	FContext* const Context = FContext::Get();

	if (InstrumentedWork)
	{
		InstrumentedWork(Arg);
	}
	else
	{
		ensureMsgf(!ForTheRuntime::IsEnsureOnAbortByLanguageEnabled(), TEXT("Could not find function %p '%s' where '%s'."), UninstrumentedWork, *GetFunctionDescription(UninstrumentedWork), ANSI_TO_TCHAR("autortfm_close"));
		Context->AbortByLanguageAndThrow();
	}

	return static_cast<autortfm_status>(Context->GetStatus());
}

UE_AUTORTFM_ALWAYS_OPEN void RTFM_autortfm_record_open_write(void*, size_t)
{
	UE_LOG(LogAutoRTFM, Fatal, TEXT("The function `autortfm_record_open_write` was called from closed code."));
}

UE_AUTORTFM_ALWAYS_OPEN void RTFM_OnCommitInternal(TFunction<void()> && Work)
{
	FContext* Context = FContext::Get();
	ASSERT(Context->GetStatus() == EContextStatus::OnTrack);
	Context->GetCurrentTransaction()->DeferUntilCommit(MoveTemp(Work));
}

UE_AUTORTFM_ALWAYS_OPEN void RTFM_OnAbortInternal(TFunction<void()>&& Work)
{
	FContext* Context = FContext::Get();
	ASSERT(Context->GetStatus() == EContextStatus::OnTrack);
	Context->GetCurrentTransaction()->DeferUntilAbort(MoveTemp(Work));
}

UE_AUTORTFM_ALWAYS_OPEN void RTFM_PushOnAbortHandlerInternal(const void* Key, TFunction<void()>&& Work)
{
	FContext* Context = FContext::Get();
	ASSERT(Context->GetStatus() == EContextStatus::OnTrack);
	Context->GetCurrentTransaction()->PushDeferUntilAbortHandler(Key, MoveTemp(Work));
}

UE_AUTORTFM_ALWAYS_OPEN void RTFM_PopOnAbortHandlerInternal(const void* Key)
{
	FContext* Context = FContext::Get();
	ASSERT(Context->GetStatus() == EContextStatus::OnTrack);
	Context->GetCurrentTransaction()->PopDeferUntilAbortHandler(Key);
}

UE_AUTORTFM_ALWAYS_OPEN void RTFM_PopAllOnAbortHandlersInternal(const void* Key)
{
	FContext* Context = FContext::Get();
	ASSERT(Context->GetStatus() == EContextStatus::OnTrack);
	Context->GetCurrentTransaction()->PopAllDeferUntilAbortHandlers(Key);
}

UE_AUTORTFM_ALWAYS_OPEN void RTFM_autortfm_on_commit(void (*Work)(void*), void* Arg)
{
	RTFM_OnCommitInternal([Work, Arg] { Work(Arg); });
}

UE_AUTORTFM_ALWAYS_OPEN void RTFM_autortfm_on_abort(void (*Work)(void*), void* Arg)
{
	RTFM_OnAbortInternal([Work, Arg] { Work(Arg); });
}

UE_AUTORTFM_ALWAYS_OPEN void RTFM_autortfm_push_on_abort_handler(const void* Key, void (*Work)(void*), void* Arg)
{
	RTFM_PushOnAbortHandlerInternal(Key, [Work, Arg] { Work(Arg); });
}

UE_AUTORTFM_ALWAYS_OPEN void RTFM_autortfm_pop_on_abort_handler(const void* Key)
{
	RTFM_PopOnAbortHandlerInternal(Key);
}

UE_AUTORTFM_ALWAYS_OPEN void* RTFM_autortfm_did_allocate(void* Ptr, size_t Size)
{
	FContext* Context = FContext::Get();
	Context->DidAllocate(Ptr, Size);
	return Ptr;
}

UE_AUTORTFM_ALWAYS_OPEN void RTFM_autortfm_did_free(void* Ptr)
{
	// We should never-ever-ever actually free memory from within closed code of
	// a transaction.
	AutoRTFM::Unreachable();
}

UE_AUTORTFM_ALWAYS_OPEN void RTFM_autortfm_check_consistency_assuming_no_races()
{
}

}  // anonymous namespace

// The AutoRTFM public API.
// Each function will be forked by the compiler into an open and closed variant.
// autortfm_is_closed() is used to branch to the closed variants declared above.
extern "C" bool autortfm_is_transactional()
{
	if (autortfm_is_closed())
	{
		return RTFM_autortfm_is_transactional();
	}

	if (ForTheRuntime::IsAutoRTFMRuntimeEnabled())
	{
		return FContext::IsTransactional();
	}

	return false;
}

extern "C" bool autortfm_is_committing_or_aborting()
{
	if (ForTheRuntime::IsAutoRTFMRuntimeEnabled())
	{
		return FContext::IsCommittingOrAborting();
	}

	return false;
}

extern "C" autortfm_result autortfm_transact(void (*UninstrumentedWork)(void*), void (*InstrumentedWork)(void*), void* Arg)
{
	if (autortfm_is_closed())
	{
		return RTFM_autortfm_transact(UninstrumentedWork, InstrumentedWork, Arg);
	}

	if (ForTheRuntime::IsAutoRTFMRuntimeEnabled())
	{
	    return static_cast<autortfm_result>(FContext::Get()->Transact(InstrumentedWork, Arg));
	}

	(*UninstrumentedWork)(Arg);
	return autortfm_committed;
}

extern "C" autortfm_result autortfm_transact_then_open(void (*UninstrumentedWork)(void*), void (*InstrumentedWork)(void*), void* Arg)
{
	if (autortfm_is_closed())
	{
		return RTFM_autortfm_transact_then_open(UninstrumentedWork, InstrumentedWork, Arg);
	}

	return TransactThenOpenImpl(UninstrumentedWork, Arg);
}

extern "C" void autortfm_commit(void (*UninstrumentedWork)(void*), void (*InstrumentedWork)(void*), void* Arg)
{
	if (autortfm_is_closed())
	{
		return RTFM_autortfm_commit(UninstrumentedWork, InstrumentedWork, Arg);
	}

    autortfm_result Result = autortfm_transact(UninstrumentedWork, InstrumentedWork, Arg);
	UE_CLOG(Result != autortfm_committed, LogAutoRTFM, Fatal, TEXT("Unexpected transaction result: %u."), Result);
}

extern "C" void autortfm_abort()
{
	if (autortfm_is_closed())
	{
		return RTFM_autortfm_abort();
	}

	UE_CLOG(!FContext::IsTransactional(), LogAutoRTFM, Fatal, TEXT("The function `autortfm_abort` was called from outside a transaction."));
	FContext::Get()->AbortByRequestAndThrow();
}

extern "C" bool autortfm_start_transaction()
{
	if (autortfm_is_closed())
	{
		RTFM_autortfm_start_transaction();
		return false;
	}

	UE_CLOG(!FContext::IsTransactional(), LogAutoRTFM, Fatal, TEXT("The function `autortfm_start_transaction` was called from outside a transact."));
	return FContext::Get()->StartTransaction();
}

extern "C" autortfm_result autortfm_commit_transaction()
{
	if (autortfm_is_closed())
	{
		RTFM_autortfm_commit_transaction();
		return autortfm_aborted_by_language;
	}

	UE_CLOG(!FContext::IsTransactional(), LogAutoRTFM, Fatal, TEXT("The function `autortfm_commit_transaction` was called from outside a transact."));
	return static_cast<autortfm_result>(FContext::Get()->CommitTransaction());
}

extern "C" autortfm_result autortfm_abort_transaction()
{
	if (autortfm_is_closed())
	{
		return RTFM_autortfm_abort_transaction();
	}

	UE_CLOG(!FContext::IsTransactional(), LogAutoRTFM, Fatal, TEXT("The function `autortfm_abort_transaction` was called from outside a transact."));
	FContext* const Context = FContext::Get();
	return static_cast<autortfm_result>(Context->AbortTransaction(false, false));
}

extern "C" autortfm_result autortfm_cascading_abort_transaction()
{
	if (autortfm_is_closed())
	{
		return RTFM_autortfm_cascading_abort_transaction();
	}

	UE_CLOG(!FContext::IsTransactional(), LogAutoRTFM, Fatal, TEXT("The function `autortfm_cascading_abort_transaction` was called from outside a transact."));
	FContext* const Context = FContext::Get();
	return static_cast<autortfm_result>(Context->AbortTransaction(false, true));
}

extern "C" void autortfm_clear_transaction_status()
{
	if (autortfm_is_closed())
	{
		return RTFM_autortfm_clear_transaction_status();
	}

	ASSERT(FContext::Get()->IsAborting());
	FContext::Get()->ClearTransactionStatus();
}

extern "C" UE_AUTORTFM_NOAUTORTFM bool autortfm_is_aborting()
{
	if (ForTheRuntime::IsAutoRTFMRuntimeEnabled())
	{
		return FContext::Get()->IsAborting();
	}

	return false;
}

extern "C" UE_AUTORTFM_NOAUTORTFM bool autortfm_current_nest_throw()
{
	FContext::Get()->Throw();
	return true;
}

extern "C" void autortfm_open(void (*Work)(void*), void* Arg)
{
	if (autortfm_is_closed())
	{
		return RTFM_autortfm_open(Work, Arg);
	}

	Work(Arg);
}

extern "C" autortfm_status autortfm_close(void (*UninstrumentedWork)(void*), void (*InstrumentedWork)(void*), void* Arg)
{
	if (autortfm_is_closed())
	{
		return RTFM_autortfm_close(UninstrumentedWork, InstrumentedWork, Arg);
	}

	autortfm_status Result = autortfm_status_ontrack;

	if (ForTheRuntime::IsAutoRTFMRuntimeEnabled())
	{
		UE_CLOG(!FContext::IsTransactional(), LogAutoRTFM, Fatal, TEXT("Close called from an outside a transaction."));

		FContext* const Context = FContext::Get();

		if (InstrumentedWork)
		{
			Result = static_cast<autortfm_status>(Context->CallClosedNest(InstrumentedWork, Arg));
		}
		else
		{
			ensureMsgf(!ForTheRuntime::IsEnsureOnAbortByLanguageEnabled(), TEXT("Could not find function %p '%s' where '%s'."), UninstrumentedWork, *GetFunctionDescription(UninstrumentedWork), ANSI_TO_TCHAR("autortfm_close"));
	        Context->AbortByLanguageAndThrow();
		}
	}
	else
	{
		UninstrumentedWork(Arg);
	}

	return Result;
}

extern "C" void autortfm_record_open_write(void* Ptr, size_t Size)
{
	if (autortfm_is_closed())
	{
		return RTFM_autortfm_record_open_write(Ptr, Size);
	}
	if (FTransaction* CurrentTransaction = FContext::Get()->GetCurrentTransaction())
	{
		CurrentTransaction->RecordWrite(Ptr, Size);
	}
}

extern "C" UE_AUTORTFM_NOAUTORTFM void autortfm_register_open_function(void* OriginalFunction, void* NewFunction)
{
	UE_LOG(LogAutoRTFM, Verbose, TEXT("Registering open %p->%p"), OriginalFunction, NewFunction);
    FunctionMapAdd(OriginalFunction, NewFunction);
}

extern "C" bool autortfm_is_on_current_transaction_stack(void* Ptr)
{
	if (FTransaction* CurrentTransaction = FContext::Get()->GetCurrentTransaction())
	{
		return CurrentTransaction->IsOnStack(Ptr);
	}
	return false;
}

void ForTheRuntime::OnCommitInternal(TFunction<void()> && Work)
{
	if (autortfm_is_closed())
	{
		return RTFM_OnCommitInternal(MoveTemp(Work));
	}

	Work();
}

void ForTheRuntime::OnAbortInternal(TFunction<void()> && Work)
{
	if (autortfm_is_closed())
	{
		return RTFM_OnAbortInternal(MoveTemp(Work));
	}
}

void ForTheRuntime::PushOnAbortHandlerInternal(const void* Key, TFunction<void()> && Work)
{
	if (autortfm_is_closed())
	{
		return RTFM_PushOnAbortHandlerInternal(Key, MoveTemp(Work));
	}
}

void ForTheRuntime::PopOnAbortHandlerInternal(const void* Key)
{
	if (autortfm_is_closed())
	{
		return RTFM_PopOnAbortHandlerInternal(Key);
	}
}

void ForTheRuntime::PopAllOnAbortHandlersInternal(const void* Key)
{
	if (autortfm_is_closed())
	{
		return RTFM_PopAllOnAbortHandlersInternal(Key);
	}
}

extern "C" void autortfm_on_commit(void (*Work)(void*), void* Arg)
{
	if (autortfm_is_closed())
	{
		return RTFM_autortfm_on_commit(Work, Arg);
	}

    Work(Arg);
}

extern "C" void autortfm_on_abort(void (*Work)(void*), void* Arg)
{
	if (autortfm_is_closed())
	{
		return RTFM_autortfm_on_abort(Work, Arg);
	}

}

extern "C" void autortfm_push_on_abort_handler(const void* Key, void (*Work)(void*), void* Arg)
{
	if (autortfm_is_closed())
	{
		return RTFM_autortfm_push_on_abort_handler(Key, Work, Arg);
	}

}

extern "C" void autortfm_pop_on_abort_handler(const void* Key)
{
	if (autortfm_is_closed())
	{
		return RTFM_autortfm_pop_on_abort_handler(Key);
	}

}

extern "C" void* autortfm_did_allocate(void* Ptr, size_t Size)
{
	if (autortfm_is_closed())
	{
		return RTFM_autortfm_did_allocate(Ptr, Size);
	}

    return Ptr;
}

extern "C" void autortfm_did_free(void* Ptr)
{
	if (autortfm_is_closed())
	{
		return RTFM_autortfm_did_free(Ptr);
	}

	// We only need to process did free if we need to track allocation locations.
	if constexpr (bTrackAllocationLocations)
	{
		if (UNLIKELY(GIsCriticalError))
		{
			return;
		}

		if (FContext::IsTransactional())
		{
			FContext* const Context = FContext::Get();

		    // We only care about frees that are occuring when the transaction
		    // is in an on-going state (it's not committing or aborting).
		    if (EContextStatus::OnTrack == Context->GetStatus())
		    {
		    	Context->DidFree(Ptr);
		    }
		}
	}
}

extern "C" void autortfm_check_consistency_assuming_no_races()
{
	if (autortfm_is_closed())
	{
		return RTFM_autortfm_check_consistency_assuming_no_races();
	}

    if (FContext::IsTransactional())
    {
        AutoRTFM::Unreachable();
    }
}

extern "C" UE_AUTORTFM_NOAUTORTFM void autortfm_check_abi(void* const Ptr, const size_t Size)
{
    struct FConstants final
    {
		const uint32_t Major = AutoRTFM::Constants::Major;
		const uint32_t Minor = AutoRTFM::Constants::Minor;
		const uint32_t Patch = AutoRTFM::Constants::Patch;

		// This is messy - but we want to do comparisons but without comparing any padding bytes.
		// Before C++20 we cannot use a default created operator== and operator!=, so we use this
		// ugly trick to just compare the members.
	private:
		auto Tied() const
		{
			return Tie(Major, Minor, Patch);
		}

	public:
		bool operator==(const FConstants& Other) const
		{
			return Tied() == Other.Tied();
		}

		bool operator!=(const FConstants& Other) const
		{
			return !(*this == Other);
		}
    } RuntimeConstants;

	UE_CLOG(sizeof(FConstants) != Size, LogAutoRTFM, Fatal, TEXT("ABI error between AutoRTFM compiler and runtime."));

    const FConstants* const CompilerConstants = static_cast<FConstants*>(Ptr);

	UE_CLOG(RuntimeConstants != *CompilerConstants, LogAutoRTFM, Fatal, TEXT("ABI error between AutoRTFM compiler and runtime."));
}

} // namespace AutoRTFM

#endif // defined(__AUTORTFM) && __AUTORTFM
