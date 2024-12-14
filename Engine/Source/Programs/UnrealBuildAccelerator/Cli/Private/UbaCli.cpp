// Copyright Epic Games, Inc. All Rights Reserved.

#include "UbaCacheClient.h"
#include "UbaCacheServer.h"
#include "UbaClient.h"
#include "UbaCompressedObjFileHeader.h"
#include "UbaCoordinatorWrapper.h"
#include "UbaFileAccessor.h"
#include "UbaNetworkBackendTcp.h"
#include "UbaPathUtils.h"
#include "UbaPlatform.h"
#include "UbaProtocol.h"
#include "UbaRootPaths.h"
#include "UbaScheduler.h"
#include "UbaSessionClient.h"
#include "UbaSessionServer.h"
#include "UbaStorageClient.h"
#include "UbaStorageProxy.h"
#include "UbaStorageServer.h"
#include "UbaStorageUtils.h"
#include "UbaVersion.h"

#include "UbaAWS.h"

#if PLATFORM_WINDOWS
#include <dbghelp.h>
#include <io.h>
#pragma comment (lib, "Dbghelp.lib")
#endif

namespace uba
{

	const tchar*	Version = GetVersionString();
	u32				DefaultCapacityGb = 20;
	const tchar*	DefaultRootDir = []() {
		static tchar buf[256];
		if (IsWindows)
			ExpandEnvironmentStringsW(TC("%ProgramData%\\Epic\\" UE_APP_NAME), buf, sizeof(buf));
		else
			GetFullPathNameW(TC("~/" UE_APP_NAME), sizeof_array(buf), buf, nullptr);
		return buf;
		}();
	u32				DefaultProcessorCount = []() { return GetLogicalProcessorCount(); }();

	bool PrintHelp(const tchar* message)
	{
		LoggerWithWriter logger(g_consoleLogWriter, TC(""));
		if (*message)
		{
			logger.Info(TC(""));
			logger.Error(TC("%s"), message);
		}

		const tchar* dbgStr = TC("");
		#if UBA_DEBUG
		dbgStr = TC(" (DEBUG)");
		#endif

		logger.Info(TC(""));
		logger.Info(TC("-------------------------------------------"));
		logger.Info(TC("   UbaCli v%s%s"), Version, dbgStr);
		logger.Info(TC("-------------------------------------------"));
		logger.Info(TC(""));
		logger.Info(TC("  UbaCli.exe [options...] <commandtype> <executable> [arguments...]"));
		logger.Info(TC(""));
		logger.Info(TC("  CommandTypes:"));
		logger.Info(TC("   local                   Will run executable locally using detoured paths"));
		logger.Info(TC("   remote                  Will wait for available agent and then run executable remotely"));
		logger.Info(TC("   agent                   Will run executable against agent spawned in process"));
		logger.Info(TC("   native                  Will run executable in a normal way"));
		logger.Info(TC(""));
		logger.Info(TC("  Options:"));
		logger.Info(TC("   -dir=<rootdir>          The directory used to store data. Defaults to \"%s\""), DefaultRootDir);
		logger.Info(TC("   -port=[<host>:]<port>   The ip/name and port (default: %u) of the machine we want to help"), DefaultPort);
		logger.Info(TC("   -log                    Log all processes detouring information to file (only works with debug builds)"));
		logger.Info(TC("   -quiet                  Does not output any logging in console except errors"));
		logger.Info(TC("   -loop=<count>           Loop the commandline <count> number of times. Will exit when/if it fails"));
		logger.Info(TC("   -workdir=<dir>          Working directory"));
		logger.Info(TC("   -checkcas               Check so all cas entries are correct"));
		logger.Info(TC("   -checkfiletable         Check so file table has correct cas stored"));
		logger.Info(TC("   -checkaws               Check if we are inside aws and output information about aws"));
		logger.Info(TC("   -deletecas              Deletes the casdb"));
		logger.Info(TC("   -getcas                 Will print hash of application"));
		logger.Info(TC("   -summary                Print summary at the end of a session"));
		logger.Info(TC("   -nocustomalloc          Disable custom allocator for processes. If you see odd crashes this can be tested"));
		logger.Info(TC("   -nostdout               Disable stdout from process."));
		logger.Info(TC("   -storeraw               Disable compression of storage. This will use more storage and might improve performance"));
		logger.Info(TC("   -maxcpu=<number>        Max number of processes that can be started. Defaults to \"%u\" on this machine"), DefaultProcessorCount);
		logger.Info(TC("   -visualizer             Spawn a visualizer that visualizes progress"));
		logger.Info(TC("   -crypto=<32chars>       Will enable crypto on network client/server"));
		logger.Info(TC("   -coordinator=<name>     Load a UbaCoordinator<name>.dll to instantiate a coordinator to get helpers"));
		logger.Info(TC("   -cache=<host>[:<port>]  Connect to cache server. Will fetch from cache unless -populatecache is set"));
		logger.Info(TC("   -populatecache          Populate cache server if connected to one"));
		logger.Info(TC("   -cachecommand=<cmd>     Send command to cache server. Will output result in log"));
		logger.Info(TC("   -writecachesummary      Write cache summary file about connected cache server"));
		logger.Info(TC(""));
		logger.Info(TC("  CoordinatorOptions (if coordinator set):"));
		logger.Info(TC("   -uri=<address>          Uri to coordinator"));
		logger.Info(TC("   -pool=<name>            Name of helper pool inside coordinator"));
		logger.Info(TC("   -oidc=<name>            Name of oidc"));
		logger.Info(TC("   -maxcores=<number>      Max number of cores that will be asked for from coordinator"));
		logger.Info(TC(""));
		logger.Info(TC("  If <executable> is a .yaml-file UbaCli creates a scheduler to execute commands from the yaml file instead"));
		
		logger.Info(TC(""));
		return false;
	}

