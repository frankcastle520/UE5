// Copyright Epic Games, Inc. All Rights Reserved.

#define Local_GetLongPathNameW uba::GetLongPathNameW

#include "UbaObjectFile.h"
#include "UbaDirectoryIterator.h"
#include "UbaFileAccessor.h"
#include "UbaImportLibWriter.h"
#include "UbaPathUtils.h"
#include "UbaVersion.h"
#include "UbaWorkManager.h"

namespace uba
{
	const tchar* Version = GetVersionString();
	u32	DefaultProcessorCount = []() { return GetLogicalProcessorCount(); }();

	int PrintHelp(const tchar* message)
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
		logger.Info(TC("   UbaObjTool v%s%s"), Version, dbgStr);
		logger.Info(TC("-------------------------------------------"));
		logger.Info(TC(""));
		logger.Info(TC("  UbaObjTool.exe [options...] <objfile/libfile>"));
		logger.Info(TC(""));
		logger.Info(TC("   Options:"));
		logger.Info(TC("    -printsymbols            Print the symbols found in obj file"));
		logger.Info(TC("    -stripexports            Will strip exports and write them out in a .exp file"));
		logger.Info(TC("    -writeimplib=<file>      Will create a import library from symbols collected from obj/lib files"));
		logger.Info(TC(""));
		logger.Info(TC("  --- OR ---"));
		logger.Info(TC(""));
		logger.Info(TC("  UbaObjTool.exe @<rspfile>"));
		logger.Info(TC(""));
		logger.Info(TC("   Response file options:"));
		logger.Info(TC("    /S:<objfile>             Obj file to strip. Will produce a .strip.obj file. Multiple allowed"));
		logger.Info(TC("    /D:<objfile>             Obj file depending on obj files to strip. Multiple allowed"));
		logger.Info(TC("    /O:<objfile>             Obj file to output containing exports and loopbacks"));
		logger.Info(TC("    /T:<platform>            Target platform"));
		logger.Info(TC("    /M:<module>              Name of module. Needed in emd files"));
		logger.Info(TC("    /COMPRESS                Write '/O' file compressed"));
		logger.Info(TC(""));
		return -1;
	}

	// TODO: Add to rsp file instead
	const char* NeededImports[] = 
	{
		"NvOptimusEnablement",
		"AmdPowerXpressRequestHighPerformance",
		"D3D12SDKVersion",
		"D3D12SDKPath",
	};

	int WrappedMain(int argc, tchar* argv[])
	{
		using namespace uba;

		TString objFile;
		bool printSymbols = false;
		bool stripExports = false;
		bool writeImpLib = false;
		bool allowLibInputs = false;
		bool isImpLibRsp = false;

		Vector<TString> objFilesToStrip;
		Vector<TString> objFilesDependencies;
		TString extraObjFile;
		Vector<TString> objFilesForImpLib;
		std::string impLibName;
		TString impLibFile;
		TString platform;
		TString moduleName;

		auto parseArg = [&](const tchar* arg, bool isRsp)
			{
				StringBuffer<> name;
				StringBuffer<> value;

				if (const tchar* equals = TStrchr(arg,'='))
				{
					name.Append(arg, equals - arg);
					value.Append(equals+1);
				}
				else
				{
					const tchar* colon = TStrchr(arg,':');
					if (colon && colon[1] != '\\')
					{
						name.Append(arg, colon - arg);
						const tchar* valueStart = colon+1;
						if (*valueStart == '\"')
							++valueStart;
						value.Append(valueStart);
						if (value.data[value.count-1] == '\"')
							value.Resize(value.count-1);

					}
					else
					{
						name.Append(arg);
					}
				}

				if (isImpLibRsp)
				{
					if (name.Equals(TC("/NOLOGO")))
					{
					}
					else if (name.Equals(TC("/errorReport")))
					{
					}
					else if (name.Equals(TC("/MACHINE")))
					{
						//if (!value.Equals(TC("x64")))
						//{
						//	logger.Error(TC("only x64 supported"));
						//	return 0;
						//}
					}
					else if (name.Equals(TC("/SUBSYSTEM")))
					{
					}
					else if (name.Equals(TC("/DEF")))
					{
						writeImpLib = true;
					}
					else if (name.Equals(TC("/NAME")))
					{
						char buffer[256];
						value.Parse(buffer, sizeof(buffer));
						impLibName = buffer;
					}
					else if (name.Equals(TC("/OUT")))
					{
						impLibFile = value.data;
					}
					else if (name.Equals(TC("/IGNORE")))
					{
					}
					else if (name.Equals(TC("/NODEFAULTLIB")))
					{
					}
					else if (name.Equals(TC("/LTCG")))
					{
					}
					else
					{
						objFilesForImpLib.push_back(name.data);
					}
				}
				else if (name.StartsWith(TC("/D")))
				{
					objFilesDependencies.push_back(value.data);
				}
				else if (name.StartsWith(TC("/S")))
				{
					objFilesToStrip.push_back(value.data);
				}
				else if (name.StartsWith(TC("/O")))
				{
					extraObjFile = value.data;
				}
				else if (name.StartsWith(TC("/T")))
				{
					platform = value.data;
				}
				else if (name.StartsWith(TC("/M")))
				{
					moduleName = value.data;
				}
				else if (name.Equals(TC("-printsymbols")))
				{
					printSymbols = true;
				}
				else if (name.Equals(TC("-writeimplib")))
				{
					impLibFile = value.data;
					writeImpLib = true;
					allowLibInputs = true;
				}
				else if (name.Equals(TC("-stripexports")))
				{
					stripExports = true;
				}
				else if (name.Equals(TC("/LIB")))
				{
					isImpLibRsp = true;
					writeImpLib = true;
				}
				else if (name.Equals(TC("-?")))
				{
					return PrintHelp(TC(""));
				}
				else if (objFile.empty() && name[0] != '-' && name[0] != '/')
				{
					objFile = name.data;
					return 0;
				}
				else
				{
					StringBuffer<> msg;
					msg.Appendf(TC("Unknown argument '%s'"), name.data);
					return PrintHelp(msg.data);
				}
				return 0;
			};

		for (int i=1; i!=argc; ++i)
		{
			const tchar* arg = argv[i];
			if (*arg == '@')
			{
				++arg;
				StringBuffer<> temp;
				if (*arg == '\"')
				{
					temp.Append(arg + 1);
					temp.Resize(temp.count - 1);
					arg = temp.data;
				}
				int res = 0;
				LoggerWithWriter logger(g_consoleLogWriter, TC(""));
				if (!ReadLines(logger, arg, [&](const TString& line)
					{
						res = parseArg(line.c_str(), true);
						return res == 0;
					}))
					return -1;
				if (res != 0)
					return res;
				continue;
			}
			int res = parseArg(arg, false);
			if (res != 0)
				return res;
		}

		FilteredLogWriter logWriter(g_consoleLogWriter, LogEntryType_Info);
		LoggerWithWriter logger(logWriter, TC(""));

		if (!objFilesToStrip.empty())
		{
			CriticalSection cs;
			Atomic<bool> success = true;
			UnorderedSymbols allExternalImports; // Imports needed from the outside of the stripped obj files

			for (auto imp : NeededImports)
				allExternalImports.insert(imp);

			u32 workerCount = DefaultProcessorCount;
			WorkManagerImpl workManager(workerCount);
			workManager.ParallelFor(workerCount, objFilesDependencies, [&](auto& it)
				{
					const TString& exiFilename = *it;

					SymbolFile symbolFile;
					if (!symbolFile.ParseFile(logger, exiFilename.c_str()))
					{
						success = false;
						return;
					}
					ScopedCriticalSection _(cs);
					allExternalImports.insert(symbolFile.imports.begin(), symbolFile.imports.end());
				});
			if (!success)
				return -1;

			UnorderedSymbols allInternalImports; // Imports that the obj files has. Note these could be existing in the obj files and then we might need to create loopbacks
			UnorderedExports allExports; // Exports from all the obj files.

			Map<TString, ObjectFile*> objectFiles;
			auto g = MakeGuard([&]() { for (auto& kv : objectFiles) delete kv.second; });

			workManager.ParallelFor(workerCount, objFilesToStrip, [&](auto& it)
				{
					const TString& exiFilename = *it;
					SymbolFile symbolFile;
					if (!symbolFile.ParseFile(logger, exiFilename.c_str()))
					{
						success = false;
						return;
					}
					ScopedCriticalSection _(cs);
					allInternalImports.insert(symbolFile.imports.begin(), symbolFile.imports.end());
					allExports.insert(symbolFile.exports.begin(), symbolFile.exports.end());
				});
			if (!success)
				return -1;

			if (!extraObjFile.empty())
			{
				{
					// Ugly hack. These symbols are exported from PosixShim and used by libcrypto.a but since we don't have an .exi file for libcrypto
					// we do it this way for now
					const char* symbolsToNeverStrip[] = 
					{
						"read_system_certificates_NP",
						"inet_ntoa",
						"gethostbyname",
						"h_errno",
						"getservbyname",
						"ioctl",
						"fcntl_shim",
					};
					for (auto symbol : symbolsToNeverStrip)
						allExternalImports.insert(symbol);
				}

				if (!ObjectFile::CreateExtraFile(logger, extraObjFile, moduleName, platform, allExternalImports, allInternalImports, allExports, true))
					return -1;
			}
			//logger.Info(TC("Reduced export count from %llu to %llu"), totalExportCount.load(), totalKeptExportCount.size());
		}
		else if (writeImpLib)
		{
			StringBuffer<> currentDir;
			GetCurrentDirectoryW(currentDir);
			currentDir.EnsureEndsWithSlash();
			ImportLibWriter writer;
			if (objFilesForImpLib.empty() && !objFile.empty())
				objFilesForImpLib.push_back(objFile);

			Atomic<bool> success = true;
			Vector<ObjectFile*> objFiles;
			objFiles.resize(objFilesForImpLib.size());
			u32 workerCount = DefaultProcessorCount;
			WorkManagerImpl workManager(workerCount);
			workManager.ParallelFor(workerCount, objFilesForImpLib, [&](auto& it)
			{
				auto& o = *it;
				StringBuffer<> fixedPath;
				FixPath(o.c_str(), currentDir.data, currentDir.count, fixedPath);
				if (fixedPath.EndsWith(TC(".res")) || (fixedPath.EndsWith(TC(".lib")) && !allowLibInputs))
					return;
				if (ObjectFile* objectFile = ObjectFile::OpenAndParse(logger, fixedPath.data))
				{
					//objectFile->RemoveExportedSymbol("DllMain"); // This is used to patch xinput.lib
					objFiles[it - objFilesForImpLib.begin()] = objectFile;
				}
				else
					success = false;
			});
			if (!success)
				return -1;

			if (impLibName.empty() && objFiles.size() == 1)
				impLibName = objFiles[0]->GetLibName();

			writer.Write(logger, objFiles, impLibName.c_str(), impLibFile.c_str());
		}
		else
		{
			if (objFile.empty())
				return PrintHelp(TC("No obj, lib or rsp file provided"));

			ObjectFile* objectFile = ObjectFile::OpenAndParse(logger, objFile.c_str());
			if (!objectFile)
				return -1;
			auto g = MakeGuard([&](){ delete objectFile; });

			if (printSymbols)
			{
				for (auto& symbol : objectFile->GetImports())
					logger.Info(TC("I %S"), symbol.c_str());

				for (auto& kv : objectFile->GetExports())
					logger.Info(TC("E %S%S"), kv.first.c_str(), kv.second.extra.c_str());
			}

			if (stripExports)
			{
				if (!objectFile->CopyMemoryAndClose())
					return false;

				const tchar* fileName = objFile.c_str();
				const tchar* lastDot = TStrrchr(fileName, '.');
				UBA_ASSERT(lastDot);
				StringBuffer<> exportsFile;
				exportsFile.Append(fileName, lastDot - fileName).Append(TC(".exi"));

				if (!objectFile->WriteImportsAndExports(logger, exportsFile.data))
					return false;

#if 0
				objectFile->StripExports(logger);
				StringBuffer<> newFilename;
				newFilename.Append(fileName, lastDot - fileName).Append(TC(".TEST")).Append(lastDot);
				FileAccessor fa(logger, newFilename.data);
				fa.CreateWrite();
				fa.Write(objectFile->GetData(), objectFile->GetDataSize());
				fa.Close();

#endif
			}
		}
		return 0;
	}
}

#if PLATFORM_WINDOWS
int wmain(int argc, wchar_t* argv[])
{
	return uba::WrappedMain(argc, argv);
}
#else
int main(int argc, char* argv[])
{
	return uba::WrappedMain(argc, argv);
}
#endif
