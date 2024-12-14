// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if (defined(__AUTORTFM) && __AUTORTFM)
#define UE_AUTORTFM 1  // Compiler is 'verse-clang'
#else
#define UE_AUTORTFM 0
#endif

#if (defined(__AUTORTFM_ENABLED) && __AUTORTFM_ENABLED)
#define UE_AUTORTFM_ENABLED 1  // Compiled with '-fautortfm'
#else
#define UE_AUTORTFM_ENABLED 0
#endif

#if !defined(UE_AUTORTFM_ENABLED_RUNTIME_BY_DEFAULT)
#define UE_AUTORTFM_ENABLED_RUNTIME_BY_DEFAULT 1
#endif

#if UE_AUTORTFM
#if defined(__has_feature)
#if __has_feature(autostm)
#define UE_AUTOSTM 1
#endif
#endif
#endif

#if !defined(UE_AUTOSTM)
#define UE_AUTOSTM 0
#endif

#if !defined(UE_AUTORTFM_STATIC_VERIFIER)
#define UE_AUTORTFM_STATIC_VERIFIER 0
#endif

#if UE_AUTORTFM
#define UE_AUTORTFM_NOAUTORTFM [[clang::noautortfm, clang::noinline]]
// #jira SOL-6589: remove clang::noinline once this JIRA is fixed.
#define UE_AUTORTFM_ALWAYS_OPEN [[clang::autortfm_always_open, clang::noinline]]
#define UE_AUTORTFM_CALLSITE_FORCEINLINE [[clang::always_inline]]
#else
#define UE_AUTORTFM_NOAUTORTFM
#define UE_AUTORTFM_ALWAYS_OPEN
#define UE_AUTORTFM_CALLSITE_FORCEINLINE
#endif

#if UE_AUTORTFM && UE_AUTORTFM_STATIC_VERIFIER
#define UE_AUTORTFM_ENSURE_SAFE [[clang::autortfm_ensure_safe]]
#define UE_AUTORTFM_ASSUME_SAFE [[clang::autortfm_assume_safe]]
#else
#define UE_AUTORTFM_ENSURE_SAFE
#define UE_AUTORTFM_ASSUME_SAFE
#endif

#if defined(UE_AUTORTFM_STANDALONE)
#include <stdlib.h>
#include <memory.h>
#include <type_traits>
#include <utility>
#define UE_AUTORTFM_API
#define UE_AUTORTFM_FORCEINLINE inline
#define UE_AUTORTFM_MEMCPY ::memcpy
#define UE_AUTORTFM_MOVE std::move
#define PRAGMA_DISABLE_UNREACHABLE_CODE_WARNINGS
#define PRAGMA_RESTORE_UNREACHABLE_CODE_WARNINGS
#else
#include <HAL/Platform.h>
#include <HAL/PlatformMemory.h>
#include <Templates/UnrealTemplate.h>
#define UE_AUTORTFM_API CORE_API
#define UE_AUTORTFM_FORCEINLINE FORCEINLINE
#define UE_AUTORTFM_MEMCPY FPlatformMemory::Memcpy
#define UE_AUTORTFM_MOVE MoveTemp
#endif

#if UE_AUTORTFM
template <typename FuncType> class TFunction;
#endif

#define UE_AUTORTFM_UNUSED(UNUSEDVAR) (void)UNUSEDVAR

