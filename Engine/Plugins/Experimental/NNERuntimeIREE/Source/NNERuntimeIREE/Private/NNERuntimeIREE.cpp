// Copyright Epic Games, Inc. All Rights Reserved.

#include "NNERuntimeIREE.h"

#ifdef WITH_NNE_RUNTIME_IREE

#if WITH_EDITOR
#include "Containers/StringConv.h"
#include "HAL/PlatformFileManager.h"
#include "Memory/SharedBuffer.h"
#include "Misc/FileHelper.h"
#endif // WITH_EDITOR

#include "HAL/Platform.h"
#include "Interfaces/ITargetPlatform.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/App.h"
#include "Misc/Paths.h"
#include "Misc/SecureHash.h"
#include "NNE.h"
#include "NNEModelData.h"
#include "NNERuntimeIREECompiler.h"
#include "NNERuntimeIREELog.h"
#include "NNERuntimeIREEMetaData.h"
#include "NNERuntimeIREEModel.h"
#include "NNERuntimeIREEModelData.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"

namespace UE::NNERuntimeIREE::CPU::Private
{
	FString GetTargetPlatformName(const ITargetPlatform* TargetPlatform)
	{
		return TargetPlatform ? TargetPlatform->IniPlatformName() : UGameplayStatics::GetPlatformName();
	}

	FString GetBinariesSubdirectory(const FString& PlatformName)
	{
		if (PlatformName.Equals("Windows"))
		{
			if (PLATFORM_64BITS)
			{
				return TEXT("Win64");
			}
			return TEXT("Win32");
		}
		else
		{
			return PlatformName;
		}
	}

	FString GetModelDataIdentifier(const FString& RuntimeName, const FGuid& Guid, const FString& FileIdString, const FString& PlatformName, const FString& Architecture)
	{
		return RuntimeName + "-" + Guid.ToString(EGuidFormats::Digits) + "-" + FString::FromInt(UNNERuntimeIREECpu::Version) + "-" + FileIdString + "-" + PlatformName + (!Architecture.IsEmpty() ? ("-" + Architecture) : "");
	}

	FString GetIntermediateModelDirPath(const FString& PlatformName, const FString& ModelName)
	{
		return FPaths::Combine("Intermediate", "Build", GetBinariesSubdirectory(PlatformName), UE_PLUGIN_NAME, ModelName);
	}

	FString GetStagedModelDirPath(const FString& PlatformName)
	{
		return FPaths::Combine("Binaries", GetBinariesSubdirectory(PlatformName), UE_PLUGIN_NAME);
	}

	FString GetPackagedModelDirPath(const FString& PlatformName)
	{
		return GetStagedModelDirPath(PlatformName);
	}

	FString GetSharedLibDirPath(const FString& PlatformName, const FString& ModelName)
	{
#if WITH_EDITOR
	return GetIntermediateModelDirPath(PlatformName, ModelName);
#else
	return GetPackagedModelDirPath(PlatformName);
#endif // WITH_EDITOR
	}
} // UE::NNERuntimeIREE::CPU::Private

FGuid UNNERuntimeIREECpu::GUID = FGuid((int32)'I', (int32)'C', (int32)'P', (int32)'U');
int32 UNNERuntimeIREECpu::Version = 0x00000005;

FString UNNERuntimeIREECpu::GetRuntimeName() const
{
	return TEXT("NNERuntimeIREECpu");
}

UNNERuntimeIREECpu::ECanCreateModelDataStatus UNNERuntimeIREECpu::CanCreateModelData(const FString& FileType, TConstArrayView64<uint8> FileData, const TMap<FString, TConstArrayView64<uint8>>& AdditionalFileData, const FGuid& FileId, const ITargetPlatform* TargetPlatform) const
{
#if WITH_EDITOR
	// Check model is not > 2GB
	if ((TArray<uint8>::SizeType)FileData.Num() != FileData.Num())
	{
		return ECanCreateModelDataStatus::Fail;
	}

	return FileType.Compare(TEXT("mlir"), ESearchCase::IgnoreCase) == 0 ? ECanCreateModelDataStatus::Ok : ECanCreateModelDataStatus::FailFileIdNotSupported;
#else
	return ECanCreateModelDataStatus::Fail;
#endif // WITH_EDITOR
}

