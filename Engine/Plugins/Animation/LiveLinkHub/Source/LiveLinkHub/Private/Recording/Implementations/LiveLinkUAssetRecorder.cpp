// Copyright Epic Games, Inc. All Rights Reserved.

#include "LiveLinkUAssetRecorder.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "Config/LiveLinkHubFileUtilities.h"
#include "Containers/Map.h"
#include "ContentBrowserModule.h"
#include "Features/IModularFeatures.h"
#include "FileHelpers.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "HAL/PlatformFileManager.h"
#include "IContentBrowserSingleton.h"
#include "LiveLinkHub.h"
#include "LiveLinkHubClient.h"
#include "LiveLinkHubLog.h"
#include "LiveLinkHubModule.h"
#include "LiveLinkPreset.h"
#include "LiveLinkTypes.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Recording/Implementations/LiveLinkUAssetRecording.h"
#include "Recording/LiveLinkHubPlaybackController.h"
#include "Recording/LiveLinkRecording.h"
#include "Settings/LiveLinkHubSettings.h"
#include "StructUtils/InstancedStruct.h"
#include "UI/Window/LiveLinkHubWindowController.h"
#include "UObject/Object.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "UObject/StrongObjectPtr.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SWidget.h"

#define LOCTEXT_NAMESPACE "LiveLinkHub.RecordingController"


namespace UAssetRecorderUtils
{
	TOptional<FLiveLinkRecordingStaticDataContainer> CreateStaticDataContainerFromFrameData(const FLiveLinkSubjectKey& SubjectKey)
	{
		TOptional<FLiveLinkRecordingStaticDataContainer> StaticDataContainer;
		FLiveLinkHubClient* LiveLinkClient = static_cast<FLiveLinkHubClient*>(&IModularFeatures::Get().GetModularFeature<ILiveLinkClient>(ILiveLinkClient::ModularFeatureName));
		TSubclassOf<ULiveLinkRole> LiveLinkRole = LiveLinkClient->GetSubjectRole_AnyThread(SubjectKey);
		
		if (const FLiveLinkStaticDataStruct* StaticData = LiveLinkClient->GetSubjectStaticData_AnyThread(SubjectKey, /* bGetOverrideData */ false))
		{
			if (StaticData->IsValid())
			{
				TSharedPtr<FInstancedStruct> StaticDataInstancedStruct = MakeShared<FInstancedStruct>();
				StaticDataInstancedStruct->InitializeAs(StaticData->GetStruct(), (uint8*)StaticData->GetBaseData());

				StaticDataContainer = FLiveLinkRecordingStaticDataContainer();
				StaticDataContainer->Role = LiveLinkRole;
				StaticDataContainer->RecordedData.Insert(MoveTemp(StaticDataInstancedStruct), 0);
				StaticDataContainer->Timestamps.Add(0.0);
			}
		}

		return StaticDataContainer;
	}
}

void FLiveLinkUAssetRecorder::StartRecording()
{
	check(!CurrentRecording.IsValid());
	CurrentRecording = MakePimpl<FLiveLinkUAssetRecordingData>();
	RecordInitialStaticData();

	bIsRecording = true;
	TimeRecordingStarted = FPlatformTime::Seconds();
}

void FLiveLinkUAssetRecorder::StopRecording()
{
	if (CurrentRecording)
	{
		bIsRecording = false;

		TimeRecordingEnded = FPlatformTime::Seconds();

		SaveRecording();
		CurrentRecording.Reset();
	}
}

bool FLiveLinkUAssetRecorder::IsRecording() const
{
	return bIsRecording;
}

void FLiveLinkUAssetRecorder::RecordBaseData(FLiveLinkRecordingBaseDataContainer& StaticDataContainer, TSharedPtr<FInstancedStruct>&& DataToRecord)
{
	const double TimeNowInSeconds = FPlatformTime::Seconds();
	StaticDataContainer.RecordedData.Add(MoveTemp(DataToRecord));
	StaticDataContainer.Timestamps.Add(TimeNowInSeconds - TimeRecordingStarted);
}

