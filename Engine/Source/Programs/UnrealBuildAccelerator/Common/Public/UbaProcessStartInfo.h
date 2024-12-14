// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UbaLogWriter.h"

namespace uba
{
	class ApplicationRules;
	class ProcessHandle;

	struct ProcessStartInfo
	{
		UBA_API ProcessStartInfo();
		UBA_API ~ProcessStartInfo();
		UBA_API ProcessStartInfo(const ProcessStartInfo&);

		UBA_API const tchar* GetDescription() const;	// Use this instead since it will use application name if description is not set

		const tchar* application = TC("");		// Application name, cl.exe etc. Use full path
		const tchar* arguments = TC("");		// Arguments. Should not include application name
		const tchar* workingDir = TC("");		// Working directory. Use full path
		const tchar* description = TC("");		// Description. Used for on-screen logging and log file names if session.logToFile is set but logFile is ""
		const tchar* logFile = TC("");			// Log file. If set, will always log. If not full path the session log dir will be prepended.
		u32 priorityClass = 0x00000020;			// Priority of process. Defaults to NORMAL_PRIORITY_CLASS
		u64 outputStatsThresholdMs = ~u64(0);	// Threshold in milliseconds where process summary should be printed to log
		bool trackInputs = false;				// Track all files read. Can read result in ProcessHandle.GetTrackedInputs()
		bool useCustomAllocator = true;			// Disable detouring of allocator inside processes. If Session.disableCustomAllocator is false this will be overridden
		bool writeOutputFilesOnFail = false;	// If set to true, output files will be written to disk regardless if process succeeds or not
		const tchar* breadcrumbs = TC("");		// If not empty, write additional information to the UBA trace file
		bool startSuspended = false;			// Start process suspended.. a bit internal atm and only supported on windows

		using LogLineCallback = void(void* userData, const tchar* line, u32 length, LogEntryType type);
		LogLineCallback* logLineFunc = nullptr;	// Callback for when log entries happens
		void* logLineUserData = nullptr;		// User data provided to logLine callback

		using ExitedCallback = void(void* userData, const ProcessHandle&);
		ExitedCallback* exitedFunc = nullptr;	// Callback for when process is done (it has already exited)
		void* userData = nullptr;				// User data provided to exit callback

		const ApplicationRules* rules = nullptr;// Internal use (for now)
		int uiLanguage = 1033;					// Internal use
	};
}