TSharedPtr<UE::NNE::FSharedModelData> UNNERuntimeIREECpu::CreateModelData(const FString& FileType, TConstArrayView64<uint8> FileData, const TMap<FString, TConstArrayView64<uint8>>& AdditionalFileData, const FGuid& FileId, const ITargetPlatform* TargetPlatform)
{
	SCOPED_NAMED_EVENT_TEXT("UNNERuntimeIREECpu::CreateModelData", FColor::Magenta);

#if WITH_EDITOR
	using namespace UE::NNERuntimeIREE::CPU::Private;

	const FString TargetPlatformName = GetTargetPlatformName(TargetPlatform);
	if (CanCreateModelData(FileType, FileData, AdditionalFileData, FileId, TargetPlatform) != ECanCreateModelDataStatus::Ok)
	{
		UE_LOG(LogNNERuntimeIREE, Warning, TEXT("UNNERuntimeIREECpu cannot create the model data with id %s (Filetype: %s) for platform %s"), *FileId.ToString(EGuidFormats::Digits).ToLower(), *FileType, *TargetPlatformName);
		return TSharedPtr<UE::NNE::FSharedModelData>();
	}

	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();

	const FString FileIdString = FileId.ToString(EGuidFormats::Digits).ToLower();
	const FString IntermediateDirFullPath = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), GetIntermediateModelDirPath(TargetPlatformName, FileIdString)));
	const FString SharedLibraryDirFullPath = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), GetSharedLibDirPath(TargetPlatformName, FileIdString)));

	FString IREEModelDataFilePath = FPaths::Combine(IntermediateDirFullPath, FileIdString) + ".ireemodeldata";

	TArray64<uint8> ResultData;
	TWeakObjectPtr<UNNERuntimeIREEModelData> IREEModelData = NewObject<UNNERuntimeIREEModelData>();
	FNNERuntimeIREECompilerResultCPU CompilerResult{};

	bool bNeedCompileMlir = true;
	if (PlatformFile.FileExists(*IREEModelDataFilePath))
	{
		SCOPED_NAMED_EVENT_TEXT("Validate", FColor::Magenta);

		FFileHelper::LoadFileToArray(ResultData, *IREEModelDataFilePath);
		
		{
			FMemoryReaderView Reader(ResultData, /*bIsPersitent =*/ true);
			IREEModelData->Serialize(Reader);
		}

		check(FileIdString.Equals(IREEModelData->FileId.ToString(EGuidFormats::Digits).ToLower()));

		{
			FMemoryReaderView Reader(IREEModelData->CompilerResult, /*bIsPersitent =*/ true);
			FNNERuntimeIREECompilerResultCPU::StaticStruct()->SerializeBin(Reader, &CompilerResult);
		}

		bNeedCompileMlir = false;
		for (int32 i = 0; i < CompilerResult.ArchitectureInfos.Num(); i++)
		{
			const FNNERuntimeIREEArchitectureInfoCPU& Info = CompilerResult.ArchitectureInfos[i];
			const FString SharedLibrarySubDirFullPath = FPaths::Combine(SharedLibraryDirFullPath, Info.RelativeDirPath);

			const FString SharedLibraryFilePath = FPaths::Combine(SharedLibrarySubDirFullPath, Info.SharedLibraryFileName);
			const FString VmfbFilePath = FPaths::Combine(SharedLibrarySubDirFullPath, Info.VmfbFileName);

			bNeedCompileMlir |= !PlatformFile.FileExists(*SharedLibraryFilePath);
			bNeedCompileMlir |= !PlatformFile.FileExists(*VmfbFilePath);
		}
	}
	
	if (bNeedCompileMlir)
	{
		SCOPED_NAMED_EVENT_TEXT("Compile", FColor::Magenta);

		PlatformFile.DeleteDirectoryRecursively(*IntermediateDirFullPath);
		PlatformFile.CreateDirectoryTree(*IntermediateDirFullPath);

		TUniquePtr<UE::NNERuntimeIREE::CPU::FCompiler> Compiler = UE::NNERuntimeIREE::CPU::FCompiler::Make(TargetPlatformName);
		if (!Compiler.IsValid())
		{
			UE_LOG(LogNNERuntimeIREE, Warning, TEXT("UNNERuntimeIREECpu failed to create a compiler to compile for platform %s"), *TargetPlatformName);
			return TSharedPtr<UE::NNE::FSharedModelData>();
		}

		TWeakObjectPtr<UNNERuntimeIREEModuleMetaData> CompilerModuleMetaData = NewObject<UNNERuntimeIREEModuleMetaData>();

		if (!Compiler->CompileMlir(FileData, FileIdString, IntermediateDirFullPath, CompilerResult, *CompilerModuleMetaData))
		{
			UE_LOG(LogNNERuntimeIREE, Warning, TEXT("UNNERuntimeIREECpu failed to compile model %s"), *FileIdString);
			return TSharedPtr<UE::NNE::FSharedModelData>();
		}

		IREEModelData->GUID = UNNERuntimeIREECpu::GUID;
		IREEModelData->Version = UNNERuntimeIREECpu::Version;
		IREEModelData->FileId = FileId;
		if (AdditionalFileData.Contains("IREEModuleMetaData"))
		{
			IREEModelData->ModuleMetaData = AdditionalFileData["IREEModuleMetaData"];
		}
		if (IREEModelData->ModuleMetaData.IsEmpty())
		{
			FMemoryWriter64 Writer(IREEModelData->ModuleMetaData, /*bIsPersitent =*/ true);
			CompilerModuleMetaData->Serialize(Writer);
		}
		{
			FMemoryWriter64 Writer(IREEModelData->CompilerResult, /*bIsPersitent =*/ true);
			FNNERuntimeIREECompilerResultCPU::StaticStruct()->SerializeBin(Writer, &CompilerResult);
		}

		{
			FMemoryWriter64 Writer(ResultData, /*bIsPersitent =*/ true);
			IREEModelData->Serialize(Writer);
		}

		FFileHelper::SaveArrayToFile(ResultData, *IREEModelDataFilePath);
	}

	// Copy files for staging
	FString StagingDirFullPath = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), GetPackagedModelDirPath(TargetPlatformName)));
	for (int32 i = 0; i < CompilerResult.ArchitectureInfos.Num(); i++)
	{
		SCOPED_NAMED_EVENT_TEXT("Copy", FColor::Magenta);

		const FNNERuntimeIREEArchitectureInfoCPU& Info = CompilerResult.ArchitectureInfos[i];
		const FString SharedLibrarySubDirFullPath = FPaths::Combine(SharedLibraryDirFullPath, Info.RelativeDirPath);
		const FString StagingSubDirFullPath = FPaths::Combine(StagingDirFullPath, Info.Architecture);
		
		const FString SharedLibraryFilePathSrc = FPaths::Combine(SharedLibrarySubDirFullPath, Info.SharedLibraryFileName);
		const FString VmfbFilePathSrc = FPaths::Combine(SharedLibrarySubDirFullPath, Info.VmfbFileName);

		const FString SharedLibraryFilePathDest = FPaths::Combine(StagingSubDirFullPath, Info.SharedLibraryFileName);
		const FString VmfbFilePathDest = FPaths::Combine(StagingSubDirFullPath, Info.VmfbFileName);

		IFileManager::Get().Copy(*SharedLibraryFilePathDest, *SharedLibraryFilePathSrc, bNeedCompileMlir);
		IFileManager::Get().Copy(*VmfbFilePathDest, *VmfbFilePathSrc, bNeedCompileMlir);
	}

	return MakeShared<UE::NNE::FSharedModelData>(MakeSharedBufferFromArray(MoveTemp(ResultData)), 0);