#ifdef __cplusplus
extern "C"
{
#endif

// The C API exists for a few reasons:
//
// - It makes linking easy. AutoRTFM has to deal with a weird kind of linking
//   where the compiler directly emits calls to functions with a given name.
//   It's easiest to do that in llvm if the functions have C linkage and C ABI.
// - It makes testing easy. Even seemingly simple C++ code introduces pitfalls
//   for AutoRTFM. So very focused tests work best when written in C.
// - It makes compiler optimizations much easier as there is no mangling to
// 	 consider when looking for functions in the runtime we can optimize.
// 
// We use snake_case for C API surface area to make it easy to distinguish.
//
// The C API should not be used directly - it is here purely as an
// implementation detail.

// This must match AutoRTFM::ETransactionResult.
typedef enum
{
    autortfm_aborted_by_request = 0,
    autortfm_aborted_by_language,
    autortfm_committed,
	autortfm_aborted_by_transact_in_on_commit,
	autortfm_aborted_by_transact_in_on_abort,
	autortfm_aborted_by_cascade,
} autortfm_result;

// This must match AutoRTFM::EContextStatus.
typedef enum
{
	autortfm_status_idle,
	autortfm_status_ontrack,
	autortfm_status_aborted_by_failed_lock_aquisition,
	autortfm_status_aborted_by_language,
	autortfm_status_aborted_by_request,
	autortfm_status_committing,
	autortfm_status_aborted_by_cascade
} autortfm_status;

#if UE_AUTORTFM_ENABLED
// Note: There is no implementation of this function.
// The AutoRTFM compiler will replace all calls to this function with a constant boolean value.
UE_AUTORTFM_API bool autortfm_is_closed(void);
#else
static UE_AUTORTFM_FORCEINLINE bool autortfm_is_closed(void)
{
    return false;
}
#endif

#if UE_AUTORTFM_ENABLED
UE_AUTORTFM_API bool autortfm_is_transactional(void);
#else
static UE_AUTORTFM_FORCEINLINE bool autortfm_is_transactional(void)
{
    return false;
}
#endif

#if UE_AUTORTFM_ENABLED
UE_AUTORTFM_API bool autortfm_is_committing_or_aborting(void);
#else
static UE_AUTORTFM_FORCEINLINE bool autortfm_is_committing_or_aborting(void)
{
	return false;
}
#endif

#if UE_AUTORTFM_ENABLED
UE_AUTORTFM_API autortfm_result autortfm_transact(void (*UninstrumentedWork)(void*), void (*InstrumentedWork)(void*), void* Arg);
#else
static UE_AUTORTFM_FORCEINLINE autortfm_result autortfm_transact(void (*UninstrumentedWork)(void*), void (*InstrumentedWork)(void*), void* Arg)
{
	UE_AUTORTFM_UNUSED(InstrumentedWork);
	UninstrumentedWork(Arg);
	return autortfm_committed;
}
#endif

#if UE_AUTORTFM_ENABLED
UE_AUTORTFM_API autortfm_result autortfm_transact_then_open(void (*UninstrumentedWork)(void*), void (*InstrumentedWork)(void*), void* Arg);
#else
static UE_AUTORTFM_FORCEINLINE autortfm_result autortfm_transact_then_open(void (*UninstrumentedWork)(void*), void (*InstrumentedWork)(void*), void* Arg)
{
	UE_AUTORTFM_UNUSED(InstrumentedWork);
	UninstrumentedWork(Arg);
    return autortfm_committed;
}
#endif

#if UE_AUTORTFM_ENABLED
UE_AUTORTFM_API void autortfm_commit(void (*UninstrumentedWork)(void*), void (*InstrumentedWork)(void*), void* Arg);
#else
static UE_AUTORTFM_FORCEINLINE void autortfm_commit(void (*UninstrumentedWork)(void*), void (*InstrumentedWork)(void*), void* Arg)
{
	UE_AUTORTFM_UNUSED(InstrumentedWork);
	UninstrumentedWork(Arg);
}
#endif

#if UE_AUTORTFM_ENABLED
UE_AUTORTFM_API autortfm_result autortfm_abort_transaction();
#else
static UE_AUTORTFM_FORCEINLINE autortfm_result autortfm_abort_transaction() { return autortfm_aborted_by_request; }
#endif

#if UE_AUTORTFM_ENABLED
UE_AUTORTFM_API autortfm_result autortfm_cascading_abort_transaction();
#else
static UE_AUTORTFM_FORCEINLINE autortfm_result autortfm_cascading_abort_transaction() { return autortfm_aborted_by_cascade; }
#endif

#if UE_AUTORTFM_ENABLED
UE_AUTORTFM_API bool autortfm_start_transaction();
#else
static UE_AUTORTFM_FORCEINLINE bool autortfm_start_transaction() { return false; }
#endif

#if UE_AUTORTFM_ENABLED
UE_AUTORTFM_API autortfm_result autortfm_commit_transaction();
#else
static UE_AUTORTFM_FORCEINLINE autortfm_result autortfm_commit_transaction() { return autortfm_aborted_by_language; }
#endif

#if UE_AUTORTFM_ENABLED
UE_AUTORTFM_API void autortfm_clear_transaction_status();
#else
static UE_AUTORTFM_FORCEINLINE void autortfm_clear_transaction_status() {}
#endif

#if UE_AUTORTFM_ENABLED
static UE_AUTORTFM_FORCEINLINE void autortfm_abort_if_transactional(void)
{
	if (autortfm_is_transactional())
	{
		autortfm_abort_transaction();
	}
}
#else
static UE_AUTORTFM_FORCEINLINE void autortfm_abort_if_transactional(void) { }
#endif

#if UE_AUTORTFM_ENABLED
static UE_AUTORTFM_FORCEINLINE void autortfm_abort_if_closed(void)
{
	if (autortfm_is_closed())
	{
		autortfm_abort_transaction();
	}
}
#else
static UE_AUTORTFM_FORCEINLINE void autortfm_abort_if_closed(void) { }
#endif

#if UE_AUTORTFM_ENABLED
UE_AUTORTFM_API void autortfm_open(void (*work)(void* arg), void* arg);
#else
static UE_AUTORTFM_FORCEINLINE void autortfm_open(void (*work)(void* arg), void* arg) { work(arg); }
#endif

#if UE_AUTORTFM_ENABLED
[[nodiscard]] UE_AUTORTFM_API autortfm_status autortfm_close(void (*UninstrumentedWork)(void*), void (*InstrumentedWork)(void*), void* Arg);
#else
PRAGMA_DISABLE_UNREACHABLE_CODE_WARNINGS
[[nodiscard]] static UE_AUTORTFM_FORCEINLINE autortfm_status autortfm_close(void (*UninstrumentedWork)(void*), void (*InstrumentedWork)(void*), void* Arg)
{
	UE_AUTORTFM_UNUSED(UninstrumentedWork);
	UE_AUTORTFM_UNUSED(InstrumentedWork);
	UE_AUTORTFM_UNUSED(Arg);
    abort();
	return autortfm_status_aborted_by_language;
}
PRAGMA_RESTORE_UNREACHABLE_CODE_WARNINGS
#endif

#if UE_AUTORTFM_ENABLED
UE_AUTORTFM_API void autortfm_record_open_write(void* Ptr, size_t Size);
#else
static UE_AUTORTFM_FORCEINLINE void autortfm_record_open_write(void* Ptr, size_t Size)
{
	UE_AUTORTFM_UNUSED(Ptr);
	UE_AUTORTFM_UNUSED(Size);
}
#endif

#if UE_AUTORTFM_ENABLED
UE_AUTORTFM_API void autortfm_register_open_function(void* original_function, void* new_function);
#else
static UE_AUTORTFM_FORCEINLINE void autortfm_register_open_function(void* original_function, void* new_function) 
{ 
	UE_AUTORTFM_UNUSED(original_function);
	UE_AUTORTFM_UNUSED(new_function);
}
#endif

#if UE_AUTORTFM_ENABLED
UE_AUTORTFM_API bool autortfm_is_on_current_transaction_stack(void* Ptr);
#else
static UE_AUTORTFM_FORCEINLINE bool autortfm_is_on_current_transaction_stack(void* Ptr)
{
	return false;
}
#endif

#if UE_AUTORTFM_ENABLED
UE_AUTORTFM_API void autortfm_on_commit(void (*work)(void* arg), void* arg);
#else
static UE_AUTORTFM_FORCEINLINE void autortfm_on_commit(void (*work)(void* arg), void* arg)
{
    work(arg);
}
#endif

#if UE_AUTORTFM_ENABLED
UE_AUTORTFM_API void autortfm_on_abort(void (*work)(void* arg), void* arg);
#else
static UE_AUTORTFM_FORCEINLINE void autortfm_on_abort(void (*work)(void* arg), void* arg)
{
	UE_AUTORTFM_UNUSED(work);
	UE_AUTORTFM_UNUSED(arg);
}
#endif

#if UE_AUTORTFM_ENABLED
UE_AUTORTFM_API void autortfm_push_on_abort_handler(const void* key, void (*work)(void* arg), void* arg);
UE_AUTORTFM_API void autortfm_pop_on_abort_handler(const void* key);
#else
static UE_AUTORTFM_FORCEINLINE void autortfm_push_on_abort_handler(const void* key, void (*work)(void* arg), void* arg)
{
	UE_AUTORTFM_UNUSED(key);
	UE_AUTORTFM_UNUSED(work);
	UE_AUTORTFM_UNUSED(arg);
}

static UE_AUTORTFM_FORCEINLINE void autortfm_pop_on_abort_handler(const void* key)
{
	UE_AUTORTFM_UNUSED(key);
}
#endif

#if UE_AUTORTFM_ENABLED
UE_AUTORTFM_API void* autortfm_did_allocate(void* ptr, size_t size);
#else
static UE_AUTORTFM_FORCEINLINE void* autortfm_did_allocate(void* ptr, size_t size)
{
	UE_AUTORTFM_UNUSED(size);
    return ptr;
}
#endif

#if UE_AUTORTFM_ENABLED
UE_AUTORTFM_API void autortfm_did_free(void* ptr);
#else
static UE_AUTORTFM_FORCEINLINE void autortfm_did_free(void* ptr)
{
	UE_AUTORTFM_UNUSED(ptr);
}
#endif

#if UE_AUTORTFM_ENABLED
UE_AUTORTFM_API void autortfm_check_consistency_assuming_no_races(void);
#else
static UE_AUTORTFM_FORCEINLINE void autortfm_check_consistency_assuming_no_races(void) { }
#endif

// If running with AutoRTFM enabled, then perform an ABI check between the
// AutoRTFM compiler and the AutoRTFM runtime, to ensure that memory is being
// laid out in an identical manner between the AutoRTFM runtime and the AutoRTFM
// compiler pass. Should not be called manually by the user, a call to this will
// be injected by the compiler into a global constructor in the AutoRTFM compiled
// code.
#if UE_AUTORTFM_ENABLED
UE_AUTORTFM_API void autortfm_check_abi(void* ptr, size_t size);
#else
static UE_AUTORTFM_FORCEINLINE void autortfm_check_abi(void* ptr, size_t size)
{
	UE_AUTORTFM_UNUSED(ptr);
	UE_AUTORTFM_UNUSED(size);
}
#endif

#ifdef __cplusplus
}

