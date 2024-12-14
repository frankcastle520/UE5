// Copyright Epic Games, Inc. All Rights Reserved. 
#include "InterchangePipelineConfigurationGeneric.h"

#include "Framework/Application/SlateApplication.h"
#include "Interfaces/IMainFrameModule.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/ConfigContext.h"
#include "Nodes/InterchangeBaseNodeContainer.h"
#include "SInterchangePipelineConfigurationDialog.h"
#include "Widgets/SWindow.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(InterchangePipelineConfigurationGeneric)


namespace UE::Interchange::Private
{
	float GetMinSizeX() { return 550.f; }
	float GetMinSizeY() { return 500.f; }

	const FVector2D& DefaultClientSize()
	{
		static FVector2D DefaultClientSize(1000.0, 650.0);
		return DefaultClientSize;
	}

	const FString& GetSizeXUid()
	{
		static FString SizeXStr = TEXT("SizeX");
		return SizeXStr;
	}

	const FString& GetSizeYUid()
	{
		static FString SizeYStr = TEXT("SizeY");
		return SizeYStr;
	}
	void RestoreClientSize(FVector2D& OutClientSize, const FString& WindowsUniqueID)
	{
		if (GConfig->DoesSectionExist(TEXT("InterchangeImportDialogOptions"), GEditorPerProjectIni))
		{
			FString SizeXUid = WindowsUniqueID + GetSizeXUid();
			FString SizeYUid = WindowsUniqueID + GetSizeYUid();
			GConfig->GetDouble(TEXT("InterchangeImportDialogOptions"), *SizeXUid, OutClientSize.X, GEditorPerProjectIni);
			GConfig->GetDouble(TEXT("InterchangeImportDialogOptions"), *SizeYUid, OutClientSize.Y, GEditorPerProjectIni);
		}
	}
	void BackupClientSize(const FVector2D& ClientSize, const FString& WindowsUniqueID)
	{
		FString SizeXUid = WindowsUniqueID + GetSizeXUid();
		FString SizeYUid = WindowsUniqueID + GetSizeYUid();
		GConfig->SetDouble(TEXT("InterchangeImportDialogOptions"), *SizeXUid, ClientSize.X, GEditorPerProjectIni);
		GConfig->SetDouble(TEXT("InterchangeImportDialogOptions"), *SizeYUid, ClientSize.Y, GEditorPerProjectIni);
	}
}

EInterchangePipelineConfigurationDialogResult UInterchangePipelineConfigurationGeneric::ShowPipelineConfigurationDialog(TArray<FInterchangeStackInfo>& PipelineStacks
	, TArray<UInterchangePipelineBase*>& OutPipelines
	, TWeakObjectPtr<UInterchangeSourceData> SourceData
	, TWeakObjectPtr <UInterchangeTranslatorBase> Translator
	, TWeakObjectPtr<UInterchangeBaseNodeContainer> BaseNodeContainer)
{
	using namespace UE::Interchange::Private;
	//Create and show the graph inspector UI dialog
	TSharedPtr<SWindow> ParentWindow;
	if(IMainFrameModule* MainFrame = FModuleManager::LoadModulePtr<IMainFrameModule>("MainFrame"))
	{
		ParentWindow = MainFrame->GetParentWindow();
	}
	FString WindowDialogUid = TEXT("ImportContentDialog");
	FVector2D ClientSize = DefaultClientSize();
	RestoreClientSize(ClientSize, WindowDialogUid);

	TSharedRef<SWindow> Window = SNew(SWindow)
		.ClientSize(ClientSize)
		.MinWidth(GetMinSizeX())
		.MinHeight(GetMinSizeY())
		.Title(NSLOCTEXT("Interchange", "PipelineConfigurationGenericTitleContent", "Import Content"));
	TSharedPtr<SInterchangePipelineConfigurationDialog> InterchangePipelineConfigurationDialog;

	Window->SetContent
	(
		SAssignNew(InterchangePipelineConfigurationDialog, SInterchangePipelineConfigurationDialog)
		.OwnerWindow(Window)
		.SourceData(SourceData)
		.bSceneImport(false)
		.bReimport(false)
		.PipelineStacks(PipelineStacks)
		.OutPipelines(&OutPipelines)
		.BaseNodeContainer(BaseNodeContainer)
		.Translator(Translator)
	);

	Window->SetOnWindowClosed(FOnWindowClosed::CreateLambda([&WindowDialogUid](const TSharedRef<SWindow>& WindowMoved)
		{
			FVector2D LastClientSize = WindowMoved->GetClientSizeInScreen();
			BackupClientSize(LastClientSize, WindowDialogUid);
		}));

	FSlateApplication::Get().AddModalWindow(Window, ParentWindow, false);
	
	if (InterchangePipelineConfigurationDialog->IsCanceled())
	{
		return EInterchangePipelineConfigurationDialogResult::Cancel;
	}

	if (InterchangePipelineConfigurationDialog->IsImportAll())
	{
		return EInterchangePipelineConfigurationDialogResult::ImportAll;
	}

	return EInterchangePipelineConfigurationDialogResult::Import;
}

