// Copyright Epic Games, Inc. All Rights Reserved.

#include "CEEditorModule.h"

#include "Cloner/CEClonerComponent.h"
#include "Cloner/CEClonerActor.h"
#include "Cloner/Customizations/CEEditorClonerActorDetailCustomization.h"
#include "Cloner/Customizations/CEEditorClonerComponentDetailCustomization.h"
#include "Cloner/Customizations/CEEditorClonerEffectorExtensionDetailCustomization.h"
#include "Cloner/Customizations/CEEditorClonerLifetimeExtensionDetailCustomization.h"
#include "Cloner/Customizations/CEEditorClonerMeshLayoutDetailCustomization.h"
#include "Cloner/Customizations/CEEditorClonerSplineLayoutDetailCustomization.h"
#include "Cloner/Extensions/CEClonerEffectorExtension.h"
#include "Cloner/Extensions/CEClonerLifetimeExtension.h"
#include "Cloner/Layouts/CEClonerMeshLayout.h"
#include "Cloner/Layouts/CEClonerSplineLayout.h"
#include "Cloner/Sequencer/MovieSceneClonerTrackEditor.h"
#include "Effector/Customizations/CEEditorEffectorComponentDetailCustomization.h"
#include "Effector/Customizations/CEEditorEffectorTypeDetailCustomization.h"
#include "Effector/CEEffectorActor.h"
#include "Effector/CEEffectorComponent.h"
#include "Effector/Customizations/CEEditorEffectorActorDetailCustomization.h"
#include "Effector/Types/CEEffectorBoundType.h"
#include "ISequencerModule.h"
#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"
#include "Styles/CEEditorStyle.h"

void FCEEditorModule::StartupModule()
{
	// Load styles
	FCEEditorStyle::Get();

	FPropertyEditorModule& PropertyModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>(TEXT("PropertyEditor"));

	// Cloner customization
	PropertyModule.RegisterCustomClassLayout(ACEClonerActor::StaticClass()->GetFName(), FOnGetDetailCustomizationInstance::CreateStatic(&FCEEditorClonerActorDetailCustomization::MakeInstance));
	PropertyModule.RegisterCustomClassLayout(UCEClonerComponent::StaticClass()->GetFName(), FOnGetDetailCustomizationInstance::CreateStatic(&FCEEditorClonerComponentDetailCustomization::MakeInstance));
	PropertyModule.RegisterCustomClassLayout(UCEClonerEffectorExtension::StaticClass()->GetFName(), FOnGetDetailCustomizationInstance::CreateStatic(&FCEEditorClonerEffectorExtensionDetailCustomization::MakeInstance));
	PropertyModule.RegisterCustomClassLayout(UCEClonerSplineLayout::StaticClass()->GetFName(), FOnGetDetailCustomizationInstance::CreateStatic(&FCEEditorClonerSplineLayoutDetailCustomization::MakeInstance));
	PropertyModule.RegisterCustomClassLayout(UCEClonerMeshLayout::StaticClass()->GetFName(), FOnGetDetailCustomizationInstance::CreateStatic(&FCEEditorClonerMeshLayoutDetailCustomization::MakeInstance));
	PropertyModule.RegisterCustomClassLayout(UCEClonerLifetimeExtension::StaticClass()->GetFName(), FOnGetDetailCustomizationInstance::CreateStatic(&FCEEditorClonerLifetimeExtensionDetailCustomization::MakeInstance));

	// Effector customization
	PropertyModule.RegisterCustomClassLayout(ACEEffectorActor::StaticClass()->GetFName(), FOnGetDetailCustomizationInstance::CreateStatic(&FCEEditorEffectorActorDetailCustomization::MakeInstance));
	PropertyModule.RegisterCustomClassLayout(UCEEffectorComponent::StaticClass()->GetFName(), FOnGetDetailCustomizationInstance::CreateStatic(&FCEEditorEffectorComponentDetailCustomization::MakeInstance));
	PropertyModule.RegisterCustomClassLayout(UCEEffectorBoundType::StaticClass()->GetFName(), FOnGetDetailCustomizationInstance::CreateStatic(&FCEEditorEffectorTypeDetailCustomization::MakeInstance));

	// Custom cloner track
	ISequencerModule& SequencerModule = FModuleManager::LoadModuleChecked<ISequencerModule>(TEXT("Sequencer"));
	ClonerTrackCreateEditorHandle = SequencerModule.RegisterTrackEditor(FOnCreateTrackEditor::CreateStatic(&FMovieSceneClonerTrackEditor::CreateTrackEditor));
}

void FCEEditorModule::ShutdownModule()
{
	if (FModuleManager::Get().IsModuleLoaded("PropertyEditor") && UObjectInitialized())
	{
		FPropertyEditorModule& PropertyModule = FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor");

		// Cloner customization
		PropertyModule.UnregisterCustomClassLayout(ACEClonerActor::StaticClass()->GetFName());
		PropertyModule.UnregisterCustomClassLayout(UCEClonerComponent::StaticClass()->GetFName());
		PropertyModule.UnregisterCustomClassLayout(UCEClonerEffectorExtension::StaticClass()->GetFName());
		PropertyModule.UnregisterCustomClassLayout(UCEClonerSplineLayout::StaticClass()->GetFName());
		PropertyModule.UnregisterCustomClassLayout(UCEClonerMeshLayout::StaticClass()->GetFName());
		PropertyModule.UnregisterCustomClassLayout(UCEClonerLifetimeExtension::StaticClass()->GetFName());

		// Effector customization
		PropertyModule.UnregisterCustomClassLayout(ACEEffectorActor::StaticClass()->GetFName());
		PropertyModule.UnregisterCustomClassLayout(UCEEffectorComponent::StaticClass()->GetFName());
		PropertyModule.UnregisterCustomClassLayout(UCEEffectorBoundType::StaticClass()->GetFName());
	}

	// Custom cloner track
	if (FModuleManager::Get().IsModuleLoaded("Sequencer"))
	{
		ISequencerModule& SequencerModule = FModuleManager::GetModuleChecked<ISequencerModule>(TEXT("Sequencer"));
		SequencerModule.UnRegisterTrackEditor(ClonerTrackCreateEditorHandle);
		ClonerTrackCreateEditorHandle.Reset();
	}
}

IMPLEMENT_MODULE(FCEEditorModule, ClonerEffectorEditor)