#include <algorithm>
#include <initializer_list>
#include <tuple>

namespace AutoRTFM
{

// The transaction result provides information on how a transaction completed. This is either Committed,
// or one of the various AbortedBy* variants to show why an abort occurred.
enum class ETransactionResult
{
	// The transaction aborted because of an explicit call to AbortTransaction.
    AbortedByRequest = autortfm_aborted_by_request,

	// The transaction aborted because of unhandled constructs in the code (atomics, unhandled function calls, etc).
    AbortedByLanguage = autortfm_aborted_by_language,

	// The transaction committed successfully. For a nested transaction this does not mean that the transaction effects
	// cannot be undone later if the parent transaction is aborted for any reason.
    Committed = autortfm_committed,

	// The transaction aborted because in a call to OnCommit, a new transaction nest was attempted which is not allowed.
	AbortedByTransactInOnCommit = autortfm_aborted_by_transact_in_on_commit,

	// The transaction aborted because in a call to OnAbort, a new transaction nest was attempted which is not allowed.
	AbortedByTransactInOnAbort = autortfm_aborted_by_transact_in_on_abort,

	// The transaction aborted because of an explicit call to CascadingAbortTransaction.
	AbortedByCascade = autortfm_aborted_by_cascade
};

// The context status shows what state the AutoRTFM context is currently in. 
enum class EContextStatus : uint8_t
{
	// An Idle status means we are not in transactional code.
	Idle = autortfm_status_idle,

	// An OnTrack status means we are in transactional code.
	OnTrack = autortfm_status_ontrack,

	// Reserved for a full STM future.
	AbortedByFailedLockAcquisition = autortfm_status_aborted_by_failed_lock_aquisition,

	// An AbortedByLanguage status means that we found some unhandled constructs in the code
	// (atomics, unhandled function calls, etc) and are currently aborting because of it.
	AbortedByLanguage = autortfm_status_aborted_by_language,

	// An AbortedByRequest status means that a call to AbortTransaction occurred and we are
	// currently aborting because of it.
	AbortedByRequest = autortfm_status_aborted_by_request,

	// A Committing status means we are currently attempting to commit a transaction.
	Committing = autortfm_status_committing,

