// Copyright Epic Games, Inc. All Rights Reserved.

#include "Module.h"
#include "VirtualScoutingLog.h"


#define LOCTEXT_NAMESPACE "FVirtualScoutingModule"


DEFINE_LOG_CATEGORY(LogVirtualScouting);


void FVirtualScoutingModule::StartupModule()
{
}


void FVirtualScoutingModule::ShutdownModule()
{
}


IMPLEMENT_MODULE(FVirtualScoutingModule, VirtualScouting)


#undef LOCTEXT_NAMESPACE
