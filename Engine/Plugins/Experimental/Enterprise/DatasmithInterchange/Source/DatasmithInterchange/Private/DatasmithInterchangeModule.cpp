// Copyright Epic Games, Inc. All Rights Reserved.

#include "DatasmithInterchangeModule.h"

#include "Engine/Blueprint.h"
#include "InterchangeDatasmithAreaLightFactory.h"
#include "InterchangeDatasmithCustomizations.h"
#include "InterchangeDatasmithLog.h"
#include "InterchangeDatasmithPipeline.h"
#include "InterchangeDatasmithTranslator.h"

#include "InterchangeReferenceMaterials/DatasmithReferenceMaterialManager.h"
#include "InterchangeReferenceMaterials/DatasmithC4DMaterialSelector.h"
#include "InterchangeReferenceMaterials/DatasmithCityEngineMaterialSelector.h"
#include "InterchangeReferenceMaterials/DatasmithRevitMaterialSelector.h"
#include "InterchangeReferenceMaterials/DatasmithSketchupMaterialSelector.h"
#include "InterchangeReferenceMaterials/DatasmithStdMaterialSelector.h"

#include "DatasmithTranslatorManager.h"
#include "InterchangeGenericAssetsPipeline.h"
#include "InterchangeGenericMeshPipeline.h"
#include "InterchangeGenericScenesPipeline.h"
#include "InterchangeManager.h"
#include "InterchangeProjectSettings.h"

#include "Logging/LogMacros.h"
#include "UObject/SoftObjectPath.h"

#if WITH_EDITOR
#include "PropertyEditorModule.h"
#endif

#define LOCTEXT_NAMESPACE "DatasmithInterchange"


DEFINE_LOG_CATEGORY(LogInterchangeDatasmith);

class FDatasmithInterchangeModule : public IDatasmithInterchangeModule
{
public:
	virtual void StartupModule() override
	{
		FCoreDelegates::OnPostEngineInit.AddRaw(this, &FDatasmithInterchangeModule::OnPostEngineInit);
	}

	virtual void ShutdownModule() override
	{
		FCoreDelegates::OnPostEngineInit.RemoveAll(this);

		UE::DatasmithInterchange::FDatasmithReferenceMaterialManager::Destroy();
		
#if WITH_EDITOR
		FPropertyEditorModule* PropertyEditorModule = FModuleManager::GetModulePtr<FPropertyEditorModule>("PropertyEditor");
		if (PropertyEditorModule)
		{
			for (FName ClassName : ClassesToUnregisterOnShutdown)
			{
				PropertyEditorModule->UnregisterCustomClassLayout(ClassName);
			}
		}
		ClassesToUnregisterOnShutdown.Empty();
#endif
	}

	void OnPostEngineInit()
	{
		using namespace UE::DatasmithInterchange;

		// Load the blueprint asset into memory while wew're on the game thread so that GetAreaLightActorBPClass() can safely be called from other threads.
		UBlueprint* AreaLightBlueprint = Cast<UBlueprint>(FSoftObjectPath(TEXT("/DatasmithContent/Datasmith/DatasmithArealight.DatasmithArealight")).TryLoad());
		//ensure(AreaLightBlueprint != nullptr);

		UInterchangeManager& InterchangeManager = UInterchangeManager::GetInterchangeManager();
		InterchangeManager.RegisterTranslator(UInterchangeDatasmithTranslator::StaticClass());
		InterchangeManager.RegisterFactory(UInterchangeDatasmithAreaLightFactory::StaticClass());

		// Add Datasmith translator to the Interchange engine
		FInterchangeTranslatorPipelines TranslatorPipeplines;
		TranslatorPipeplines.Translator = TSoftClassPtr<UInterchangeTranslatorBase>(UInterchangeDatasmithTranslator::StaticClass());
		TranslatorPipeplines.Pipelines.Add(FSoftObjectPath(TEXT("/DatasmithInterchange/InterchangeDatasmithDefault.InterchangeDatasmithDefault")));
		{
			FInterchangeImportSettings& ImportSettings = FInterchangeProjectSettingsUtils::GetMutableDefaultImportSettings(false);
			FInterchangePipelineStack& PipelineStack = ImportSettings.PipelineStacks[TEXT("Assets")];
			PipelineStack.PerTranslatorPipelines.Add(TranslatorPipeplines);
		}

		{
			FInterchangeImportSettings& ImportSettings = FInterchangeProjectSettingsUtils::GetMutableDefaultImportSettings(true);
			FInterchangePipelineStack& PipelineStack = ImportSettings.PipelineStacks[TEXT("Scene")];
			PipelineStack.PerTranslatorPipelines.Add(TranslatorPipeplines);
		}

		FDatasmithReferenceMaterialManager::Create();

		//A minimal set of natively supported reference materials
		FDatasmithReferenceMaterialManager::Get().RegisterSelector(TEXT("C4D"), MakeShared< FDatasmithC4DMaterialSelector >());
		FDatasmithReferenceMaterialManager::Get().RegisterSelector(TEXT("Revit"), MakeShared< FDatasmithRevitMaterialSelector >());
		FDatasmithReferenceMaterialManager::Get().RegisterSelector(TEXT("SketchUp"), MakeShared< FDatasmithSketchUpMaterialSelector >());
		FDatasmithReferenceMaterialManager::Get().RegisterSelector(TEXT("CityEngine"), MakeShared< FDatasmithCityEngineMaterialSelector >());
		FDatasmithReferenceMaterialManager::Get().RegisterSelector(TEXT("StdMaterial"), MakeShared< FDatasmithStdMaterialSelector >());

#if WITH_EDITOR
		ClassesToUnregisterOnShutdown.Reset();
		// Register details customizations
		FPropertyEditorModule& PropertyEditorModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");

		ClassesToUnregisterOnShutdown.Add(UInterchangeDatasmithTranslatorSettings::StaticClass()->GetFName());
		PropertyEditorModule.RegisterCustomClassLayout(ClassesToUnregisterOnShutdown.Last(), FOnGetDetailCustomizationInstance::CreateStatic(&FInterchangeDatasmithTranslatorSettingsCustomization::MakeInstance));
#endif
	}

#if WITH_EDITOR
	TArray<FName> ClassesToUnregisterOnShutdown;
#endif
};

IMPLEMENT_MODULE(FDatasmithInterchangeModule, DatasmithInterchange);

#undef LOCTEXT_NAMESPACE