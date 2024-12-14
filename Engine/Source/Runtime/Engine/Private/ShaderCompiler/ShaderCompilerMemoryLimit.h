// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	ShaderCompilerMemoryLimit.h: Wrapper for Windows specific Job Object functionality.
=============================================================================*/

#pragma once

#include "CoreMinimal.h"
#include "HAL/PlatformProcess.h"


/** Output structure for polling job object limitation violation status. See FWindowsResourceRestrictedJobObject::QueryLimitViolationStatus(). */
struct FJobObjectLimitationInfo
{
	/** Size (in bytes) of the job memory limitation. */
	int64 MemoryLimit = 0;

	/** Size (in bytes) of the job memory usage. When QueryLimitViolationStatus() returns true, this will be greater than JobMemoryLimit, since the job object violated the limitation requirements. */
	int64 MemoryUsed = 0;
};


#if PLATFORM_WINDOWS

#include "Windows/MinimalWindowsApi.h"
#include "Windows/WindowsHWrapper.h"

/** Wrapper lass for a resource restricted job object. This is only available on Windows and acts as a placeholder on other platforms. */
class FWindowsResourceRestrictedJobObject
{
public:
	FWindowsResourceRestrictedJobObject(const FWindowsResourceRestrictedJobObject&) = delete;
	FWindowsResourceRestrictedJobObject& operator = (const FWindowsResourceRestrictedJobObject&) = delete;

	FWindowsResourceRestrictedJobObject(const FString& InJobName, int32 InitialJobMemoryLimitMiB = 0);
	~FWindowsResourceRestrictedJobObject();

	/** Assigns the specified process to this job object. */
	void AssignProcess(const FProcHandle& Process);

	/** Sets the memory limitation for this job object in MiB. This must be greater than or equal to 1024 MiB. */
	void SetMemoryLimit(int32 InJobMemoryLimitMiB);

	/** Queryies the status of the current memory usage. */
	bool QueryStatus(FJobObjectLimitationInfo& OutInfo);

	/** Queries the status of limitation violation notifications. */
	bool QueryLimitViolationStatus(FJobObjectLimitationInfo& OutInfo);

private:
	static FString GetErrorMessage();

	bool IsValid() const;

	void CreateAndLinkCompletionPort();

private:
	FString JobName;
	HANDLE JobObject = nullptr;
	HANDLE CompletionPort = nullptr;
	int32 MemoryLimit = 0;
};

using FResourceRestrictedJobObject = FWindowsResourceRestrictedJobObject;

#else

/** Wrapper lass for a resource restricted job object. This is only available on Windows and acts as a placeholder on other platforms. */
class FGenericResourceRestrictedJobObject
{
public:
	FGenericResourceRestrictedJobObject(const FGenericResourceRestrictedJobObject&) = delete;
	FGenericResourceRestrictedJobObject& operator = (const FGenericResourceRestrictedJobObject&) = delete;

	FGenericResourceRestrictedJobObject(const FString& JobName, int32 InitialJobMemoryLimitMiB = 0);

	void AssignProcess(const FProcHandle& Process);
	void SetMemoryLimit(int32 InJobMemoryLimitMiB);
	bool QueryStatus(FJobObjectLimitationInfo& OutInfo);
	bool QueryLimitViolationStatus(FJobObjectLimitationInfo& OutInfo);
};

using FResourceRestrictedJobObject = FGenericResourceRestrictedJobObject;

#endif // PLATFORM_WINDOWS
