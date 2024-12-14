// Copyright Epic Games, Inc. All Rights Reserved.

#include "TraceInsightsCoreModule.h"

#include "Modules/ModuleManager.h"

#include "InsightsCore/Common/InsightsCoreStyle.h"
#include "InsightsCore/Filter/ViewModels/Filters.h"

IMPLEMENT_MODULE(FTraceInsightsCoreModule, TraceInsightsCore);

void FTraceInsightsCoreModule::StartupModule()
{
	UE::Insights::FInsightsCoreStyle::Initialize();
	UE::Insights::FFilterService::Initialize();
}

void FTraceInsightsCoreModule::ShutdownModule()
{
	UE::Insights::FFilterService::Shutdown();
	UE::Insights::FInsightsCoreStyle::Shutdown();
}
