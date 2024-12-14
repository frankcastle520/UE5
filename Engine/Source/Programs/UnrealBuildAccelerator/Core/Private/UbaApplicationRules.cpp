// Copyright Epic Games, Inc. All Rights Reserved.

#include "UbaApplicationRules.h"
#include "UbaPlatform.h"

namespace uba
{
	class ApplicationRulesVC : public ApplicationRules
	{
		using Super = ApplicationRules;

	public:
		virtual bool AllowDetach() const override
		{
			return true;
		}

		virtual u64 FileTypeMaxSize(const StringBufferBase& file, bool isSystemOrTempFile) const override
		{
			if (file.EndsWith(TC(".pdb")))
				return 14ull * 1024 * 1024 * 1024; // This is ridiculous
			if (file.EndsWith(TC(".json")) || file.EndsWith(TC(".exp")))
				return 32 * 1024 * 1024;
			if (file.EndsWith(TC(".obj")) || (isSystemOrTempFile && file.Contains(TC("_cl_")))) // There are _huge_ obj files when building with -stresstestunity
				return 1024 * 1024 * 1024;
			return Super::FileTypeMaxSize(file, isSystemOrTempFile);
		}
		virtual bool CanDetour(const tchar* file) const override
		{
			if (file[0] == '\\' && file[1] == '\\' && !Contains(file, TC("vctip_"))) // This might be too aggressive but will cover pipes etc.. might need revisit
				return false;
			return true;
		}
		virtual bool IsThrowAway(const StringView& fileName, bool isRunningRemote) const override
		{
			return fileName.Contains(TC("vctip_")) || Super::IsThrowAway(fileName, isRunningRemote);
		}
		virtual bool KeepInMemory(const StringView& fileName, const tchar* systemTemp, bool isRunningRemote) const override
		{
			if (fileName.Contains(TC("\\vctip_")))
				return true;
			if (fileName.Contains(systemTemp))
				return true;
			return Super::KeepInMemory(fileName, systemTemp, isRunningRemote);
		}

		virtual bool IsExitCodeSuccess(u32 exitCode) const override
		{
			return exitCode == 0;
		}
	};

	class ApplicationRulesClExe : public ApplicationRulesVC
	{
	public:
		virtual bool IsOutputFile(const StringView& fileName) const override
		{
			return fileName.EndsWith(TC(".obj"))
				|| fileName.EndsWith(TC(".dep.json"))
				|| fileName.EndsWith(TC(".sarif"))
				|| fileName.EndsWith(TC(".rc2.res")) // Not really an obj file.. 
				;// || fileName.EndsWith(TC(".h.pch") // Not tested enough
		}

		virtual bool IsRarelyRead(const StringBufferBase& file) const override
		{
			return file.EndsWith(TC(".cpp"))
				|| file.EndsWith(TC(".obj.rsp"));
		}

		virtual bool IsRarelyReadAfterWritten(const StringView& fileName) const override
		{
			return fileName.EndsWith(TC(".dep.json"))
				|| fileName.EndsWith(TC(".sarif"))
				|| fileName.EndsWith(TC(".exe"))
				|| fileName.EndsWith(TC(".dll"));
		}

		virtual bool NeedsSharedMemory(const tchar* file) const override
		{
			return Contains(file, TC("\\_cl_")); // This file is needed when cl.exe spawns link.exe
		}

		virtual bool IsCacheable() const override
		{
			return true;
		}

		virtual bool StoreFileCompressed(const StringView& fileName) const
		{
			return fileName.EndsWith(TC(".obj"));
		}

		virtual bool ShouldExtractSymbols(const StringView& fileName) const
		{
			return fileName.EndsWith(TC(".obj"));
		}
	};

	class ApplicationRulesVcLink : public ApplicationRulesVC
	{
		using Super = ApplicationRulesVC;
	public:
		virtual bool IsOutputFile(const StringView& fileName) const override
		{
			return fileName.EndsWith(TC(".lib"))
				|| fileName.EndsWith(TC(".exp"))
				|| fileName.EndsWith(TC(".pdb"))
				|| fileName.EndsWith(TC(".dll"))
				|| fileName.EndsWith(TC(".exe"))
				|| fileName.EndsWith(TC(".rc2.res")); // Not really an obj file.. 
		}