#else
	return TSharedPtr<UE::NNE::FSharedModelData>();
#endif // WITH_EDITOR
}

FString UNNERuntimeIREECpu::GetModelDataIdentifier(const FString& FileType, TConstArrayView64<uint8> FileData, const TMap<FString, TConstArrayView64<uint8>>& AdditionalFileData, const FGuid& FileId, const ITargetPlatform* TargetPlatform) const
{
	// Leave architecture blank as there is only one model data for all architectures of a given platform, only the vmfb and shared lib are different
	FString PlatformName = UE::NNERuntimeIREE::CPU::Private::GetTargetPlatformName(TargetPlatform);
	return UE::NNERuntimeIREE::CPU::Private::GetModelDataIdentifier(GetRuntimeName(), UNNERuntimeIREECpu::GUID, FileId.ToString(EGuidFormats::Digits), PlatformName, "");
}

UNNERuntimeIREECpu::ECanCreateModelCPUStatus UNNERuntimeIREECpu::CanCreateModelCPU(const TObjectPtr<UNNEModelData> ModelData) const
{
	check(ModelData != nullptr);

	TSharedPtr<UE::NNE::FSharedModelData> SharedData = ModelData->GetModelData(GetRuntimeName());
	if (!SharedData.IsValid())
	{
		return ECanCreateModelCPUStatus::Fail;
	}

	if (!UNNERuntimeIREEModelData::IsSameGuidAndVersion(SharedData->GetView(), UNNERuntimeIREECpu::GUID, UNNERuntimeIREECpu::Version))
	{
		return ECanCreateModelCPUStatus::Fail;
	}

	return ECanCreateModelCPUStatus::Ok;
}

