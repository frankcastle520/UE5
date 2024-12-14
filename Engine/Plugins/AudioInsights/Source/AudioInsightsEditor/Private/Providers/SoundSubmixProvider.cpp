// Copyright Epic Games, Inc. All Rights Reserved.

#include "Providers/SoundSubmixProvider.h"

#include "AssetRegistry/AssetData.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AudioInsightsEditorDashboardFactory.h"
#include "AudioInsightsEditorModule.h"
#include "Sound/SoundSubmix.h"

namespace UE::Audio::Insights
{
	FSoundSubmixProvider::FSoundSubmixProvider()
		: TDeviceDataMapTraceProvider<uint32, TSharedPtr<FSoundSubmixAssetDashboardEntry>>(GetName_Static())
	{
		FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");

		AssetRegistryModule.Get().OnAssetAdded().AddRaw(this, &FSoundSubmixProvider::OnAssetAdded);
		AssetRegistryModule.Get().OnAssetRemoved().AddRaw(this, &FSoundSubmixProvider::OnAssetRemoved);
		AssetRegistryModule.Get().OnFilesLoaded().AddRaw(this, &FSoundSubmixProvider::OnFilesLoaded);

		FEditorDashboardFactory::OnActiveAudioDeviceChanged.AddRaw(this, &FSoundSubmixProvider::OnActiveAudioDeviceChanged);
	}
	
	FSoundSubmixProvider::~FSoundSubmixProvider()
	{
		if (FAssetRegistryModule* AssetRegistryModule = FModuleManager::GetModulePtr<FAssetRegistryModule>("AssetRegistry"))
		{
			AssetRegistryModule->Get().OnAssetAdded().RemoveAll(this);
			AssetRegistryModule->Get().OnAssetRemoved().RemoveAll(this);
			AssetRegistryModule->Get().OnFilesLoaded().RemoveAll(this);
		}

		FEditorDashboardFactory::OnActiveAudioDeviceChanged.RemoveAll(this);
	}

	FName FSoundSubmixProvider::GetName_Static()
	{
		return "SubmixesProvider";
	}

	void FSoundSubmixProvider::OnAssetAdded(const FAssetData& InAssetData)
	{
		if (bAreFilesLoaded && InAssetData.AssetClassPath == FTopLevelAssetPath(USoundSubmix::StaticClass()))
		{
			AddSubmixAsset(InAssetData);
		}
	}

	void FSoundSubmixProvider::OnAssetRemoved(const FAssetData& InAssetData)
	{
		if (InAssetData.AssetClassPath == FTopLevelAssetPath(USoundSubmix::StaticClass()))
		{
			RemoveSubmixAsset(InAssetData);
		}
	}

	void FSoundSubmixProvider::OnFilesLoaded()
	{
		bAreFilesLoaded = true;
		UpdateSubmixAssetNames();
	}

	void FSoundSubmixProvider::OnActiveAudioDeviceChanged()
	{
		UpdateSubmixAssetNames();
	}

	void FSoundSubmixProvider::AddSubmixAsset(const FAssetData& InAssetData)
	{
		const bool bIsSubmixAssetAlreadAdded = SubmixDataViewEntries.ContainsByPredicate(
			[AssetName = InAssetData.GetObjectPathString()](const TSharedPtr<FSoundSubmixAssetDashboardEntry>& SoundSubmixAssetDashboardEntry)
			{
				return SoundSubmixAssetDashboardEntry->Name == AssetName;
			});

		if (!bIsSubmixAssetAlreadAdded)
		{
			const FAudioInsightsEditorModule AudioInsightsEditorModule = FAudioInsightsEditorModule::GetChecked();
			const ::Audio::FDeviceId AudioDeviceId = AudioInsightsEditorModule.GetDeviceId();

			TSharedPtr<FSoundSubmixAssetDashboardEntry> SoundSubmixAssetDashboardEntry = MakeShared<FSoundSubmixAssetDashboardEntry>();
			SoundSubmixAssetDashboardEntry->DeviceId    = AudioDeviceId;
			SoundSubmixAssetDashboardEntry->Name        = InAssetData.GetObjectPathString();
			SoundSubmixAssetDashboardEntry->SoundSubmix = Cast<USoundSubmix>(InAssetData.GetAsset());

			SubmixDataViewEntries.Add(MoveTemp(SoundSubmixAssetDashboardEntry));

			OnSubmixAssetAdded.Broadcast(InAssetData.GetAsset());
			++LastUpdateId;
		}
	}

