// Copyright Epic Games, Inc. All Rights Reserved.

#include "EditorTelemetryModule.h"
#include "EditorTelemetry.h"
#include "StudioTelemetry.h"
#include "Modules/ModuleManager.h"

IMPLEMENT_MODULE(FEditorTelemetryModule, EditorTelemetry);

void FEditorTelemetryModule::StartupModule()
{
	FStudioTelemetry::Get().GetOnStartSession().AddLambda([this]()
		{
			FEditorTelemetry::Get().StartSession();
		}
	);
	
	FStudioTelemetry::Get().GetOnEndSession().AddLambda([this]()
		{
			FEditorTelemetry::Get().EndSession();
		}
	);
}

void FEditorTelemetryModule::ShutdownModule()
{
	FEditorTelemetry::Get().EndSession();
}
