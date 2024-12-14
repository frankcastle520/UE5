// Copyright Epic Games, Inc. All Rights Reserved.

#include "UbaCacheServer.h"
#include "UbaFile.h"
#include "UbaHttpServer.h"
#include "UbaNetworkBackendTcp.h"
#include "UbaNetworkServer.h"
#include "UbaPlatform.h"
#include "UbaProtocol.h"
#include "UbaStorageServer.h"
#include "UbaVersion.h"

#if PLATFORM_WINDOWS
#include <dbghelp.h>
#include <io.h>
#pragma comment (lib, "Dbghelp.lib")
#endif

namespace uba
{
	const tchar*	Version = GetVersionString();
	u32				DefaultCapacityGb = 500;
	u32				DefaultExpiration = 3*24*60*60;
	const tchar*	DefaultRootDir = []() {
		static tchar buf[256];
		if (IsWindows)
			ExpandEnvironmentStringsW(TC("%ProgramData%\\Epic\\" UE_APP_NAME), buf, sizeof(buf));
		else
			GetFullPathNameW(TC("~/" UE_APP_NAME), sizeof_array(buf), buf, nullptr);
		return buf;
		}();
	u32				DefaultProcessorCount = []() { return GetLogicalProcessorCount(); }();

	int PrintHelp(const tchar* message)
	{
		LoggerWithWriter logger(g_consoleLogWriter, TC(""));
		if (*message)
		{
			logger.Info(TC(""));
			logger.Error(TC("%s"), message);
		}
		logger.Info(TC(""));
		logger.Info(TC("-------------------------------------------"));
		logger.Info(TC("   UbaCacheService v%s (%u)"), Version, CacheNetworkVersion);
		logger.Info(TC("-------------------------------------------"));
		logger.Info(TC(""));
		logger.Info(TC("  -dir=<rootdir>          The directory used to store data. Defaults to \"%s\""), DefaultRootDir);
		logger.Info(TC("  -port=[<host>:]<port>   The ip/name and port (default: %u) to listen for clients on"), DefaultCachePort);
		logger.Info(TC("  -capacity=<gigaby>      Capacity of local store. Defaults to %u gigabytes"), DefaultCapacityGb);
		logger.Info(TC("  -expiration=<seconds>   Time until unused cache entries get deleted. Defaults to %s (%u seconds)"), TimeToText(MsToTime(DefaultExpiration*1000)).str, DefaultExpiration);
		logger.Info(TC("  -http=<port>            If set, a http server will be started and listen on <port>"));
		logger.Info(TC(""));
		return -1;
	}

	ReaderWriterLock* g_exitLock = new ReaderWriterLock();
	LoggerWithWriter* g_logger;
	Atomic<bool> g_shouldExit;

	bool ShouldExit()
	{
		return g_shouldExit || IsEscapePressed();
	}
	
	void CtrlBreakPressed()
	{
		g_shouldExit = true;

		g_exitLock->EnterWrite();
		if (g_logger)
			g_logger->Info(TC("  Exiting..."));
		g_exitLock->LeaveWrite();
	}

	#if PLATFORM_WINDOWS
	BOOL ConsoleHandler(DWORD signal)
	{
		CtrlBreakPressed();
		return TRUE;
	}
	#else
	void ConsoleHandler(int sig)
	{
		CtrlBreakPressed();
	}
	#endif
	
	StringBuffer<> g_rootDir(DefaultRootDir);

	//LONG WINAPI UbaUnhandledExceptionFilter(EXCEPTION_POINTERS* ExceptionInfo)
	//{
	//	time_t rawtime;
	//	time(&rawtime);
	//	tm ti;
	//	localtime_s(&ti, &rawtime);
	//
	//	StringBuffer<> dumpFile;
	//	dumpFile.Append(g_rootDir).EnsureEndsWithSlash().Appendf(TC("UbaCliCrash_%02u%02u%02u_%02u%02u%02u.dmp"), ti.tm_year - 100, ti.tm_mon + 1, ti.tm_mday, ti.tm_hour, ti.tm_min, ti.tm_sec);
	//
	//	wprintf(TC("Unhandled exception - Writing minidump %s\n"), dumpFile.data);
	//	HANDLE hFile = ::CreateFileW(dumpFile.data, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_DELETE | FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	//	MINIDUMP_EXCEPTION_INFORMATION mei;
	//	mei.ThreadId = GetCurrentThreadId();
	//	mei.ClientPointers = TRUE;
	//	mei.ExceptionPointers = ExceptionInfo;
	//	MiniDumpWriteDump(GetCurrentProcess(), ::GetCurrentProcessId(), hFile, MiniDumpNormal, &mei, NULL, NULL);
	//	return EXCEPTION_EXECUTE_HANDLER;
	//}


