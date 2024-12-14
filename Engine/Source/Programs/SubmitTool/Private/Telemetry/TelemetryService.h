// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "StandAloneTelemetry.h"
#include "ITelemetry.h"

class FTelemetryService
{
public:
	static const TSharedPtr<ITelemetry> & Get();
	static void Init(const FString& InUrl, const FGuid& InSessionID);
	static void Shutdown();
	static void BlockFlush(float InTimeout);
private:
	static void Set(TSharedPtr<ITelemetry> InInstance);
	static TSharedPtr<ITelemetry> TelemetryInstance;
	static FCriticalSection InstanceCriticalSection;
};