	// An AbortedByCascade status means that a call to CascadingAbortTransaction occurred and
	// we are currently aborting because of it.
	AbortedByCascade = autortfm_status_aborted_by_cascade
};

#if UE_AUTORTFM
namespace ForTheRuntime
{
	UE_AUTORTFM_API void OnCommitInternal(TFunction<void()>&& Work);
	UE_AUTORTFM_API void OnAbortInternal(TFunction<void()>&& Work);
	UE_AUTORTFM_API void PushOnAbortHandlerInternal(const void* Key, TFunction<void()>&& Work);
	UE_AUTORTFM_API void PopOnAbortHandlerInternal(const void* Key);
	UE_AUTORTFM_API void PopAllOnAbortHandlersInternal(const void* Key);
} // namespace ForTheRuntime
#endif

template<typename TFunctor>
void AutoRTFMFunctorInvoker(void* Arg) { (*static_cast<const TFunctor*>(Arg))(); }

#if UE_AUTORTFM
extern "C" UE_AUTORTFM_API void* autortfm_lookup_function(void* OriginalFunction, const char* Where);

template<typename TFunctor>
auto AutoRTFMLookupInstrumentedFunctorInvoker(const TFunctor& Functor) -> void(*)(void*)
{
	// keep this as a single expression to help ensure that even Debug builds optimize this.
	// if we put intermediate results in local variables then the compiler emits loads
	// and stores to the stack which confuse our custom pass that tries to strip away
	// the actual call to autortfm_lookup_function
	void (*Result)(void*) = reinterpret_cast<void(*)(void*)>(autortfm_lookup_function(reinterpret_cast<void*>(&AutoRTFMFunctorInvoker<TFunctor>), "AutoRTFMLookupInstrumentedFunctorInvoker"));
	return Result;
}
#else
template<typename TFunctor>
auto AutoRTFMLookupInstrumentedFunctorInvoker(const TFunctor& Functor) -> void(*)(void*)
{
	return nullptr;
}
#endif

// Tells if we are currently running in a transaction. This will return true in an open nest
// (see `Open`). This function is handled specially in the compiler and will be constant folded
// as true in closed code, or preserved as a function call in open code.
static UE_AUTORTFM_FORCEINLINE bool IsTransactional() { return autortfm_is_transactional(); }

// Tells if we are currently running in the closed nest of a transaction. By default,
// transactional code is in a closed nest; the only way to be in an open nest is to request it
// via `Open`. This function is handled specially in the compiler and will be constant folded
// as true in closed code, and false in open code.
static UE_AUTORTFM_FORCEINLINE bool IsClosed() { return autortfm_is_closed(); }

// Tells us if we are currently committing or aborting a transaction. This will return true
// in an on-abort or on-commit.
static UE_AUTORTFM_FORCEINLINE bool IsCommittingOrAborting() { return autortfm_is_committing_or_aborting(); }

// Returns true if the passed-in pointer is on the stack of the currently-executing transaction.
// This is occasionally necessary when writing OnAbort handlers for objects on the stack, since 
// we don't want to scribble on stack memory that might have been reused.
static UE_AUTORTFM_FORCEINLINE bool IsOnCurrentTransactionStack(void* Ptr) 
{ 
	return autortfm_is_on_current_transaction_stack(Ptr);
}

// Run the functor in a transaction. Memory writes and other side effects get instrumented
// and will be reversed if the transaction aborts.
// 
// If this begins a nested transaction, the instrumented effects are logged onto the root
// transaction, so the effects can be reversed later if the root transaction aborts, even
// if this nested transaction succeeds.
//
// If AutoRTFM is disabled, the code will be ran non-transactionally.
template<typename TFunctor>
static UE_AUTORTFM_FORCEINLINE ETransactionResult Transact(const TFunctor& Functor)
{
	ETransactionResult Result =
		static_cast<ETransactionResult>(
			autortfm_transact(
				&AutoRTFMFunctorInvoker<TFunctor>,
				AutoRTFMLookupInstrumentedFunctorInvoker<TFunctor>(Functor),
				const_cast<void*>(static_cast<const void*>(&Functor))));

	return Result;
}

// This is just like calling Transact([&] { Open([&] { Functor(); }); });  
// The reason we expose it is that it allows the caller's module to not
// be compiled with the AutoRTFM instrumentation of functions if the only
// thing that's being invoked is a function in the open.
template<typename TFunctor>
static UE_AUTORTFM_FORCEINLINE ETransactionResult TransactThenOpen(const TFunctor& Functor)
{
	ETransactionResult Result =
		static_cast<ETransactionResult>(
			autortfm_transact_then_open(
				&AutoRTFMFunctorInvoker<TFunctor>,
				AutoRTFMLookupInstrumentedFunctorInvoker<TFunctor>(Functor),
				const_cast<void*>(static_cast<const void*>(&Functor))));

	return Result;
}

// Run the callback in a transaction like Transact, but abort program
// execution if the result is anything other than autortfm_committed.
// Useful for testing.
template<typename TFunctor>
static UE_AUTORTFM_FORCEINLINE void Commit(const TFunctor& Functor)
{
    autortfm_commit(
		&AutoRTFMFunctorInvoker<TFunctor>,
		AutoRTFMLookupInstrumentedFunctorInvoker<TFunctor>(Functor),
		const_cast<void*>(static_cast<const void*>(&Functor)));
}

// End a transaction and discard all effects.
static UE_AUTORTFM_FORCEINLINE ETransactionResult AbortTransaction()
{
	return static_cast<ETransactionResult>(autortfm_abort_transaction());
}

// End a transaction nest and discard all effects. This cascades, meaning
// an abort of a nested transaction will cause all transactions in the
// nest to abort.
static UE_AUTORTFM_FORCEINLINE ETransactionResult CascadingAbortTransaction()
{
	return static_cast<ETransactionResult>(autortfm_cascading_abort_transaction());
}

// Abort if running in a transaction.
static UE_AUTORTFM_FORCEINLINE void AbortIfTransactional()
{
    autortfm_abort_if_transactional();
}

// Abort if running in closed code.
static UE_AUTORTFM_FORCEINLINE void AbortIfClosed()
{
    autortfm_abort_if_closed();
}

namespace Detail
{
template <typename T, typename = void>
struct THasAssignFromOpenToClosedMethod : std::false_type {};
template <typename T>
struct THasAssignFromOpenToClosedMethod<T, std::void_t<decltype(T::AutoRTFMAssignFromOpenToClosed(std::declval<T&>(), std::declval<T>()))>> : std::true_type {};
}

// Evaluates to true if the type T has a static method with the signature:
//    static void AutoRTFMAssignFromOpenToClosed(T& Closed, U Open)
// Where `U` is `T`, `const T&` or `T&&`. Supports both copy assignment and move assignment.
template <typename T>
static constexpr bool HasAssignFromOpenToClosedMethod = Detail::THasAssignFromOpenToClosedMethod<T>::value;

// Template class used to declare a method for safely copying or moving an 
// object of type T from open to closed transactions.
// Specializations of TAssignFromOpenToClosed must have at least one static
// method with the signature:
//   static void Assign(T& Closed, U Open);
// Where `U` is `T`, `const T&` or `T&&`. Supports both copy assignment and move assignment.
//
// TAssignFromOpenToClosed has pre-declared specializations for basic primitive
// types, and can be extended with user-declared template specializations.
//
// TAssignFromOpenToClosed has a pre-declared specialization that detects and
// calls a static method on T with the signature:
//    static void AutoRTFMAssignFromOpenToClosed(T& Closed, U Open)
// Where `U` is `T`, `const T&` or `T&&`. Supports both copy assignment and move assignment.
template<typename T, typename = void>
struct TAssignFromOpenToClosed;

namespace Detail
{
template <typename T, typename = void>
struct THasAssignFromOpenToClosedTrait : std::false_type {};
template <typename T>
struct THasAssignFromOpenToClosedTrait<T, std::void_t<decltype(TAssignFromOpenToClosed<T>::Assign(std::declval<T&>(), std::declval<T>()))>> : std::true_type {};
}

// Evaluates to true if the type T supports assigning from open to closed transactions.
template <typename T>
static constexpr bool HasAssignFromOpenToClosedTrait = Detail::THasAssignFromOpenToClosedTrait<T>::value;

// Specialization of TAssignFromOpenToClosed for fundamental types.
template <typename T>
struct TAssignFromOpenToClosed<T, std::enable_if_t<std::is_fundamental_v<T>>>
{
	UE_AUTORTFM_FORCEINLINE static void Assign(T& Closed, T Open) { Closed = Open; }
};

// Specialization of TAssignFromOpenToClosed for raw pointer types.
template <typename T>
struct TAssignFromOpenToClosed<T*, void>
{
	UE_AUTORTFM_FORCEINLINE static void Assign(T*& Closed, T* Open) { Closed = Open; }
};

// Specialization of TAssignFromOpenToClosed for std::tuple.
template <typename ... TYPES>
struct TAssignFromOpenToClosed<std::tuple<TYPES...>, std::enable_if_t<(HasAssignFromOpenToClosedTrait<TYPES> && ...)>>
{
	template<size_t I = 0, typename SRC = void>
	UE_AUTORTFM_FORCEINLINE static void AssignElements(std::tuple<TYPES...>& Closed, SRC&& Open)
	{
		if constexpr(I < sizeof...(TYPES))
		{
			using E = std::tuple_element_t<I, std::tuple<TYPES...>>;
			TAssignFromOpenToClosed<E>::Assign(std::get<I>(Closed), std::get<I>(std::forward<SRC>(Open)));
			AssignElements<I+1>(Closed, std::forward<SRC>(Open));
		}
	}

