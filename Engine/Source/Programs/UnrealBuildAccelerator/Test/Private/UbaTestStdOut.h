// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UbaSessionServer.h"
#include "UbaStorageServer.h"

namespace uba
{
	bool TestStdOut(LoggerWithWriter& logger, const StringBufferBase& testRootDir, bool remote, const tchar* app = nullptr, const tchar* arg = nullptr, const tchar* expectedOut = nullptr)
	{
		LogWriter& logWriter = logger.m_writer;
		NetworkBackendTcp networkBackend(logWriter);

		bool ctorSuccess = true;
		NetworkServer networkServer(ctorSuccess, { logWriter });

		StringBuffer<> rootDir;
		rootDir.Append(testRootDir).Append(TC("Uba"));
		if (!DeleteAllFiles(logger, rootDir.data))
			return false;

		StorageServerCreateInfo storageServerInfo(networkServer, rootDir.data, logWriter);
		storageServerInfo.casCapacityBytes = 1024ull * 1024 * 1024;
		StorageServer storageServer(storageServerInfo);

		SessionServerCreateInfo sessionServerInfo(storageServer, networkServer, logWriter);
		sessionServerInfo.checkMemory = false;
		sessionServerInfo.rootDir = rootDir.data;
		sessionServerInfo.traceEnabled = true;
		//sessionServerInfo.remoteLogEnabled = true;
		SessionServer sessionServer(sessionServerInfo);

		auto sg = MakeGuard([&]() { networkServer.DisconnectClients(); });

		StringBuffer<> workingDir;
		workingDir.Append(testRootDir).Append(TC("WorkingDir"));
		if (!DeleteAllFiles(logger, workingDir.data))
			return false;
		if (!storageServer.CreateDirectory(workingDir.data))
			return false;
		if (!DeleteAllFiles(logger, workingDir.data, false))
			return false;
		workingDir.EnsureEndsWithSlash();

		if (!networkServer.StartListen(networkBackend))
			return logger.Error(TC("Failed to listen"));

		StringBuffer<> testApp;
		if (!app)
		{
			GetTestAppPath(logger, testApp);
			app = testApp.data;
		}
		if (!arg)
		{
			arg = TC("-stdout=rootprocess");
			expectedOut = TC("rootprocess");
		}

		ProcessStartInfo pi;
		pi.application = app;
		pi.arguments = arg;
		pi.workingDir = workingDir.data;
		pi.description = TC("StdOutDesc");
		pi.logFile = TC("Log");

		auto ph = remote ? sessionServer.RunProcessRemote(pi) : sessionServer.RunProcess(pi);
		if (!ph.WaitForExit(5000))
			return logger.Error(TC("Timed out waiting for process"));
		if (auto ec = ph.GetExitCode())
			return logger.Error(TC("Process exited with error code %u"), ec);

		networkBackend.StopListen();
		networkServer.DisconnectClients();
		sessionServer.WaitOnAllTasks();

		auto& logLines = ph.GetLogLines();
		if (logLines.size() != 1)
			return logger.Error(TC("Application %s produced %u log line(s) but expected 1"), app, u32(logLines.size()));
		if (!Equals(logLines[0].text.c_str(), expectedOut))
			return logger.Error(TC("Application %s produced non-matching log line: %s"), app, logLines[0].text.c_str());

		return true;
	}

	bool TestStdOutLocal(LoggerWithWriter& logger, const StringBufferBase& testRootDir)
	{
		if (!IsWindows)
			return true;
		return TestStdOut(logger, testRootDir, false);
	}

	bool TestStdOutRemote(LoggerWithWriter& logger, const StringBufferBase& testRootDir)
	{
		return TestStdOut(logger, testRootDir, true);
	}

	bool TestStdOutViaCmd(LoggerWithWriter& logger, const StringBufferBase& testRootDir)
	{
		if (!IsWindows)
			return true;
		StringBuffer<> args;
		args.Append(TC("/c \""));
		GetTestAppPath(logger, args);
		args.Append(TC(" -stdout=foo\""));
		return TestStdOut(logger, testRootDir, false, TC("c:\\windows\\system32\\cmd.exe"), args.data, TC("foo"));
	}
}