EInterchangePipelineConfigurationDialogResult UInterchangePipelineConfigurationGeneric::ShowScenePipelineConfigurationDialog(TArray<FInterchangeStackInfo>& PipelineStacks
	, TArray<UInterchangePipelineBase*>& OutPipelines
	, TWeakObjectPtr<UInterchangeSourceData> SourceData
	, TWeakObjectPtr <UInterchangeTranslatorBase> Translator
	, TWeakObjectPtr<UInterchangeBaseNodeContainer> BaseNodeContainer)
{
	using namespace UE::Interchange::Private;
	//Create and show the graph inspector UI dialog
	TSharedPtr<SWindow> ParentWindow;
	if (IMainFrameModule* MainFrame = FModuleManager::LoadModulePtr<IMainFrameModule>("MainFrame"))
	{
		ParentWindow = MainFrame->GetParentWindow();
	}

	FString WindowDialogUid = TEXT("ImportSceneDialog");
	FVector2D ClientSize = DefaultClientSize();
	RestoreClientSize(ClientSize, WindowDialogUid);

	TSharedRef<SWindow> Window = SNew(SWindow)
		.ClientSize(ClientSize)
		.MinWidth(GetMinSizeX())
		.MinHeight(GetMinSizeY())
		.Title(NSLOCTEXT("Interchange", "PipelineConfigurationGenericTitleScene", "Import Scene"));
	TSharedPtr<SInterchangePipelineConfigurationDialog> InterchangePipelineConfigurationDialog;

	Window->SetContent
	(
		SAssignNew(InterchangePipelineConfigurationDialog, SInterchangePipelineConfigurationDialog)
		.OwnerWindow(Window)
		.SourceData(SourceData)
		.bSceneImport(true)
		.bReimport(false)
		.PipelineStacks(PipelineStacks)
		.OutPipelines(&OutPipelines)
		.BaseNodeContainer(BaseNodeContainer)
		.Translator(Translator)
	);

	Window->SetOnWindowClosed(FOnWindowClosed::CreateLambda([&WindowDialogUid](const TSharedRef<SWindow>& WindowMoved)
		{
			FVector2D LastClientSize = WindowMoved->GetClientSizeInScreen();
			BackupClientSize(LastClientSize, WindowDialogUid);
		}));

	FSlateApplication::Get().AddModalWindow(Window, ParentWindow, false);

	if (InterchangePipelineConfigurationDialog->IsCanceled())
	{
		return EInterchangePipelineConfigurationDialogResult::Cancel;
	}

	if (InterchangePipelineConfigurationDialog->IsImportAll())
	{
		return EInterchangePipelineConfigurationDialogResult::ImportAll;
	}

	return EInterchangePipelineConfigurationDialogResult::Import;
}

EInterchangePipelineConfigurationDialogResult UInterchangePipelineConfigurationGeneric::ShowReimportPipelineConfigurationDialog(TArray<FInterchangeStackInfo>& PipelineStacks
	, TArray<UInterchangePipelineBase*>& OutPipelines
	, TWeakObjectPtr<UInterchangeSourceData> SourceData
	, TWeakObjectPtr <UInterchangeTranslatorBase> Translator
	, TWeakObjectPtr<UInterchangeBaseNodeContainer> BaseNodeContainer
	, TWeakObjectPtr <UObject> ReimportAsset)
{
	using namespace UE::Interchange::Private;
	//Create and show the graph inspector UI dialog
	TSharedPtr<SWindow> ParentWindow;
	if (IMainFrameModule* MainFrame = FModuleManager::LoadModulePtr<IMainFrameModule>("MainFrame"))
	{
		ParentWindow = MainFrame->GetParentWindow();
	}

	FString WindowDialogUid = TEXT("ImportContentDialog");
	FVector2D ClientSize = DefaultClientSize();
	RestoreClientSize(ClientSize, WindowDialogUid);

	TSharedRef<SWindow> Window = SNew(SWindow)
		.ClientSize(ClientSize)
		.MinWidth(GetMinSizeX())
		.MinHeight(GetMinSizeY())
		.Title(NSLOCTEXT("Interchange", "PipelineConfigurationGenericTitleReimportContent", "Reimport Content"));
	TSharedPtr<SInterchangePipelineConfigurationDialog> InterchangePipelineConfigurationDialog;

	Window->SetContent
	(
		SAssignNew(InterchangePipelineConfigurationDialog, SInterchangePipelineConfigurationDialog)
		.OwnerWindow(Window)
		.SourceData(SourceData)
		.bSceneImport(false)
		.bReimport(true)
		.PipelineStacks(PipelineStacks)
		.OutPipelines(&OutPipelines)
		.BaseNodeContainer(BaseNodeContainer)
		.ReimportObject(ReimportAsset)
		.Translator(Translator)
	);

	Window->SetOnWindowClosed(FOnWindowClosed::CreateLambda([&WindowDialogUid](const TSharedRef<SWindow>& WindowMoved)
		{
			FVector2D LastClientSize = WindowMoved->GetClientSizeInScreen();
			BackupClientSize(LastClientSize, WindowDialogUid);
		}));

	FSlateApplication::Get().AddModalWindow(Window, ParentWindow, false);

	if (InterchangePipelineConfigurationDialog->IsCanceled())
	{
		return EInterchangePipelineConfigurationDialogResult::Cancel;
	}

	if (InterchangePipelineConfigurationDialog->IsImportAll())
	{
		return EInterchangePipelineConfigurationDialogResult::ImportAll;
	}

	return EInterchangePipelineConfigurationDialogResult::Import;
}
