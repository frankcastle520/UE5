// Copyright Epic Games, Inc. All Rights Reserved.

#if defined(UE_WITH_IAS_TOOL)

#include "Command.h"

#include <Containers/UnrealString.h>
#include <HAL/FileManager.h>
#include <HAL/PlatformProcess.h>
#include <IO/IoBuffer.h>
#include <IO/Http/Client.h>
#include <Misc/Paths.h>
#include <Misc/ScopeExit.h>

#if PLATFORM_WINDOWS
#	include <Windows/AllowWindowsPlatformTypes.h>
#		include <winsock2.h>
#		include <ws2tcpip.h>
#	include <Windows/HideWindowsPlatformTypes.h>
#endif

#include <cstdio>

namespace UE::IoStore::Tool
{

////////////////////////////////////////////////////////////////////////////////
static void LoadCaCerts()
{
	using namespace UE::IoStore::HTTP;

	IFileManager& Ifm = IFileManager::Get();
	FString PemPath = FPaths::EngineContentDir() / TEXT("Certificates/ThirdParty/cacert.pem");
	FArchive* Reader = Ifm.CreateFileReader(*PemPath);
	check(Reader != nullptr)

	uint32 Size = uint32(Reader->TotalSize());
	FIoBuffer PemData(Size);
	FMutableMemoryView PemView = PemData.GetMutableView();
	Reader->Serialize(PemView.GetData(), Size);

	FCertRoots CaRoots(PemData.GetView());
	check(CaRoots.IsValid());

	FCertRoots::SetDefault(MoveTemp(CaRoots));
	delete Reader;
}

////////////////////////////////////////////////////////////////////////////////
static int32 PurlCommandEntry(const FContext& Context)
{
#if PLATFORM_WINDOWS
	WSADATA WsaData;
	if (WSAStartup(MAKEWORD(2, 2), &WsaData) == 0x0a9e0493)
		return 1;
	ON_SCOPE_EXIT { WSACleanup(); };
#endif

	using namespace UE::IoStore::HTTP;

	LoadCaCerts();

	FStringView Url = Context.Get<FStringView>(TEXT("Url"));
	auto AnsiUrl = StringCast<ANSICHAR>(Url.GetData(), Url.Len());

	FString Method(Context.Get<FStringView>(TEXT("-Method"), TEXT("GET")));
	Method = Method.ToUpper();
	auto AnsiMethod = StringCast<ANSICHAR>(*Method);

	bool bChunked = false;
	uint32 ContentSize = 0;
	auto Sink = [Dest=FIoBuffer(), &ContentSize, &bChunked] (const FTicketStatus& Status) mutable
	{
		if (Status.GetId() == FTicketStatus::EId::Response)
		{
			FResponse& Response = Status.GetResponse();
			FAnsiStringView Message = Response.GetStatusMessage();
			std::printf("%d %.*s\n",
				Response.GetStatusCode(),
				Message.Len(),
				Message.GetData()
			);
			Response.ReadHeaders([] (FAnsiStringView Name, FAnsiStringView Value)
			{
				std::printf("%.*s: %.*s\n",
					Name.Len(),
					Name.GetData(),
					Value.Len(),
					Value.GetData()
				);
				return true;
			});

			bChunked = (Response.GetContentLength() == -1);
			Response.SetDestination(&Dest);
			return;
		}

		if (Status.GetId() == FTicketStatus::EId::Content)
		{
			ContentSize += uint32(Dest.GetSize());
			return;
		}

		if (Status.GetId() == FTicketStatus::EId::Error)
		{
			const char* Reason = Status.GetErrorReason();
			std::printf("ERROR: %s\n", Reason);
			return;
		}
	};

	FEventLoop Loop;
	FEventLoop::FRequestParams RequestParams;
	if (Context.Get<bool>(TEXT("-Redirect")))
	{
		RequestParams.bAutoRedirect = true;
	}
	FRequest Request = Loop.Request(AnsiMethod, AnsiUrl, &RequestParams);
	Loop.Send(MoveTemp(Request), Sink);
	
	while (Loop.Tick(-1))
	{
		FPlatformProcess::SleepNoStats(0.1f);
	}

	std::printf("Data: %u bytes\n", ContentSize);

	return 0;
}

////////////////////////////////////////////////////////////////////////////////
static FCommand PurlCommand(
	PurlCommandEntry,
	TEXT("Purl"),
	TEXT("Uses IoStore's HTTP client to download a URL"),
	{
		TArgument<FStringView>(TEXT("Url"), TEXT("Url to download")),
		TArgument<FStringView>(TEXT("-Method"), TEXT("Request method")),
		TArgument<bool>(TEXT("-Redirect"), TEXT("Follow 30x redirects")),
	}
);

} // namespace UE::IoStore::Tool

#endif // UE_WITH_IAS_TOOL