	template<typename SRC>
	UE_AUTORTFM_FORCEINLINE static void Assign(std::tuple<TYPES...>& Closed, SRC&& Open)
	{
		AssignElements(Closed, std::forward<SRC>(Open));
	}
};

// Specialization of TAssignFromOpenToClosed for types that have a static method
// with the signature:
//    static void AutoRTFMAssignFromOpenToClosed(T& Closed, U Open)
// Where `U` is `T`, `const T&` or `T&&`. Supports both copy assignment and move assignment.
template <typename T>
struct TAssignFromOpenToClosed<T, std::enable_if_t<HasAssignFromOpenToClosedMethod<T>>>
{
	template<typename OPEN>
	UE_AUTORTFM_FORCEINLINE static void Assign(T& Closed, OPEN&& Open)
	{
		Closed.AutoRTFMAssignFromOpenToClosed(Closed, std::forward<OPEN>(Open));
	}
};

// Specialization of TAssignFromOpenToClosed for `void` (used to make IsSafeToReturnFromOpen<void> work).
template <>
struct TAssignFromOpenToClosed<void, void>;

// Evaluates to true if the type T is safe to return from Open().
template <typename T>
static constexpr bool IsSafeToReturnFromOpen = HasAssignFromOpenToClosedTrait<T> || std::is_same_v<T, void>;

// Executes the given code non-transactionally regardless of whether we are in
// a transaction or not. Returns the value returned by Functor.
// TReturn must be void or a type that can be safely copied from the open to a closed transaction.
// TAssignFromOpenToClosed must have a specialization for the type that is being returned.
template<typename TFunctor, typename TReturn = decltype(std::declval<TFunctor>()())>
static UE_AUTORTFM_FORCEINLINE TReturn Open(const TFunctor& Functor)
{
	static_assert(IsSafeToReturnFromOpen<TReturn>,
		"function return type is not safe to return from Open()");

	if (!autortfm_is_closed())
	{
		return Functor();
	}

	if constexpr (IsSafeToReturnFromOpen<TReturn>)
	{
		if constexpr (std::is_same_v<void, TReturn>)
		{
			struct FCallHelper
			{
				UE_AUTORTFM_NOAUTORTFM static void Call(void* Arg)
				{
					const TFunctor& Fn = *reinterpret_cast<TFunctor*>(Arg);
					UE_AUTORTFM_CALLSITE_FORCEINLINE Fn();
				}
			};
			autortfm_open(&FCallHelper::Call, const_cast<void*>(static_cast<const void*>(&Functor)));
		}
		else
		{
			struct FCallHelper
			{
				UE_AUTORTFM_NOAUTORTFM static void Call(void* Arg)
				{
					FCallHelper& Self = *reinterpret_cast<FCallHelper*>(Arg);
					UE_AUTORTFM_CALLSITE_FORCEINLINE
						TAssignFromOpenToClosed<TReturn>::Assign(Self.ReturnValue, std::move(Self.Functor()));
				}
				const TFunctor& Functor;
				TReturn ReturnValue{};
			};
			FCallHelper Helper{Functor};
			autortfm_open(&FCallHelper::Call, reinterpret_cast<void*>(&Helper));
			return Helper.ReturnValue;
		}
	}
}

// Always executes the given code transactionally when called from a transaction nest
// (whether we are in open or closed code).
//
// Will crash if called outside of a transaction nest.
template<typename TFunctor> [[nodiscard]] static UE_AUTORTFM_FORCEINLINE EContextStatus Close(const TFunctor& Functor)
{
    return static_cast<EContextStatus>(
		autortfm_close(
			&AutoRTFMFunctorInvoker<TFunctor>,
			AutoRTFMLookupInstrumentedFunctorInvoker<TFunctor>(Functor),
			const_cast<void*>(static_cast<const void*>(&Functor))));
}

#if UE_AUTORTFM
// Have some work happen when this transaction commits. For nested transactions,
// this just adds the work to the work deferred until the outer nest's commit.
// If this is called outside a transaction or from an open nest then the work
// happens immediately.
template<typename TFunctor> static UE_AUTORTFM_FORCEINLINE void OnCommit(const TFunctor& Work)
{
	if (autortfm_is_closed())
	{
		ForTheRuntime::OnCommitInternal(Work);
	}
	else
	{
		UE_AUTORTFM_CALLSITE_FORCEINLINE Work();
	}
}
#else
// Have some work happen when this transaction commits. For nested transactions,
// this just adds the work to the work deferred until the outer nest's commit.
// If this is called outside a transaction or from an open nest then the work
// happens immediately.
template<typename TFunctor> static UE_AUTORTFM_FORCEINLINE void OnCommit(const TFunctor& Work) { Work(); }
#endif

#if UE_AUTORTFM
// Have some work happen when this transaction aborts. If this is called
// outside a transaction or from an open nest then the work is ignored.
template<typename TFunctor> static UE_AUTORTFM_FORCEINLINE void OnAbort(const TFunctor& Work)
{
	if (autortfm_is_closed())
	{
		ForTheRuntime::OnAbortInternal(Work);
	}
}

// Register a handler for transaction abort. Takes a key parameter so that
// the handler can be unregistered (see PopOnAbortHandler). This is useful
// for scoped mutations that need an abort handler present unless execution
// reaches the end of the relevant scope.
template<typename TFunctor> static UE_AUTORTFM_FORCEINLINE void PushOnAbortHandler(const void* Key, const TFunctor& Work)
{
	if (autortfm_is_closed())
	{
		ForTheRuntime::PushOnAbortHandlerInternal(Key, Work);
	}
}

// Unregister the most recently pushed handler (via PushOnAbortHandler) for the given key.
static UE_AUTORTFM_FORCEINLINE void PopOnAbortHandler(const void* Key)
{
	if (autortfm_is_closed())
	{
		ForTheRuntime::PopOnAbortHandlerInternal(Key);
	}
}

// Unregister all pushed handlers (via PushOnAbortHandler) for the given key.
static UE_AUTORTFM_FORCEINLINE void PopAllOnAbortHandlers(const void* Key)
{
	if (autortfm_is_closed())
	{
		ForTheRuntime::PopAllOnAbortHandlersInternal(Key);
	}
}
#else
// Have some work happen when this transaction aborts. If this is called
// outside a transaction or from an open nest then the work is ignored.
template<typename TFunctor> static UE_AUTORTFM_FORCEINLINE void OnAbort(const TFunctor&) {}

// Register a handler for transaction abort. Takes a key parameter so that
// the handler can be unregistered (see PopOnAbortHandler). This is useful
// for scoped mutations that need an abort handler present unless execution
// reaches the end of the relevant scope.
template<typename TFunctor> static UE_AUTORTFM_FORCEINLINE void PushOnAbortHandler(const void* Key, const TFunctor&) {}

// Unregister the most recently pushed handler (via PushOnAbortHandler) for the given key.
static UE_AUTORTFM_FORCEINLINE void PopOnAbortHandler(const void* Key) {}


// Unregister all pushed handlers (via PushOnAbortHandler) for the given key.
static UE_AUTORTFM_FORCEINLINE void PopAllOnAbortHandlers(const void* Key) {}
#endif

// Inform the runtime that we have performed a new object allocation. It's only
// necessary to call this inside of custom malloc implementations. As an
// optimization, you can choose to then only have your malloc return the pointer
// returned by this function. It's guaranteed to be equal to the pointer you
// passed, but it's blessed specially from the compiler's perspective, leading
// to some nice optimizations. This does nothing when called from open code.
static UE_AUTORTFM_FORCEINLINE void* DidAllocate(void* Ptr, size_t Size)
{
    return autortfm_did_allocate(Ptr, Size);
}

// Inform the runtime that we have free'd a given memory location.
static UE_AUTORTFM_FORCEINLINE void DidFree(void* Ptr)
{
    autortfm_did_free(Ptr);
}

// Informs the runtime that a block of memory is about to be overwritten in the open.
// During a transaction, this allows the runtime to copy the data in preparation for
// a possible abort. Normally, tracking memory overwrites should be automatically
// handled by AutoRTFM, but manual overwrite tracking may be required for third-party 
// libraries or outside compilers (such as ISPC).
static UE_AUTORTFM_FORCEINLINE void RecordOpenWrite(void* Ptr, size_t Size)
{
	autortfm_record_open_write(Ptr, Size);
}

// Informs the runtime that a block of memory is about to be overwritten.
template<typename TTYPE> static UE_AUTORTFM_FORCEINLINE void RecordOpenWrite(TTYPE* Ptr)
{
	autortfm_record_open_write(Ptr, sizeof(TTYPE));
}

// A collection of power-user functions that are reserved for use by the AutoRTFM runtime only.
namespace ForTheRuntime
{
	// An enum to represent the various ways we want to enable/disable the AutoRTFM runtime.
	enum EAutoRTFMEnabledState
	{
		// Disable AutoRTFM.
		AutoRTFM_Disabled = 0,

