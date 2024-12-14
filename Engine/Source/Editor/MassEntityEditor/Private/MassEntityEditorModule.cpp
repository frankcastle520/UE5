// Copyright Epic Games, Inc. All Rights Reserved.

#include "MassEntityEditorModule.h"
#include "MassEditorStyle.h"
#include "Modules/ModuleManager.h"
#if WITH_UNREAL_DEVELOPER_TOOLS
#include "MessageLogModule.h"
#include "Engine/World.h"
#include "Logging/MessageLog.h"
#include "MassDebugger.h"
#include "MassEntityEditor.h"
#endif // WITH_UNREAL_DEVELOPER_TOOLS

#define LOCTEXT_NAMESPACE "Mass"

IMPLEMENT_MODULE(FMassEntityEditorModule, MassEntityEditor)

void FMassEntityEditorModule::StartupModule()
{
	MenuExtensibilityManager = MakeShareable(new FExtensibilityManager);
	ToolBarExtensibilityManager = MakeShareable(new FExtensibilityManager);

	FMassEntityEditorStyle::Initialize();

#if WITH_UNREAL_DEVELOPER_TOOLS
	FMessageLogModule& MessageLogModule = FModuleManager::LoadModuleChecked<FMessageLogModule>("MessageLog");
	FMessageLogInitializationOptions InitOptions;
	InitOptions.bShowPages = true;
	InitOptions.bShowFilters = true;
	MessageLogModule.RegisterLogListing(UE::Mass::Editor::MessageLogPageName
		, FText::FromName(UE::Mass::Editor::MessageLogPageName), InitOptions);

	OnWorldCleanupHandle = FWorldDelegates::OnWorldCleanup.AddStatic(&FMassEntityEditorModule::OnWorldCleanup);

	FModuleManager::Get().LoadModule("MassEntityDebugger");
#endif // WITH_UNREAL_DEVELOPER_TOOLS
}

void FMassEntityEditorModule::ShutdownModule()
{
	ProcessorClassCache.Reset();
	MenuExtensibilityManager.Reset();
	ToolBarExtensibilityManager.Reset();

	FMassEntityEditorStyle::Shutdown();

#if WITH_UNREAL_DEVELOPER_TOOLS
	FWorldDelegates::OnWorldCleanup.Remove(OnWorldCleanupHandle);
#endif // WITH_UNREAL_DEVELOPER_TOOLS
}

#if WITH_UNREAL_DEVELOPER_TOOLS
void FMassEntityEditorModule::OnWorldCleanup(UWorld* /*World*/, bool /*bSessionEnded*/, bool /*bCleanupResources*/)
{
	// clearing out messages from the world being cleaned up
	FMessageLog(UE::Mass::Editor::MessageLogPageName).NewPage(FText::FromName(UE::Mass::Editor::MessageLogPageName));
}
#endif // WITH_UNREAL_DEVELOPER_TOOLS

#undef LOCTEXT_NAMESPACE
