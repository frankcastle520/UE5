// Copyright Epic Games, Inc. All Rights Reserved.

#include "AssetDefinition_NiagaraSimCache.h"

#include "ContentBrowserMenuContexts.h"
#include "DesktopPlatformModule.h"
#include "EditorDirectories.h"
#include "NiagaraEditorStyle.h"
#include "NiagaraSimCacheJson.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Toolkits/NiagaraSimCacheToolkit.h"
#include "ToolMenus.h"
#include "Widgets/Notifications/SNotificationList.h"

#define LOCTEXT_NAMESPACE "UAssetDefinition_NiagaraSimCache"

DEFINE_LOG_CATEGORY_STATIC(LogNiagaraSimCache, Log, All);

FLinearColor UAssetDefinition_NiagaraSimCache::GetAssetColor() const
{
	return FNiagaraEditorStyle::Get().GetColor("NiagaraEditor.AssetColors.SimCache").ToFColor(true);
}

EAssetCommandResult UAssetDefinition_NiagaraSimCache::OpenAssets(const FAssetOpenArgs& OpenArgs) const
{
	for (UNiagaraSimCache* SimCache : OpenArgs.LoadObjects<UNiagaraSimCache>())
	{
		const TSharedRef< FNiagaraSimCacheToolkit > NewNiagaraSimCacheToolkit(new FNiagaraSimCacheToolkit());
		NewNiagaraSimCacheToolkit->Initialize(OpenArgs.GetToolkitMode(), OpenArgs.ToolkitHost, SimCache);
	}

	return EAssetCommandResult::Handled;
}

// Menu Extensions
//--------------------------------------------------------------------

namespace MenuExtension_NiagaraSimCache
{	
	void ExportToDisk(const FToolMenuContext& InContext)
	{
		FString ExportFolder;
		
		if (IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get())
		{
			const bool bFolderPicked = DesktopPlatform->OpenDirectoryDialog(
				FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr),
				LOCTEXT("ExportSimCache", "Pick SimCache Export Folder" ).ToString(),
				*(FEditorDirectories::Get().GetLastDirectory(ELastDirectory::GENERIC_EXPORT)),
				ExportFolder);

			if (!bFolderPicked)
			{
				return;
			}
			FEditorDirectories::Get().SetLastDirectory(ELastDirectory::GENERIC_EXPORT, ExportFolder); 
		}
		
		bool bWarning = false;
		const UContentBrowserAssetContextMenuContext* CBContext = UContentBrowserAssetContextMenuContext::FindContextWithAssets(InContext);
		IFileManager& FileManager = IFileManager::Get();
		for (UNiagaraSimCache* Cache : CBContext->LoadSelectedObjects<UNiagaraSimCache>())
		{
			if (Cache == nullptr)
			{
				continue;
			}

			FString CacheRootFolder = FPaths::Combine(ExportFolder, FPaths::MakeValidFileName(Cache->GetName(), '_'));
			if (FileManager.DirectoryExists(*CacheRootFolder))
			{
				if (!FileManager.DeleteDirectory(*CacheRootFolder, true, true))
				{
					UE_LOG(LogNiagaraSimCache, Warning, TEXT("Unable to delete existing folder %s"), *CacheRootFolder);
					bWarning = true;
					continue;
				}
			}
			if (!FileManager.MakeDirectory(*CacheRootFolder, true))
			{
				UE_LOG(LogNiagaraSimCache, Warning, TEXT("Unable to create folder %s"), *CacheRootFolder);
				bWarning = true;
				continue;
			}
			if (!FNiagaraSimCacheJson::DumpToFile(*Cache, CacheRootFolder, FNiagaraSimCacheJson::EExportType::SeparateEachFrame))
			{
				bWarning = true;
			}
		}
		
		FNotificationInfo Info(LOCTEXT("ExportToDisk_DoneInfo", "Export completed."));
		Info.ExpireDuration = 4.0f;
		if (bWarning)
		{
			Info.Text = LOCTEXT("ExportData_DoneWarn", "Export completed with warnings.\nPlease check the log.");
			Info.Image = FCoreStyle::Get().GetBrush(TEXT("MessageLog.Warning"));
		}
		FSlateNotificationManager::Get().AddNotification(Info);
	}
	
	static FDelayedAutoRegisterHelper DelayedAutoRegister(EDelayedRegisterRunPhase::EndOfEngineInit, []{ 
		UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateLambda([]()
		{
			FToolMenuOwnerScoped OwnerScoped("Niagara SimCache");
			UToolMenu* Menu = UE::ContentBrowser::ExtendToolMenu_AssetContextMenu(UNiagaraSimCache::StaticClass());
		
			FToolMenuSection& Section = Menu->FindOrAddSection("GetAssetActions");
			Section.AddDynamicEntry(NAME_None, FNewToolMenuSectionDelegate::CreateLambda([](FToolMenuSection& InSection)
			{
				{
					const TAttribute<FText> Label = LOCTEXT("ExportToDisk", "Export To Disk");
					const TAttribute<FText> ToolTip = LOCTEXT("ExportToDiskTooltip", "Exports the raw data for each frame to disk. Note that data from data interfaces is only exported if they implement support for it.");
					const FSlateIcon Icon = FSlateIcon();

					FToolUIAction UIAction;
					UIAction.ExecuteAction = FToolMenuExecuteAction::CreateStatic(&ExportToDisk);
					InSection.AddMenuEntry("ExportToDisk", Label, ToolTip, Icon, UIAction);
				}
			}));
		}));
	});
}

#undef LOCTEXT_NAMESPACE
