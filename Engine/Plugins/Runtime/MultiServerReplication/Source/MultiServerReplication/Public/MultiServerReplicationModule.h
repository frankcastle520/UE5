// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Modules/ModuleInterface.h"

/** See MultiServerNode for more details about multi-server networking as a whole. */
class FMultiServerReplicationModule : public IModuleInterface
{
public:

	FMultiServerReplicationModule() {}
	virtual ~FMultiServerReplicationModule() {}

	// IModuleInterface
	virtual void StartupModule() override {}
	virtual void ShutdownModule() override {}
	virtual bool SupportsDynamicReloading() override { return false; }
	virtual bool SupportsAutomaticShutdown() override { return false; }
};