		virtual bool IsThrowAway(const StringView& fileName, bool isRunningRemote) const override
		{
			return fileName.Contains(TC(".sup.")); // .sup.lib/exp are throw-away files that we don't want created
		}

		virtual bool CanExist(const tchar* file) const override
		{
			return Contains(file, TC("vctip.exe")) == false; // This is hacky but we don't want to start vctip.exe
		}
		virtual bool NeedsSharedMemory(const tchar* file) const override
		{
			return Contains(file, TC("lnk{")) // This file is shared from link.exe to mt.exe and rc.exe so we need to put it shared memory
			 	|| Contains(file, TC("\\_cl_")) // When link.exe is spawned by cl.exe we might use this which is in shared memory
			 	|| EndsWith(file, TStrlen(file), TC(".manifest")); // lld-link.exe is using a different name for files shared with child processes
		}

		virtual bool IsRarelyRead(const StringBufferBase& file) const override
		{
			return file.EndsWith(TC(".exp"))
				|| file.EndsWith(TC(".dll.rsp"))
				|| file.EndsWith(TC(".lib.rsp"))
				|| file.EndsWith(TC(".ilk"))
				|| file.EndsWith(TC(".pdb"));
		}

		virtual bool AllowStorageProxy(const StringBufferBase& file) const override
		{
			if (file.EndsWith(TC(".obj")))
				return false;
			return Super::AllowStorageProxy(file);
		}

		virtual bool IsRarelyReadAfterWritten(const StringView& fileName) const override
		{
			return fileName.EndsWith(TC(".pdb"))
				|| fileName.EndsWith(TC(".exe"))
				|| fileName.EndsWith(TC(".dll"));
		}

		virtual bool IsCacheable() const override
		{
			return true;
		}

		virtual bool ShouldDecompressFiles(const StringView& fileName) const override
		{
			return fileName.IsEmpty() || fileName.EndsWith(TC(".obj"));
		}
	};

	class ApplicationRulesLinkExe : public ApplicationRulesVcLink
	{
		using Super = ApplicationRulesVcLink;

		virtual const tchar* const* LibrariesToPreload() const override
		{
			// Special handling.. it seems loading bcrypt.dll can deadlock when using mimalloc so we make sure to load it here directly instead
			// There is a setting to disable bcrypt dll loading inside mimalloc but with that change mimalloc does not work with older versions of windows
			static constexpr const tchar* preloads[] = 
			{
				TC("bcrypt.dll"),
				TC("bcryptprimitives.dll"),
				nullptr,
			};
			return preloads;
		}
	};

	class ApplicationRulesLldLinkExe : public ApplicationRulesVcLink
	{
		using Super = ApplicationRulesVcLink;

		virtual bool KeepInMemory(const StringView& fileName, const tchar* systemTemp, bool isRunningRemote) const override
		{
			return fileName.EndsWith(TC(".manifest")) && fileName.Contains(systemTemp);
		}

		virtual u64 FileTypeMaxSize(const StringBufferBase& file, bool isSystemOrTempFile) const override
		{
			if (file.Contains(TC(".pdb.tmp")))
				return 14ull * 1024 * 1024 * 1024; // This is ridiculous
			return ApplicationRulesVcLink::FileTypeMaxSize(file, isSystemOrTempFile);
		}

		virtual bool IsOutputFile(const StringView& fileName) const override
		{
			return false//Super::IsOutputFile(fileName)
				|| fileName.Contains(TC(".exe.tmp"))
				|| fileName.Contains(TC(".pdb.tmp"));
		}
	};

	// ==== Clang tool chain ====

	class ApplicationRulesClang : public ApplicationRules
	{
	public:
		virtual bool EnableVectoredExceptionHandler() const override
		{
			return true;
		}

		virtual bool IsExitCodeSuccess(u32 exitCode) const override
		{
			return exitCode == 0;
		}
	};

	class ApplicationRulesClangPlusPlusExe : public ApplicationRulesClang
	{
	public:
		virtual bool AllowDetach() const override
		{
			return true;
		}

		virtual bool IsOutputFile(const StringView& fileName) const override
		{
			return fileName.EndsWith(TC(".c.d"))
				|| fileName.EndsWith(TC(".h.d"))
				|| fileName.EndsWith(TC(".cc.d"))
				|| fileName.EndsWith(TC(".cpp.d"))
				|| fileName.EndsWith(TC(".o.tmp")) // Clang writes to tmp file and then move
				|| fileName.EndsWith(TC(".obj.tmp")) // Clang (verse) writes to tmp file and then move
				;// || fileName.EndsWith(TC(".gch.tmp")); // Need to fix "has been modified since the precompiled header"
		}

