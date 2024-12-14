// Copyright Epic Games, Inc. All Rights Reserved.

#include "ConcertInsightsSynchronizedTrace.h"

#define LOCTEXT_NAMESPACE "FConcertInsightsSynchronizedTraceModule"

namespace UE::ConcertInsightsSyncTrace
{
	void FConcertInsightsSyncTraceModule::StartupModule()
	{
    
	}

	void FConcertInsightsSyncTraceModule::ShutdownModule()
	{
    
	}
}


#undef LOCTEXT_NAMESPACE
    
IMPLEMENT_MODULE(UE::ConcertInsightsSyncTrace::FConcertInsightsSyncTraceModule, ConcertInsightsCore)