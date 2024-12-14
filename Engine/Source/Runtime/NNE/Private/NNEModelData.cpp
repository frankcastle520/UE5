// Copyright Epic Games, Inc. All Rights Reserved.

#include "NNEModelData.h"

#include "NNE.h"
#include "NNEAttributeMap.h"
#include "NNEModelOptimizerInterface.h"
#include "NNERuntimeFormat.h"
#include "Serialization/CustomVersion.h"
#include "UObject/AssetRegistryTagsContext.h"
#include "UObject/WeakInterfacePtr.h"

namespace UE::NNE::ModelData
{
	enum Version : uint32
	{
		V0 = 0, // Initial
		V1 = 1, // TargetRuntimes and AssetImportData
		V2 = 2, // Re-arrange fields and store only ModelData in cooked assets
		V3 = 3, // Adding AdditionalFileData
		V4 = 4, // Support for > 2GB models
		// New versions can be added above this line
		VersionPlusOne,
		Latest = VersionPlusOne - 1
	};

	const FGuid GUID(0x9513202e, 0xeba1b279, 0xf17fe5ba, 0xab90c3f2);
	FCustomVersionRegistration NNEModelDataVersion(GUID, Version::Latest, TEXT("NNEModelDataVersion"));// Always save with the latest version

	FString GetRuntimesAsString(TArrayView<const FString> Runtimes)
	{
		if (Runtimes.Num() == 0)
		{
			return TEXT("All");
		}

		FString RuntimesAsOneString;
		bool bIsFirstRuntime = true;

		for (const FString& Runtime : Runtimes)
		{
			if (!bIsFirstRuntime)
			{
				RuntimesAsOneString += TEXT(", ");
			}
			RuntimesAsOneString += Runtime;
			bIsFirstRuntime = false;
		}
		return RuntimesAsOneString;
	}

	inline TSharedPtr<UE::NNE::FSharedModelData> CreateModelData(const FString& RuntimeName, const FString& FileType, const TArray64<uint8>& FileData, const TMap<FString, TConstArrayView64<uint8>>& AdditionalFileData, const FGuid& FileId, const ITargetPlatform* TargetPlatform)
	{
		TWeakInterfacePtr<INNERuntime> NNERuntime = UE::NNE::GetRuntime<INNERuntime>(RuntimeName);
		if (NNERuntime.IsValid())
		{
			INNERuntime::ECanCreateModelDataStatus CanCreateModelDataStatus = NNERuntime->CanCreateModelData(FileType, FileData, AdditionalFileData, FileId, TargetPlatform);
			if (CanCreateModelDataStatus == INNERuntime::ECanCreateModelDataStatus::Ok)
			{
				return NNERuntime->CreateModelData(FileType, FileData, AdditionalFileData, FileId, TargetPlatform);
			}
			else if (CanCreateModelDataStatus == INNERuntime::ECanCreateModelDataStatus::FailFileIdNotSupported)
			{
				UE_LOG(LogNNE, Display, TEXT("Runtime %s does not support Filetype: %s, skipping the model data creation for model with id %s "), *RuntimeName, *FileType, *FileId.ToString(EGuidFormats::Digits).ToLower());
			}
			else
			{
				UE_LOG(LogNNE, Warning, TEXT("Runtime %s cannot create the model data with id %s (Filetype: %s)"), *RuntimeName, *FileId.ToString(EGuidFormats::Digits).ToLower(), *FileType);
			}
		}
		else
		{
			UE_LOG(LogNNE, Error, TEXT("No runtime '%s' found. Valid runtimes are: "), *RuntimeName);
			TArray<FString> Runtimes = UE::NNE::GetAllRuntimeNames();
			for (int32 i = 0; i < Runtimes.Num(); i++)
			{
				UE_LOG(LogNNE, Error, TEXT("- %s"), *Runtimes[i]);
			}
		}
		return TSharedPtr<UE::NNE::FSharedModelData>();
	}

} // UE::NNE::ModelData

namespace UE::NNE
{
	FSharedModelData::FSharedModelData(FSharedBuffer InData, uint32 InMemoryAlignment) : Data(InData), MemoryAlignment(InMemoryAlignment)
	{
		checkf(Data.IsOwned(), TEXT("InData data must be ownned!"));
		checkf(MemoryAlignment <= 1 || (((uintptr_t)(const void*)(InData.GetData())) % MemoryAlignment == 0), TEXT("InData must be aligned with InMemoryAlignment!"))
	}

	FSharedModelData::FSharedModelData()
	{
		MemoryAlignment = 0;
	}

