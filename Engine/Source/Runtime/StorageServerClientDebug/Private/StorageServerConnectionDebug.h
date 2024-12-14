// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/Canvas.h"
#include "HAL/CriticalSection.h"
#include <vector>
#include "IStorageServerPlatformFile.h"

#if !UE_BUILD_SHIPPING
class FStorageServerConnectionDebug
{
public:
	FStorageServerConnectionDebug( IStorageServerPlatformFile* InStorageServerPlatformFile )
		: StorageServerPlatformFile(InStorageServerPlatformFile)
		, HostAddress(InStorageServerPlatformFile->GetHostAddr())
	{
	}

	bool OnTick(float); // FTickerDelegate
	void OnDraw(UCanvas*, APlayerController*); // FDebugDrawDelegate

private:
	double MaxReqThroughput = 0.0;
	double MinReqThroughput = 0.0;
	uint32 ReqCount = 0;
	double Throughput = 0.0;

	struct HistoryItem
	{
		double Time;
		double MaxRequestThroughput;
		double MinRequestThroughput;
		double Throughput;
		uint32 RequestCount;
	};

	std::vector<HistoryItem> History = {{0, 0, 0, 0, 0}};

	static constexpr float UpdateStatsTimer = 1.0;
	double UpdateStatsTime = 0.0;

	IStorageServerPlatformFile* StorageServerPlatformFile = nullptr;
	FString HostAddress;

	FCriticalSection CS;
};
#endif // !UE_BUILD_SHIPPING