		virtual bool IsRarelyRead(const StringBufferBase& file) const override
		{
			return file.EndsWith(TC(".cpp"))
				|| file.EndsWith(TC(".o.rsp"));
		}

		virtual bool IsRarelyReadAfterWritten(const StringView& fileName) const override
		{
			return fileName.EndsWith(TC(".d"));
		}

		virtual bool AllowMiMalloc() const override
		{
			return true;
		}

		virtual bool IsCacheable() const override
		{
			return true;
		}

		virtual bool StoreFileCompressed(const StringView& fileName) const
		{
			return fileName.EndsWith(TC(".obj")) || fileName.EndsWith(TC(".o"));
		}

		virtual bool ShouldExtractSymbols(const StringView& fileName) const
		{
			return fileName.EndsWith(TC(".obj")) || fileName.EndsWith(TC(".o"));
		}
	};

	class ApplicationRulesLdLLdExe : public ApplicationRulesClang
	{
		using Super = ApplicationRulesClang;

		virtual bool IsOutputFile(const StringView& fileName) const override
		{
			return fileName.Contains(TC(".tmp")); // both .so.tmp and .tmp123456
		}

		virtual bool IsRarelyRead(const StringBufferBase& file) const override
		{
			return file.EndsWith(TC(".so.rsp"));
		}

		virtual u64 FileTypeMaxSize(const StringBufferBase& file, bool isSystemOrTempFile) const override
		{
			return 14ull * 1024 * 1024 * 1024; // This is ridiculous (needed for asan targets)
		}

		virtual bool ShouldDecompressFiles(const StringView& fileName) const override
		{
			return fileName.IsEmpty() || fileName.EndsWith(TC(".o"));
		}
	};

	class ApplicationRulesLlvmObjCopyExe : public ApplicationRulesClang
	{
		using Super = ApplicationRulesClang;

		virtual bool IsOutputFile(const StringView& fileName) const override
		{
			return fileName.Contains(TC(".temp-stream-"));
		}

		virtual u64 FileTypeMaxSize(const StringBufferBase& file, bool isSystemOrTempFile) const override
		{
			if (IsOutputFile(file))
				return 14ull * 1024 * 1024 * 1024; // This is ridiculous (needed for asan targets)
			return Super::FileTypeMaxSize(file, isSystemOrTempFile);
		}
	};

	class ApplicationRulesDumpSymsExe : public ApplicationRulesClang
	{
		virtual bool IsOutputFile(const StringView& fileName) const override
		{
			return false; // fileName.EndsWith(TC(".psym")); With psym as output file the BreakpadSymbolEncoder fails to output a .sym file
		}
	};

	class ApplicationRulesOrbisClangPlusPlusExe : public ApplicationRulesClangPlusPlusExe
	{
		using Super = ApplicationRulesClangPlusPlusExe;

		virtual bool IsThrowAway(const StringView& fileName, bool isRunningRemote) const override
		{
			return fileName.EndsWith(TC("-telemetry.json")) || Super::IsThrowAway(fileName, isRunningRemote);
		}

		//virtual bool NeedsSharedMemory(const tchar* file)
		//{
		//	return fileName.Contains(TC("lto-llvm")); // Used by a clang based platform's link time optimization pass. Shared from lto process back to linker process
		//}
	};

	class ApplicationRulesOrbisLdExe : public ApplicationRules
	{
		using Super = ApplicationRules;

		//virtual bool IsOutputFile(const StringView& fileName) const override
		//{
		//	return fileName.EndsWith(TC(".self")) || Equals(file, TC("Symbols.map"));
		//}
		virtual bool KeepInMemory(const StringView& fileName, const tchar* systemTemp, bool isRunningRemote) const override
		{
			return Super::KeepInMemory(fileName, systemTemp, isRunningRemote)
				|| fileName.Contains(TC("thinlto-"));// Used by a clang based platform's link time optimization pass. Shared from lto process back to linker process
		}
		virtual bool NeedsSharedMemory(const tchar* file) const override
		{
			return Contains(file, TC("thinlto-")); // Used by a clang based platform's link time optimization pass. Shared from lto process back to linker process
		}