	TConstArrayView64<uint8> FSharedModelData::GetView() const
	{
		return TConstArrayView64<uint8>(static_cast<const uint8*>(Data.GetData()), Data.GetSize());
	}

	uint32 FSharedModelData::GetMemoryAlignment() const
	{
		return MemoryAlignment;
	}
} // UE::NNE

void UNNEModelData::GetAssetRegistryTags(FAssetRegistryTagsContext Context) const
{
	Context.AddTag(FAssetRegistryTag("TargetRuntimes", UE::NNE::ModelData::GetRuntimesAsString(GetTargetRuntimes()), FAssetRegistryTag::TT_Alphabetical));
	Super::GetAssetRegistryTags(Context);
}

void UNNEModelData::Serialize(FArchive& Ar)
{
	// Store the asset version (no effect in load)
	Ar.UsingCustomVersion(UE::NNE::ModelData::GUID);

	if (Ar.IsSaving() || Ar.IsCountingMemory())	{
		bool bWriteModelData = true;
		if (Ar.IsCooking())
		{
			// Optimize storage: FileData and AdditionalFileData are not required anymore because we have the model and can cook it for every runtime
			TArray<FString> TmpTargetRuntimes;
			Ar << TmpTargetRuntimes;
			FString TmpFileType;
			Ar << TmpFileType;
			TArray64<uint8> TmpFileData;
			Ar << TmpFileData;
			int32 NumAdditionalFileDataItems = 0;
			Ar << NumAdditionalFileDataItems;
			FGuid TmpGuid;
			Ar << TmpGuid;

			// Cooking must recreate all model data but only if file data is still available
			if (FileData.Num() > 0)
			{
				ModelData.Reset();

				// No target runtime means all currently registered ones
				TArray<FString, TInlineAllocator<10>> CookRuntimeNames;
				if (GetTargetRuntimes().IsEmpty())
				{
					CookRuntimeNames.Append(UE::NNE::GetAllRuntimeNames());
				}
				else
				{
					CookRuntimeNames.Append(GetTargetRuntimes());
				}

				TMap<FString, TConstArrayView64<uint8>> AdditionalFileDataView;
				TSet<FString> Keys;
				AdditionalFileData.GetKeys(Keys);
				for (auto& Key : Keys)
				{
					AdditionalFileDataView.Emplace(Key, AdditionalFileData[Key]);
				}

				for (const FString& RuntimeName : CookRuntimeNames)
				{
					TSharedPtr<UE::NNE::FSharedModelData> SharedModelData = UE::NNE::ModelData::CreateModelData(RuntimeName, FileType, FileData, AdditionalFileDataView, FileId, Ar.GetArchiveState().CookingTarget());
					if (SharedModelData.IsValid() && SharedModelData->GetView().Num() > 0)
					{
						ModelData.Add(RuntimeName, SharedModelData);
					}
				}
			}
		}
		else
		{
			// Only cooked assets optimize storage
			Ar << TargetRuntimes;
			Ar << FileType;
			Ar << FileData;

			int32 NumAdditionalFileDataItems = AdditionalFileData.Num();
			Ar << NumAdditionalFileDataItems;
			TSet<FString> Keys;
			AdditionalFileData.GetKeys(Keys);
			for (auto& Key : Keys)
			{
				Ar << Key;
				Ar << AdditionalFileData[Key];
			}

			Ar << FileId;

#if WITH_EDITOR
			// In editor (when not cooking), no model data is stored as model data can always be recreated and unnecessary data in subversion control should be avoided
			bWriteModelData = false;
#endif //WITH_EDITOR
		}

		if (bWriteModelData)
		{
			TArray<FString> RuntimeNames;
			ModelData.GetKeys(RuntimeNames);
			int32 NumItems = RuntimeNames.Num();

			Ar << NumItems;
			for (int32 i = 0; i < NumItems; i++)
			{
				Ar << RuntimeNames[i];

				uint32 MemoryAlignment = ModelData[RuntimeNames[i]]->GetMemoryAlignment();
				Ar << MemoryAlignment;

				uint64 DataSize = ModelData[RuntimeNames[i]]->GetView().Num();
				Ar << DataSize;

				Ar.Serialize((void*)ModelData[RuntimeNames[i]]->GetView().GetData(), DataSize);
			}
		}
		else
		{
			int32 NumItems = 0;
			Ar << NumItems;
		}
	}
	else if (Ar.IsLoading())
	{
		TObjectPtr<class UAssetImportData> AssetImportData;
		int32 NumItems = 0;
		int32 NumAdditionalFileDataItems = 0;
		FString Name;
		uint32 MemoryAlignment = 0;
		uint64 DataSize = 0;
		TArray64<uint8> Data;
		TArray<uint8> Data_Size32Bits;
		void* RawData = nullptr;
		int32 Index = 0;

		switch (Ar.CustomVer(UE::NNE::ModelData::GUID))
		{
		case UE::NNE::ModelData::Version::V0:
			TargetRuntimes.Empty();
			Ar << FileType;
			Ar << Data_Size32Bits;
			FileData = MoveTemp(Data_Size32Bits);
			Ar << FileId;
			Ar << NumItems;
			for (Index = 0; Index < NumItems; Index++)
			{
				Ar << Name;
				Ar << Data_Size32Bits;
				ModelData.Add(Name, MakeShared<UE::NNE::FSharedModelData>(MakeSharedBufferFromArray(MoveTemp(Data_Size32Bits)), 0));
			}
			UE_LOG(LogNNE, Warning, TEXT("[DEPRECATION] The asset %s (v0) is deprecated. Please right-click the asset and select 'Save' to update it to the latest version."), *this->GetName());
			break;

		case UE::NNE::ModelData::Version::V1:
			TargetRuntimes.Empty();
			if (!Ar.IsLoadingFromCookedPackage())
			{
				Ar << TargetRuntimes;
				Ar << AssetImportData;
			}
			Ar << FileType;
			Ar << Data_Size32Bits;
			FileData = MoveTemp(Data_Size32Bits);
			Ar << FileId;
			Ar << NumItems;
			for (Index = 0; Index < NumItems; Index++)
			{
				Ar << Name;
				Ar << Data_Size32Bits;
				ModelData.Add(Name, MakeShared<UE::NNE::FSharedModelData>(MakeSharedBufferFromArray(MoveTemp(Data_Size32Bits)), 0));
			}
			UE_LOG(LogNNE, Warning, TEXT("[DEPRECATION] The asset %s (v1) is deprecated. Please right-click the asset and select 'Save' to update it to the latest version."), *this->GetName());
			break;

		case UE::NNE::ModelData::Version::V2:
			Ar << TargetRuntimes;
			Ar << FileType;
			Ar << Data_Size32Bits;
			FileData = MoveTemp(Data_Size32Bits);
			Ar << FileId;
			Ar << NumItems;
			for (Index = 0; Index < NumItems; Index++)
			{
				Ar << Name;
				Ar << MemoryAlignment;
				Ar << DataSize;
				RawData = FMemory::Malloc(DataSize, MemoryAlignment);
				Ar.Serialize(RawData, DataSize);
				ModelData.Add(Name, MakeShared<UE::NNE::FSharedModelData>(FSharedBuffer::TakeOwnership(RawData, DataSize, FMemory::Free), MemoryAlignment));
			}
			break;

		case UE::NNE::ModelData::Version::V3:
			Ar << TargetRuntimes;
			Ar << FileType;
			Ar << Data_Size32Bits;
			FileData = MoveTemp(Data_Size32Bits);
			Ar << NumAdditionalFileDataItems;
			AdditionalFileData.Empty();
			for (Index = 0; Index < NumAdditionalFileDataItems; Index++)
			{
				Ar << Name;
				Ar << Data_Size32Bits;
				Data = MoveTemp(Data_Size32Bits);
				AdditionalFileData.Add(Name, Data);
			}
			Ar << FileId;
			Ar << NumItems;
			for (Index = 0; Index < NumItems; Index++)
			{
				Ar << Name;
				Ar << MemoryAlignment;
				Ar << DataSize;
				RawData = FMemory::Malloc(DataSize, MemoryAlignment);
				Ar.Serialize(RawData, DataSize);
				ModelData.Add(Name, MakeShared<UE::NNE::FSharedModelData>(FSharedBuffer::TakeOwnership(RawData, DataSize, FMemory::Free), MemoryAlignment));
			}
			break;

		case UE::NNE::ModelData::Version::V4:
			Ar << TargetRuntimes;
			Ar << FileType;
			Ar << FileData;
			Ar << NumAdditionalFileDataItems;
			AdditionalFileData.Empty();
			for (Index = 0; Index < NumAdditionalFileDataItems; Index++)
			{
				Ar << Name;
				Ar << Data;
				AdditionalFileData.Add(Name, Data);
			}
			Ar << FileId;
			Ar << NumItems;
			for (Index = 0; Index < NumItems; Index++)
			{
				Ar << Name;
				Ar << MemoryAlignment;
				Ar << DataSize;
				RawData = FMemory::Malloc(DataSize, MemoryAlignment);
				Ar.Serialize(RawData, DataSize);
				ModelData.Add(Name, MakeShared<UE::NNE::FSharedModelData>(FSharedBuffer::TakeOwnership(RawData, DataSize, FMemory::Free), MemoryAlignment));
			}
			break;

		default:
			UE_LOG(LogNNE, Error, TEXT("Unknown asset version %d: Deserialisation failed, please reimport the original model."), Ar.CustomVer(UE::NNE::ModelData::GUID));
			break;
		}
	}
}

