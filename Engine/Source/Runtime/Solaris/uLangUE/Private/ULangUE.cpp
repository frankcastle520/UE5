// Copyright Epic Games, Inc. All Rights Reserved.

#include "ULangUE.h"

#include <cstdio>
#include "CoreGlobals.h"
#include "Misc/OutputDeviceRedirector.h"
#include "SolarisLogging.h"
#include "uLang/Common/Common.h"
#include "uLang/Common/Text/UTF8String.h"

DEFINE_LOG_CATEGORY(LogSolaris);

/* uLangUE_Impl
 *****************************************************************************/

namespace uLangUE_Impl
{
	size_t NumActiveInitializations{0};

	/// Function that is called by uLang when an assert fails
	uLang::EErrorAction AssertFailed(uLang::EAssertSeverity Severity, const ANSICHAR* ExprText, const ANSICHAR* FileText, int32_t Line, const ANSICHAR* Format, ...)
	{
		va_list Args;
		va_start(Args, Format);
		uLang::CUTF8String Message(Format, Args);
		va_end(Args);

#if DO_CHECK || DO_GUARD_SLOW || DO_ENSURE
		FDebug::EnsureFailed(ExprText, FileText, Line, nullptr, UTF8_TO_TCHAR(*Message));
#else
		// Use Error instead of Fatal so we don't terminate the process. This unfortunately won't
		// report the failure to CR, but at least for Severity=Fatal, the caller will terminate the
		// process, and this will have printed the message to the log.
		UE_LOG(LogSolaris, Error, TEXT("%s"), UTF8_TO_TCHAR(*Message));
#endif

		return (Severity == uLang::EAssertSeverity::Fatal || FPlatformMisc::IsDebuggerPresent())
			? uLang::EErrorAction::Break
			: uLang::EErrorAction::Continue;
	}
	constexpr uLang::SSystemParams::FAssert AssertFailedFn = &AssertFailed;

	void LogMessage(uLang::ELogVerbosity Verbosity, const char* Format, ...)
	{
#if !NO_LOGGING
		va_list Args;
		va_start(Args, Format);
		uLang::CUTF8String Message(Format, Args);
		va_end(Args);

		// Only print if the level is less than the global verbosity
		if (uLang::GetSystemParams()._Verbosity < Verbosity)
		{
			return;
		}

		ELogVerbosity::Type UEVerbosity;
		switch (Verbosity)
		{
		case uLang::ELogVerbosity::Error:          UEVerbosity = ELogVerbosity::Error; break;
		case uLang::ELogVerbosity::Warning:        UEVerbosity = ELogVerbosity::Warning; break;
		case uLang::ELogVerbosity::Log:            UEVerbosity = ELogVerbosity::Log; break;
		case uLang::ELogVerbosity::Verbose:        UEVerbosity = ELogVerbosity::Verbose; break;
		default:                                   UEVerbosity = ELogVerbosity::Display; break;
		}

		// Print to console
		if (Verbosity <= uLang::ELogVerbosity::Verbose)
		{
			FILE* Out = Verbosity <= uLang::ELogVerbosity::Warning ? stderr : stdout;
			::fprintf(Out, "%s\n", *Message);
		}

		// Also send to log
		FString MessageW(*Message);
		GLog->Serialize(*MessageW, UEVerbosity, LogSolaris.GetCategoryName());
#endif
	}
}

void uLangUE::Initialize()
{
	// Only initialize uLang if it was previously uninitialized or deinitialized.
	const size_t PreviousNumActiveInitializations = uLangUE_Impl::NumActiveInitializations++;
	if (!PreviousNumActiveInitializations)
	{
		// Initialize uLang core
		uLang::SSystemParams SystemParams = {
			ULANG_API_VERSION,
			[](size_t NumBytes) -> void* { return FMemory::Malloc(NumBytes); },
			[](void* Memory, size_t NumBytes) -> void* { return FMemory::Realloc(Memory, NumBytes); },
			&FMemory::Free,
			uLangUE_Impl::AssertFailedFn,
			&uLangUE_Impl::LogMessage,
		};

		uLang::Initialize(SystemParams);
	}
}

void uLangUE::DeInitialize()
{
	// Only deinitialize uLang if this was the last active initialization.
	const size_t PreviousNumActiveInitializations = uLangUE_Impl::NumActiveInitializations--;
	ULANG_ASSERT(PreviousNumActiveInitializations > 0);
	if (PreviousNumActiveInitializations == 1)
	{
		uLang::DeInitialize();
	}
}