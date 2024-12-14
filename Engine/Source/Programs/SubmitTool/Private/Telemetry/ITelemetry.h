// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

struct FAnalyticsEventAttribute;

class ITelemetry
{
public:
	virtual void Start(const FString& InCurrentStream) const = 0;
	virtual void BlockFlush(float InTimeout) const = 0;
	virtual void SubmitSucceeded() const = 0;
	virtual void CustomEvent(const FString& InEventId, const TArray<FAnalyticsEventAttribute>& InAttribs) const = 0;
};