		// Enable AutoRTFM for *all* Verse code (not just failure contexts).
		AutoRTFM_Enabled,

		// Force disable AutoRTFM - once set the AutoRTFM runtime cannot be re-enabled.
		AutoRTFM_ForcedDisabled,

		// Force enable AutoRTFM - once set the AutoRTFM runtime cannot be re-enabled.
		AutoRTFM_ForcedEnabled,

		AutoRTFM_EnabledForAllVerse [[deprecated("Use AutoRTFM_Enabled instead!")]],
	};

	// An enum to represent whether we should abort and retry transactions (for testing purposes).
	enum EAutoRTFMRetryTransactionState 
	{
		// Do not abort and retry transactions (the default).
		NoRetry = 0,

		// Abort and retry non-nested transactions (EG. only abort the parent transactional nest).
		RetryNonNested,

		// Abort and retry nested-transactions too. Will be slower as each nested-transaction will
		// be aborted and retried at least *twice* (once when the non-nested transaction runs the
		// first time, and a second time when the non-nested transaction is doing its retry after
		// aborting).
		RetryNestedToo,
	};

	// Set whether the AutoRTFM runtime is enabled or disabled. Returns true when the state was changed
	// successfully.
	UE_AUTORTFM_API bool SetAutoRTFMRuntime(EAutoRTFMEnabledState State);

