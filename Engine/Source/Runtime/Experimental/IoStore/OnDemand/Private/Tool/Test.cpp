// Copyright Epic Games, Inc. All Rights Reserved.

#if defined(UE_WITH_IAS_TOOL)

#include "Command.h"

#include <Async/TaskGraphInterfaces.h>
#include <Containers/StringConv.h>
#include <Containers/UnrealString.h>
#include <Misc/CommandLine.h>

namespace UE::IoStore {

////////////////////////////////////////////////////////////////////////////////
namespace IasJournaledFileCacheTest { void Tests(const TCHAR*);					}
namespace HTTP						{ void IasHttpTest(const ANSICHAR*, uint32);}

namespace Tool {

////////////////////////////////////////////////////////////////////////////////
void CommandTest();

////////////////////////////////////////////////////////////////////////////////
static void HttpTests(const FContext& Context)
{
	auto TestHost = Context.Get<FStringView>(TEXT("-Host"), TEXT("localhost"));

	auto TestHostAnsi = StringCast<ANSICHAR>(TestHost.GetData());
	uint32 Seed = Context.Get<uint32>(TEXT("-HttpSeed"), 493);
	HTTP::IasHttpTest(TestHostAnsi.Get(), Seed);
}

////////////////////////////////////////////////////////////////////////////////
static void CacheTests(const FContext& Context)
{
	FStringView CacheDirStr = Context.Get<FStringView>(TEXT("-Dir"));

	const TCHAR* CacheDir = CacheDirStr.IsEmpty() ? nullptr : CacheDirStr.GetData();
	IasJournaledFileCacheTest::Tests(CacheDir);
}

////////////////////////////////////////////////////////////////////////////////
static int32 TestCommandEntry(const FContext& Context)
{
	FStringView Only = Context.Get<FStringView>(TEXT("-Only"));

	if (Only.IsEmpty())
	{
		CommandTest();
	}

	if (Only.IsEmpty() || Only == TEXT("cache"))
	{
		CacheTests(Context);
	}

	if (Only.IsEmpty() || Only == TEXT("http"))
	{
		HttpTests(Context);
	}

	return 0;
}

////////////////////////////////////////////////////////////////////////////////
static FCommand TestCommand(
	TestCommandEntry,
	TEXT("Test"),
	TEXT("Run IAS tests"),
	{
		TArgument<FStringView>(TEXT("-Host"), TEXT("Host of the HTTP test server")),
		TArgument<FStringView>(TEXT("-Dir"), TEXT("Primary directory to use for cache tests")),
		TArgument<FStringView>(TEXT("-Only"), TEXT("Only run a particular test (http|cache)")),
		TArgument<uint32>(TEXT("-HttpSeed"), TEXT("Integer value to seed test HTTP server")),
	}
);

} // namespace Tool
} // namespace UE::IoStore

#endif // UE_WITH_IAS_TOOL
