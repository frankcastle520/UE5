// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IConcertInsightsSyncTraceModule.h"

namespace UE::ConcertInsightsSyncTrace
{
	class FConcertInsightsSyncTraceModule : public IConcertInsightsSyncTraceModule
	{
	public:

		//~ Begin IModuleInterface Interface
		virtual void StartupModule() override;
		virtual void ShutdownModule() override;
		//~ End IModuleInterface Interface
	};
}

