// Copyright Epic Games, Inc. All Rights Reserved.

#include "StorageServerConnectionDebug.h"

#include "Debug/DebugDrawService.h"
#include "Engine/GameEngine.h"
#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"
#include "ProfilingDebugging/CsvProfiler.h"
#include "ProfilingDebugging/CountersTrace.h"
#include "Templates/UniquePtr.h"
#include "StorageServerClientModule.h"
#include "HAL/IConsoleManager.h"
#include "HAL/Runnable.h"
#include "HAL/RunnableThread.h"
#include "HAL/Event.h"
#include "HAL/PlatformProcess.h"
#include "Misc/ScopeLock.h"
#include "Misc/CoreDelegates.h"

#if !UE_BUILD_SHIPPING

CSV_DEFINE_CATEGORY(ZenServerStats, true);

CSV_DEFINE_STAT(ZenServerStats, ThroughputMbps);
CSV_DEFINE_STAT(ZenServerStats, MaxReqThroughputMbps);
CSV_DEFINE_STAT(ZenServerStats, MinReqThroughputMbps);
CSV_DEFINE_STAT(ZenServerStats, RequestCountPerSec);

TRACE_DECLARE_UNCHECKED_FLOAT_COUNTER(ZenClient_ThroughputMbps,       TEXT("ZenClient/ThroughputMbps (decompressed)"));
TRACE_DECLARE_UNCHECKED_FLOAT_COUNTER(ZenClient_MaxReqThroughputMbps, TEXT("ZenClient/MaxReqThroughputMbps (decompressed)"));
TRACE_DECLARE_UNCHECKED_FLOAT_COUNTER(ZenClient_MinReqThroughputMbps, TEXT("ZenClient/MinReqThroughputMbps (decompressed)"));
TRACE_DECLARE_UNCHECKED_INT_COUNTER(  ZenClient_RequestCountPerSec,   TEXT("ZenClient/RequestCountPerSec"));


static bool GZenShowGraphs = false;
static FAutoConsoleVariableRef CVarZenShowGraphs(
	TEXT("zen.showgraphs"),
	GZenShowGraphs,
	TEXT("Show ZenServer Stats Graph"),
	ECVF_Default
);

static bool GZenShowStats = true;
static FAutoConsoleVariableRef CVarZenShowStats(
	TEXT("zen.showstats"),
	GZenShowStats,
	TEXT("Show ZenServer Stats"),
	ECVF_Default
);


namespace
{
	static constexpr int    OneMinuteSeconds = 60;
	static constexpr double WidthSeconds = OneMinuteSeconds * 0.25;
}


bool FStorageServerConnectionDebug::OnTick(float)
{
	FScopeLock Lock(&CS);

	static constexpr double FrameSeconds = 1.0;
	
	double StatsTimeNow = FPlatformTime::Seconds();
	double Duration = StatsTimeNow - UpdateStatsTime;

	//Persistent debug message and CSV stats
	if (Duration > UpdateStatsTimer)
	{
		UpdateStatsTime = StatsTimeNow;

		IStorageServerPlatformFile::FConnectionStats Stats;
		StorageServerPlatformFile->GetAndResetConnectionStats(Stats);
		if (Stats.MaxRequestThroughput > Stats.MinRequestThroughput)
		{
			MaxReqThroughput = Stats.MaxRequestThroughput;
			MinReqThroughput = Stats.MinRequestThroughput;
		
			Throughput = ((double)(Stats.AccumulatedBytes * 8) / Duration) / 1000000.0; //Mbps
			ReqCount = ceil((double)Stats.RequestCount / Duration);
		}

		if (GZenShowStats && GEngine)
		{
			FString ZenConnectionDebugMsg;
			ZenConnectionDebugMsg = FString::Printf(TEXT("ZenServer streaming from %s [%.2fMbps]"), *HostAddress, Throughput);
			GEngine->AddOnScreenDebugMessage((uint64)this, 86400.0f, FColor::White, ZenConnectionDebugMsg, false);
		}
		
		History.push_back({ StatsTimeNow, MaxReqThroughput, MinReqThroughput, Throughput, ReqCount });

		TRACE_COUNTER_SET(ZenClient_ThroughputMbps,       Throughput);
		TRACE_COUNTER_SET(ZenClient_MaxReqThroughputMbps, MaxReqThroughput);
		TRACE_COUNTER_SET(ZenClient_MinReqThroughputMbps, MinReqThroughput);
		TRACE_COUNTER_SET(ZenClient_RequestCountPerSec,   ReqCount);
	}

	while (!History.empty() && StatsTimeNow - History.front().Time > WidthSeconds)
	{
		History.erase(History.begin());
	}

	//CSV stats need to be written per frame (only send if we're running from the gamethread ticker, not the startup debug thread)
	if (IsInGameThread())
	{
		CSV_CUSTOM_STAT_DEFINED(ThroughputMbps, Throughput, ECsvCustomStatOp::Set);
		CSV_CUSTOM_STAT_DEFINED(MaxReqThroughputMbps, MaxReqThroughput, ECsvCustomStatOp::Set);
		CSV_CUSTOM_STAT_DEFINED(MinReqThroughputMbps, MinReqThroughput, ECsvCustomStatOp::Set);
		CSV_CUSTOM_STAT_DEFINED(RequestCountPerSec, (int32)ReqCount, ECsvCustomStatOp::Set);
	}

	return true;
}