	int WrappedMain(int argc, tchar* argv[])
	{
		using namespace uba;
		//SetUnhandledExceptionFilter(UbaUnhandledExceptionFilter);

		u32 storageCapacityGb = DefaultCapacityGb;
		StringBuffer<256> workDir;
		StringBuffer<128> listenIp;
		u16 port = DefaultCachePort;
		u16 httpPort = 0;
		bool quiet = false;
		bool storeCompressed = true;
		u32 expirationTimeSeconds = DefaultExpiration;

		for (int i=1; i!=argc; ++i)
		{
			StringBuffer<> name;
			StringBuffer<> value;

			if (const tchar* equals = TStrchr(argv[i],'='))
			{
				name.Append(argv[i], equals - argv[i]);
				value.Append(equals+1);
			}
			else
			{
				name.Append(argv[i]);
			}

			if (name.Equals(TC("-port")))
			{
				if (const tchar* portIndex = value.First(':'))
				{
					StringBuffer<> portStr(portIndex + 1);
					if (!portStr.Parse(port))
						return PrintHelp(TC("Invalid value for port in -port"));
					listenIp.Append(value.data, portIndex - value.data);
				}
				else
				{
					if (!value.Parse(port))
						return PrintHelp(TC("Invalid value for -port"));
				}
			}
			else if (name.Equals(TC("-dir")))
			{
				if (value.IsEmpty())
					return PrintHelp(TC("-dir needs a value"));
				if ((g_rootDir.count = GetFullPathNameW(value.Replace('/', PathSeparator).data, g_rootDir.capacity, g_rootDir.data, nullptr)) == 0)
					return PrintHelp(StringBuffer<>().Appendf(TC("-dir has invalid path %s"), g_rootDir.data).data);
			}
			else if (name.Equals(TC("-capacity")))
			{
				if (!value.Parse(storageCapacityGb))
					return PrintHelp(TC("Invalid value for -capacity"));
			}
			else if (name.Equals(TC("-expiration")))
			{
				if (!value.Parse(expirationTimeSeconds))
					return PrintHelp(TC("Invalid value for -expire"));
			}
			else if (name.Equals(TC("-http")))
			{
				if (!value.Parse(httpPort))
					httpPort = 80;
			}
			else if (name.Equals(TC("-?")))
			{
				return PrintHelp(TC(""));
			}
			else
			{
				StringBuffer<> msg;
				msg.Appendf(TC("Unknown argument '%s'"), name.data);
				return PrintHelp(msg.data);
			}
		}

		FilteredLogWriter logWriter(g_consoleLogWriter, quiet ? LogEntryType_Info : LogEntryType_Detail);
		LoggerWithWriter logger(logWriter, TC(""));

		g_exitLock->EnterWrite();
		g_logger = &logger;
		g_exitLock->LeaveWrite();
		auto glg = MakeGuard([]() { g_exitLock->EnterWrite(); g_logger = nullptr; g_exitLock->LeaveWrite(); });

		const tchar* dbgStr = TC("");
		#if UBA_DEBUG
		dbgStr = TC(" (DEBUG)");
		#endif
		logger.Info(TC("UbaCacheService v%s(%u)%s (Workers: %u, Rootdir: \"%s\", StoreCapacity: %uGb, Expiration: %s)"), Version, CacheNetworkVersion, dbgStr, GetLogicalProcessorCount(), g_rootDir.data, storageCapacityGb, TimeToText(MsToTime(expirationTimeSeconds)*1000, true).str);

		u64 maintenanceReserveSizeMb = 128;

		if (SupportsHugePages())
		{
			u64 hugePageCount = GetHugePageCount();
			u64 recommendedHugePageCount = (maintenanceReserveSizeMb*GetLogicalProcessorCount())/2;
			if (hugePageCount < recommendedHugePageCount)
				logger.Info(TC("  Improve maintenance performance by enabling %llu huge pages on system (%llu enabled)"), recommendedHugePageCount, hugePageCount);
		}

		logger.Info(TC(""));

		u64 storageCapacity = u64(storageCapacityGb)*1000*1000*1000;

		StringBuffer<512> currentDir;
		GetCurrentDirectoryW(currentDir);

		if (workDir.IsEmpty())
			workDir.Append(currentDir);

		// TODO: Change workdir to make it full

		#if PLATFORM_WINDOWS
		SetConsoleCtrlHandler(ConsoleHandler, TRUE);
		#else
		signal(SIGINT, ConsoleHandler);
		signal(SIGTERM, ConsoleHandler);
		#endif

		NetworkBackendTcp networkBackend(logWriter);
		NetworkServerCreateInfo nsci(logWriter);
		//nsci.workerCount = 4;
		bool ctorSuccess = true;
		NetworkServer networkServer(ctorSuccess, nsci);
		if (!ctorSuccess)
			return -1;

		StorageServerCreateInfo storageInfo(networkServer, g_rootDir.data, logWriter);
		storageInfo.casCapacityBytes = storageCapacity;
		storageInfo.storeCompressed = storeCompressed;
		storageInfo.allowFallback = false;
		storageInfo.manuallyHandleOverflow = true;
		storageInfo.writeRecievedCasFilesToDisk = true;
		StorageServer storageServer(storageInfo);

		if (!storageServer.LoadCasTable(true, true))
			return -1;

		CacheServerCreateInfo cacheInfo(storageServer, g_rootDir.data, logWriter);
		cacheInfo.expirationTimeSeconds = expirationTimeSeconds;
		cacheInfo.maintenanceReserveSize = maintenanceReserveSizeMb * 1024 * 1024;
		CacheServer cacheServer(cacheInfo);

		if (!cacheServer.Load())
			return -1;

		if (!cacheServer.RunMaintenance(true, ShouldExit))
			return -1;

		HttpServer httpServer(logWriter, networkBackend);

		if (httpPort)
		{
			httpServer.AddCommandHandler([&](const tchar* command, tchar* arguments)
				{
					if (!Equals(command, TC("addcrypto")))
						return "Unknown command ('addcrypto' only available)";
					u64 expirationSeconds = 60;
					if (tchar* comma  = TStrchr(arguments, ','))
					{
						*comma = 0;
						if (!Parse(expirationSeconds, comma+1, TStrlen(comma+1)))
							return "Failed to parse expiration seconds";
					}

					u8 crypto128Data[16];
					if (!CryptoFromString(crypto128Data, 16, arguments))
						return "Failed to read crypto argument";
					u64 expirationTime = GetTime() + MsToTime(expirationSeconds*1000);
					networkServer.RegisterCryptoKey(crypto128Data, expirationTime);
					return (const char*)nullptr;
				});
			httpServer.StartListen(httpPort);
		}

		{
			auto stopListen = MakeGuard([&]() { networkBackend.StopListen(); });
			auto stopServer = MakeGuard([&]() { networkServer.DisconnectClients(); });

			if (!networkServer.StartListen(networkBackend, port, listenIp.data))
				return -1;

			while (!ShouldExit() && !cacheServer.ShouldShutdown())
			{
				Sleep(1000);
				if (!cacheServer.RunMaintenance(false, ShouldExit))
					break;
			}
		}

		storageServer.SaveCasTable(true);
		cacheServer.Save();
		return 0;
	}
}

#if PLATFORM_WINDOWS
int wmain(int argc, wchar_t* argv[])
{
	int res = uba::WrappedMain(argc, argv);
	return res;
}
#else
int main(int argc, char* argv[])
{
	return uba::WrappedMain(argc, argv);
}
#endif
