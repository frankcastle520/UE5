// Copyright Epic Games, Inc. All Rights Reserved.
#include "ChaosFleshGeneratorToolsMenuExtender.h"
#include "MLDeformerEditorToolkit.h"
#include "PropertyEditorModule.h"
#include "SFleshGeneratorWidget.h"

namespace UE::Chaos::FleshGenerator
{
	class FChaosFleshGeneratorModule : public IModuleInterface
	{
	public:
		virtual void StartupModule() override
		{
			UE::MLDeformer::FMLDeformerEditorToolkit::AddToolsMenuExtender(CreateToolsMenuExtender());
			FPropertyEditorModule& PropertyModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
			PropertyModule.RegisterCustomClassLayout("FleshGeneratorProperties", FOnGetDetailCustomizationInstance::CreateStatic(&FFleshGeneratorDetails::MakeInstance));
		}

		virtual void ShutdownModule() override
		{
			FPropertyEditorModule& PropertyModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
			PropertyModule.UnregisterCustomClassLayout("FleshGeneratorProperties");
		}
	};
};

IMPLEMENT_MODULE(UE::Chaos::FleshGenerator::FChaosFleshGeneratorModule, ChaosFleshGenerator)