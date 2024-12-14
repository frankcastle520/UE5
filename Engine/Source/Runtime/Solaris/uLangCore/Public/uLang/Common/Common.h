// Copyright Epic Games, Inc. All Rights Reserved.
// Base Declarations

#pragma once

#include <stdlib.h>  // size_t etc.
#include <stdint.h>  // Sized integer types, minimums and maximums: int##_t, uint##_t, INT##_MAX, UINT##_MAX
#include <new>       // placement new

//------------------------------------------------------------------
// Sanity check platform defines
#ifndef ULANG_PLATFORM
#error ULANG_PLATFORM must be defined.
#endif

#ifndef ULANG_PLATFORM_WINDOWS
// Windows requires special handling for now...
#define ULANG_PLATFORM_WINDOWS 0
#endif

#if !defined(ULANG_PLATFORM_HOLOLENS)
#define ULANG_PLATFORM_HOLOLENS 0
#endif

#if !defined(ULANG_PLATFORM_MAC)
#define ULANG_PLATFORM_MAC 0
#endif

#if !defined(ULANG_PLATFORM_LINUX)
#define ULANG_PLATFORM_LINUX 0
#endif

#define ULANG_PLATFORM_POSIX (ULANG_PLATFORM_MAC || ULANG_PLATFORM_LINUX)

#ifndef ULANG_PLATFORM_EXTENSION
#define ULANG_PLATFORM_EXTENSION 0
#endif

//------------------------------------------------------------------
// Define macros to help with the inclusion of platform specific headers
#define ULANG_STRINGIFY(x) ULANG_STRINGIFY_INNER(x)
#define ULANG_STRINGIFY_INNER(x) #x

#define ULANG_PREPROCESSOR_JOIN(x, y) ULANG_PREPROCESSOR_JOIN_INNER(x, y)
#define ULANG_PREPROCESSOR_JOIN_INNER(x, y) x##y

// Update SNDBS.cs if this macro changes
#if ULANG_PLATFORM_EXTENSION
#define ULANG_PLATFORM_HEADER_NAME(Suffix) ULANG_STRINGIFY(ULANG_PREPROCESSOR_JOIN(ULANG_PLATFORM, Suffix))
#else
#define ULANG_PLATFORM_HEADER_NAME(Suffix) ULANG_STRINGIFY(ULANG_PREPROCESSOR_JOIN(ULANG_PLATFORM/ULANG_PLATFORM, Suffix))
#endif

// Update SNDBS.cs if this macro changes
#if ULANG_PLATFORM_EXTENSION
#define ULANG_PLATFORM_HEADER_NAME_WITH_PREFIX(Prefix, Suffix) ULANG_STRINGIFY(Prefix/ULANG_PREPROCESSOR_JOIN(ULANG_PLATFORM, Suffix))
#else
#define ULANG_PLATFORM_HEADER_NAME_WITH_PREFIX(Prefix, Suffix) ULANG_STRINGIFY(Prefix/ULANG_PLATFORM/ULANG_PREPROCESSOR_JOIN(ULANG_PLATFORM, Suffix))
#endif

//------------------------------------------------------------------
// Include platform-specific header
#include ULANG_PLATFORM_HEADER_NAME_WITH_PREFIX(uLang/Common/Platform,Common.h)

//------------------------------------------------------------------
/// To prevent API mismatch in dynamic linking situations
#define ULANG_API_VERSION 2

//------------------------------------------------------------------
// DLL API

#ifndef DLLIMPORT
    #define DLLIMPORT ULANG_DLLIMPORT
    #define DLLEXPORT ULANG_DLLEXPORT
#endif

#ifndef DLLIMPORT_VTABLE
    #define DLLIMPORT_VTABLE ULANG_DLLIMPORT_VTABLE
    #define DLLEXPORT_VTABLE ULANG_DLLEXPORT_VTABLE
#endif

//------------------------------------------------------------------
// Warnings

#if defined(__GNUC__) || defined(__clang__) // Check __clang__ first, since Clang on Windows also defines _MSC_VER
    #define ULANG_THIRD_PARTY_INCLUDES_START \
        _Pragma("GCC diagnostic push") \
        _Pragma("GCC diagnostic ignored \"-Wundef\"") \
        _Pragma("GCC diagnostic ignored \"-Wswitch-default\"") \
        _Pragma("GCC diagnostic ignored \"-Wswitch-enum\"")
        _Pragma("GCC diagnostic ignored \"-Wenum-compare\"")
    #define ULANG_THIRD_PARTY_INCLUDES_END \
        _Pragma("GCC diagnostic pop")

    _Pragma("GCC diagnostic ignored \"-Wunused-function\"")

