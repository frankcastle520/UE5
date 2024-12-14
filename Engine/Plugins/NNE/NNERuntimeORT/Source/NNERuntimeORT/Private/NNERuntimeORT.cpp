// Copyright Epic Games, Inc. All Rights Reserved.

#include "NNERuntimeORT.h"

#include "Misc/SecureHash.h"
#include "NNE.h"
#include "NNEAttributeMap.h"
#include "NNEModelData.h"
#include "NNEModelOptimizerInterface.h"
#include "NNERuntimeORTModel.h"
#include "NNERuntimeORTModelFormat.h"
#include "NNERuntimeORTUtils.h"

#if PLATFORM_WINDOWS
#include "ID3D12DynamicRHI.h"
#endif // PLATFORM_WINDOWS

#include UE_INLINE_GENERATED_CPP_BY_NAME(NNERuntimeORT)

DEFINE_LOG_CATEGORY(LogNNERuntimeORT);

FGuid UNNERuntimeORTCpu::GUID = FGuid((int32)'O', (int32)'C', (int32)'P', (int32)'U');
int32 UNNERuntimeORTCpu::Version = 0x00000004;

FGuid UNNERuntimeORTDml::GUID = FGuid((int32)'O', (int32)'D', (int32)'M', (int32)'L');
int32 UNNERuntimeORTDml::Version = 0x00000004;

namespace UE::NNERuntimeORT::Private::Details
{ 
	//Should be kept in sync with OnnxFileLoaderHelper::InitUNNEModelDataFromFile()
	static FString OnnxExternalDataDescriptorKey(TEXT("OnnxExternalDataDescriptor"));
	static FString OnnxExternalDataBytesKey(TEXT("OnnxExternalDataBytes"));

	FOnnxDataDescriptor MakeOnnxDataDescriptor(TConstArrayView64<uint8> FileData, const TMap<FString, TConstArrayView64<uint8>>& AdditionalFileData)
	{
		FOnnxDataDescriptor OnnxDataDescriptor = {};
		OnnxDataDescriptor.OnnxModelDataSize = FileData.Num();

		if (AdditionalFileData.Contains(OnnxExternalDataDescriptorKey))
		{
			TConstArrayView<uint8> OnnxExternalDataDescriptorBuffer = AdditionalFileData[OnnxExternalDataDescriptorKey];
			FMemoryReaderView OnnxExternalDataDescriptorReader(OnnxExternalDataDescriptorBuffer, /*bIsPersistent = */true);
			TMap<FString, int64> ExternalDataSizes;

			OnnxExternalDataDescriptorReader << ExternalDataSizes;

			int64 CurrentBucketOffset = OnnxDataDescriptor.OnnxModelDataSize;
			for (const auto& Element : ExternalDataSizes)
			{
				const FString DataFilePath = Element.Key;
				FOnnxAdditionalDataDescriptor DataDescriptor;
				DataDescriptor.Path = DataFilePath;
				DataDescriptor.Offset = CurrentBucketOffset;
				DataDescriptor.Size = Element.Value;

				OnnxDataDescriptor.AdditionalDataDescriptors.Emplace(DataDescriptor);
				CurrentBucketOffset += Element.Value;
			}
		}
		return OnnxDataDescriptor;
	}

	void WriteOnnxModelData(FMemoryWriter64 Writer, TConstArrayView64<uint8> FileData, const TMap<FString, TConstArrayView64<uint8>>& AdditionalFileData)
	{
		FOnnxDataDescriptor Descriptor = MakeOnnxDataDescriptor(FileData, AdditionalFileData);
		check(FileData.Num() == Descriptor.OnnxModelDataSize);
		Writer << Descriptor;

		Writer.Serialize(const_cast<uint8*>(FileData.GetData()), FileData.Num());

		if (!Descriptor.AdditionalDataDescriptors.IsEmpty())
		{
			

			check(AdditionalFileData.Contains(OnnxExternalDataBytesKey));
			Writer.Serialize(const_cast<uint8*>(AdditionalFileData[OnnxExternalDataBytesKey].GetData()), AdditionalFileData[OnnxExternalDataBytesKey].Num());
		}
	}
} // namespace UE::NNERuntimeORT::Private::Details

