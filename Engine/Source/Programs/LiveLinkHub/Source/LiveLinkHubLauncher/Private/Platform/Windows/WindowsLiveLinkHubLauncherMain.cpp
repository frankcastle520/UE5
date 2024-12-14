// Copyright Epic Games, Inc. All Rights Reserved.

#include "CoreMinimal.h"
#include "LiveLinkHubRun.h"
#include "HAL/ExceptionHandling.h"
#include "LaunchEngineLoop.h"
#include "Windows/WindowsHWrapper.h"
#include "Misc/CommandLine.h"
#include "Misc/OutputDeviceError.h"


// Opt in to new D3D12 redist and tell the loader where to search for D3D12Core.dll.
// The D3D loader looks for these symbol exports in the .exe module.
// We only support this on x64 Windows Desktop platforms. Other platforms or non-redist-aware 
// versions of Windows will transparently load default OS-provided D3D12 library.
#define USE_D3D12_REDIST (PLATFORM_DESKTOP && PLATFORM_CPU_X86_FAMILY && PLATFORM_64BITS && 1)
#if USE_D3D12_REDIST
extern "C" { _declspec(dllexport) extern const UINT D3D12SDKVersion = 614; } // D3D12_SDK_VERSION
extern "C" { _declspec(dllexport) extern const char* D3D12SDKPath = ".\\D3D12\\"; }
#endif // USE_D3D12_REDIST


/**
 * The main application entry point for Windows platforms.
 *
 * @param hInInstance Handle to the current instance of the application.
 * @param hPrevInstance Handle to the previous instance of the application (always NULL).
 * @param lpCmdLine Command line for the application.
 * @param nShowCmd Specifies how the window is to be shown.
 * @return Application's exit value.
 */
int32 WINAPI WinMain(_In_ HINSTANCE hInInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ char* lpCmdLine, _In_ int32 nShowCmd)
{
	hInstance = hInInstance;

	const TCHAR* CmdLine = ::GetCommandLineW();
	CmdLine = FCommandLine::RemoveExeName(CmdLine);

#if !UE_BUILD_SHIPPING
	if (FParse::Param(CmdLine, TEXT("crashreports")))
	{
		GAlwaysReportCrash = true;
	}
#endif

	int32 ErrorLevel = 0;

#if UE_BUILD_DEBUG
	if (!GAlwaysReportCrash)
#else
	if (FPlatformMisc::IsDebuggerPresent() && !GAlwaysReportCrash)
#endif
	{
		ErrorLevel = RunLiveLinkHub(CmdLine);
	}
	else
	{
#if !PLATFORM_SEH_EXCEPTIONS_DISABLED
		__try
#endif
 		{
			GIsGuarded = 1;
			ErrorLevel = RunLiveLinkHub(CmdLine);
			GIsGuarded = 0;
		}
#if !PLATFORM_SEH_EXCEPTIONS_DISABLED
		__except (ReportCrash(GetExceptionInformation()))
		{
			ErrorLevel = 1;
			GError->HandleError();
			FPlatformMisc::RequestExit(true);
		}
#endif
	}

	FEngineLoop::AppExit();

	return ErrorLevel;
}