		virtual bool ShouldDecompressFiles(const StringView& fileName) const override
		{
			return fileName.IsEmpty() || fileName.EndsWith(TC(".o"));
		}
	};

	class ApplicationRulesProsperoClangPlusPlusExe : public ApplicationRulesClangPlusPlusExe
	{
		using Super = ApplicationRulesClangPlusPlusExe;

		virtual bool IsOutputFile(const StringView& fileName) const override
		{
			return fileName.Contains(TC(".self")) || Super::IsOutputFile(fileName);
		}

		virtual bool IsThrowAway(const StringView& fileName, bool isRunningRemote) const override
		{
			return Super::IsThrowAway(fileName, isRunningRemote)
				|| (isRunningRemote && fileName.EndsWith(TC("-telemetry.json")));
		}

		//virtual bool NeedsSharedMemory(const tchar* file)
		//{
		//	return fileName.Contains(TC("lto-llvm")); // Used by a clang based platform's link time optimization pass. Shared from lto process back to linker process
		//}
	};

	class ApplicationRulesProsperoLldExe : public ApplicationRules
	{
		using Super = ApplicationRules;

		virtual bool IsOutputFile(const StringView& fileName) const override
		{
			return fileName.Contains(TC(".self"));
		}

		virtual bool IsThrowAway(const StringView& fileName, bool isRunningRemote) const override
		{
			return Super::IsThrowAway(fileName, isRunningRemote)
				|| (isRunningRemote && fileName.EndsWith(TC("-telemetry.json")));
		}

		//virtual bool NeedsSharedMemory(const tchar* file)
		//{
		//	return fileName.Contains(TC("lto-llvm")); // Used by a clang based platform's link time optimization pass. Shared from lto process back to linker process
		//}

		virtual bool ShouldDecompressFiles(const StringView& fileName) const override
		{
			return fileName.IsEmpty() || fileName.EndsWith(TC(".o"));
		}
	};

	// ====

	class ApplicationRulesISPCExe : public ApplicationRules
	{
		virtual bool AllowDetach() const override
		{
			return true;
		}

		virtual bool IsOutputFile(const StringView& fileName) const override
		{
			return fileName.Contains(TC(".generated.dummy"))
				|| fileName.EndsWith(TC(".ispc.bc"))
				|| fileName.EndsWith(TC(".ispc.txt"))
				|| fileName.EndsWith(TC(".obj"))
				|| fileName.EndsWith(TC(".o")); // Used when compiling for linux
		}

		virtual bool IsCacheable() const override
		{
			return true;
		}

		virtual bool StoreFileCompressed(const StringView& fileName) const
		{
			return fileName.EndsWith(TC(".obj"));
		}

		virtual bool ShouldExtractSymbols(const StringView& fileName) const
		{
			return fileName.EndsWith(TC(".obj")) || fileName.EndsWith(TC(".o"));
		}
	};

	class ApplicationRulesUBTDll : public ApplicationRules
	{
		virtual bool IsOutputFile(const StringView& fileName) const override
		{
			return false;
			// TODO: These does not work when UnrealBuildTool creates these files multiple times in a row (building multiple targets)
			// ... on output they get stored as file mappings.. and next execution of ubt opens them for write (writing file mappings not implemented right now)
			//return fileName.EndsWith(TC(".modules"))
			//	|| fileName.EndsWith(TC(".target"))
			//	|| fileName.EndsWith(TC(".version"));
		}
	};

	class ApplicationRulesPVSStudio : public ApplicationRules
	{
		virtual bool IsOutputFile(const StringView& fileName) const override
		{
			return fileName.EndsWith(TC(".PVS-Studio.log"))
				|| fileName.EndsWith(TC(".pvslog"))
				|| fileName.EndsWith(TC(".stacktrace.txt"));
		}
		
		virtual bool IsRarelyRead(const StringBufferBase& file) const override
		{
			return file.EndsWith(TC(".i"))
				|| file.EndsWith(TC(".PVS-Studio.log"))
				|| file.EndsWith(TC(".pvslog"))
				|| file.EndsWith(TC(".stacktrace.txt"));
		}

#if PLATFORM_WINDOWS
		virtual void RepairMalformedLibPath(const tchar* path) const override
		{
			// There is a bug where the path passed into wsplitpath_s is malformed and not null terminated correctly
			const tchar* pext = TStrstr(path, TC(".dll"));
			if (pext == nullptr) pext = TStrstr(path, TC(".DLL"));
			if (pext == nullptr) pext = TStrstr(path, TC(".exe"));
			if (pext == nullptr) pext = TStrstr(path, TC(".EXE"));
			if (pext != nullptr && *(pext + 4) != 0) *(const_cast<tchar*>(pext + 4)) = 0;
		}
#endif // #if PLATFORM_WINDOWS
	};