UNNERuntimeORTCpu::ECanCreateModelDataStatus UNNERuntimeORTCpu::CanCreateModelData(const FString& FileType, TConstArrayView64<uint8> FileData, const TMap<FString, TConstArrayView64<uint8>>& AdditionalFileData, const FGuid& FileId, const ITargetPlatform* TargetPlatform) const
{
	return (!FileData.IsEmpty() && FileType.Compare("onnx", ESearchCase::IgnoreCase) == 0) ? ECanCreateModelDataStatus::Ok : ECanCreateModelDataStatus::FailFileIdNotSupported;
}

TSharedPtr<UE::NNE::FSharedModelData> UNNERuntimeORTCpu::CreateModelData(const FString& FileType, TConstArrayView64<uint8> FileData, const TMap<FString, TConstArrayView64<uint8>>& AdditionalFileData, const FGuid& FileId, const ITargetPlatform* TargetPlatform)
{
	using namespace UE::NNERuntimeORT::Private;

	if (CanCreateModelData(FileType, FileData, AdditionalFileData, FileId, TargetPlatform) != ECanCreateModelDataStatus::Ok)
	{
		UE_LOG(LogNNERuntimeORT, Error, TEXT("Cannot create the CPU model data with id %s (Filetype: %s)"), *FileId.ToString(EGuidFormats::Digits).ToLower(), *FileType);
		return {};
	}

	TConstArrayView64<uint8> OptimizedModelView = FileData;
	TArray64<uint8> OptimizedModelBuffer;

	//For now only optimize model if there is no external data (as additional data are serialized from the unoptimized model below)
	if (AdditionalFileData.IsEmpty())
	{
		
		if (GraphOptimizationLevel OptimizationLevel = GetGraphOptimizationLevelForCPU(false, IsRunningCookCommandlet()); OptimizationLevel > GraphOptimizationLevel::ORT_DISABLE_ALL)
		{
			TUniquePtr<Ort::SessionOptions> SessionOptions = CreateSessionOptionsDefault(Environment.ToSharedRef());
			SessionOptions->SetGraphOptimizationLevel(OptimizationLevel);
			SessionOptions->EnableCpuMemArena();

			if (!OptimizeModel(Environment.ToSharedRef(), *SessionOptions, FileData, OptimizedModelBuffer))
			{
				UE_LOG(LogNNERuntimeORT, Error, TEXT("Failed to optimize model for CPU with id %s, model data will not be available"), *FileId.ToString(EGuidFormats::Digits).ToLower());
				return {};
			}

			OptimizedModelView = OptimizedModelBuffer;
		}
	}

	TArray64<uint8> Result;
	FMemoryWriter64 Writer(Result, /*bIsPersitent =*/ true);
	Writer << UNNERuntimeORTCpu::GUID;
	Writer << UNNERuntimeORTCpu::Version;

	Details::WriteOnnxModelData(Writer, OptimizedModelView, AdditionalFileData);

	return MakeShared<UE::NNE::FSharedModelData>(MakeSharedBufferFromArray(MoveTemp(Result)), 0);
}

FString UNNERuntimeORTCpu::GetModelDataIdentifier(const FString& FileType, TConstArrayView64<uint8> FileData, const TMap<FString, TConstArrayView64<uint8>>& AdditionalFileData, const FGuid& FileId, const ITargetPlatform* TargetPlatform) const
{
	return FileId.ToString(EGuidFormats::Digits) + "-" + UNNERuntimeORTCpu::GUID.ToString(EGuidFormats::Digits) + "-" + FString::FromInt(UNNERuntimeORTCpu::Version);
}

void UNNERuntimeORTCpu::Init(TSharedRef<UE::NNERuntimeORT::Private::FEnvironment> InEnvironment)
{
	Environment = InEnvironment;
}

FString UNNERuntimeORTCpu::GetRuntimeName() const
{
	return TEXT("NNERuntimeORTCpu");
}