#elif defined(_MSC_VER)
    #pragma warning(disable: 4100) // 'identifier' : unreferenced formal parameter                                                      https://docs.microsoft.com/en-us/cpp/error-messages/compiler-warnings/compiler-warning-level-4-c4100
    #pragma warning(disable: 4127) // Conditional expression is constant                                                                https://docs.microsoft.com/en-us/cpp/error-messages/compiler-warnings/compiler-warning-level-4-c4127
    #pragma warning(disable: 4251) // 'type' needs to have dll-interface to be used by clients of 'type'                                https://docs.microsoft.com/en-us/cpp/error-messages/compiler-warnings/compiler-warning-level-1-c4251
    #pragma warning(disable: 4275) // non - DLL-interface classkey 'identifier' used as base for DLL-interface classkey 'identifier'    https://docs.microsoft.com/en-us/cpp/error-messages/compiler-warnings/compiler-warning-level-2-c4275
    #pragma warning(disable: 6255) // _alloca indicates failure by raising a stack overflow exception. Consider using _malloca instead.
    #pragma warning(disable: 6326) // Potential comparison of a constant with another constant.

    // Disable warnings that would otherwise occur in third party includes such as LLVM
    #define ULANG_THIRD_PARTY_INCLUDES_START \
        __pragma(warning(push)) \
        __pragma(warning(disable: 4141 4189 4244 4245 4267 4324 4458 4624 4668 6313 4389 5054))
    #define ULANG_THIRD_PARTY_INCLUDES_END \
        __pragma(warning(pop))
#else
    #define ULANG_THIRD_PARTY_INCLUDES_START
    #define ULANG_THIRD_PARTY_INCLUDES_END
#endif

#if defined(__clang__)
    #define ULANG_IGNORE_CLASS_MEMACCESS_WARNING_START \
        _Pragma("GCC diagnostic push") \
        _Pragma("GCC diagnostic ignored \"-Wuninitialized\"") \
        _Pragma("GCC diagnostic ignored \"-Wdynamic-class-memaccess\"")
    #define ULANG_IGNORE_CLASS_MEMACCESS_WARNING_END \
        _Pragma("GCC diagnostic pop")
#elif defined(__GNUC__)
    #define ULANG_IGNORE_CLASS_MEMACCESS_WARNING_START \
        _Pragma("GCC diagnostic push") \
        _Pragma("GCC diagnostic ignored \"-Wuninitialized\"") \
        _Pragma("GCC diagnostic ignored \"-Wmaybe-uninitialized\"") \
        _Pragma("GCC diagnostic ignored \"-Wclass-memaccess\"")
    #define ULANG_IGNORE_CLASS_MEMACCESS_WARNING_END \
        _Pragma("GCC diagnostic pop")
#else
    #define ULANG_IGNORE_CLASS_MEMACCESS_WARNING_START
    #define ULANG_IGNORE_CLASS_MEMACCESS_WARNING_END
#endif

#ifndef ULANG_SILENCE_SECURITY_WARNING_START
#define ULANG_SILENCE_SECURITY_WARNING_START
#endif
#ifndef ULANG_SILENCE_SECURITY_WARNING_END
#define ULANG_SILENCE_SECURITY_WARNING_END
#endif

//------------------------------------------------------------------
// Dynamic symbol visibility

#if defined(_MSC_VER)
    #define ULANG_DLLIMPORT __declspec(dllimport)
    #define ULANG_DLLEXPORT __declspec(dllexport)
    #define ULANG_DLLIMPORT_VTABLE
    #define ULANG_DLLEXPORT_VTABLE
#elif defined(__clang__)
    #if ULANG_PLATFORM_POSIX
        #define ULANG_DLLIMPORT __attribute__((visibility("default")))
        #define ULANG_DLLEXPORT __attribute__((visibility("default")))
    #else
        #define ULANG_DLLIMPORT __declspec(dllimport)
        #define ULANG_DLLEXPORT __declspec(dllexport)
    #endif
    #define ULANG_DLLIMPORT_VTABLE __attribute__((__type_visibility__("default")))
    #define ULANG_DLLEXPORT_VTABLE __attribute__((__type_visibility__("default")))
#elif defined(__GNUC__)
    #define ULANG_DLLIMPORT __attribute__((visibility("default")))
    #define ULANG_DLLEXPORT __attribute__((visibility("default")))
    #define ULANG_DLLIMPORT_VTABLE __attribute__((visibility("default")))
    #define ULANG_DLLEXPORT_VTABLE __attribute__((visibility("default")))