TSharedPtr<UE::NNE::IModelCPU> UNNERuntimeIREECpu::CreateModelCPU(const TObjectPtr<UNNEModelData> ModelData)
{
	check(ModelData != nullptr);

	using namespace UE::NNERuntimeIREE::CPU::Private;

	if (CanCreateModelCPU(ModelData) != ECanCreateModelCPUStatus::Ok)
	{
		UE_LOG(LogNNERuntimeIREE, Warning, TEXT("UNNERuntimeIREECpu cannot create a model from the model data with id %s"), *ModelData->GetFileId().ToString(EGuidFormats::Digits));
		return TSharedPtr<UE::NNE::IModelCPU>();
	}

	FString CurrentArchitecture = "";
#if PLATFORM_CPU_X86_FAMILY
	CurrentArchitecture = "x86_64";
#elif PLATFORM_CPU_ARM_FAMILY
	CurrentArchitecture = "arm64";
#endif

	TSharedPtr<UE::NNE::FSharedModelData> SharedData = ModelData->GetModelData(GetRuntimeName());
	check(SharedData.IsValid());

	TConstArrayView<uint8> SharedDataView = SharedData->GetView();

	TWeakObjectPtr<UNNERuntimeIREEModelData> IREEModelData = NewObject<UNNERuntimeIREEModelData>();
	{
		FMemoryReaderView Reader(SharedDataView, /*bIsPersitent =*/ true);
		IREEModelData->Serialize(Reader);
	}

	if (IREEModelData->ModuleMetaData.IsEmpty())
	{
		UE_LOG(LogNNERuntimeIREE, Warning, TEXT("UNNERuntimeIREECpu failed to find any module meta data, please reimport the original model"));
		return TSharedPtr<UE::NNE::IModelCPU>();
	}

	TWeakObjectPtr<UNNERuntimeIREEModuleMetaData> ModuleMetaData = NewObject<UNNERuntimeIREEModuleMetaData>();
	{
		FMemoryReaderView Reader(IREEModelData->ModuleMetaData, /*bIsPersitent =*/ true);
		ModuleMetaData->Serialize(Reader);
	}

	if (ModuleMetaData->FunctionMetaData.IsEmpty())
	{
		UE_LOG(LogNNERuntimeIREE, Warning, TEXT("UNNERuntimeIREECpu failed to parse the module meta data, please reimport the original model"));
		return TSharedPtr<UE::NNE::IModelCPU>();
	}

	FNNERuntimeIREECompilerResultCPU CompilerResult{};
	{
		FMemoryReaderView Reader(IREEModelData->CompilerResult, /*bIsPersitent =*/ true);
		FNNERuntimeIREECompilerResultCPU::StaticStruct()->SerializeBin(Reader, &CompilerResult);
	}

	int32 ArchitectureIndex = -1;
	for (int32 i = 0; i < CompilerResult.ArchitectureInfos.Num(); i++)
	{
		if ((CompilerResult.ArchitectureInfos[i].Architecture.IsEmpty() && ArchitectureIndex < 0) || CompilerResult.ArchitectureInfos[i].Architecture.Equals(CurrentArchitecture))
		{
			ArchitectureIndex = i;
		}
	}

	if (ArchitectureIndex < 0)
	{
		UE_LOG(LogNNERuntimeIREE, Warning, TEXT("UNNERuntimeIREECpu failed to find a matching architecture for \'%s\'"), *CurrentArchitecture);
		return TSharedPtr<UE::NNE::IModelCPU>();
	}

	const FNNERuntimeIREEArchitectureInfoCPU& ArchitectureInfo = CompilerResult.ArchitectureInfos[ArchitectureIndex];

	const FString FileIdString = IREEModelData->FileId.ToString(EGuidFormats::Digits).ToLower();
	const FString SharedLibraryDirFullPath = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), GetSharedLibDirPath(UGameplayStatics::GetPlatformName(), FileIdString)));
	const FString SharedLibrarySubDirFullPath = FPaths::Combine(SharedLibraryDirFullPath, ArchitectureInfo.RelativeDirPath);

	TSharedPtr<UE::NNE::IModelCPU> Model = UE::NNERuntimeIREE::CPU::FModel::Make(SharedLibrarySubDirFullPath, ArchitectureInfo.SharedLibraryFileName, ArchitectureInfo.VmfbFileName, ArchitectureInfo.SharedLibraryEntryPointName, *ModuleMetaData);
	if (!Model.IsValid())
	{
		UE_LOG(LogNNERuntimeIREE, Warning, TEXT("UNNERuntimeIREECpu could not initialize the model created from model data with id %s"), *FileIdString);
		return TSharedPtr<UE::NNE::IModelCPU>();
	}

	return Model;
}

