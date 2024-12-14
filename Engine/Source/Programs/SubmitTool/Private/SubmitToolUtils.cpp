// Copyright Epic Games, Inc. All Rights Reserved.

#include "SubmitToolUtils.h"

#include "Misc/Paths.h"
#include "Logging/SubmitToolLog.h"
#include "Widgets/SWindow.h"
#include "Framework/Application/SlateApplication.h"

#if PLATFORM_WINDOWS
#include "Windows/AllowWindowsPlatformTypes.h"
#include <ShlObj.h>
#include "Windows/HideWindowsPlatformTypes.h"
#endif

#define LOCTEXT_NAMESPACE "FSubmitToolUtils"

FString FSubmitToolUtils::GetLocalAppDataPath()
{
#if PLATFORM_WINDOWS
	const FString LocalAppData = FPlatformMisc::GetEnvironmentVariable(TEXT("LOCALAPPDATA"));
#elif PLATFORM_MAC
	const FString LocalAppData = FPaths::Combine(FPlatformMisc::GetEnvironmentVariable(TEXT("HOME")), TEXT("Library"), TEXT("Application Support"));
#elif PLATFORM_LINUX
	const FString LocalAppData = FPaths::Combine(FPlatformMisc::GetEnvironmentVariable(TEXT("HOME")), TEXT(".local"), TEXT("share"));
#else
	static_assert(false);
#endif

	return LocalAppData;
}

void FSubmitToolUtils::CopyDiagnosticFilesToClipboard(TConstArrayView<FString> Files)
{
#if PLATFORM_WINDOWS
	if (OpenClipboard(GetActiveWindow()))
	{
		verify(EmptyClipboard());
		HGLOBAL GlobalMem;
		SIZE_T RequiredSize = sizeof(DROPFILES) + sizeof(TCHAR);
		for (const FString& File : Files)
		{
			RequiredSize += (File.Len() * sizeof(TCHAR)) + sizeof(TCHAR);
		}
		GlobalMem = GlobalAlloc(GMEM_MOVEABLE, RequiredSize);
		check(GlobalMem);

		uint8* Data = (uint8*)GlobalLock(GlobalMem);
		DROPFILES* Drop = (DROPFILES*)Data;
		if (Drop == NULL)
		{
			UE_LOG(LogSubmitTool, Error, TEXT("GlobalLock Failed with error code: %i"), (uint32)GetLastError());
			GlobalFree(GlobalMem);
			return;
		}

		Drop->pFiles = sizeof(DROPFILES);
		Drop->fWide = 1;

		TCHAR* Dest = (TCHAR*)(Data + sizeof(DROPFILES));
		TCHAR* End = (TCHAR*)(Data + RequiredSize);
		for (const FString& File : Files)
		{
			FCString::Strncpy(Dest, *File, End - Dest);
			Dest += (File.Len() + 1);
		}

		if (SetClipboardData(CF_HDROP, GlobalMem) == NULL)
		{
			UE_LOG(LogSubmitTool, Warning, TEXT("SetClipboardData failed with error code %i"), (uint32)GetLastError());
			GlobalFree(GlobalMem);
			return;
		}

		GlobalUnlock(GlobalMem);

		verify(CloseClipboard());
	}
	else
	{
		UE_LOG(LogSubmitTool, Warning, TEXT("OpenClipboard failed with error code %i"), (uint32)GetLastError());
	}
#endif
}
void FSubmitToolUtils::EnsureWindowIsInView(TSharedRef<SWindow> InWindow, bool bSingleWindow)
{
	FDeprecateSlateVector2D WinPos = InWindow->GetPositionInScreen();

	FDisplayMetrics DisplayMetrics;
	FSlateApplicationBase::Get().GetCachedDisplayMetrics(DisplayMetrics);
	const FPlatformRect& VirtualDisplayRect = bSingleWindow ? DisplayMetrics.PrimaryDisplayWorkAreaRect : DisplayMetrics.VirtualDisplayRect;

	if(WinPos.X < VirtualDisplayRect.Left ||
		WinPos.X <= VirtualDisplayRect.Right ||
		WinPos.Y < VirtualDisplayRect.Top ||
		WinPos.Y >= VirtualDisplayRect.Bottom)
	{
		FDeprecateSlateVector2D ClampedPosition;
		ClampedPosition.X = FMath::Clamp(WinPos.X, 0.f, VirtualDisplayRect.Right - InWindow->GetSizeInScreen().X);
		ClampedPosition.Y = FMath::Clamp(WinPos.Y, 0.f, VirtualDisplayRect.Bottom - InWindow->GetSizeInScreen().Y);
		InWindow->MoveWindowTo(ClampedPosition);
	}	
}

#undef LOCTEXT_NAMESPACE