void UNNEModelData::Init(const FString& Type, TConstArrayView64<uint8> Buffer, const TMap<FString, TConstArrayView64<uint8>>& AdditionalBuffers)
{
	TargetRuntimes.Empty();
	FileType = Type;
	FileData = Buffer;
	AdditionalFileData.Empty();
	for (auto& Element : AdditionalBuffers)
	{
		AdditionalFileData.Emplace(Element.Key, AdditionalBuffers[Element.Key]);
	}
	FPlatformMisc::CreateGuid(FileId);
	ModelData.Empty();
}

TArrayView<const FString> UNNEModelData::GetTargetRuntimes() const 
{ 
	return TargetRuntimes;
}

void UNNEModelData::SetTargetRuntimes(TArrayView<const FString> RuntimeNames)
{
	TargetRuntimes = RuntimeNames;

	if (RuntimeNames.Num() > 0)
	{
		TArray<FString, TInlineAllocator<10>> CookedRuntimes;
		ModelData.GetKeys(CookedRuntimes);
		for (const FString& Runtime : CookedRuntimes)
		{
			if (!TargetRuntimes.Contains(Runtime))
			{
				ModelData.Remove(Runtime);
			}
		}
		ModelData.Compact();
	}
}

FString UNNEModelData::GetFileType() const
{
	return FileType;
}