UNNERuntimeORTCpu::ECanCreateModelCPUStatus UNNERuntimeORTCpu::CanCreateModelCPU(const TObjectPtr<UNNEModelData> ModelData) const
{
	check(ModelData != nullptr);

	constexpr int32 GuidSize = sizeof(UNNERuntimeORTCpu::GUID);
	constexpr int32 VersionSize = sizeof(UNNERuntimeORTCpu::Version);
	const TSharedPtr<UE::NNE::FSharedModelData> SharedData = ModelData->GetModelData(GetRuntimeName());

	if (!SharedData.IsValid())
	{
		return ECanCreateModelCPUStatus::Fail;
	}

	TConstArrayView64<uint8> Data = SharedData->GetView();

	if (Data.Num() <= GuidSize + VersionSize)
	{
		return ECanCreateModelCPUStatus::Fail;
	}

	bool bResult = FGenericPlatformMemory::Memcmp(&(Data[0]), &(UNNERuntimeORTCpu::GUID), GuidSize) == 0;
	bResult &= FGenericPlatformMemory::Memcmp(&(Data[GuidSize]), &(UNNERuntimeORTCpu::Version), VersionSize) == 0;

	return bResult ? ECanCreateModelCPUStatus::Ok : ECanCreateModelCPUStatus::Fail;
}

TSharedPtr<UE::NNE::IModelCPU> UNNERuntimeORTCpu::CreateModelCPU(const TObjectPtr<UNNEModelData> ModelData)
{
	check(ModelData != nullptr);

	if (CanCreateModelCPU(ModelData) != ECanCreateModelCPUStatus::Ok)
	{
		UE_LOG(LogNNERuntimeORT, Error, TEXT("Cannot create a CPU model from the model data with id %s"), *ModelData->GetFileId().ToString(EGuidFormats::Digits));
		return TSharedPtr<UE::NNE::IModelCPU>();
	}

	const TSharedRef<UE::NNE::FSharedModelData> SharedData = ModelData->GetModelData(GetRuntimeName()).ToSharedRef();

	return MakeShared<UE::NNERuntimeORT::Private::FModelORTCpu>(Environment.ToSharedRef(), SharedData);
}

/*
 * UNNERuntimeORTDml
 */
void UNNERuntimeORTDml::Init(TSharedRef<UE::NNERuntimeORT::Private::FEnvironment> InEnvironment, bool bInDirectMLAvailable)
{
	Environment = InEnvironment;
	bDirectMLAvailable = bInDirectMLAvailable;
	bD3D12Available = UE::NNERuntimeORT::Private::IsD3D12Available();
	bD3D12DeviceNPUAvailable = UE::NNERuntimeORT::Private::IsD3D12DeviceNPUAvailable();
}

FString UNNERuntimeORTDml::GetRuntimeName() const
{
	return TEXT("NNERuntimeORTDml");
}

UNNERuntimeORTDml::ECanCreateModelDataStatus UNNERuntimeORTDml::CanCreateModelData(const FString& FileType, TConstArrayView64<uint8> FileData, const TMap<FString, TConstArrayView64<uint8>>& AdditionalFileData, const FGuid& FileId, const ITargetPlatform* TargetPlatform) const
{
	return (!FileData.IsEmpty() && FileType.Compare("onnx", ESearchCase::IgnoreCase) == 0) ? ECanCreateModelDataStatus::Ok : ECanCreateModelDataStatus::FailFileIdNotSupported;
}