#else
    #define ULANG_DLLIMPORT
    #define ULANG_DLLEXPORT
    #define ULANG_DLLIMPORT_VTABLE
    #define ULANG_DLLEXPORT_VTABLE
#endif

//------------------------------------------------------------------
// Alignment

#if defined(__clang__) || defined(__GNUC__)
    #define ULANG_GCC_PACK(n) __attribute__((packed,aligned(n)))
    #define ULANG_GCC_ALIGN(n) __attribute__((aligned(n)))
#else
    #define ULANG_GCC_PACK(n)
    #define ULANG_GCC_ALIGN(n)
#endif
#if defined(_MSC_VER)
    #define ULANG_MS_ALIGN(n) __declspec(align(n))
#else
    #define ULANG_MS_ALIGN(n)
#endif

//------------------------------------------------------------------
// Inlining

#define ULANG_FORCEINLINE inline
#define ULANG_FORCENOINLINE

#if 0
#if defined(_MSC_VER)
    #define ULANG_FORCEINLINE __forceinline
    #define ULANG_FORCENOINLINE __declspec(noinline)
#elif defined(__clang__) || defined(__GNUC__)
    #define ULANG_FORCEINLINE inline __attribute__ ((always_inline))
    #define ULANG_FORCENOINLINE __attribute__((noinline))
#else
    #define ULANG_FORCEINLINE inline
    #define ULANG_FORCENOINLINE
#endif
#endif

//------------------------------------------------------------------
// Restrict

#define ULANG_RESTRICT __restrict

//------------------------------------------------------------------
// Likely

#if defined(__clang__) || defined(__GNUC__)
    #define ULANG_LIKELY(x) __builtin_expect(!!(x), 1)
    #define ULANG_UNLIKELY(x) __builtin_expect(!!(x), 0)
#else
    #define ULANG_LIKELY(x) (x)
    #define ULANG_UNLIKELY(x) (x)
#endif

//------------------------------------------------------------------
// Array count

#if defined(_MSC_VER) && (_MSC_VER >= 1400)
    #define ULANG_COUNTOF(x) _countof(x)
#else
    #define ULANG_COUNTOF(x) (sizeof(x)/sizeof(x[0]))
#endif

//------------------------------------------------------------------
// Switch case fall-through warning suppression.

#if __cplusplus >= 201703L
    #define ULANG_FALLTHROUGH [[fallthrough]]
#elif defined(__GNUC__) && __GNUC__ >= 7 && !defined(__clang__)
    #define ULANG_FALLTHROUGH __attribute__((fallthrough))
#else
    #define ULANG_FALLTHROUGH
#endif

//------------------------------------------------------------------
// Mark a code path that is unreachable.

#define ULANG_UNREACHABLE() while(true) { ULANG_BREAK(); }

//------------------------------------------------------------------
// Static analysis

#if defined( __clang_analyzer__ )
    // A fake function marked with noreturn that acts as a marker for CA_ASSUME to ensure the
    // static analyzer doesn't take an analysis path that is assumed not to be navigable.
    void ULANG_CA_AssumeNoReturn() __attribute__((analyzer_noreturn));

    #define ULANG_CA_ASSUME(Expr)  (__builtin_expect(!bool(Expr), 0) ? ULANG_CA_AssumeNoReturn() : (void)0)
#elif defined( _PREFAST_ ) || defined( PVS_STUDIO )
    #define ULANG_CA_ASSUME(Expr) __analysis_assume( !!( Expr ) )
#else
    #define ULANG_CA_ASSUME(Expr) ((void)Expr)
#endif

//------------------------------------------------------------------
// Asserts

// ULANG_ERRORF, ULANG_ASSERTF: Check for FATAL error conditions - conditional expression is KEPT in release builds.
// ULANG_VERIFYF: Check for RECOVERABLE error conditions - conditional expression is KEPT in release builds.
// ULANG_ENSUREF: Check for RECOVERABLE error conditions - conditional expression is KEPT in release builds.