TConstArrayView64<uint8> UNNEModelData::GetFileData() const
{
	return FileData;
}

TConstArrayView64<uint8> UNNEModelData::GetAdditionalFileData(const FString& Key) const
{
	return AdditionalFileData.Contains(Key) ? AdditionalFileData[Key] : TConstArrayView<uint8>();
}

void UNNEModelData::ClearFileDataAndFileType()
{
	FileType = "";
	FileData.Empty();
	AdditionalFileData.Empty();
}

FGuid UNNEModelData::GetFileId() const
{
	return FileId;
}

TSharedPtr<UE::NNE::FSharedModelData> UNNEModelData::GetModelData(const FString& RuntimeName)
{
	// Check model data is supporting the requested target runtime
	TArrayView<const FString> TargetRuntimesNames = GetTargetRuntimes();
	if (!TargetRuntimesNames.IsEmpty() && !TargetRuntimesNames.Contains(RuntimeName))
	{
		UE_LOG(LogNNE, Error, TEXT("Runtime '%s' is not among the target runtimes. Target runtimes are: "), *RuntimeName);
		for (const FString& TargetRuntimesName : TargetRuntimesNames)
		{
			UE_LOG(LogNNE, Error, TEXT("- %s"), *TargetRuntimesName);
		}
		return TSharedPtr<UE::NNE::FSharedModelData>();
	}

	// Check if we have a local cache hit
	TSharedPtr<UE::NNE::FSharedModelData>* LocalDataPtr = ModelData.Find(RuntimeName);
	if (LocalDataPtr)
	{
		return *LocalDataPtr;
	}

	// After this point FileData is required to either get the cache id or recreate it from scratch
	if (FileData.Num() < 1)
	{
		UE_LOG(LogNNE, Error, TEXT("Cannot create model data from empty file data."));
		return TSharedPtr<UE::NNE::FSharedModelData>();
	}

	TMap<FString, TConstArrayView64<uint8>> AdditionalFileDataView;
	TSet<FString> Keys;
	AdditionalFileData.GetKeys(Keys);
	for (auto& Key : Keys)
	{
		AdditionalFileDataView.Emplace(Key, AdditionalFileData[Key]);
	}

	// Try to create the model
	TSharedPtr<UE::NNE::FSharedModelData> CreatedData = UE::NNE::ModelData::CreateModelData(RuntimeName, FileType, FileData, AdditionalFileDataView, FileId, nullptr);
	if (!CreatedData.IsValid() || CreatedData->GetView().Num() < 1)
	{
		return TSharedPtr<UE::NNE::FSharedModelData>();
	}

	// Cache the model
	ModelData.Add(RuntimeName, CreatedData);

	return CreatedData;
}

void UNNEModelData::ClearModelData()
{
	ModelData.Empty();
}