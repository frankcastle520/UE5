// Copyright Epic Games, Inc. All Rights Reserved.

#include "AvalancheEffectorsEditorModule.h"
#include "AvaEffectorsEditorCommands.h"
#include "AvaEffectorsEditorStyle.h"
#include "AvaInteractiveToolsDelegates.h"
#include "Cloner/AvaClonerActorTool.h"
#include "Cloner/AvaClonerActorVis.h"
#include "Cloner/AvaClonerEditorOutlinerContextMenu.h"
#include "Cloner/CEClonerComponent.h"
#include "ComponentVisualizers.h"
#include "Effector/AvaEffectorActorTool.h"
#include "Effector/AvaEffectorActorVis.h"
#include "Effector/AvaEffectorEditorOutlinerContextMenu.h"
#include "Effector/CEEffectorComponent.h"
#include "Framework/Application/SlateApplication.h"
#include "IAvalancheComponentVisualizersModule.h"
#include "IAvaOutlinerModule.h"
#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"

void FAvalancheEffectorsEditorModule::StartupModule()
{
	FCoreDelegates::OnPostEngineInit.AddRaw(this, &FAvalancheEffectorsEditorModule::PostEngineInit);

	FAvaEffectorsEditorCommands::Register();

	FAvaInteractiveToolsDelegates::GetRegisterToolsDelegate().AddRaw(this, &FAvalancheEffectorsEditorModule::RegisterTools);

	RegisterOutlinerItems();
}

void FAvalancheEffectorsEditorModule::ShutdownModule()
{
	FCoreDelegates::OnPostEngineInit.RemoveAll(this);

	FAvaEffectorsEditorCommands::Unregister();

	FAvaInteractiveToolsDelegates::GetRegisterToolsDelegate().RemoveAll(this);

	UnregisterOutlinerItems();
}

void FAvalancheEffectorsEditorModule::PostEngineInit()
{
	if (FSlateApplication::IsInitialized())
	{
		RegisterComponentVisualizers();
	}
}

void FAvalancheEffectorsEditorModule::RegisterTools(IAvalancheInteractiveToolsModule* InModule)
{
	InModule->RegisterTool(
		IAvalancheInteractiveToolsModule::Get().CategoryNameActor,
		GetDefault<UAvaEffectorActorTool>()->GetToolParameters()
	);

	InModule->RegisterTool(
		IAvalancheInteractiveToolsModule::Get().CategoryNameActor,
		GetDefault<UAvaClonerActorTool>()->GetToolParameters()
	);
}

void FAvalancheEffectorsEditorModule::RegisterComponentVisualizers()
{
	IAvalancheComponentVisualizersModule::RegisterComponentVisualizer<UCEEffectorComponent, FAvaEffectorActorVisualizer>(&Visualizers);
	IAvalancheComponentVisualizersModule::RegisterComponentVisualizer<UCEClonerComponent, FAvaClonerActorVisualizer>(&Visualizers);
}

void FAvalancheEffectorsEditorModule::RegisterOutlinerItems()
{
	IAvaOutlinerModule& OutlinerModule = IAvaOutlinerModule::Get();

	OutlinerContextClonerDelegateHandle = OutlinerModule.GetOnExtendOutlinerItemContextMenu()
		.AddStatic(&FAvaClonerEditorOutlinerContextMenu::OnExtendOutlinerContextMenu);

	OutlinerContextEffectorDelegateHandle = OutlinerModule.GetOnExtendOutlinerItemContextMenu()
		.AddStatic(&FAvaEffectorEditorOutlinerContextMenu::OnExtendOutlinerContextMenu);
}

void FAvalancheEffectorsEditorModule::UnregisterOutlinerItems()
{
	if (IAvaOutlinerModule::IsLoaded())
	{
		IAvaOutlinerModule& OutlinerModule = IAvaOutlinerModule::Get();

		OutlinerModule.GetOnExtendOutlinerItemContextMenu().Remove(OutlinerContextClonerDelegateHandle);
        OutlinerContextClonerDelegateHandle.Reset();

		OutlinerModule.GetOnExtendOutlinerItemContextMenu().Remove(OutlinerContextEffectorDelegateHandle);
		OutlinerContextEffectorDelegateHandle.Reset();
	}
}

IMPLEMENT_MODULE(FAvalancheEffectorsEditorModule, AvalancheEffectorsEditor)
