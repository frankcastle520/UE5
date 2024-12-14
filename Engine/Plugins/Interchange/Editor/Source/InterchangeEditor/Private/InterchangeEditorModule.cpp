// Copyright Epic Games, Inc. All Rights Reserved.
#include "InterchangeEditorModule.h"

#include "AssetToolsModule.h"
#include "Engine/Engine.h"
#include "IMessageLogListing.h"
#include "InterchangeEditorLog.h"
#include "InterchangeFbxAssetImportDataConverter.h"
#include "InterchangeManager.h"
#include "InterchangeUsdTranslatorSettingsCustomization.h"
#include "MessageLogModule.h"
#include "Misc/App.h"
#include "Misc/CoreDelegates.h"
#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"
#include "Settings/EditorLoadingSavingSettings.h"

#define LOCTEXT_NAMESPACE "InterchangeEditorModule"

DEFINE_LOG_CATEGORY(LogInterchangeEditor);

namespace UE::Interchange::InterchangeEditorModule
{
	static bool bOldAutoSaveState = false;
	bool HasErrorsOrWarnings(TStrongObjectPtr<UInterchangeResultsContainer> InResultsContainer)
	{
		for (UInterchangeResult* Result : InResultsContainer->GetResults())
		{
			if (Result->GetResultType() != EInterchangeResultType::Success)
			{
				return true;
			}
		}

		return false;
	}

	void LogErrors(TStrongObjectPtr<UInterchangeResultsContainer> InResultsContainer)
	{
		// Only showing when we have errors or warnings for now
		if (FApp::IsUnattended() || !HasErrorsOrWarnings(InResultsContainer))
		{
			return;
		}

		FMessageLogModule& MessageLogModule = FModuleManager::LoadModuleChecked<FMessageLogModule>("MessageLog");
		TSharedPtr<IMessageLogListing> LogListing = MessageLogModule.GetLogListing(FName("Interchange"));

		if (ensure(LogListing))
		{
			const FText LogListingLabel = NSLOCTEXT("InterchangeImport", "Label", "Interchange Import");
			LogListing->SetLabel(LogListingLabel);

			TArray<TSharedRef<FTokenizedMessage>> TokenizedMessages;
			bool bHasErrors = false;

			for (UInterchangeResult* Result : InResultsContainer->GetResults())
			{
				const EInterchangeResultType ResultType = Result->GetResultType();
				if (ResultType != EInterchangeResultType::Success)
				{
					TokenizedMessages.Add(FTokenizedMessage::Create(ResultType == EInterchangeResultType::Error ? EMessageSeverity::Error : EMessageSeverity::Warning, Result->GetMessageLogText()));
					bHasErrors |= ResultType == EInterchangeResultType::Error;
				}
			}

			LogListing->AddMessages(TokenizedMessages);
			LogListing->NotifyIfAnyMessages(NSLOCTEXT("Interchange", "LogAndNotify", "There were issues with the import."), EMessageSeverity::Info);
		}
	}

	void ImportStarted()
	{
		//Store AutoSave setting and Set autosave to false:
		UEditorLoadingSavingSettings* LoadingSavingSettings = GetMutableDefault<UEditorLoadingSavingSettings>();
		// Disable autosaving while the Interchange is in progress.
		bOldAutoSaveState = LoadingSavingSettings->bAutoSaveEnable;
		LoadingSavingSettings->bAutoSaveEnable = false;
	}

	void ImportFinished()
	{
		//Reinstate AutoSave
		UEditorLoadingSavingSettings* LoadingSavingSettings = GetMutableDefault<UEditorLoadingSavingSettings>();
		LoadingSavingSettings->bAutoSaveEnable = bOldAutoSaveState;
	}
} //ns UE::Interchange::InterchangeEditorModule

FInterchangeEditorModule& FInterchangeEditorModule::Get()
{
	return FModuleManager::LoadModuleChecked< FInterchangeEditorModule >(INTERCHANGEEDITOR_MODULE_NAME);
}

bool FInterchangeEditorModule::IsAvailable()
{
	return FModuleManager::Get().IsModuleLoaded(INTERCHANGEEDITOR_MODULE_NAME);
}

void FInterchangeEditorModule::StartupModule()
{
	using namespace UE::Interchange;

	auto RegisterItems = [this]()
	{
		FDelegateHandle InterchangeEditorModuleDelegate;
		FDelegateHandle InterchangeEditorModuleDelegateOnImportStarted;
		FDelegateHandle InterchangeEditorModuleDelegateOnImportFinished;
		FDelegateHandle InterchangeEditorModuleDelegateOnSanitizeName;

		UInterchangeManager& InterchangeManager = UInterchangeManager::GetInterchangeManager();
		InterchangeEditorModuleDelegate = InterchangeManager.OnBatchImportComplete.AddStatic(&InterchangeEditorModule::LogErrors);
		InterchangeEditorModuleDelegateOnImportStarted = InterchangeManager.OnImportStarted.AddStatic(&InterchangeEditorModule::ImportStarted);
		InterchangeEditorModuleDelegateOnImportFinished = InterchangeManager.OnImportFinished.AddStatic(&InterchangeEditorModule::ImportFinished);
		InterchangeManager.RegisterImportDataConverter(UInterchangeFbxAssetImportDataConverter::StaticClass());

		InterchangeEditorModuleDelegateOnSanitizeName = InterchangeManager.OnSanitizeName.AddLambda([](FString& SanitizeName, const ESanitizeNameTypeFlags NameType)
			{
				//Call the asset tools sanitize
				IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
				AssetTools.SanitizeName(SanitizeName);
			});

		auto UnregisterItems = [InterchangeEditorModuleDelegate
			, InterchangeEditorModuleDelegateOnImportStarted
			, InterchangeEditorModuleDelegateOnImportFinished
			, InterchangeEditorModuleDelegateOnSanitizeName]()
		{
			UInterchangeManager& InterchangeManager = UInterchangeManager::GetInterchangeManager();
			InterchangeManager.OnBatchImportComplete.Remove(InterchangeEditorModuleDelegate);
			InterchangeManager.OnImportStarted.Remove(InterchangeEditorModuleDelegateOnImportStarted);
			InterchangeManager.OnImportFinished.Remove(InterchangeEditorModuleDelegateOnImportFinished);
			InterchangeManager.OnSanitizeName.Remove(InterchangeEditorModuleDelegateOnSanitizeName);
		};

		InterchangeManager.OnPreDestroyInterchangeManager.AddLambda(UnregisterItems);

		// Translator settings customizations
		FPropertyEditorModule& PropertyModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>(TEXT("PropertyEditor"));
		PropertyModule.RegisterCustomClassLayout(
			TEXT("InterchangeUsdTranslatorSettings"),
			FOnGetDetailCustomizationInstance::CreateStatic(&FInterchangeUsdTranslatorSettingsCustomization::MakeInstance)
		);
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

void FInterchangeEditorModule::ShutdownModule()
{
	//Translator settings customizations
	if (FPropertyEditorModule* PropertyModule = FModuleManager::GetModulePtr<FPropertyEditorModule>(TEXT("PropertyEditor")))
	{
		PropertyModule->UnregisterCustomClassLayout(TEXT("InterchangeUsdTranslatorSettings"));
	}
}

IMPLEMENT_MODULE(FInterchangeEditorModule, InterchangeEditor)

#undef LOCTEXT_NAMESPACE // "InterchangeEditorModule"

