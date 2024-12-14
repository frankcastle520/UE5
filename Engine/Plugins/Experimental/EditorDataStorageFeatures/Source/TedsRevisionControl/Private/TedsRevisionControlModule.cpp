// Copyright Epic Games, Inc. All Rights Reserved.

#include "TedsRevisionControlModule.h"

void FTedsRevisionControlModule::StartupModule()
{
}

void FTedsRevisionControlModule::ShutdownModule()
{
}

void FTedsRevisionControlModule::AddReferencedObjects(FReferenceCollector& Collector)
{
}

FString FTedsRevisionControlModule::GetReferencerName() const
{
	return TEXT("Teds: Revision Control Module");
}

IMPLEMENT_MODULE(FTedsRevisionControlModule, TedsRevisionControl)