void FLiveLinkUAssetRecorder::RecordStaticData(const FLiveLinkSubjectKey& SubjectKey, TSubclassOf<ULiveLinkRole> Role, const FLiveLinkStaticDataStruct& StaticData)
{
	if (bIsRecording && CurrentRecording)
	{
		FLiveLinkRecordingStaticDataContainer& StaticDataContainer = CurrentRecording->StaticData.FindOrAdd(SubjectKey);
		TSharedPtr<FInstancedStruct> NewData = MakeShared<FInstancedStruct>();
		NewData->InitializeAs(StaticData.GetStruct(), (uint8*)StaticData.GetBaseData());
		StaticDataContainer.Role = Role;

		RecordBaseData(StaticDataContainer, MoveTemp(NewData));
	}
}

void FLiveLinkUAssetRecorder::RecordFrameData(const FLiveLinkSubjectKey& SubjectKey, const FLiveLinkFrameDataStruct& FrameData)
{
	if (bIsRecording && CurrentRecording)
	{
		FLiveLinkRecordingBaseDataContainer& FrameDataContainer = CurrentRecording->FrameData.FindOrAdd(SubjectKey);
		TSharedPtr<FInstancedStruct> NewData = MakeShared<FInstancedStruct>();
		NewData->InitializeAs(FrameData.GetStruct(), (uint8*)FrameData.GetBaseData());

		RecordBaseData(FrameDataContainer, MoveTemp(NewData));
	}
}

bool FLiveLinkUAssetRecorder::OpenSaveDialog(const FString& InDefaultPath, const FString& InNewNameSuggestion, FString& OutPackageName)
{
	TSharedRef<SWindow> RootWindow = FModuleManager::Get().GetModuleChecked<FLiveLinkHubModule>("LiveLinkHub").GetLiveLinkHub()->GetRootWindow();

	FSaveAssetDialogConfig SaveAssetDialogConfig;
	{
		SaveAssetDialogConfig.DefaultPath = InDefaultPath;
		SaveAssetDialogConfig.DefaultAssetName = InNewNameSuggestion;
		SaveAssetDialogConfig.AssetClassNames.Add(ULiveLinkRecording::StaticClass()->GetClassPathName());
		SaveAssetDialogConfig.ExistingAssetPolicy = ESaveAssetDialogExistingAssetPolicy::AllowButWarn;
		SaveAssetDialogConfig.DialogTitleOverride = LOCTEXT("SaveLiveLinkRecordingDialogTitle", "Save Live Link Recording");
		SaveAssetDialogConfig.WindowOverride = RootWindow;
	}

	FContentBrowserModule& ContentBrowserModule = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser");
	FString SaveObjectPath = ContentBrowserModule.Get().CreateModalSaveAssetDialog(SaveAssetDialogConfig);

	if (!SaveObjectPath.IsEmpty())
	{
		OutPackageName = FPackageName::ObjectPathToPackageName(SaveObjectPath);
		return true;
	}

	return false;
}

