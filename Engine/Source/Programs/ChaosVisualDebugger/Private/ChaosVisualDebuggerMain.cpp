// Copyright Epic Games, Inc. All Rights Reserved.

#include "ChaosVisualDebuggerMain.h"

#include "RequiredProgramMainCPPInclude.h"
#include "EditorViewportClient.h"
#include "LaunchEngineLoop.h"
#include "HAL/PlatformSplash.h"

IMPLEMENT_APPLICATION(ChaosVisualDebugger, "ChaosVisualDebugger");

DEFINE_LOG_CATEGORY_STATIC(LogChaosVisualDebugger, Log, All);

// Opt in to new D3D12 redist and tell the loader where to search for D3D12Core.dll.
// The D3D loader looks for these symbol exports in the .exe module.
// We only support this on x64 Windows Desktop platforms. Other platforms or non-redist-aware 
// versions of Windows will transparently load default OS-provided D3D12 library.
#if USE_D3D12_REDIST
extern "C" { _declspec(dllexport) extern const UINT D3D12SDKVersion = 614; } // D3D12_SDK_VERSION
extern "C" { _declspec(dllexport) extern const char* D3D12SDKPath = ".\\D3D12\\"; }
#endif // USE_D3D12_REDIST

int32 RunChaosVisualDebugger(const TCHAR* CommandLine)
{
	FTaskTagScope Scope(ETaskTag::EGameThread);

#if !(UE_BUILD_SHIPPING)

	// If "-waitforattach" or "-WaitForDebugger" was specified, halt startup and wait for a debugger to attach before continuing
	if (FParse::Param(CommandLine, TEXT("waitforattach")) || FParse::Param(CommandLine, TEXT("WaitForDebugger")))
	{
		while (!FPlatformMisc::IsDebuggerPresent())
		{
			FPlatformProcess::Sleep(0.1f);
		}
		UE_DEBUG_BREAK();
	}

#endif

	// Override the stack size for the thread pool.
	FQueuedThreadPool::OverrideStackSize = 256 * 1024;

	const FText AppName = NSLOCTEXT("ChaosVisualDebugger", "ChaosVisualDebuggerSplashText", "Chaos Visual Debugger");
	FPlatformSplash::SetSplashText(SplashTextType::GameName, *AppName.ToString());

	FString Command;
	const bool bIsRunningCommand = FParse::Value(CommandLine, TEXT("-RUN="), Command);

	const FString CommandLineString = CommandLine;
	const FString FinalCommandLine = bIsRunningCommand ? CommandLine : CommandLineString + TEXT(" EDITOR");

	// start up the main loop
	const int32 Result = GEngineLoop.PreInit(*FinalCommandLine);

	if (Result != 0)
	{
		UE_LOG(LogChaosVisualDebugger, Error, TEXT("EngineLoop PreInit failed!"));
		return Result;
	}

	if (!bIsRunningCommand)
	{
		// Register navigation commands for all viewports
		FViewportNavigationCommands::Register();

		GEngineLoop.Init();

		// Hide the splash screen now that everything is ready to go
		FPlatformSplash::Hide();

		while (!IsEngineExitRequested())
		{
			GEngineLoop.Tick();
		}
	}

	// Make sure all rendering thread work is done before we attempt to exit the program
	FFrameEndSync::Sync(true);

	GEngineLoop.Exit();

	return Result;
}