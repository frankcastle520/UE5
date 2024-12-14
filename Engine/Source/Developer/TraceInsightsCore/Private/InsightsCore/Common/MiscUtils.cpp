// Copyright Epic Games, Inc. All Rights Reserved.

#include "InsightsCore/Common/MiscUtils.h"

#include "HAL/FileManagerGeneric.h"
#include "HAL/PlatformProcess.h"
#include "Misc/FileHelper.h"

namespace UE::Insights
{

////////////////////////////////////////////////////////////////////////////////////////////////////

bool FMiscUtils::OpenUnrealInsights(const TCHAR* InCmdLine)
{
	if (InCmdLine == nullptr)
	{
		InCmdLine = TEXT("");
	}

	const TCHAR* ExecutablePath = FPlatformProcess::ExecutablePath();

	constexpr bool bLaunchDetached = true;
	constexpr bool bLaunchHidden = false;
	constexpr bool bLaunchReallyHidden = false;

	uint32 ProcessID = 0;
	const int32 PriorityModifier = 0;
	const TCHAR* OptionalWorkingDirectory = nullptr;

	void* PipeWriteChild = nullptr;
	void* PipeReadChild = nullptr;

	FProcHandle Handle = FPlatformProcess::CreateProc(ExecutablePath, InCmdLine, bLaunchDetached, bLaunchHidden, bLaunchReallyHidden, &ProcessID, PriorityModifier, OptionalWorkingDirectory, PipeWriteChild, PipeReadChild);
	if (Handle.IsValid())
	{
		FPlatformProcess::CloseProc(Handle);
		return true;
	}

	return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

} // namespace UE::Insights