	// Query whether the AutoRTFM runtime is enabled.
	UE_AUTORTFM_API bool IsAutoRTFMRuntimeEnabled();

	// Query whether the AutoRTFM runtime is enabled for *all* Verse code (not just failure contexts).
	[[deprecated("Use IsAutoRTFMRuntimeEnabled instead!")]]
	UE_AUTORTFM_API bool IsAutoRTFMRuntimeEnabledForAllVerse();

	// Set whether we should trigger an ensure on an abort-by-language.
	UE_AUTORTFM_API void SetEnsureOnAbortByLanguage(bool bEnabled);

	// Returns whether the runtime will trigger an ensure on an abort-by-language, or not.
	UE_AUTORTFM_API bool IsEnsureOnAbortByLanguageEnabled();

	// Set whether we should retry transactions.
	UE_AUTORTFM_API void SetRetryTransaction(EAutoRTFMRetryTransactionState State);

	// Returns whether we should retry transactions.
	UE_AUTORTFM_API EAutoRTFMRetryTransactionState GetRetryTransaction();

	// Returns true if we should retry non-nested transactions.
	UE_AUTORTFM_API bool ShouldRetryNonNestedTransactions();

	// Returns true if we should also retry nested transactions.
	UE_AUTORTFM_API bool ShouldRetryNestedTransactionsToo();

	// Manually create a new transaction from open code and push it as a transaction nest.
	// Can only be called within an already active parent transaction (EG. this cannot start
	// a transaction nest itself).
	static UE_AUTORTFM_FORCEINLINE bool StartTransaction()
	{
		return autortfm_start_transaction();
	}
	
	// Manually commit the top transaction nest, popping it from the execution scope.
	// Can only be called within an already active parent transaction (EG. this cannot end
	// a transaction nest itself).
	static UE_AUTORTFM_FORCEINLINE ETransactionResult CommitTransaction()
	{
		return static_cast<ETransactionResult>(autortfm_commit_transaction());
	}

	// Manually clear the status of a user abort from the top transaction in a nest.
	static UE_AUTORTFM_FORCEINLINE void ClearTransactionStatus()
	{
		autortfm_clear_transaction_status();
	}

	// Register a transactional version of a function that wasn't compiled by the
	// autortfm compiler. Normally, code is transactionalized by the compiler by
	// emitting a clone that has transactional openation, with some magic to
	// redirect all function calls within a transaction to the transactional clone.
	// This allows you to hook in your own transactionalized implementations of
	// functions that the compiler did not see.
	//
	// Use with great caution!
	//
	// This results in calls to ClosedVariant to happen in open mode. We will call
	// ClosedVariant's nontransactional version within the transaction. This happens
	// with the additional caveat that the function signatures must match.
	static UE_AUTORTFM_FORCEINLINE void RegisterOpenFunction(void* const OpenFunction, void* const ClosedVariant)
	{
		autortfm_register_open_function(OpenFunction, ClosedVariant);
	}

	struct FRegisterOpenFunction
	{
		FRegisterOpenFunction(void* const OriginalFunction, void* const NewFunction)
		{
			RegisterOpenFunction(OriginalFunction, NewFunction);
		}
	};

	// Reserved for future.
	static UE_AUTORTFM_FORCEINLINE void RecordOpenRead(void const*, size_t) {}

	// Reserved for future.
	template<typename TTYPE> static UE_AUTORTFM_FORCEINLINE void RecordOpenRead(TTYPE*) {}

	// WriteMemory first records the memory span as written (see RecordOpenWrite) and then copies the specified value into it.
	static UE_AUTORTFM_FORCEINLINE void WriteMemory(void* DestPtr, void const* SrcPtr, size_t Size)
	{
		RecordOpenWrite(DestPtr, Size);
		UE_AUTORTFM_MEMCPY(DestPtr, SrcPtr, Size);
	}

	// WriteMemory first records the memory span as written (see RecordOpenWrite) and then copies the specified value into it.
	template<typename TTYPE> static UE_AUTORTFM_FORCEINLINE void WriteMemory(TTYPE* DestPtr, TTYPE const* SrcPtr)
	{
		if constexpr (std::is_trivially_copyable<TTYPE>::value)
		{
			RecordOpenWrite(DestPtr, sizeof(TTYPE));
			*DestPtr = *SrcPtr;
		}
		else
		{
			WriteMemory(DestPtr, SrcPtr, sizeof(TTYPE));
		}
	}

	// WriteMemory first records the memory span as written (see RecordOpenWrite) and then copies the specified value into it.
	template<typename TTYPE> static UE_AUTORTFM_FORCEINLINE void WriteMemory(TTYPE* DestPtr, TTYPE const SrcValue)
	{
		if constexpr (std::is_trivially_copyable<TTYPE>::value)
		{
			RecordOpenWrite(DestPtr, sizeof(TTYPE));
			*DestPtr = SrcValue;
		}
		else
		{
			WriteMemory(DestPtr, &SrcValue, sizeof(TTYPE));
		}
	}

	// If running in a transaction, then perform a consistency check of the
	// transaction's read-write set. If possible, this compares the read-write set's
	// expected values with the actual values in global memory. Does nothing when
	// called outside of a transaction. May do nothing if debugging features aren't
	// enabled in the autortfm runtime.
	static UE_AUTORTFM_FORCEINLINE void CheckConsistencyAssumingNoRaces()
	{
		autortfm_check_consistency_assuming_no_races();
	}

} // namespace ForTheRuntime

} // namespace AutoRTFM

// Macro-based variants so we completely compile away when not in use, even in debug builds
#if UE_AUTORTFM

namespace AutoRTFM::Private
{
	struct FOpenHelper
	{
		template<typename FunctorType>
		void operator+(FunctorType F)
		{
			AutoRTFM::Open(UE_AUTORTFM_MOVE(F));
		}
	};
	struct FOnAbortHelper
	{
		template<typename FunctorType>
		void operator+(FunctorType F)
		{
			AutoRTFM::OnAbort(UE_AUTORTFM_MOVE(F));
		}
	};
	struct FOnCommitHelper
	{
		template<typename FunctorType>
		void operator+(FunctorType F)
		{
			AutoRTFM::OnCommit(UE_AUTORTFM_MOVE(F));
		}
	};
	struct FTransactHelper
	{
		template<typename FunctorType>
		void operator+(FunctorType F)
		{
			AutoRTFM::Transact(UE_AUTORTFM_MOVE(F));
		}
	};
} // namespace AutoRTFM::Private