FString UNNERuntimeIREEGpu::GetRuntimeName() const
{
	return TEXT("");
}

UNNERuntimeIREEGpu::ECanCreateModelDataStatus UNNERuntimeIREEGpu::CanCreateModelData(const FString& FileType, TConstArrayView64<uint8> FileData, const TMap<FString, TConstArrayView64<uint8>>& AdditionalFileData, const FGuid& FileId, const ITargetPlatform* TargetPlatform) const
{
#if WITH_EDITOR
	// Check model is not > 2GB
	if ((TArray<uint8>::SizeType)FileData.Num() != FileData.Num())
	{
		return ECanCreateModelDataStatus::Fail;
	}

	return FileType.Compare(TEXT("mlir"), ESearchCase::IgnoreCase) == 0 ? ECanCreateModelDataStatus::Ok : ECanCreateModelDataStatus::FailFileIdNotSupported;
#else
	return ECanCreateModelDataStatus::Fail;
#endif // WITH_EDITOR
}

TSharedPtr<UE::NNE::FSharedModelData> UNNERuntimeIREEGpu::CreateModelData(const FString& FileType, TConstArrayView64<uint8> FileData, const TMap<FString, TConstArrayView64<uint8>>& AdditionalFileData, const FGuid& FileId, const ITargetPlatform* TargetPlatform)
{
	return TSharedPtr<UE::NNE::FSharedModelData>();
}

FString UNNERuntimeIREEGpu::GetModelDataIdentifier(const FString& FileType, TConstArrayView64<uint8> FileData, const TMap<FString, TConstArrayView64<uint8>>& AdditionalFileData, const FGuid& FileId, const ITargetPlatform* TargetPlatform) const
{
	FString PlatformName = UE::NNERuntimeIREE::CPU::Private::GetTargetPlatformName(TargetPlatform);
	return UE::NNERuntimeIREE::CPU::Private::GetModelDataIdentifier(GetRuntimeName(), GetGUID(), FileId.ToString(EGuidFormats::Digits), PlatformName, "");
}

