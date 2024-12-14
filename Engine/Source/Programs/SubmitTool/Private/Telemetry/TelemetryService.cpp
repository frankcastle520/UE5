// Copyright Epic Games, Inc. All Rights Reserved.

#include "TelemetryService.h"

#include "Logging/SubmitToolLog.h"
#include "NullTelemetry.h"
#include "StandAloneTelemetry.h"

FCriticalSection FTelemetryService::InstanceCriticalSection;
TSharedPtr<ITelemetry> FTelemetryService::TelemetryInstance = nullptr;

const TSharedPtr<ITelemetry>& FTelemetryService::Get()
{
	return TelemetryInstance;
}

void FTelemetryService::Init(const FString& InUrl, const FGuid& InSessionID)
{
	if (InUrl.IsEmpty())
	{
		Set(MakeShared<FNullTelemetry>());
	}

	Set(MakeShared<FStandAloneTelemetry>(InUrl, InSessionID));
}

void FTelemetryService::Set(TSharedPtr<ITelemetry> InInstance)
{
	if (InInstance == nullptr)
	{
		UE_LOG(LogSubmitTool, Warning, TEXT("Trying to set a null telemetry instance. Operation aborted."));
		return;
	}

	InstanceCriticalSection.Lock();
	
	TelemetryInstance = InInstance;

	InstanceCriticalSection.Unlock();
}

void FTelemetryService::BlockFlush(float InTimeout)
{
	TelemetryInstance->BlockFlush(InTimeout);
}

void FTelemetryService::Shutdown()
{
	TelemetryInstance = nullptr;
}