void FStorageServerConnectionDebug::OnDraw(UCanvas* Canvas, APlayerController*)
{
	FScopeLock Lock(&CS);

	static constexpr float  ViewXRel = 0.2f;
	static constexpr float  ViewYRel = 0.12f;
	static constexpr float  ViewWidthRel = 0.4f;
	static constexpr float  ViewHeightRel = 0.18f;
	static constexpr double TextHeight = 16.0;
	static constexpr double MaxHeightScaleThroughput = 6000;
	static constexpr double MaxHeightScaleRequest = 5000;
	static constexpr int	LineThickness = 3;
	static double			HeightScaleThroughput = MaxHeightScaleThroughput;
	static double			HeightScaleRequest = MaxHeightScaleRequest;

	if (GZenShowGraphs)
	{
		double StatsTimeNow = FPlatformTime::Seconds();

		int ViewX = (int)(ViewXRel * Canvas->ClipX);
		int ViewY = (int)(ViewYRel * Canvas->ClipY);
		int ViewWidth = (int)(ViewWidthRel * Canvas->ClipX);;
		int ViewHeight = (int)(ViewHeightRel * Canvas->ClipY);;
		double PixelsPerSecond = ViewWidth / WidthSeconds;

		auto DrawLine =
			[Canvas](double X0, double Y0, double X1, double Y1, const FLinearColor& Color, double Thickness)
			{
				FCanvasLineItem Line{ FVector2D{X0, Y0}, FVector2D{X1, Y1} };
				Line.SetColor(Color);
				Line.LineThickness = Thickness;
				Canvas->DrawItem(Line);
			};

		auto DrawString =
			[Canvas](const FString& Str, int X, int Y, bool bCentre = true)
			{
				FCanvasTextItem Text{ FVector2D(X, Y), FText::FromString(Str), GEngine->GetTinyFont(), FLinearColor::Yellow };
				Text.EnableShadow(FLinearColor::Black);
				Text.bCentreX = bCentre;
				Text.bCentreY = bCentre;
				Canvas->DrawItem(Text);
			};

		double MaxValueInHistory = 0.0;

		if (History.size())
		{
			ViewY += TextHeight;
			DrawString(FString::Printf(TEXT("Request Throughput MIN/MAX: [%.2f] / [%.2f] Mbps"), History[History.size()-1].MinRequestThroughput, History[History.size()-1].MaxRequestThroughput), ViewX, ViewY, false);
			ViewY += TextHeight;
		}

		//FIRST GRAPH
		MaxValueInHistory = 0.0;
		double HeightScale = HeightScaleThroughput;
		ViewY += TextHeight;
		{ // draw graph edges + label
			const FLinearColor Color = FLinearColor::White;
			DrawLine(ViewX, ViewY + ViewHeight, ViewX + ViewWidth, ViewY + ViewHeight, Color, 1);
			DrawLine(ViewX, ViewY, ViewX, ViewY + ViewHeight, Color, 1);
			DrawLine(ViewX + ViewWidth, ViewY, ViewX + ViewWidth, ViewY + ViewHeight, Color, 1);
			DrawString(TEXT("ZenServer Throughput Mbps"), ViewX, ViewY + ViewHeight + 10, false);
		}

		for (int I = History.size() - 1; I >= 0; --I)
		{
			const auto& Item = History[I];
			const double       X = ViewX + ViewWidth - PixelsPerSecond * (StatsTimeNow - Item.Time);
			const double       H = FMath::Min(ViewHeight, ViewHeight * (Item.Throughput / HeightScale));
			const double       Y = ViewY + ViewHeight - H;
			const FLinearColor Color = FLinearColor::Yellow;
			
			DrawLine(X, ViewY + ViewHeight - 1, X, Y, Color, LineThickness);
			DrawString(FString::Printf(TEXT("%.2f"), Item.Throughput), X, Y - 11);

			if (Item.Throughput > MaxValueInHistory)
				MaxValueInHistory = Item.Throughput;
		}
		HeightScaleThroughput = FMath::Min(MaxHeightScaleThroughput, FMath::Max(MaxValueInHistory, 1.0));

		//SECOND GRAPH
		MaxValueInHistory = 0.0;
		ViewY += ViewHeight + (TextHeight * 2) ;
		HeightScale = HeightScaleRequest;

		{ // draw graph edges + label
			const FLinearColor Color = FLinearColor::White;
			DrawLine(ViewX, ViewY + ViewHeight, ViewX + ViewWidth, ViewY + ViewHeight, Color, 1);
			DrawLine(ViewX, ViewY, ViewX, ViewY + ViewHeight, Color, 1);
			DrawLine(ViewX + ViewWidth, ViewY, ViewX + ViewWidth, ViewY + ViewHeight, Color, 1);
			DrawString(TEXT("ZenServer Request/Sec Count"), ViewX, ViewY + ViewHeight + 10, false);
		}

		for (int I = History.size() - 1; I >= 0; --I)
		{
			const auto& Item = History[I];
			const double       X = ViewX + ViewWidth - PixelsPerSecond * (StatsTimeNow - Item.Time);
			const double       H = FMath::Min(ViewHeight, ViewHeight * (Item.RequestCount / HeightScale));
			const double       Y = ViewY + ViewHeight - H;
			const FLinearColor Color = FLinearColor::Gray;

			DrawLine(X, ViewY + ViewHeight - 1, X, Y, Color, LineThickness);
			DrawString(FString::Printf(TEXT("%.d"), Item.RequestCount), X, Y - 11);

			if (Item.RequestCount > MaxValueInHistory)
				MaxValueInHistory = Item.RequestCount;
		}
		HeightScaleRequest = FMath::Min(MaxHeightScaleRequest, FMath::Max(MaxValueInHistory, 1.0));
	}
}