UNNERuntimeIREEGpu::ECanCreateModelGPUStatus UNNERuntimeIREEGpu::CanCreateModelGPU(const TObjectPtr<UNNEModelData> ModelData) const
{
	check(ModelData != nullptr);

	TSharedPtr<UE::NNE::FSharedModelData> SharedData = ModelData->GetModelData(GetRuntimeName());
	if (!SharedData.IsValid())
	{
		return ECanCreateModelGPUStatus::Fail;
	}

	TConstArrayView<uint8> SharedDataView = SharedData->GetView();
	FGuid Guid = GetGUID();
	int32 Version = GetVersion();
	int32 GuidSize = sizeof(Guid);
	int32 VersionSize = sizeof(Version);
	if (SharedDataView.Num() <= GuidSize + VersionSize)
	{
		return ECanCreateModelGPUStatus::Fail;
	}

	bool bResult = FGenericPlatformMemory::Memcmp(&(SharedDataView[0]), &(Guid), GuidSize) == 0;
	bResult &= FGenericPlatformMemory::Memcmp(&(SharedDataView[GuidSize]), &(Version), VersionSize) == 0;

	return bResult ? ECanCreateModelGPUStatus::Ok : ECanCreateModelGPUStatus::Fail;
}

TSharedPtr<UE::NNE::IModelGPU> UNNERuntimeIREEGpu::CreateModelGPU(const TObjectPtr<UNNEModelData> ModelData)
{
	check(ModelData != nullptr);

	if (CanCreateModelGPU(ModelData) != ECanCreateModelGPUStatus::Ok)
	{
		UE_LOG(LogNNERuntimeIREE, Warning, TEXT("UNNERuntimeIREEGpu cannot create a model from the model data with id %s"), *ModelData->GetFileId().ToString(EGuidFormats::Digits));
		return TSharedPtr<UE::NNE::IModelGPU>();
	}

	check(ModelData->GetModelData(GetRuntimeName()).IsValid());

	UE::NNE::IModelGPU* IModel = nullptr;
	TConstArrayView<uint8> SharedDataView = ModelData->GetModelData(GetRuntimeName())->GetView();

	return TSharedPtr<UE::NNE::IModelGPU>(IModel);
}

bool UNNERuntimeIREEGpu::IsAvailable() const
{
	return false;
}

FGuid UNNERuntimeIREEGpu::GetGUID() const
{
	return FGuid();
}

int32 UNNERuntimeIREEGpu::GetVersion() const
{
	return 0;
}

FGuid UNNERuntimeIREECuda::GUID = FGuid((int32)'I', (int32)'G', (int32)'C', (int32)'U');
int32 UNNERuntimeIREECuda::Version = 0x00000001;

FString UNNERuntimeIREECuda::GetRuntimeName() const
{
	return TEXT("NNERuntimeIREECuda");
}

bool UNNERuntimeIREECuda::IsAvailable() const
{
	return false;
}

FGuid UNNERuntimeIREECuda::GetGUID() const
{
	return GUID;
}

int32 UNNERuntimeIREECuda::GetVersion() const
{
	return Version;
}

FGuid UNNERuntimeIREEVulkan::GUID = FGuid((int32)'I', (int32)'G', (int32)'V', (int32)'U');
int32 UNNERuntimeIREEVulkan::Version = 0x00000001;

FString UNNERuntimeIREEVulkan::GetRuntimeName() const
{
	return TEXT("NNERuntimeIREEVulkan");
}

bool UNNERuntimeIREEVulkan::IsAvailable() const
{
	return false;
}

FGuid UNNERuntimeIREEVulkan::GetGUID() const
{
	return GUID;
}

int32 UNNERuntimeIREEVulkan::GetVersion() const
{
	return Version;
}

FGuid UNNERuntimeIREERdg::GUID = FGuid((int32)'I', (int32)'R', (int32)'D', (int32)'G');
int32 UNNERuntimeIREERdg::Version = 0x00000001;

FString UNNERuntimeIREERdg::GetRuntimeName() const
{
	return TEXT("NNERuntimeIREERdg");
}