	StorageServer* g_storageServer;
	
	void CtrlBreakPressed()
	{
		if (g_storageServer)
		{
			g_storageServer->SaveCasTable(true);
			LoggerWithWriter(g_consoleLogWriter).Info(TC("CAS table saved..."));
		}
		abort();
	}

	#if PLATFORM_WINDOWS
	BOOL ConsoleHandler(DWORD signal)
	{
		if (signal == CTRL_C_EVENT)
			CtrlBreakPressed();
		return FALSE;
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


	bool WrappedMain(int argc, tchar* argv[])
	{
		using namespace uba;
		//SetUnhandledExceptionFilter(UbaUnhandledExceptionFilter);

		u32 storageCapacityGb = DefaultCapacityGb;
		StringBuffer<256> workDir;
		StringBuffer<128> listenIp;
		StringBuffer<128> cacheHost;
		TString crypto;
		TString coordinatorName;
		TString coordinatorPool;
		u32 coordinatorMaxCoreCount = 400;
		u16 port = DefaultPort;
		u32 maxProcessCount = DefaultProcessorCount;
		bool launchVisualizer = false;
		bool storeCompressed = true;
		bool disableCustomAllocator = false;
		bool quiet = false;
		bool checkCas = false;
		bool checkCas2 = false;
		bool checkAws = false;
		bool getCas = false;
		bool deleteCas = false;
		bool enableStdOut = true;
		bool printSummary = false;
		bool populateCache = false;
		bool writeCacheSummary = false;
		TString checkFileTable;
		TString cacheFilterString;
		TString cacheCommand;
		TString testCompress;
		TString testDecompress;

		u32 loopCount = 1;

		enum CommandType
		{
			CommandType_NotSet,
			CommandType_Local,
			CommandType_Remote,
			CommandType_Native,
			CommandType_Agent,
			CommandType_None,
		};

		CommandType commandType = CommandType_NotSet;

		TString application;
		TString arguments;

		for (int i=1; i!=argc; ++i)
		{
			StringBuffer<> name;
			StringBuffer<32*1024> value;
			const tchar* arg = argv[i];

			if (const tchar* equals = TStrchr(arg,'='))
			{
				name.Append(arg, equals - arg);
				value.Append(equals+1);
			}
			else
			{
				name.Append(arg);
			}

			if (!application.empty())
			{
				if (!arguments.empty())
					arguments += ' ';
				TString argTemp;
				bool hasSpace = TStrchr(arg, ' ');
				if (hasSpace)
				{
					argTemp = arg;
					size_t index = 0;
					while (true) {
							index = argTemp.find('\"', index);
							if (index == std::string::npos) break;
							argTemp.replace(index, 1, TC("\\\""));
							index += 2;
					}
					arg = argTemp.c_str();
					arguments += TC("\"");
				}
				arguments += arg;
				if (hasSpace)
					arguments += TC("\"");
				continue;
			}
			if (commandType != CommandType_NotSet)
			{
				application = arg;
			}
			else if (name.Equals(TC("local")))
			{
				commandType = CommandType_Local;
			}
			else if (name.Equals(TC("remote")))
			{
				commandType = CommandType_Remote;
			}
			else if (name.Equals(TC("native")))
			{
				commandType = CommandType_Native;
			}
			else if (name.Equals(TC("agent")))
			{
				commandType = CommandType_Agent;
			}
			else if (IsWindows && name.Equals(TC("-visualizer")))
			{
				launchVisualizer = true;
			}
			else if (name.Equals(TC("-crypto")))
			{
				if (value.IsEmpty())
					value.Append(TC("0123456789abcdef0123456789abcdef"));
				crypto = value.data;
			}
			else if (name.Equals(TC("-coordinator")))
			{
				if (value.IsEmpty())
					return PrintHelp(TC("-coordinator needs a value"));
				coordinatorName = value.data;
			}
			else if (name.Equals(TC("-pool")))
			{
				if (value.IsEmpty())
					return PrintHelp(TC("-pool needs a value"));
				coordinatorPool = value.data;
			}
			else if (name.Equals(TC("-maxcores")))
			{
				if (value.IsEmpty())
					return PrintHelp(TC("-maxcores needs a value"));
				if (!value.Parse(coordinatorMaxCoreCount))
					return PrintHelp(TC("Invalid value for -maxcores"));
			}
			else if (name.Equals(TC("-workdir")))
			{
				if (value.IsEmpty())
					return PrintHelp(TC("-workdir needs a value"));
				if ((workDir.count = GetFullPathNameW(value.data, workDir.capacity, workDir.data, nullptr)) == 0)
					return PrintHelp(StringBuffer<>().Appendf(TC("-workdir has invalid path %s"), value.data).data);
			}
			else if (name.Equals(TC("-capacity")))
			{
				if (!value.Parse(storageCapacityGb))
					return PrintHelp(TC("Invalid value for -capacity"));
			}
			else if (name.Equals(TC("-port")))
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
			else if (name.Equals(TC("-loop")))
			{
				if (!value.Parse(loopCount))
					return PrintHelp(TC("Invalid value for -loop"));
			}
			else if (name.Equals(TC("-quiet")))
			{
				quiet = true;
			}
			else if (name.Equals(TC("-nocustomalloc")))
			{
				disableCustomAllocator = true;
			}
			else if (name.Equals(TC("-maxcpu")))
			{
				if (!value.Parse(maxProcessCount))
					return PrintHelp(TC("Invalid value for -maxcpu"));
			}
			else if (name.Equals(TC("-nostdout")))
			{
				enableStdOut = false;
			}
			else if (name.Equals(TC("-checkcas")))
			{
				checkCas = true;
			}
			else if (name.Equals(TC("-checkfiletable")))
			{
				if (value.IsEmpty())
					return PrintHelp(TC("-checkfiletable needs a value"));
				StringBuffer<> temp;
				if ((temp.count = GetFullPathNameW(value.Replace('/', PathSeparator).data, temp.capacity, temp.data, nullptr)) == 0)
					return PrintHelp(StringBuffer<>().Appendf(TC("-checkfiletable has invalid path %s"), temp.data).data);
				checkFileTable = temp.data;
			}
			else if (name.Equals(TC("-checkcas2")))
			{
				checkCas2 = true;
			}
			else if (name.Equals(TC("-checkaws")))
			{
				checkAws = true;
			}
			else if (name.Equals(TC("-testcompress")))
			{
				if (value.IsEmpty())
					return PrintHelp(TC("-testCompress needs a value"));
				testCompress = value.data;
			}
			else if (name.Equals(TC("-testdecompress")))
			{
				if (value.IsEmpty())
				{
					if (testCompress.empty())
						return PrintHelp(TC("-testDecompress needs a value"));
					value.Clear().Append(g_rootDir).EnsureEndsWithSlash().Append(TC("castemp")).EnsureEndsWithSlash().Append(TC("TestCompress.tmp"));
				}
				testDecompress = value.data;
			}
			else if (name.Equals(TC("-deletecas")))
			{
				deleteCas = true;
			}
			else if (name.Equals(TC("-getcas")))
			{
				getCas = true;
			}
			else if (name.Equals(TC("-summary")))
			{
				printSummary = true;
			}
			else if (name.Equals(TC("-cache")))
			{
				if (value.IsEmpty())
					return PrintHelp(TC("-cache needs a value"));
				cacheHost.Append(value);
			}
			else if (name.Equals(TC("-populatecache")))
			{
				populateCache = true;
			}
			else if (name.Equals(TC("-cachecommand")))
			{
				if (value.IsEmpty())
					return PrintHelp(TC("-cachecommand needs a value"));
				cacheCommand = value.data;
				commandType = CommandType_None;
				quiet = true;
			}
			else if (name.Equals(TC("-writecachesummary")))
			{
				writeCacheSummary = true;
				cacheFilterString = value.data;
				commandType = CommandType_None;
			}
			else if (name.Equals(TC("-storeraw")))
			{
				storeCompressed = false;
			}
			else if (name.Equals(TC("-dir")))
			{
				if (value.IsEmpty())
					return PrintHelp(TC("-dir needs a value"));
				if ((g_rootDir.count = GetFullPathNameW(value.Replace('/', PathSeparator).data, g_rootDir.capacity, g_rootDir.data, nullptr)) == 0)
					return PrintHelp(StringBuffer<>().Appendf(TC("-dir has invalid path %s"), g_rootDir.data).data);
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

		FilteredLogWriter logWriter(g_consoleLogWriter, quiet ? LogEntryType_Warning : LogEntryType_Detail);
		LoggerWithWriter logger(logWriter, TC(""));

		if (deleteCas)
		{
			StorageImpl(StorageCreateInfo(g_rootDir.data, logWriter)).DeleteAllCas();
			for (u32 i=0; i!=4; ++i)
			{
				StringBuffer<> clientRootDir;
				clientRootDir.Append(g_rootDir).Append("Agent").AppendValue(i);
				StorageImpl(StorageCreateInfo(clientRootDir.data, logWriter)).DeleteAllCas();
			}
		}

		if (checkCas)
		{
			StorageCreateInfo storageInfo(g_rootDir.data, logWriter);
			storageInfo.casCapacityBytes = 0;
			storageInfo.storeCompressed = storeCompressed;
			StorageImpl storage(storageInfo);
			return storage.CheckCasContent(DefaultProcessorCount);
		}

		if (!checkFileTable.empty())
		{
			StorageCreateInfo storageInfo(g_rootDir.data, logWriter);
			storageInfo.casCapacityBytes = 0;
			storageInfo.storeCompressed = storeCompressed;
			StorageImpl storage(storageInfo);
			if (!storage.LoadCasTable())
				return false;
			return storage.CheckFileTable(checkFileTable.data(), DefaultProcessorCount);
		}

		if (checkCas2) // Creates a storage server and storage client and transfer _all_ cas files over network
		{
			NetworkBackendTcp networkBackend(logWriter);
			NetworkServerCreateInfo nsci(logWriter);
			bool ctorSuccess = true;
			NetworkServer server(ctorSuccess, nsci);
			StorageServerCreateInfo storageInfo(server, g_rootDir.data, logWriter);
			storageInfo.casCapacityBytes = 0;
			storageInfo.storeCompressed = storeCompressed;
			StorageServer storageServer(storageInfo);

			StringBuffer<> rootDir2(g_rootDir.data);
			rootDir2.Append("_CHECKCAS2");
			DeleteAllFiles(logger, rootDir2.data);
			Client client;

			auto g = MakeGuard([&]() { server.DisconnectClients(); });
			if (!server.StartListen(networkBackend, 1347, TC("127.0.0.1")))
				return false;
			ClientInitInfo cii { logWriter, networkBackend, rootDir2.data, TC("127.0.0.1"), 1347, TC("foo") };
			cii.createSession = false;
			cii.addDirSuffix = false;
			if (!client.Init(cii))
				return false;
			bool success = true;
			WorkManagerImpl workManager(DefaultProcessorCount);
			storageServer.TraverseAllCasFiles([&](const CasKey& casKey, u64 size)
				{
					workManager.AddWork([&, casKey]()
						{
							Storage::RetrieveResult res;
							storageServer.EnsureCasFile(casKey, TC("Dummy"));
							if (!client.storageClient->RetrieveCasFile(res, AsCompressed(casKey, false), TC("")))
								success = false;
							if (!client.storageClient->RetrieveCasFile(res, casKey, TC("")))
								success = false;
						}, 1, TC(""));
				});
			workManager.FlushWork();
			return success;
		}

#if UBA_USE_AWS
		if (checkAws)
		{
			AWS aws;
			StringBuffer<> info;
			if (aws.QueryInformation(logger, info, g_rootDir.data))
			{
				logger.Info(TC("We are inside AWS: %s (%s)"), info.data, aws.GetAvailabilityZone());
				
				StringBuffer<> reason;
				u64 terminateTime;
				if (aws.IsTerminating(logger, reason, terminateTime))
					logger.Info(TC(".. and are being terminated: %s"), reason.data);
			}
			else
				logger.Info(TC("Seems like we are not running inside aws."));
			return true;
		}
#endif
		
		u64 testCompressOriginalSize = 0;
		if (!testCompress.empty())
		{
			WorkManagerImpl workManager(DefaultProcessorCount);

			FileAccessor fa(logger, testCompress.c_str());
			if (!fa.OpenMemoryRead())
				return logger.Error(TC("Failed to open file %s"), testCompress.c_str());
			u64 fileSize = fa.GetSize();
			u8* mem = fa.GetData();

			testCompressOriginalSize = fileSize;

			StorageCreateInfo storageInfo(g_rootDir.data, logWriter);
			storageInfo.casCapacityBytes = 0;
			storageInfo.storeCompressed = storeCompressed;
			storageInfo.workManager = &workManager;
			StorageImpl storage(storageInfo);

			Storage::WriteResult res;
			CompressedObjFileHeader header { CalculateCasKey(mem, fileSize, true, &workManager, testCompress.c_str()) };

			StringBuffer<> dest;
			dest.Append(storage.GetTempPath()).Append(TC("TestCompress.tmp"));
			if (!storage.WriteCompressed(res, TC("MemoryMap"), InvalidFileHandle, mem, fileSize, dest.data, &header, sizeof(header), 0))
				return false;
			if (testDecompress.empty())
				return true;
		}

		if (!testDecompress.empty())
		{
			WorkManagerImpl workManager(DefaultProcessorCount);

			FileAccessor fa(logger, testDecompress.c_str());
			if (!fa.OpenMemoryRead())
				return logger.Error(TC("Failed to open file %s"), testDecompress.c_str());
			u64 fileSize = fa.GetSize();
			u8* mem = fa.GetData();

			StorageCreateInfo storageInfo(g_rootDir.data, logWriter);
			storageInfo.casCapacityBytes = 0;
			storageInfo.storeCompressed = storeCompressed;
			storageInfo.workManager = &workManager;
			StorageImpl storage(storageInfo);

			auto& h = *(CompressedObjFileHeader*)mem;
			if (!h.IsValid())
				return logger.Error(TC("File %s is not a compressed file"), testDecompress.c_str());

			BinaryReader reader(mem, 0, fileSize);
			reader.Skip(sizeof(CompressedObjFileHeader));
			u64 decompressedSize = reader.ReadU64();

			if (testCompressOriginalSize && decompressedSize != testCompressOriginalSize)
				return logger.Error(TC("Compressed file %s has wrong decompressed size."), testDecompress.c_str());

			StringBuffer<> dest;
			dest.Append(storage.GetTempPath()).Append(TC("TestDecompress.tmp"));
			FileAccessor faDest(logger, dest.data);
			if (!faDest.CreateMemoryWrite(false, DefaultAttributes(), decompressedSize))
				return false;
			u8* destMem = faDest.GetData();

			OO_SINTa decoredMemSize = OodleLZDecoder_MemorySizeNeeded(OodleLZ_Compressor_Kraken);
			void* decoderMem = malloc(decoredMemSize);
			auto mg = MakeGuard([decoderMem]() { free(decoderMem); });

			while (reader.GetLeft())
			{
				u32 compressedBlockSize = reader.ReadU32();
				u32 decompressedBlockSize = reader.ReadU32();

				OO_SINTa decompLen = OodleLZ_Decompress(reader.GetPositionData(), (OO_SINTa)compressedBlockSize, destMem, (OO_SINTa)decompressedBlockSize, OodleLZ_FuzzSafe_Yes, OodleLZ_CheckCRC_No, OodleLZ_Verbosity_None, NULL, 0, NULL, NULL, decoderMem, decoredMemSize);
				if (decompLen != decompressedBlockSize)
					return logger.Error(TC("Failed to decompress %s"), testDecompress.c_str());
				destMem += decompressedBlockSize;
				reader.Skip(compressedBlockSize);
			}

			return faDest.Close();
		}

		if (commandType == CommandType_NotSet)
		{
			const tchar* errorMsg = argc == 1 ? TC("") : TC("\nERROR: First argument must be command type. Options are 'local,remote or native'");
			StringBuffer<> msg;
			return PrintHelp(errorMsg);
		}

		StringBuffer<512> currentDir;
		GetCurrentDirectoryW(currentDir);

		if (commandType != CommandType_None)
		{
			if (application.empty())
				return PrintHelp(TC("No executable provided"));

			if (!IsAbsolutePath(application.c_str()))
			{
				StringBuffer<> fullApplicationName;
				if (!SearchPathForFile(logger, fullApplicationName, application.c_str(), currentDir.data))
					return logger.Error(TC("Failed to find full path to %s"), application.c_str());
				application = fullApplicationName.data;
			}

			if (getCas)
			{
				FileAccessor fa(logger, application.c_str());
				if (!fa.OpenMemoryRead())
					return logger.Error(TC("Failed to open file %s"), application.c_str());
				u64 fileSize = fa.GetSize();
				u8* data = fa.GetData();
				bool is64Bit = true;

				CasKey key = CalculateCasKey(data, fileSize, false, nullptr, application.c_str());

				if (data[0] != 'M' || data[1] != 'Z')
					is64Bit = false;
				else
				{
					u32 offset = *(u32*)(data + 0x3c);
					is64Bit = *(u32*)(data + offset) == 0x00004550;
				}
				logger.Info(TC("%s"), application.c_str());
				logger.Info(TC("  Is64Bit: %s"), (is64Bit ? TC("true") : TC("false")));
				logger.Info(TC("  Size: %llu"), fileSize);
				logger.Info(TC("  CasKey: %s"), CasKeyString(key).str);
				return true;
			}
		}

		const tchar* dbgStr = TC("");
		#if UBA_DEBUG
		dbgStr = TC(" (DEBUG)");
		#endif
		logger.Info(TC("UbaCli v%s%s (Rootdir: \"%s\", StoreCapacity: %uGb)\n"), Version, dbgStr, g_rootDir.data, storageCapacityGb);

		u64 storageCapacity = u64(storageCapacityGb)*1000*1000*1000;

		if (workDir.IsEmpty())
			workDir.Append(currentDir);

		// TODO: Change workdir to make it full


		StringBuffer<> logFile;
		#if UBA_DEBUG
		logFile.count = GetFullPathNameW(g_rootDir.data, logFile.capacity, logFile.data, nullptr);
		logFile.EnsureEndsWithSlash().Append(TC("DebugLog.log"));
		#endif

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
		NetworkServer& networkServer = *new NetworkServer(ctorSuccess, nsci);
		auto destroyServer = MakeGuard([&]() { delete &networkServer; });
		if (!ctorSuccess)
			return false;

		if (!crypto.empty())
		{
			u8 crypto128Data[16];
			if (!CryptoFromString(crypto128Data, 16, crypto.c_str()))
				return logger.Error(TC("Failed to parse crypto key %s"), crypto.c_str());
			networkServer.RegisterCryptoKey(crypto128Data);
			logger.Info(TC("Using crypto key %s for connections"), crypto.c_str());
		}


		bool isRemote = commandType == CommandType_Remote || commandType == CommandType_Agent;
		bool useScheduler = EndsWith(application.c_str(), application.size(), TC(".yaml"));

		StorageServerCreateInfo storageInfo(networkServer, g_rootDir.data, logWriter);
		storageInfo.casCapacityBytes = storageCapacity;
		storageInfo.storeCompressed = storeCompressed;
		StorageServer& storageServer = *new StorageServer(storageInfo);
		auto destroyStorage = MakeGuard([&]() { delete &storageServer; });

		SessionServerCreateInfo info(storageServer, networkServer, logWriter);
		info.useUniqueId = useScheduler;
		info.traceEnabled = true;
		//info.detailedTrace = true;
		info.launchVisualizer = launchVisualizer;
		info.disableCustomAllocator = disableCustomAllocator;
		//info.shouldWriteToDisk = shouldWriteToDisk;
		info.rootDir = g_rootDir.data;
		//info.traceName.Append(TC("TESTTRACE"));
		//info.storeObjFilesCompressed = true;
		//info.extractObjFilesSymbols = true;
		#if UBA_DEBUG_LOG_ENABLED
		info.remoteLogEnabled = true;
		#endif
		//info.remoteTraceEnabled = true;

		info.deleteSessionsOlderThanSeconds = 1;
		SessionServer& sessionServer = *new SessionServer(info);
		auto destroySession = MakeGuard([&]() { delete &sessionServer; });

		CacheClient* cacheClient = nullptr;
		auto ccg = MakeGuard([&]() { if (!cacheClient) return; auto& nc = cacheClient->GetClient(); nc.Disconnect(); delete cacheClient; delete &nc; });

		auto CreateCacheClient = [&]()
			{
				auto nc = new NetworkClient(ctorSuccess, {logWriter});
				cacheClient = new CacheClient({logWriter, storageServer, *nc, sessionServer});
			};

		if (cacheHost.count)
		{
			CreateCacheClient();
			if (!cacheClient->GetClient().Connect(networkBackend, cacheHost.data, DefaultCachePort))
				return logger.Error(TC("Failed to connect to cache server"));

			if (!storageServer.LoadCasTable(true))
				return false;

			if (!cacheCommand.empty())
			{
				LoggerWithWriter consoleLogger(g_consoleLogWriter);
				return cacheClient->ExecuteCommand(consoleLogger, cacheCommand.data());
			}

			if (writeCacheSummary)
			{
				StringBuffer<> tempFile(sessionServer.GetTempPath());
				Guid guid;
				CreateGuid(guid);
				tempFile.Append(GuidToString(guid).str).Append(TC(".txt"));
				if (!cacheClient->ExecuteCommand(logger, TC("content"), tempFile.data, cacheFilterString.data()))
					return false;
				logger.Info(TC("Cache status summary written to %s"), tempFile.data);

				#if PLATFORM_WINDOWS
				ShellExecuteW(NULL, L"open", tempFile.data, NULL, NULL, SW_SHOW);
				#endif
				return true;
			}
		}

		// Remove empty spaces and line feeds etc at the end.. just to solve annoying copy paste command lines and accidentally getting line feed
		while (!arguments.empty())
		{
			tchar lastChar = arguments[arguments.size()-1];
			if (lastChar != '\n' && lastChar != '\r' && lastChar != '\t' && lastChar != ' ')
				break;
			arguments.resize(arguments.size() - 1);
		}


		if (isRemote)
		{
			if (!storageServer.m_casTableLoaded)
				if (!storageServer.LoadCasTable(true))
					return false;
			if (!networkServer.StartListen(networkBackend, port, listenIp.data))
				return false;
		}
		auto stopServer = MakeGuard([&]() { networkServer.DisconnectClients(); });

		auto stopListen = MakeGuard([&]() { networkBackend.StopListen(); });

		auto RunLocal = [&](const TString& app, const TString& arg, bool enableDetour)
		{
			u64 start = GetTime();
			ProcessStartInfo pinfo;
			pinfo.description = app.c_str();
			pinfo.application = app.c_str();
			pinfo.arguments = arg.c_str();
			pinfo.workingDir = workDir.data;

			u32 bucketId = 1337;
			if (cacheClient)
			{
				CacheResult cacheResult;
				cacheClient->FetchFromCache(cacheResult, RootPaths(), bucketId, pinfo);
				if (cacheResult.hit)
				{
					logger.Info(TC("%s run took %s [cached]"), (enableDetour ? TC("Boxed") : TC("Native")), TimeToText(GetTime() - start).str);
					return true;
				}
			}


			pinfo.logFile = logFile.data;
			pinfo.logLineUserData = &logger;
			if (enableStdOut)
				pinfo.logLineFunc = [](void* userData, const tchar* line, u32 length, LogEntryType type) { ((Logger*)userData)->Log(type, line, length); };
			if (populateCache)
				pinfo.trackInputs = true;
			logger.Info(TC("Running %s %s"), app.c_str(), arg.c_str());
			ProcessHandle process = sessionServer.RunProcess(pinfo, false, enableDetour);
			if (process.GetExitCode() != 0)
				return logger.Error(TC("Error exit code: %u"), process.GetExitCode());
			logger.Info(TC("%s run took %s"), (enableDetour ? TC("Boxed") : TC("Native")), TimeToText(GetTime() - start).str);

			if (populateCache)
			{
				logger.Error(TC("Populating cache not implemented... todo"));
				RootPaths rootPaths;
				cacheClient->WriteToCache(rootPaths, 0, pinfo, nullptr, 1, nullptr, 0, nullptr, 0);
			}
			return true;
		};

		auto RunRemote = [&](const TString& app, const TString& arg)
		{
			u64 start = GetTime();
			ProcessStartInfo pinfo;
			pinfo.description = app.c_str();
			pinfo.application = app.c_str();
			pinfo.arguments = arg.c_str();
			pinfo.workingDir = workDir.data;
			pinfo.logFile = logFile.data;
			pinfo.logLineUserData = &logger;
			if (enableStdOut)
				pinfo.logLineFunc = [](void* userData, const tchar* line, u32 length, LogEntryType type) { ((Logger*)userData)->Log(type, line, length); };
			logger.Info(TC("Running %s %s"), app.c_str(), arg.c_str());
			ProcessHandle process = sessionServer.RunProcessRemote(pinfo);
			process.WaitForExit(~0u);
			if (process.GetExitCode() != 0)
				return logger.Error(TC("Error exit code: %u"), process.GetExitCode());
			u64 time = GetTime() - start;
			logger.Info(TC("Remote run took %s"), TimeToText(time).str);
			return true;
		};

		auto RunWithClient = [&](const Function<bool()>& func, u32 clientCount)
			{
				Vector<Client> clients;
				clients.resize(clientCount);
				u32 clientIndex = 0;
				for (auto& c : clients)
				{
					ClientInitInfo cii { logWriter, networkBackend, g_rootDir.data, TC("127.0.0.1"), port, TC("DummyZone"), maxProcessCount/u32(clients.size()), clientIndex++};
					if (!c.Init(cii))
						return false;
				}
				return func();
			};

		auto RunAgent = [&](const TString& app, const TString& arg)
		{
			return RunWithClient([&]() { return RunRemote(app, arg); }, 1);
		};

		CoordinatorWrapper coordinator;

		auto RunScheduler = [&](const tchar* yamlFile)
		{
			/*
			bool ctorSuccess;
			NetworkBackendMemory nbm(logWriter);
			
			StringBuffer<> cacheRootDir(g_rootDir);
			cacheRootDir.Append("CacheServer");
			NetworkServer cacheNetworkServer(ctorSuccess);
			StorageServerCreateInfo storageInfo2(cacheNetworkServer, cacheRootDir.data, logWriter);
			storageInfo2.writeRecievedCasFilesToDisk = true;
			StorageServer cacheStorageServer(storageInfo2);
			CacheServer cacheServer(logWriter, cacheRootDir.data, cacheNetworkServer, cacheStorageServer);
			auto csg = MakeGuard([&]() { cacheServer.Save(); });

			NetworkClient cacheNetworkClient(ctorSuccess);
			CacheClient cacheClient(logWriter, storageServer, cacheNetworkClient, sessionServer);

			if (false)
			{
				cacheServer.Load();

				cacheNetworkServer.StartListen(nbm);
				cacheNetworkClient.Connect(nbm, TC("127.0.0.1"));
			}
			auto g = MakeGuard([&]() { cacheNetworkClient.Disconnect(); cacheNetworkServer.DisconnectClients(); });
			*/
			auto g = MakeGuard([&]() { if (cacheClient) cacheClient->GetClient().Disconnect(); });

			SchedulerCreateInfo info(sessionServer);
			info.forceRemote = isRemote;
			info.forceNative = commandType == CommandType_Native;
			info.maxLocalProcessors = maxProcessCount;
			info.cacheClient = cacheClient;
			info.writeToCache = populateCache;
			Scheduler scheduler(info);

			if (!scheduler.EnqueueFromFile(yamlFile))
				return false;

			u32 queued, activeLocal, activeRemote, outFinished;
			scheduler.GetStats(queued, activeLocal, activeRemote, outFinished);

			bool success = true;
			Atomic<u32> counter;
			static Event finished(true);

			scheduler.SetProcessFinishedCallback([&](const ProcessHandle& ph)
				{
					auto& si = ph.GetStartInfo();
					const tchar* desc = si.description;
					if (ph.GetExitCode() != 0 && ph.GetExitCode() != ProcessCancelExitCode)
					{
						logger.Error(TC("%s - Error exit code: %u (%s %s)"), desc, ph.GetExitCode(), si.application, si.arguments);
						success = false;
					}
					u32 c = ++counter;
					logger.BeginScope();
					StringBuffer<128> extra;
					if (ph.IsRemote())
						extra.Append(TC(" [RemoteExecutor: ")).Append(ph.GetExecutingHost()).Append(']');
					else if (ph.GetExecutionType() == ProcessExecutionType_Native)
						extra.Append(TC(" (Not detoured)"));
					else if (ph.GetExecutionType() == ProcessExecutionType_FromCache)
						extra.Append(TC(" (From cache)"));
					logger.Info(TC("[%u/%u] %s%s"), c, queued, desc, extra.data);
					for (auto& line : ph.GetLogLines())
						if (line.text != desc && !StartsWith(line.text.c_str(), TC("   Creating library")))
							logger.Log(line.type, line.text.c_str(), u32(line.text.size()));
					logger.EndScope();

					if (c == queued)
						finished.Set();
				});

			auto RunQueue = [&]()
				{
					logger.Info(TC("Running Scheduler with %u processes"), queued);
					u64 start = GetTime();
					scheduler.Start();
					if (!finished.IsSet())
						return false;
					u64 time = GetTime() - start;
					logger.Info(TC("Scheduler run took %s"), TimeToText(time).str);
					logger.Info(TC(""));
					stopServer.Execute();
					return success;
				};

			if (commandType == CommandType_Agent)
				return RunWithClient([&]() { return RunQueue(); }, maxProcessCount == 1 ? 1 : 4);
			else
				return RunQueue();
		};


		if (!coordinatorName.empty())
		{
			StringBuffer<512> coordinatorWorkDir(g_rootDir);
			coordinatorWorkDir.EnsureEndsWithSlash().Append(coordinatorName);
			StringBuffer<512> binariesDir;
			if (!GetDirectoryOfCurrentModule(logger, binariesDir))
				return false;

			CoordinatorCreateInfo cinfo;
			cinfo.workDir = coordinatorWorkDir.data;
			cinfo.binariesDir = binariesDir.data;

			// TODO: This is very horde specific.. maybe all these parameters should be a string or something
			cinfo.pool = coordinatorPool.c_str();
			cinfo.maxCoreCount = coordinatorMaxCoreCount;
			cinfo.logging = true;
			if (!coordinator.Create(logger, coordinatorName.c_str(), cinfo, networkBackend, networkServer))
				return false;
		}
		auto cg = MakeGuard([&]() { coordinator.Destroy(); });

#if 0	// Annoying that link.exe/lld-link.exe needs path to windows folder.. 
		if (!useScheduler)
		{
			StringBuffer<2048> temp;
			temp.count = GetEnvironmentVariableW(TC("PATH"), temp.data, temp.capacity);
			temp.Append(TC("c:\\sdk\\AutoSDK\\HostWin64\\Win64\\Windows Kits\\10\\bin\\10.0.19041.0\\x64;"));
			SetEnvironmentVariableW(TC("PATH"), temp.data);

		}
#endif

		for (u32 i=0; i!=loopCount; ++i)
		{
			bool success = false;

			if (useScheduler)
			{
				success = RunScheduler(application.c_str());
			}
			else
			{
				switch (commandType)
				{
				case CommandType_Native:
					success = RunLocal(application, arguments, false);
					break;
				case CommandType_Local:
					success = RunLocal(application, arguments, true);
					break;
				case CommandType_Remote:
					success = RunRemote(application, arguments);
					break;
				case CommandType_Agent:
					success = RunAgent(application, arguments);
				}
			}
			if (!success)
				return false;

			if (false)
				networkServer.DisconnectClients();

		}

		logger.BeginScope();
		if (printSummary)
		{
			sessionServer.PrintSummary(logger);
			storageServer.PrintSummary(logger);
			networkServer.PrintSummary(logger);
			KernelStats::GetGlobal().Print(logger, true);
		}
		logger.EndScope();

		return true;
	}
}

#if PLATFORM_WINDOWS
int wmain(int argc, wchar_t* argv[])
{
	int res = uba::WrappedMain(argc, argv) ? 0 : -1;
	Sleep(1); // Here to be able to put a breakpoint just before exit :-)
	return res;
}
#else
int main(int argc, char* argv[])
{
	return uba::WrappedMain(argc, argv) ? 0 : -1;
}
#endif