class FStorageServerClientDebugModule
	: public IModuleInterface
	, public FRunnable
{
public:
	virtual void StartupModule() override
	{
		if (IStorageServerPlatformFile* StorageServerPlatformFile = IStorageServerClientModule::FindStorageServerPlatformFile())
		{
			ConnectionDebug.Reset( new FStorageServerConnectionDebug(StorageServerPlatformFile) );
			OnDrawDebugHandle = UDebugDrawService::Register(TEXT("Game"), FDebugDrawDelegate::CreateRaw(ConnectionDebug.Get(), &FStorageServerConnectionDebug::OnDraw) );

			// start by capturing engine initialization stats on a background thread
			StartThread();

			// once the engine has initialized, switch to a more lightweight gamethread ticker
			FCoreDelegates::OnPostEngineInit.AddLambda([this]
			{
				StopThread();
				StartTick();
			});

			// load the low-level network tracing module too, so we get platform bandwidth stats as well
			if (FModuleManager::Get().ModuleExists(TEXT("LowLevelNetTrace")))
			{
				FModuleManager::Get().LoadModule(TEXT("LowLevelNetTrace"));
			}
		}
	}

	virtual void ShutdownModule() override
	{
		if (ConnectionDebug.IsValid())
		{
			StopThread();
			StopTick();
			UDebugDrawService::Unregister(OnDrawDebugHandle);
			ConnectionDebug.Reset();
		}
	}

	void StartThread()
	{
		check(!Thread.IsValid());
		ThreadStopEvent = FPlatformProcess::GetSynchEventFromPool(true);
		Thread.Reset( FRunnableThread::Create(this, TEXT("StorageServerStartupDebug"), 0, TPri_Lowest) );
	}

	void StopThread()
	{
		if (Thread.IsValid())
		{
			Thread.Reset();
			FPlatformProcess::ReturnSynchEventToPool(ThreadStopEvent);
			ThreadStopEvent = nullptr;
		}
	}

	void StartTick()
	{
		check(!TickHandle.IsValid());
		TickHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateRaw(ConnectionDebug.Get(), &FStorageServerConnectionDebug::OnTick));
	}

	void StopTick()
	{
		if (TickHandle.IsValid())
		{
			FTSTicker::GetCoreTicker().RemoveTicker(TickHandle);
			TickHandle.Reset();
		}
	}

	// FRunnable interface
	virtual uint32 Run() override
	{
		while(!ThreadStopEvent->Wait(10))
		{
			ConnectionDebug->OnTick(0);
		}
		return 0;
	}

	virtual void Stop() override
	{
		ThreadStopEvent->Trigger();
	}
	// end of FRunnable interface


	TUniquePtr<FStorageServerConnectionDebug> ConnectionDebug;
	FDelegateHandle OnDrawDebugHandle;

	TUniquePtr<FRunnableThread> Thread;
	FEvent* ThreadStopEvent = nullptr;

	FTSTicker::FDelegateHandle TickHandle;

};

IMPLEMENT_MODULE(FStorageServerClientDebugModule, StorageServerClientDebug);

#else

// shipping stub
IMPLEMENT_MODULE(FDefaultModuleImpl, StorageServerClientDebug);

#endif // !UE_BUILD_SHIPPING



