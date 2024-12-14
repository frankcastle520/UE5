// Copyright Epic Games, Inc. All Rights Reserved.

#include "ActorModifierCoreEditorModule.h"

#include "ActorModifierCoreBlueprint.h"
#include "KismetCompilerModule.h"
#include "Modifiers/ActorModifierCoreStack.h"
#include "Modifiers/Blueprints/ActorModifierCoreBlueprintBase.h"
#include "Modifiers/Customizations/ActorModifierCoreEditorDetailCustomization.h"
#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"

void FActorModifierCoreEditorModule::StartupModule()
{
	RegisterDetailCustomizations();
	RegisterBlueprintCustomizations();
}

void FActorModifierCoreEditorModule::ShutdownModule()
{
	UnregisterDetailCustomizations();
}

void FActorModifierCoreEditorModule::RegisterDetailCustomizations() const
{
	// Register custom layouts
	static FName PropertyEditor("PropertyEditor");
	FPropertyEditorModule& PropertyModule = FModuleManager::GetModuleChecked<FPropertyEditorModule>(PropertyEditor);

	PropertyModule.RegisterCustomClassLayout(UActorModifierCoreStack::StaticClass()->GetFName()
		, FOnGetDetailCustomizationInstance::CreateStatic(&FActorModifierCoreEditorDetailCustomization::MakeInstance));
}

void FActorModifierCoreEditorModule::UnregisterDetailCustomizations() const
{
	// Unregister custom layouts
	static FName PropertyEditor("PropertyEditor");
	if (FModuleManager::Get().IsModuleLoaded(PropertyEditor))
	{
		FPropertyEditorModule& PropertyModule = FModuleManager::GetModuleChecked<FPropertyEditorModule>(PropertyEditor);
		PropertyModule.UnregisterCustomClassLayout(UActorModifierCoreStack::StaticClass()->GetFName());
	}
}

void FActorModifierCoreEditorModule::RegisterBlueprintCustomizations()
{
	IKismetCompilerInterface& KismetCompilerModule = FModuleManager::LoadModuleChecked<IKismetCompilerInterface>("KismetCompiler");
	KismetCompilerModule.OverrideBPTypeForClass(UActorModifierCoreBlueprintBase::StaticClass(), UActorModifierCoreBlueprint::StaticClass());
}

IMPLEMENT_MODULE(FActorModifierCoreEditorModule, ActorModifierCoreEditor)