TSharedPtr<UE::NNE::FSharedModelData> UNNERuntimeORTDml::CreateModelData(const FString& FileType, TConstArrayView64<uint8> FileData, const TMap<FString, TConstArrayView64<uint8>>& AdditionalFileData, const FGuid& FileId, const ITargetPlatform* TargetPlatform)
{
	using namespace UE::NNERuntimeORT::Private;

	if (CanCreateModelData(FileType, FileData, AdditionalFileData, FileId, TargetPlatform) != ECanCreateModelDataStatus::Ok)
	{
		UE_LOG(LogNNERuntimeORT, Error, TEXT("Cannot create the Dml model data with id %s (Filetype: %s)"), *FileId.ToString(EGuidFormats::Digits).ToLower(), *FileType);
		return {};
	}

	TConstArrayView64<uint8> OptimizedModelView = FileData;
	TArray64<uint8> OptimizedModelBuffer;

	//For now only optimize model if there is no external data (as additional data are serialized from the unoptimized model below)
	if (AdditionalFileData.IsEmpty())
	{
		if (GraphOptimizationLevel OptimizationLevel = GetGraphOptimizationLevelForDML(false, IsRunningCookCommandlet()); OptimizationLevel > GraphOptimizationLevel::ORT_DISABLE_ALL)
		{
			TUniquePtr<Ort::SessionOptions> SessionOptions = CreateSessionOptionsDefault(Environment.ToSharedRef());
			SessionOptions->SetGraphOptimizationLevel(OptimizationLevel);
			SessionOptions->SetExecutionMode(ExecutionMode::ORT_SEQUENTIAL);
			SessionOptions->DisableMemPattern();

			if (!OptimizeModel(Environment.ToSharedRef(), *SessionOptions, FileData, OptimizedModelBuffer))
			{
				UE_LOG(LogNNERuntimeORT, Error, TEXT("Failed to optimize model for DirectML with id %s, model data will not be available"), *FileId.ToString(EGuidFormats::Digits).ToLower());

				return {};
			}

			OptimizedModelView = OptimizedModelBuffer;
		}
	}

	TArray64<uint8> Result;
	FMemoryWriter64 Writer(Result, /*bIsPersitent =*/ true);
	Writer << UNNERuntimeORTDml::GUID;
	Writer << UNNERuntimeORTDml::Version;

	Details::WriteOnnxModelData(Writer, OptimizedModelView, AdditionalFileData);

	return MakeShared<UE::NNE::FSharedModelData>(MakeSharedBufferFromArray(MoveTemp(Result)), 0);
}

FString UNNERuntimeORTDml::GetModelDataIdentifier(const FString& FileType, TConstArrayView64<uint8> FileData, const TMap<FString, TConstArrayView64<uint8>>& AdditionalFileData, const FGuid& FileId, const ITargetPlatform* TargetPlatform) const
{
	return FileId.ToString(EGuidFormats::Digits) + "-" + UNNERuntimeORTDml::GUID.ToString(EGuidFormats::Digits) + "-" + FString::FromInt(UNNERuntimeORTDml::Version);
}

UNNERuntimeORTDml::ECanCreateModelGPUStatus UNNERuntimeORTDml::CanCreateModelGPU(const TObjectPtr<UNNEModelData> ModelData) const
{
	if (!bDirectMLAvailable)
	{
		return ECanCreateModelCommonStatus::Fail;
	}

	if (!bD3D12Available)
	{
		return ECanCreateModelCommonStatus::Fail;
	}

	return CanCreateModelCommon(ModelData, false) == ECanCreateModelCommonStatus::Ok ? ECanCreateModelGPUStatus::Ok : ECanCreateModelGPUStatus::Fail;
}

TSharedPtr<UE::NNE::IModelGPU> UNNERuntimeORTDml::CreateModelGPU(const TObjectPtr<UNNEModelData> ModelData)
{
#if PLATFORM_WINDOWS
	check(ModelData);

	if (CanCreateModelGPU(ModelData) != ECanCreateModelGPUStatus::Ok)
	{
		UE_LOG(LogNNERuntimeORT, Error, TEXT("Cannot create a GPU model from the model data with id %s"), *ModelData->GetFileId().ToString(EGuidFormats::Digits));
		return {};
	}

	const TSharedRef<UE::NNE::FSharedModelData> SharedData = ModelData->GetModelData(GetRuntimeName()).ToSharedRef();

	return MakeShared<UE::NNERuntimeORT::Private::FModelORTDmlGPU>(Environment.ToSharedRef(), SharedData);
#else // PLATFORM_WINDOWS
	return TSharedPtr<UE::NNE::IModelGPU>();
#endif // PLATFORM_WINDOWS
}

UNNERuntimeORTDml::ECanCreateModelRDGStatus UNNERuntimeORTDml::CanCreateModelRDG(TObjectPtr<UNNEModelData> ModelData) const
{
	if (!bDirectMLAvailable)
	{
		return ECanCreateModelCommonStatus::Fail;
	}

#if PLATFORM_WINDOWS
	if (!IsRHID3D12())
	{
		return ECanCreateModelCommonStatus::Fail;
	}
#endif // PLATFORM_WINDOWS

	return CanCreateModelCommon(ModelData) == ECanCreateModelCommonStatus::Ok ? ECanCreateModelRDGStatus::Ok : ECanCreateModelRDGStatus::Fail;
}