	class ApplicationRulesShaderCompileWorker : public ApplicationRules
	{
		virtual bool IsRarelyRead(const StringBufferBase& file) const override
		{
			return file.Contains(TC(".uba."));
		}
	};

	class ApplicationRulesUbaObjTool : public ApplicationRules
	{
		virtual bool IsOutputFile(const StringView& fileName) const override
		{
			return fileName.EndsWith(TC(".obj"))
				|| fileName.EndsWith(TC(".exp"));
		}

		virtual bool StoreFileCompressed(const StringView& fileName) const
		{
			return fileName.EndsWith(TC(".obj"));
		}
	};

	const RulesRec* GetApplicationRules()
	{
		// TODO: Add support for data driven rules.
		// Note, they need to be possible to serialize from server to client and then from client to each detoured process

		static RulesRec rules[]
		{
			{ TC(""),							new ApplicationRules() },		// Must be index 0
#if PLATFORM_WINDOWS
			{ TC("cl.exe"),						new ApplicationRulesClExe() },	// Must be index 1
			{ TC("link.exe"),					new ApplicationRulesLinkExe() }, // Must be index 2
			{ TC("lib.exe"),					new ApplicationRulesVcLink() },
			{ TC("cvtres.exe"),					new ApplicationRulesLinkExe() },
			{ TC("mt.exe"),						new ApplicationRulesVcLink() },
			{ TC("rc.exe"),						new ApplicationRulesVcLink() },
			{ TC("lld-link.exe"),				new ApplicationRulesLldLinkExe() },
			{ TC("clang++.exe"),				new ApplicationRulesClangPlusPlusExe() },
			{ TC("clang-cl.exe"),				new ApplicationRulesClangPlusPlusExe() },
			{ TC("verse-clang-cl.exe"),			new ApplicationRulesClangPlusPlusExe() },
			{ TC("ispc.exe"),					new ApplicationRulesISPCExe() },
			{ TC("orbis-clang.exe"),			new ApplicationRulesOrbisClangPlusPlusExe() },
			{ TC("orbis-ld.exe"),				new ApplicationRulesOrbisLdExe() },
			{ TC("orbis-ltop.exe"),				new ApplicationRulesOrbisLdExe() },
			{ TC("prospero-clang.exe"),			new ApplicationRulesProsperoClangPlusPlusExe() },
			{ TC("prospero-lld.exe"),			new ApplicationRulesProsperoLldExe() },
			{ TC("dump_syms.exe"),				new ApplicationRulesDumpSymsExe() },
			{ TC("ld.lld.exe"),					new ApplicationRulesLdLLdExe() },
			{ TC("llvm-objcopy.exe"),			new ApplicationRulesLlvmObjCopyExe() },
			{ TC("UnrealBuildTool.dll"),		new ApplicationRulesUBTDll() },
			{ TC("PVS-Studio.exe"),				new ApplicationRulesPVSStudio() },
			{ TC("UbaObjTool.exe"),				new ApplicationRulesUbaObjTool() },
			{ TC("ShaderCompileWorker.exe"),	new ApplicationRulesShaderCompileWorker() },
			//{ L"MSBuild.dll"),				new ApplicationRules() },
			//{ L"BreakpadSymbolEncoder.exe"),	new ApplicationRulesClang() },
			//{ L"cmd.exe"),		new ApplicationRules() },
#else
			{ TC("clang++"),					new ApplicationRulesClangPlusPlusExe() },
			{ TC("ld.lld"),						new ApplicationRulesLdLLdExe() },
			{ TC("ShaderCompileWorker"),		new ApplicationRulesShaderCompileWorker() },
#endif
			{ nullptr, nullptr }
		};

		static bool initRules = []()
			{
				u32 index = 0;
				for (RulesRec* rec = rules; rec->app; ++rec)
					rec->rules->index = index++;
				return true;
			}();

		return rules;
	}
}