bool FLiveLinkUAssetRecorder::GetSavePresetPackageName(FString& OutName)
{
	using namespace UE::LiveLinkHub::FileUtilities::Private;
	const FDateTime Today = FDateTime::Now();

	TMap<FString, FStringFormatArg> FormatArgs;
	FormatArgs.Add(TEXT("date"), Today.ToString());
	
	FFilenameTemplateData TemplateData;
	ParseFilenameTemplate(GetDefault<ULiveLinkHubSettings>()->FilenameTemplate, TemplateData);

	const FString DefaultName = TemplateData.FileName;
	const FString DefaultFolder = TemplateData.FolderPath;
	
	const FString ContentDir = FPaths::ProjectContentDir();
	const FString DialogStartPath = FPaths::Combine(TEXT("/Game"), TemplateData.FolderPath);
	const FString AbsoluteFolderPath = FPaths::Combine(ContentDir, TemplateData.FolderPath);

	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();

	// Create directory if it doesn't exist
	if (!PlatformFile.DirectoryExists(*AbsoluteFolderPath))
	{
		if (!PlatformFile.CreateDirectoryTree(*AbsoluteFolderPath))
		{
			UE_LOG(LogLiveLinkHub, Error, TEXT("Failed to create directory %s."), *AbsoluteFolderPath);
			return false;
		}

		FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
		AssetRegistryModule.Get().ScanPathsSynchronous({ TEXT("/Game") }, true);
	}
	FString UniquePackageName;
	FString UniqueAssetName;

	FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools");
	AssetToolsModule.Get().CreateUniqueAssetName(DefaultFolder / DefaultName, TEXT(""), UniquePackageName, UniqueAssetName);

	const FString DialogStartName = FPaths::GetCleanFilename(UniqueAssetName);

	FString UserPackageName;
	FString NewPackageName;

	// Get destination for asset
	bool bFilenameValid = false;
	while (!bFilenameValid)
	{
		if (!OpenSaveDialog(DialogStartPath, DialogStartName, UserPackageName))
		{
			return false;
		}

		NewPackageName = FString::Format(*UserPackageName, FormatArgs);

		{
			// Ensure we aren't saving over an existing recording as this isn't supported yet... the package isn't saved properly.
			// todo: We should support saving over/deleting existing recordings. This is simply a low risk fix to a playback crash for 5.5.0.
			AssetToolsModule.Get().CreateUniqueAssetName(NewPackageName, TEXT(""), NewPackageName, UniqueAssetName);
		}
		
		FText OutError;
		bFilenameValid = FFileHelper::IsFilenameValidForSaving(NewPackageName, OutError);
	}

	OutName = MoveTemp(NewPackageName);
	return true;
}

void FLiveLinkUAssetRecorder::SaveRecording()
{
	FString PackageName;
	if (!GetSavePresetPackageName(PackageName))
	{
		return;
	}

	// Saving into a new package
	const FString NewAssetName = FPackageName::GetLongPackageAssetName(PackageName);
	UPackage* NewPackage = CreatePackage(*PackageName);

	if (TObjectPtr<ULiveLinkUAssetRecording> NewRecording = NewObject<ULiveLinkUAssetRecording>(NewPackage, *NewAssetName, RF_Public | RF_Standalone))
	{
		const double RecordingLength = TimeRecordingEnded - TimeRecordingStarted;
		NewRecording->InitializeNewRecordingData(MoveTemp(*CurrentRecording), RecordingLength);

		NewRecording->MarkPackageDirty();

		FAssetRegistryModule::AssetCreated(NewRecording);

		const TStrongObjectPtr<ULiveLinkUAssetRecording> NewRecordingStrongPtr(NewRecording);
		
		// Create a task to run on a separate thread for saving all frame data and writing the final UAsset to disk.
		// We use a container rather than just one task on the chance a save operation is still running when another recording is being saved.
		FAsyncTask<FLiveLinkSaveRecordingAsyncTask>& AsyncTask = *AsyncSaveTasks.Add(NewRecordingStrongPtr,
			MakeUnique<FAsyncTask<FLiveLinkSaveRecordingAsyncTask>>(NewRecording, this));
		AsyncTask.StartBackgroundTask();
	}
}

void FLiveLinkUAssetRecorder::RecordInitialStaticData()
{
	FLiveLinkHubClient* LiveLinkClient = static_cast<FLiveLinkHubClient*>(&IModularFeatures::Get().GetModularFeature<ILiveLinkClient>(ILiveLinkClient::ModularFeatureName));
	TArray<FLiveLinkSubjectKey> Subjects = LiveLinkClient->GetSubjects(true, true);

	for (const FLiveLinkSubjectKey& Subject : Subjects)
	{
		TOptional<FLiveLinkRecordingStaticDataContainer> StaticDataContainer = UAssetRecorderUtils::CreateStaticDataContainerFromFrameData(Subject);
		if (StaticDataContainer)
		{
			CurrentRecording->StaticData.Add(Subject, MoveTemp(*StaticDataContainer));
		}
	}
}