#if defined(__clang__) && __has_warning("-Wdeprecated-this-capture")
#define UE_AUTORTFM_BEGIN_DISABLE_WARNINGS _Pragma("clang diagnostic push") _Pragma("clang diagnostic ignored \"-Wdeprecated-this-capture\"")
#define UE_AUTORTFM_END_DISABLE_WARNINGS _Pragma("clang diagnostic pop")
#else
#define UE_AUTORTFM_BEGIN_DISABLE_WARNINGS
#define UE_AUTORTFM_END_DISABLE_WARNINGS
#endif

#define UE_AUTORTFM_OPEN_IMPL          ::AutoRTFM::Private::FOpenHelper{} + [&]()
#define UE_AUTORTFM_ONABORT_IMPL(...)  ::AutoRTFM::Private::FOnAbortHelper{} + [__VA_ARGS__]()
#define UE_AUTORTFM_ONCOMMIT_IMPL(...) ::AutoRTFM::Private::FOnCommitHelper{} + [__VA_ARGS__]()
#define UE_AUTORTFM_TRANSACT_IMPL      ::AutoRTFM::Private::FTransactHelper{} + [&]() { __VA_ARGS__ })
#else

// Do nothing, these should be followed by blocks that should be either executed or not executed
#define UE_AUTORTFM_OPEN_IMPL
#define UE_AUTORTFM_ONABORT_IMPL(...) while (false)
#define UE_AUTORTFM_ONCOMMIT_IMPL(...)
#define UE_AUTORTFM_TRANSACT_IMPL
#endif

// Runs a block of code in the open, non-transactionally. Anything performed in the open will not be undone if a transaction fails.
// Calls should be written like this: UE_AUTORTFM_OPEN { ... code ... };
#define UE_AUTORTFM_OPEN  UE_AUTORTFM_OPEN_IMPL
#define UE_AUTORTFM_OPEN2 UE_AUTORTFM_OPEN_IMPL

// Runs a block of code if a transaction aborts.
// In non-transactional code paths the block of code will not be executed at all.
// The macro arguments are the capture specification for the lambda.
// Calls should be written like this: UE_AUTORTFM_ONABORT(=) { ... code ... };
#define UE_AUTORTFM_ONABORT(...)  UE_AUTORTFM_ONABORT_IMPL(__VA_ARGS__)
#define UE_AUTORTFM_ONABORT2(...) UE_AUTORTFM_ONABORT_IMPL(__VA_ARGS__)

// Runs a block of code if a transaction commits successfully.
// In non-transactional code paths the block of code will be executed immediately.
// The macro arguments are the capture specification for the lambda.
// Calls should be written like this: UE_AUTORTFM_ONCOMMIT(=) { ... code ... };
#define UE_AUTORTFM_ONCOMMIT(...)  UE_AUTORTFM_ONCOMMIT_IMPL(__VA_ARGS__)
#define UE_AUTORTFM_ONCOMMIT2(...) UE_AUTORTFM_ONCOMMIT_IMPL(__VA_ARGS__)

// Runs a block of code in the closed, transactionally, within a new transaction.
// Calls should be written like this: UE_AUTORTFM_TRANSACT { ... code ... };
#define UE_AUTORTFM_TRANSACT  UE_AUTORTFM_TRANSACT_IMPL
#define UE_AUTORTFM_TRANSACT2 UE_AUTORTFM_TRANSACT_IMPL

#define UE_AUTORTFM_CONCAT_IMPL(A, B) A ## B
#define UE_AUTORTFM_CONCAT(A, B) UE_AUTORTFM_CONCAT_IMPL(A, B)

#if UE_AUTORTFM
#define UE_AUTORTFM_REGISTER_OPEN_FUNCTION_EXPLICIT_IMPL(OriginalFunction, NewFunction) static const AutoRTFM::ForTheRuntime::FRegisterOpenFunction UE_AUTORTFM_CONCAT(AutoRTFMFunctionRegistration, __COUNTER__)(reinterpret_cast<void*>(OriginalFunction), reinterpret_cast<void*>(NewFunction))
#define UE_AUTORTFM_REGISTER_OPEN_FUNCTION_IMPL(OriginalFunction) UE_AUTORTFM_REGISTER_OPEN_FUNCTION_EXPLICIT(OriginalFunction, RTFM_ ## OriginalFunction)
#define UE_AUTORTFM_REGISTER_SELF_FUNCTION_IMPL(OriginalFunction) UE_AUTORTFM_REGISTER_OPEN_FUNCTION_EXPLICIT(OriginalFunction, OriginalFunction)
#else
#define UE_AUTORTFM_REGISTER_OPEN_FUNCTION_EXPLICIT_IMPL(OriginalFunction, NewFunction)
#define UE_AUTORTFM_REGISTER_OPEN_FUNCTION_IMPL(OriginalFunction)
#define UE_AUTORTFM_REGISTER_SELF_FUNCTION_IMPL(OriginalFunction)
#endif

// Register that a specific OpenFunction maps to a closed variant when called in closed code.
#define UE_AUTORTFM_REGISTER_OPEN_FUNCTION_EXPLICIT(OpenFunction, ClosedVariant) UE_AUTORTFM_REGISTER_OPEN_FUNCTION_EXPLICIT_IMPL(OpenFunction, ClosedVariant)

// Tells the runtime that OpenFunction maps to an explicit closed variant with the "RTFM_" prefix.
#define UE_AUTORTFM_REGISTER_OPEN_FUNCTION(OpenFunction) UE_AUTORTFM_REGISTER_OPEN_FUNCTION_IMPL(OpenFunction)

// Tells the runtime that OpenFunction maps to itself in closed code (EG. it has no transactional semantics).
#define UE_AUTORTFM_REGISTER_SELF_FUNCTION(OpenFunction) UE_AUTORTFM_REGISTER_SELF_FUNCTION_IMPL(OpenFunction)

#endif // __cplusplus