TSharedPtr<UE::NNE::IModelRDG> UNNERuntimeORTDml::CreateModelRDG(TObjectPtr<UNNEModelData> ModelData)
{
#if PLATFORM_WINDOWS
	check(ModelData);

	if (CanCreateModelRDG(ModelData) != ECanCreateModelRDGStatus::Ok)
	{
		UE_LOG(LogNNERuntimeORT, Error, TEXT("Cannot create a RDG model from the model data with id %s"), *ModelData->GetFileId().ToString(EGuidFormats::Digits));
		return {};
	}

	const TSharedRef<UE::NNE::FSharedModelData> SharedData = ModelData->GetModelData(GetRuntimeName()).ToSharedRef();

	return MakeShared<UE::NNERuntimeORT::Private::FModelORTDmlRDG>(Environment.ToSharedRef(), SharedData);
#else // PLATFORM_WINDOWS
	return {};
#endif // PLATFORM_WINDOWS
}

UNNERuntimeORTDml::ECanCreateModelNPUStatus UNNERuntimeORTDml::CanCreateModelNPU(const TObjectPtr<UNNEModelData> ModelData) const
{
	if (!bDirectMLAvailable)
	{
		return ECanCreateModelCommonStatus::Fail;
	}

	if (!bD3D12DeviceNPUAvailable)
	{
		return ECanCreateModelCommonStatus::Fail;
	}

	return CanCreateModelCommon(ModelData) == ECanCreateModelCommonStatus::Ok ? ECanCreateModelRDGStatus::Ok : ECanCreateModelRDGStatus::Fail;
}

TSharedPtr<UE::NNE::IModelNPU> UNNERuntimeORTDml::CreateModelNPU(const TObjectPtr<UNNEModelData> ModelData)
{
#if PLATFORM_WINDOWS
	check(ModelData);

	if (CanCreateModelNPU(ModelData) != ECanCreateModelRDGStatus::Ok)
	{
		UE_LOG(LogNNERuntimeORT, Error, TEXT("Cannot create a model NPU from the model data with id %s"), *ModelData->GetFileId().ToString(EGuidFormats::Digits));
		return {};
	}

	const TSharedRef<UE::NNE::FSharedModelData> SharedData = ModelData->GetModelData(GetRuntimeName()).ToSharedRef();

	return MakeShared<UE::NNERuntimeORT::Private::FModelORTNpu>(Environment.ToSharedRef(), SharedData);
#else // PLATFORM_WINDOWS
	return {};
#endif // PLATFORM_WINDOWS
}

UNNERuntimeORTDml::ECanCreateModelCommonStatus UNNERuntimeORTDml::CanCreateModelCommon(const TObjectPtr<UNNEModelData> ModelData, bool bRHID3D12Required) const
{
#if PLATFORM_WINDOWS
	check(ModelData != nullptr);

	constexpr int32 GuidSize = sizeof(UNNERuntimeORTDml::GUID);
	constexpr int32 VersionSize = sizeof(UNNERuntimeORTDml::Version);
	const TSharedPtr<UE::NNE::FSharedModelData> SharedData = ModelData->GetModelData(GetRuntimeName());

	if (!SharedData.IsValid())
	{
		return ECanCreateModelCommonStatus::Fail;
	}

	TConstArrayView64<uint8> Data = SharedData->GetView();

	if (Data.Num() <= GuidSize + VersionSize)
	{
		return ECanCreateModelCommonStatus::Fail;
	}

	static const FGuid DeprecatedGUID = FGuid((int32)'O', (int32)'G', (int32)'P', (int32)'U');

	bool bResult = FGenericPlatformMemory::Memcmp(&(Data[0]), &(UNNERuntimeORTDml::GUID), GuidSize) == 0;
	bResult |= FGenericPlatformMemory::Memcmp(&(Data[0]), &(DeprecatedGUID), GuidSize) == 0;
	bResult &= FGenericPlatformMemory::Memcmp(&(Data[GuidSize]), &(UNNERuntimeORTDml::Version), VersionSize) == 0;

	return bResult ? ECanCreateModelCommonStatus::Ok : ECanCreateModelCommonStatus::Fail;
#else // PLATFORM_WINDOWS
	return ECanCreateModelCommonStatus::Fail;
#endif // PLATFORM_WINDOWS
}