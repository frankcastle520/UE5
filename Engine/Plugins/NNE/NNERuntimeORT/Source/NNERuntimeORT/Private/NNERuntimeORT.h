// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "NNERuntime.h"
#include "NNERuntimeCPU.h"
#include "NNERuntimeNPU.h"
#include "NNERuntimeGPU.h"
#include "NNERuntimeRDG.h"
#include "UObject/Class.h"
#include "UObject/Object.h"
#include "UObject/UObjectBaseUtility.h"

#include "NNERuntimeORT.generated.h"

DECLARE_LOG_CATEGORY_EXTERN(LogNNERuntimeORT, Log, All);

namespace UE::NNERuntimeORT::Private
{
	class FEnvironment;
}

UCLASS()
class UNNERuntimeORTCpu : public UObject, public INNERuntime, public INNERuntimeCPU
{
	GENERATED_BODY()

private:
	TSharedPtr<UE::NNERuntimeORT::Private::FEnvironment> Environment;

public:
	static FGuid GUID;
	static int32 Version;

	virtual ~UNNERuntimeORTCpu() = default;

	void Init(TSharedRef<UE::NNERuntimeORT::Private::FEnvironment> InEnvironment);

	virtual FString GetRuntimeName() const override;
	
	virtual ECanCreateModelDataStatus CanCreateModelData(const FString& FileType, TConstArrayView64<uint8> FileData, const TMap<FString, TConstArrayView64<uint8>>& AdditionalFileData, const FGuid& FileId, const ITargetPlatform* TargetPlatform) const override;
	virtual TSharedPtr<UE::NNE::FSharedModelData> CreateModelData(const FString& FileType, TConstArrayView64<uint8> FileData, const TMap<FString, TConstArrayView64<uint8>>& AdditionalFileData, const FGuid& FileId, const ITargetPlatform* TargetPlatform) override;
	virtual FString GetModelDataIdentifier(const FString& FileType, TConstArrayView64<uint8> FileData, const TMap<FString, TConstArrayView64<uint8>>& AdditionalFileData, const FGuid& FileId, const ITargetPlatform* TargetPlatform) const override;

	virtual ECanCreateModelCPUStatus CanCreateModelCPU(const TObjectPtr<UNNEModelData> ModelData) const override;
	virtual TSharedPtr<UE::NNE::IModelCPU> CreateModelCPU(const TObjectPtr<UNNEModelData> ModelData) override;
};

UCLASS()
class UNNERuntimeORTDml : public UObject, public INNERuntime, public INNERuntimeGPU, public INNERuntimeRDG, public INNERuntimeNPU
{
	GENERATED_BODY()

	using ECanCreateModelCommonStatus = UE::NNE::EResultStatus;

private:
	TSharedPtr<UE::NNERuntimeORT::Private::FEnvironment> Environment;

public:
	static FGuid GUID;
	static int32 Version;
	
	virtual ~UNNERuntimeORTDml() = default;

	void Init(TSharedRef<UE::NNERuntimeORT::Private::FEnvironment> InEnvironment, bool bInDirectMLAvailable);

	virtual FString GetRuntimeName() const override;

	virtual ECanCreateModelDataStatus CanCreateModelData(const FString& FileType, TConstArrayView64<uint8> FileData, const TMap<FString, TConstArrayView64<uint8>>& AdditionalFileData, const FGuid& FileId, const ITargetPlatform* TargetPlatform) const override;
	virtual TSharedPtr<UE::NNE::FSharedModelData> CreateModelData(const FString& FileType, TConstArrayView64<uint8> FileData, const TMap<FString, TConstArrayView64<uint8>>& AdditionalFileData, const FGuid& FileId, const ITargetPlatform* TargetPlatform) override;
	virtual FString GetModelDataIdentifier(const FString& FileType, TConstArrayView64<uint8> FileData, const TMap<FString, TConstArrayView64<uint8>>& AdditionalFileData, const FGuid& FileId, const ITargetPlatform* TargetPlatform) const override;

	virtual ECanCreateModelGPUStatus CanCreateModelGPU(const TObjectPtr<UNNEModelData> ModelData) const override;
	virtual TSharedPtr<UE::NNE::IModelGPU> CreateModelGPU(const TObjectPtr<UNNEModelData> ModelData) override;

	virtual ECanCreateModelRDGStatus CanCreateModelRDG(TObjectPtr<UNNEModelData> ModelData) const override;
	virtual TSharedPtr<UE::NNE::IModelRDG> CreateModelRDG(TObjectPtr<UNNEModelData> ModelData) override;

	virtual ECanCreateModelNPUStatus CanCreateModelNPU(const TObjectPtr<UNNEModelData> ModelData) const override;
	virtual TSharedPtr<UE::NNE::IModelNPU> CreateModelNPU(const TObjectPtr<UNNEModelData> ModelData) override;

private:
	ECanCreateModelCommonStatus CanCreateModelCommon(const TObjectPtr<UNNEModelData> ModelData, bool bRHID3D12Required = true) const;

	bool bDirectMLAvailable = false;
	bool bD3D12Available = false;
	bool bD3D12DeviceNPUAvailable = false;
};