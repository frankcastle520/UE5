// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"

#if !UE_BUILD_SHIPPING && !WITH_EDITOR

#include "Logging/LogMacros.h"

#include "InsightsFrontend/ITraceInsightsFrontendModule.h"

DECLARE_LOG_CATEGORY_EXTERN(InsightsTestRunner, Log, All);

namespace UE::Insights
{

class FTestRunner
{
public:
	virtual ~FTestRunner() {}

	void Run(const FString& InCmd);

private:
	bool bIsRunningTests = false;
};

} // namespace UE::Insights

#endif //UE_BUILD_SHIPPING && !WITH_EDITOR