#if ULANG_DO_CHECK
    #define ULANG_ASSERT(expr) { if (ULANG_UNLIKELY((bool)!(expr))) { if ((*::uLang::GetSystemParams()._AssertFailed)(::uLang::EAssertSeverity::Fatal, #expr, __FILE__, __LINE__, "") == ::uLang::EErrorAction::Break) { ULANG_BREAK(); } ULANG_CA_ASSUME(false); } }
    #define ULANG_VERIFY(expr) { if (ULANG_UNLIKELY((bool)!(expr)) && (*::uLang::GetSystemParams()._AssertFailed)(::uLang::EAssertSeverity::Recoverable, #expr, __FILE__, __LINE__, "") == ::uLang::EErrorAction::Break) ULANG_BREAK(); }
    #define ULANG_ENSURE(expr) ( ULANG_LIKELY(!!(expr)) || (\
        ( ((*::uLang::GetSystemParams()._AssertFailed)(::uLang::EAssertSeverity::Recoverable, #expr, __FILE__, __LINE__, "") == ::uLang::EErrorAction::Break) && \
            ([] ()->bool { ULANG_BREAK(); return false; } ()) \
        ) \
    ) )

    #define ULANG_ERRORF(format, ...)        { if ((*::uLang::GetSystemParams()._AssertFailed)(::uLang::EAssertSeverity::Fatal, "", __FILE__, __LINE__, format, ##__VA_ARGS__) == ::uLang::EErrorAction::Break) ULANG_BREAK(); }
    #define ULANG_ASSERTF(expr, format, ...) { if (ULANG_UNLIKELY((bool)!(expr))) { if ((*::uLang::GetSystemParams()._AssertFailed)(::uLang::EAssertSeverity::Fatal, #expr, __FILE__, __LINE__, format, ##__VA_ARGS__) == ::uLang::EErrorAction::Break) { ULANG_BREAK(); } ULANG_CA_ASSUME(false); } }
    #define ULANG_VERIFYF(expr, format, ...) { if (ULANG_UNLIKELY((bool)!(expr)) && (*::uLang::GetSystemParams()._AssertFailed)(::uLang::EAssertSeverity::Recoverable, #expr, __FILE__, __LINE__, format, ##__VA_ARGS__) == ::uLang::EErrorAction::Break) ULANG_BREAK(); }
    #define ULANG_ENSUREF(expr, format, ...) ( ULANG_LIKELY(!!(expr)) || (\
        ( ((*::uLang::GetSystemParams()._AssertFailed)(::uLang::EAssertSeverity::Recoverable, #expr, __FILE__, __LINE__, format, ##__VA_ARGS__) == ::uLang::EErrorAction::Break) && \
            ([] ()->bool { ULANG_BREAK(); return false; } ()) \
        ) \
    ) )
#else
    #define ULANG_ASSERT(expr)  (void(sizeof(!!(expr))))
    #define ULANG_VERIFY(expr)  (void(sizeof(!!(expr))))
    #define ULANG_ENSURE(expr)  (!!(expr))

    #define ULANG_ERRORF(format, ...)         (void(0))
    #define ULANG_ASSERTF(expr, format, ...)  (void(sizeof(!!(expr))))
    #define ULANG_VERIFYF(expr, format, ...)  (void(sizeof(!!(expr))))
    #define ULANG_ENSUREF(expr, format, ...)  (!!(expr))
#endif

//------------------------------------------------------------------
// Logging

#define VERSE_SUPPRESS_UNUSED(_Variable) (void)(_Variable);

// Ensure either just enumerator (Error), fully qualified (ELogVerbosity::Error) or variable (MyVerbosity) work equally well.
#define USING_ELogVerbosity \
    constexpr uLang::ELogVerbosity Error   = uLang::ELogVerbosity::Error;   VERSE_SUPPRESS_UNUSED(Error) \
    constexpr uLang::ELogVerbosity Warning = uLang::ELogVerbosity::Warning; VERSE_SUPPRESS_UNUSED(Warning) \
    constexpr uLang::ELogVerbosity Display = uLang::ELogVerbosity::Display; VERSE_SUPPRESS_UNUSED(Display) \
    constexpr uLang::ELogVerbosity Log     = uLang::ELogVerbosity::Log;     VERSE_SUPPRESS_UNUSED(Log) \
    constexpr uLang::ELogVerbosity Verbose = uLang::ELogVerbosity::Verbose; VERSE_SUPPRESS_UNUSED(Verbose) \

