// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AnalyticsProviderConfigurationDelegate.h"
#include "AnalyticsET.h"
#include "IAnalyticsProviderET.h"
#include "ITelemetry.h"

class FStandAloneTelemetry : public ITelemetry
{
public:
	FStandAloneTelemetry(const FString& InUrl, const FGuid& InSessionID);
	virtual ~FStandAloneTelemetry();
	virtual void Start(const FString& InCurrentStream) const override;
	virtual void BlockFlush(float InTimeout) const override;
	virtual void SubmitSucceeded() const override;
	virtual void CustomEvent(const FString& InEventId, const TArray<FAnalyticsEventAttribute>& InAttribs) const override;

private:
	TSharedPtr<IAnalyticsProviderET> Provider;
};