// Copyright Epic Games, Inc. All Rights Reserved.

#include "Rundown/AvaRundownSerializationUtils.h"

#include "Backends/JsonStructDeserializerBackend.h"
#include "Backends/JsonStructSerializerBackend.h"
#include "HAL/FileManager.h"
#include "Rundown/AvaRundown.h"
#include "StructDeserializer.h"
#include "StructSerializer.h"

#define LOCTEXT_NAMESPACE "AvaRundownSerializationUtils"

namespace UE::AvaMedia::RundownSerializationUtils::Private
{
	// Don't serialize the transient properties.
	static auto TransientPropertyFilter = [](const FProperty* InCurrentProp, const FProperty* InParentProp)
	{
		const bool bIsTransient = InCurrentProp && InCurrentProp->HasAnyPropertyFlags(CPF_Transient); 
		return !bIsTransient; 
	};

	struct FRundownSerializerPolicies : public FStructSerializerPolicies
	{
		FRundownSerializerPolicies()
		{
			PropertyFilter = TransientPropertyFilter;
		}
	};

	struct FRundownDeserializerPolicies : public FStructDeserializerPolicies
	{
		FRundownDeserializerPolicies()
		{
			PropertyFilter = TransientPropertyFilter;
		}
	};
}

namespace UE::AvaMedia::RundownSerializationUtils
{
	bool SaveRundownToJson(const UAvaRundown* InRundown, FArchive& InArchive, FText& OutErrorMessage)
	{
		if (!IsValid(InRundown))
		{
			OutErrorMessage = LOCTEXT("SaveRundownJson_InvalidRundown", "Invalid rundown.");
			return false;
		}

		// Remark: this is hardcoded to encode in utf16-le.
		FJsonStructSerializerBackend Backend(InArchive, EStructSerializerBackendFlags::Default);
		FStructSerializer::Serialize(InRundown, *InRundown->GetClass(), Backend, Private::FRundownSerializerPolicies());
		return true;
	}

	bool SaveRundownToJson(const UAvaRundown* InRundown, const TCHAR* InFilepath, FText& OutErrorMessage)
	{
		const TUniquePtr<FArchive> FileWriter(IFileManager::Get().CreateFileWriter(InFilepath));
		if (FileWriter)
		{
			const bool bSerialized = SaveRundownToJson(InRundown, *FileWriter, OutErrorMessage);
			FileWriter->Close();
			return bSerialized;
		}

		OutErrorMessage = LOCTEXT("SaveRundownJson_FailedFileWriting", "Failed to open file for writing.");
		return false;
	}
	
	bool LoadRundownFromJson(UAvaRundown* InRundown, FArchive& InArchive, FText& OutErrorMessage)
	{
		if (!IsValid(InRundown))
		{
			OutErrorMessage = LOCTEXT("LoadRundownJson_InvalidRundown", "Invalid rundown.");
			return false;
		}
			
		// Serializing doesn't reset content, it will add to it.
		// so we need to explicitly make the rundown empty first.
		if (!InRundown->Empty())
		{
			// One reason this could fail is if the rundown is currently playing.
			if (InRundown->IsPlaying())
			{
				OutErrorMessage = LOCTEXT("LoadRundownJson_RundownIsPlaying", "Cannot import on a playing rundown. Stop rundown playback first.");
			}
			else
			{
				OutErrorMessage = LOCTEXT("LoadRundownJson_FailedClearRundown", "Failed to clear rundown content.");
			}
			return false;
		}
		
		FJsonStructDeserializerBackend Backend(InArchive);
		const bool bLoaded = FStructDeserializer::Deserialize(InRundown, *InRundown->GetClass(), Backend, Private::FRundownDeserializerPolicies());
		if (bLoaded)
		{
			InRundown->PostLoad();
			InRundown->MarkPackageDirty();
		}
		else
		{
			OutErrorMessage =
				FText::Format(LOCTEXT("LoadRundownJson_DeserializerError", "Json Deserializer error: {0}"),
					FText::FromString(Backend.GetLastErrorMessage()));
		}
		return bLoaded;
	}

	bool LoadRundownFromJson(UAvaRundown* InRundown, const TCHAR* InFilepath, FText& OutErrorMessage)
	{
		const TUniquePtr<FArchive> FileReader(IFileManager::Get().CreateFileReader(InFilepath));
		if (FileReader)
		{
			const bool bLoaded = LoadRundownFromJson(InRundown, *FileReader, OutErrorMessage);
			FileReader->Close();
			return bLoaded;
		}

		OutErrorMessage = LOCTEXT("FileNotFound", "File not found");
		return false;
	}
}

#undef LOCTEXT_NAMESPACE
