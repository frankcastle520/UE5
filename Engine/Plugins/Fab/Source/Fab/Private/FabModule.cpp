// Copyright Epic Games, Inc. All Rights Reserved.

#include "FabModule.h"

#include "Engine.h"

#include "FabAuthentication.h"
#include "FabBrowser.h"
#include "FabDownloader.h"
#include "FabLog.h"
#include "FabSettingsCustomization.h"
#include "InterchangeManager.h"

#include "PropertyEditorModule.h"
#include "Engine/RendererSettings.h"
#include "Modules/ModuleManager.h"

#include "Pipelines/Factories/InterchangeInstancedFoliageTypeFactory.h"

#include "Runtime/Launch/Resources/Version.h"

#if (ENGINE_MAJOR_VERSION >=5 && ENGINE_MINOR_VERSION <=3)

#include "Settings/EditorExperimentalSettings.h"

#endif

DEFINE_LOG_CATEGORY(LogFab)

class FFabModule : public IFabModule
{
public:
	virtual void StartupModule() override
	{
		if (GIsEditor && !IsRunningCommandlet())
		{
			FFabBrowser::Init();
			FabAuthentication::Init();

			URendererSettings* RendererSettings                = GetMutableDefault<URendererSettings>();
			RendererSettings->bEnableVirtualTextureOpacityMask = true;
			RendererSettings->PostEditChange();

#if (ENGINE_MAJOR_VERSION >=5 && ENGINE_MINOR_VERSION <=3)
			{
				UEditorExperimentalSettings* EditorSettings    = GetMutableDefault<UEditorExperimentalSettings>();
				EditorSettings->bEnableAsyncTextureCompilation = false;
				EditorSettings->PostEditChange();
			}
#endif

			FPropertyEditorModule& PropertyModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
			PropertyModule.RegisterCustomClassLayout("FabSettings", FOnGetDetailCustomizationInstance::CreateStatic(&FFabSettingsCustomization::MakeInstance));

			auto RegisterItems = []()
			{
				UInterchangeManager::GetInterchangeManager().RegisterFactory(UInterchangeInstancedFoliageTypeFactory::StaticClass());
			};

			if (GEngine)
			{
				RegisterItems();
			}
			else
			{
				FCoreDelegates::OnPostEngineInit.AddLambda(RegisterItems);
			}
		}
	}

	virtual void ShutdownModule() override
	{
		if (GIsEditor && !IsRunningCommandlet())
		{
			if (FModuleManager::Get().IsModuleLoaded("PropertyEditor"))
			{
				FPropertyEditorModule& PropertyModule = FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor");
				PropertyModule.UnregisterCustomClassLayout("FabSettings");
			}
			FabAuthentication::Shutdown();
			FFabBrowser::Shutdown();
			FFabDownloadRequest::ShutdownBpsModule();
		}
	}
};

IMPLEMENT_MODULE(FFabModule, Fab);
