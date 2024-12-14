// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UbaLogWriter.h"

namespace uba
{
	class Config;
	class Storage;

	struct SessionCreateInfo
	{
		SessionCreateInfo(Storage& s, LogWriter& w = g_consoleLogWriter) : storage(s), logWriter(w) {}

		void Apply(Config& config);

		Storage& storage;
		LogWriter& logWriter;
		const tchar* rootDir = nullptr;			// Root dir for logs, binaries, temp files
		const tchar* traceName = nullptr;		// Name of trace. This name can be used by UbaVisualizer to watch progress live
		const tchar* traceOutputFile = nullptr; // Output file. Will be written at end of run.
		const tchar* extraInfo = nullptr;		// Extra info that will be stored in the trace info about the session
		bool logToFile = false;					// Set to true to have all processes write a log file with function calls.
		bool useUniqueId = true;				// If true, id of session will be "yymmdd_hhmmss". Otherwise "Debug"
		bool disableCustomAllocator = false;	// Disable detouring of allocator inside processes.
		bool launchVisualizer = false;			// Launch a UbaVisualizer process (this automatically enable trace)
		bool allowMemoryMaps = IsWindows;		// Use memory maps where possible. Session creates memory maps of files that processes use
		bool allowKeepFilesInMemory = IsWindows;// Allow detoured process to keep output files in memory and send them to session through file mapping handle
		bool allowOutputFiles = IsWindows;		// Allow (selected) output files to be written to disk after process has ended.
		bool allowSpecialApplications = true;	// Allow uba to expand cmd.exe and call known commands instead of running additional process
		bool suppressLogging = false;			// Suppress all logging produced by detoured processes. Will be made in detoured process to improve performance when needed
		bool shouldWriteToDisk = true;			// Set to false to skip writing output files to disk
		bool traceEnabled = false;				// Set to true to always create in-memory trace data. Is not needed if traceName, traceOutputFile or launchVisualizer is set
		bool detailedTrace = false;				// Enable detailed trace to include jobs, individual file I/O etc in trace dump
		bool traceChildProcesses = false;		// Trace and visualize child processes so they can be seen in visualizer
		bool storeObjFilesCompressed = false;	// Compiler will write .obj files compressed to disk and linker will decompress when reading
		bool extractObjFilesSymbols = false;	// Will extract import/export symbols and clear export table from obj file.
		u64 deleteSessionsOlderThanSeconds = 12 * 60 * 60; // Delete session folders older than 12 hours by default . Set to 0 to not delete or 1 to delete all
		u64 keepOutputFileMemoryMapsThreshold = 256 * 1024; // If allowMemoryMaps is true, output files will be kept in memory if smaller than this size
	};
}
