// Copyright Epic Games, Inc. All Rights Reserved.

#include "PixelStreaming2ServersModule.h"

FPixelStreaming2ServersModule& FPixelStreaming2ServersModule::Get()
{
	return FModuleManager::LoadModuleChecked<FPixelStreaming2ServersModule>("PixelStreaming2Servers");
}

void FPixelStreaming2ServersModule::StartupModule()
{
	// Any startup logic require goes here.
}

int FPixelStreaming2ServersModule::NextPort()
{
	// TODO: RTCP-7026 Checking for in-use ports.
	return (4000 + NextGeneratedPort.Increment()) % 65535;
}

IMPLEMENT_MODULE(FPixelStreaming2ServersModule, PixelStreaming2Servers)
