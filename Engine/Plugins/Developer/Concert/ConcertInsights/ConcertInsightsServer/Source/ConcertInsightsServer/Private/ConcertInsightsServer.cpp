// Copyright Epic Games, Inc. All Rights Reserved.

#include "ConcertInsightsServer.h"

#define LOCTEXT_NAMESPACE "FConcertInsightsServerModule"

namespace UE::ConcertInsightsServer
{
	void FConcertInsightsServerModule::StartupModule()
	{
		TraceControls = ConcertInsightsCore::FTraceControls::Make<FServerTraceControls>();
	}

	void FConcertInsightsServerModule::ShutdownModule()
	{}
}


#undef LOCTEXT_NAMESPACE
    
IMPLEMENT_MODULE(UE::ConcertInsightsServer::FConcertInsightsServerModule, ConcertInsightsServer)