	void FSoundSubmixProvider::RemoveSubmixAsset(const FAssetData& InAssetData)
	{
		const int32 FoundSubmixAssetNameIndex = SubmixDataViewEntries.IndexOfByPredicate(
			[AssetName = InAssetData.GetObjectPathString()](const TSharedPtr<FSoundSubmixAssetDashboardEntry>& SoundSubmixAssetDashboardEntry)
			{
				return SoundSubmixAssetDashboardEntry->Name == AssetName;
			});

		if (FoundSubmixAssetNameIndex != INDEX_NONE)
		{
			const FAudioInsightsEditorModule AudioInsightsEditorModule = FAudioInsightsEditorModule::GetChecked();
			const ::Audio::FDeviceId AudioDeviceId = AudioInsightsEditorModule.GetDeviceId();

			RemoveDeviceEntry(AudioDeviceId, SubmixDataViewEntries[FoundSubmixAssetNameIndex]->SoundSubmix->GetUniqueID());

			SubmixDataViewEntries.RemoveAt(FoundSubmixAssetNameIndex);

			OnSubmixAssetRemoved.Broadcast(InAssetData.GetAsset());
			++LastUpdateId;
		}
	}

	void FSoundSubmixProvider::UpdateSubmixAssetNames()
	{
		// Get all USoundSubmix assets
		TArray<FAssetData> AssetDataArray;

		FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
		AssetRegistryModule.Get().GetAssetsByClass(FTopLevelAssetPath(USoundSubmix::StaticClass()), AssetDataArray);

		// Build SubmixDataViewEntries
		Reset();
		SubmixDataViewEntries.Empty();

		for (const FAssetData& AssetData : AssetDataArray)
		{
			AddSubmixAsset(AssetData);
		}

		SubmixDataViewEntries.Sort([](const TSharedPtr<FSoundSubmixAssetDashboardEntry>& A, const TSharedPtr<FSoundSubmixAssetDashboardEntry>& B)
		{
			return A->GetDisplayName().CompareToCaseIgnored(B->GetDisplayName()) < 0;
		});

		OnSubmixAssetListUpdated.Broadcast();
	}

	bool FSoundSubmixProvider::ProcessMessages()
	{
		const FAudioInsightsEditorModule AudioInsightsEditorModule = FAudioInsightsEditorModule::GetChecked();
		const ::Audio::FDeviceId AudioDeviceId = AudioInsightsEditorModule.GetDeviceId();

		for (const TSharedPtr<FSoundSubmixAssetDashboardEntry>& SubmixDataViewEntry : SubmixDataViewEntries)
		{
			if (SubmixDataViewEntry.IsValid() && SubmixDataViewEntry->SoundSubmix.IsValid())
			{
				UpdateDeviceEntry(AudioDeviceId, SubmixDataViewEntry->SoundSubmix->GetUniqueID(), [&SubmixDataViewEntry](TSharedPtr<FSoundSubmixAssetDashboardEntry>& Entry)
				{
					if (!Entry.IsValid())
					{
						Entry = SubmixDataViewEntry;
					}
				});
			}
		}

		return true;
	}

	UE::Trace::IAnalyzer* FSoundSubmixProvider::ConstructAnalyzer(TraceServices::IAnalysisSession& InSession)
	{
		return nullptr;
	}
} // namespace UE::Audio::Insights