UNNERuntimeIREERdg::ECanCreateModelDataStatus UNNERuntimeIREERdg::CanCreateModelData(const FString& FileType, TConstArrayView64<uint8> FileData, const TMap<FString, TConstArrayView64<uint8>>& AdditionalFileData, const FGuid& FileId, const ITargetPlatform* TargetPlatform) const
{
#if WITH_EDITOR
	// Check model is not > 2GB
	if ((TArray<uint8>::SizeType)FileData.Num() != FileData.Num())
	{
		return ECanCreateModelDataStatus::Fail;
	}

	return	FileType.Compare(TEXT("mlir"), ESearchCase::IgnoreCase) == 0 ? ECanCreateModelDataStatus::Ok : ECanCreateModelDataStatus::FailFileIdNotSupported;
#else
	return ECanCreateModelDataStatus::Fail;
#endif // WITH_EDITOR
}

TSharedPtr<UE::NNE::FSharedModelData> UNNERuntimeIREERdg::CreateModelData(const FString& FileType, TConstArrayView64<uint8> FileData, const TMap<FString, TConstArrayView64<uint8>>& AdditionalFileData, const FGuid& FileId, const ITargetPlatform* TargetPlatform)
{
	return TSharedPtr<UE::NNE::FSharedModelData>();
}

FString UNNERuntimeIREERdg::GetModelDataIdentifier(const FString& FileType, TConstArrayView64<uint8> FileData, const TMap<FString, TConstArrayView64<uint8>>& AdditionalFileData, const FGuid& FileId, const ITargetPlatform* TargetPlatform) const
{
	FString PlatformName = UE::NNERuntimeIREE::CPU::Private::GetTargetPlatformName(TargetPlatform);
	return UE::NNERuntimeIREE::CPU::Private::GetModelDataIdentifier(GetRuntimeName(), UNNERuntimeIREERdg::GUID, FileId.ToString(EGuidFormats::Digits), PlatformName, "");
}

UNNERuntimeIREERdg::ECanCreateModelRDGStatus UNNERuntimeIREERdg::CanCreateModelRDG(const TObjectPtr<UNNEModelData> ModelData) const
{
	check(ModelData != nullptr);

	TSharedPtr<UE::NNE::FSharedModelData> SharedData = ModelData->GetModelData(GetRuntimeName());
	if (!SharedData.IsValid())
	{
		return ECanCreateModelRDGStatus::Fail;
	}

	TConstArrayView<uint8> SharedDataView = SharedData->GetView();
	int32 GuidSize = sizeof(UNNERuntimeIREERdg::GUID);
	int32 VersionSize = sizeof(UNNERuntimeIREERdg::Version);
	if (SharedDataView.Num() <= GuidSize + VersionSize)
	{
		return ECanCreateModelRDGStatus::Fail;
	}

	bool bResult = FGenericPlatformMemory::Memcmp(&(SharedDataView[0]), &(UNNERuntimeIREERdg::GUID), GuidSize) == 0;
	bResult &= FGenericPlatformMemory::Memcmp(&(SharedDataView[GuidSize]), &(UNNERuntimeIREERdg::Version), VersionSize) == 0;

	return bResult ? ECanCreateModelRDGStatus::Ok : ECanCreateModelRDGStatus::Fail;
}

TSharedPtr<UE::NNE::IModelRDG> UNNERuntimeIREERdg::CreateModelRDG(const TObjectPtr<UNNEModelData> ModelData)
{
	check(ModelData != nullptr);

	if (CanCreateModelRDG(ModelData) != ECanCreateModelRDGStatus::Ok)
	{
		UE_LOG(LogNNERuntimeIREE, Warning, TEXT("UNNERuntimeIREERdg cannot create a model from the model data with id %s"), *ModelData->GetFileId().ToString(EGuidFormats::Digits));
		return TSharedPtr<UE::NNE::IModelRDG>();
	}

	check(ModelData->GetModelData(GetRuntimeName()).IsValid());

	UE::NNE::IModelRDG* IModel = nullptr;
	TConstArrayView<uint8> SharedDataView = ModelData->GetModelData(GetRuntimeName())->GetView();

	return TSharedPtr<UE::NNE::IModelRDG>(IModel);
}

bool UNNERuntimeIREERdg::IsAvailable() const
{
	return false;
}

#endif // WITH_NNE_RUNTIME_IREE