#define ULANG_LOGF(verbosity, format, ...) \
    {   USING_ELogVerbosity \
        (*::uLang::GetSystemParams()._LogMessage)(verbosity, format, ##__VA_ARGS__); \
    }

//------------------------------------------------------------------
// Memory

#if !defined(ULANG_AGGRESSIVE_MEMORY_SAVING)
    #define ULANG_AGGRESSIVE_MEMORY_SAVING 0
#endif


namespace uLang
{

// Type of nullptr
using NullPtrType = std::nullptr_t;

// Language default sized storage types

using Integer = int64_t;  /// Default size for uLang Integer Type
using Float   = double;   /// Default size for uLang Float Type
using Boolean = bool;     /// Default size for uLang Boolean Type

inline const uint32_t uint32_invalid = UINT32_MAX;

/// Result returned from a visitor functor indicating how to continue or to quit early.
enum class EVisitResult : int8_t
{
    Continue      = 0,  // Continue to exhaustively iterate through all items
    SkipChildren  = 1,  // Skip iterating through any children/sub-items and continue though other items in hierarchy
    Stop          = 2   // Stop iterating through items and early exit
};

/// Iteration result returned from an iteration
enum class EIterateResult : int8_t
{
    Stopped   = 0,  // Iteration was terminated early by the visitor functor and wants the caller to exit early
    Completed = 1,  // Iteration is done iterating and wants the caller to continue iterating if there are more elements to iterate over
};

/// Generic error codes
enum class EResult : int8_t
{
    Unspecified = -1,   ///< Not sure if success or failure
    OK = 0,             ///< Success
    Error,              ///< Some error occurred
};

/// Generic action after error has occurred
enum class EErrorAction : int8_t
{
    Continue = 0,
    Break,
};

// Comparison result value
enum class EEquate : int8_t
{
    Less    = -1,
    Equal   = 0,
    Greater = 1
};


/// Enum used in constructors to indicate they should not initialize anything
enum ENoInit { NoInit };

/// Enum used to force default initialization
enum EDefaultInit { DefaultInit };

/// Used to signify an unspecified index
enum EIndex { IndexNone = -1 };

//------------------------------------------------------------------
// System Initialization

enum class EAssertSeverity : int8_t
{
    Fatal = 0,
    Recoverable
};

enum class ELogVerbosity : int8_t
{
    Error,           // Prints an error to console (and log file)
    Warning,         // Prints a warning to console (and log file)
    Display,         // Prints a message to console (and log file)
    Verbose,         // Prints a verbose message to console (and log file)
    Log,             // Prints a message to a log file (does not print to console)
};

/// Parameters to initialize the uLang module
struct SSystemParams
{
    using FMalloc  = void * (*)(size_t);
    using FRealloc = void * (*)(void *, size_t);
    using FFree    = void   (*)(void *);

    using FAssert = EErrorAction(*)(EAssertSeverity /* Severity */, const char * /* Expr */, const char * /* File */, int32_t /* Line */, const char * /* Format */, ...);
    using FLog = void(*)(ELogVerbosity /* Verbosity */, const char* /* Format */, ...);

    int      _APIVersion;   ///< Set this to ULANG_API_VERSION

    FMalloc  _HeapMalloc;   ///< Allocate system heap memory
    FRealloc _HeapRealloc;  ///< Reallocate system heap memory
    FFree    _HeapFree;     ///< Free system heap memory

    FAssert  _AssertFailed; ///< Called when an assert fails
    FLog     _LogMessage;   ///< Print a message

    ELogVerbosity _Verbosity = ELogVerbosity::Display;  ///< Won't print anything under the _Verbosity level

    SSystemParams(int APIVersion, FMalloc HeapMalloc, FRealloc HeapRealloc, FFree HeapFree, FAssert AssertFailed, FLog LogMessage = nullptr)
        : _APIVersion(APIVersion), _HeapMalloc(HeapMalloc), _HeapRealloc(HeapRealloc), _HeapFree(HeapFree), _AssertFailed(AssertFailed), _LogMessage(LogMessage)
    {
    }
};
ULANGCORE_API bool operator==(const SSystemParams& Lhs, const SSystemParams& Rhs);

/// Global variable for efficient access
ULANGCORE_API SSystemParams& GetSystemParams();

//------------------------------------------------------------------
// Memory management

/// Global variable for efficient access
class CAllocatorInstance;
extern ULANGCORE_API const CAllocatorInstance GSystemAllocatorInstance;

/**
 * Initialize the uLang Module
 *
 * @param Params Parameters to initialize the uLang module
 * @return EResult indicating outcome of operation
 */
ULANGCORE_API EResult Initialize(const SSystemParams & Params);

/**
 * Utility function for uLang modules to verify against.
 */
ULANGCORE_API bool IsInitialized();

/**
 * Deinitialize the uLang Module
 *
 * @return EResult indicating outcome of operation
 */
ULANGCORE_API EResult DeInitialize();

/**
 * Setter function for the global verbosity level in SSystemParams
 */
ULANGCORE_API void SetGlobalVerbosity(const uLang::ELogVerbosity GlobalVerbosity);

}