void FLiveLinkUAssetRecorder::OnRecordingDataSaved_GameThread(FLiveLinkSaveRecordingAsyncTask* InTask)
{
	const TStrongObjectPtr<ULiveLinkUAssetRecording> Recording = InTask->GetRecording().Pin();
	if (ensure(Recording.IsValid()))
	{
		FSavePackageArgs SavePackageArgs;
		SavePackageArgs.TopLevelFlags = RF_Public | RF_Standalone;
		SavePackageArgs.Error = GLog;
		SavePackageArgs.SaveFlags = SAVE_Async;

		const FString PackageFileName = FPackageName::LongPackageNameToFilename(Recording->GetPackage()->GetName(), FPackageName::GetAssetPackageExtension());
		if (!UPackage::SavePackage(Recording->GetPackage(), Recording.Get(), *PackageFileName, MoveTemp(SavePackageArgs)))
		{
			UE_LOG(LogLiveLinkHub, Error, TEXT("Package '%s' was not saved"), *PackageFileName);
		}
	}

	InTask->NotifyPackageSaveStarted();
}

void FLiveLinkUAssetRecorder::OnRecordingSaveThreadFinished_GameThread(FLiveLinkSaveRecordingAsyncTask* InTask)
{
	// Make sure we see the saved file on disk in the asset registry.
	const FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	AssetRegistryModule.Get().ScanPathsSynchronous({ TEXT("/Game") }, true);
	
	TStrongObjectPtr<ULiveLinkUAssetRecording> Recording = InTask->GetRecording().Pin();
	if (ensure(Recording.IsValid()))
	{
		// Finish task first to make sure strong reference to the recording is cleared.
		if (const TUniquePtr<FAsyncTask<FLiveLinkSaveRecordingAsyncTask>>* AsyncTask = AsyncSaveTasks.Find(Recording))
		{
			AsyncTask->Get()->EnsureCompletion();
			ensure(AsyncSaveTasks.Remove(Recording) > 0);
		}
		else
		{
			UE_LOG(LogLiveLinkHub, Error, TEXT("Could not find save task for recording: '%s'"), *Recording->GetName());
		}
	
		const FLiveLinkHubModule& LiveLinkHubModule = FModuleManager::Get().GetModuleChecked<FLiveLinkHubModule>("LiveLinkHub");
		const TStrongObjectPtr<ULiveLinkRecording> PlaybackRecording = LiveLinkHubModule.GetPlaybackController()->GetRecording();

		UPackage* PackageToUnload = Recording->GetPackage();
		const bool bIsPlayingThisRecording = PlaybackRecording.Get() == Recording.Get();

		if (!bIsPlayingThisRecording)
		{
			// Unload as this is not used again until the user loads it, and allows the bulk animation data to obtain a file handle correctly.
			Recording.Reset();
			LiveLinkHubModule.GetPlaybackController()->UnloadRecordingPackage(PackageToUnload);
		}
	}
}

void FLiveLinkUAssetRecorder::FLiveLinkSaveRecordingAsyncTask::DoWork()
{
	check(LiveLinkRecording.IsValid());

	// Write to bulk data.
	LiveLinkRecording->SaveRecordingData();

	// Let the game thread start the actual package save. Editor targets will assert if SavePackage is called from another thread.
	FSimpleDelegateGraphTask::CreateAndDispatchWhenReady(
		FSimpleDelegateGraphTask::FDelegate::CreateRaw(Recorder, &FLiveLinkUAssetRecorder::OnRecordingDataSaved_GameThread, this),
	TStatId(),
	nullptr,
	ENamedThreads::GameThread);

	// Wait for the game thread to signal it has started saving the package.
	constexpr uint32 TimeoutMillis = 5000;
	if (!PackageSaveStartedEvent->Wait(TimeoutMillis))
	{
		UE_LOG(LogLiveLinkHub, Error, TEXT("Timed out waiting for package save."));
	}

	// Block the async thread until the package saves. We wait because the engine doesn't provide any proper callback
	// when the package finishes saving async, and we still have some cleanup to do once it finishes saving.
	UPackage::WaitForAsyncFileWrites();

	// Cleanup and finish the thread.
	FSimpleDelegateGraphTask::CreateAndDispatchWhenReady(
	FSimpleDelegateGraphTask::FDelegate::CreateRaw(Recorder, &FLiveLinkUAssetRecorder::OnRecordingSaveThreadFinished_GameThread, this),
	TStatId(),
	nullptr,
	ENamedThreads::GameThread);
}

#undef LOCTEXT_NAMESPACE
