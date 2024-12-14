// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ITelemetry.h"

class FNullTelemetry : public ITelemetry
{
public:
	virtual void Start(const FString& InCurrentStream) const override {}
	virtual void BlockFlush(float InTimeout) const {}
	virtual void SubmitSucceeded() const override {}
	virtual void CustomEvent(const FString& InEventId, const TArray<FAnalyticsEventAttribute>& InAttribs) const override {}
};