// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "NNEModelBase.h"
#include "NNEModelData.h"
#include "NNEOnnxruntime.h"
#include "NNERuntimeCPU.h"
#include "NNERuntimeNPU.h"
#include "NNERuntimeGPU.h"
#include "NNERuntimeORTEnv.h"
#include "NNERuntimeRDG.h"
#include "NNETensor.h"
#include "NNETypes.h"

namespace UE::NNERuntimeORT::Private
{

struct FRuntimeConf
{
	ExecutionMode ExecutionMode = ExecutionMode::ORT_SEQUENTIAL;
};

template <class ModelInterface, class TensorBinding>
class FModelInstanceORTBase : public NNE::Internal::FModelInstanceBase<ModelInterface>
{

public:
	FModelInstanceORTBase(const FRuntimeConf& InRuntimeConf, TSharedRef<FEnvironment> InEnvironment);
	virtual ~FModelInstanceORTBase();

	virtual typename ModelInterface::ESetInputTensorShapesStatus SetInputTensorShapes(TConstArrayView<NNE::FTensorShape> InInputShapes) override;

	bool Init(TConstArrayView64<uint8> ModelData);

	typename ModelInterface::ERunSyncStatus RunSync(TConstArrayView<TensorBinding> InInputBindings, TConstArrayView<TensorBinding> InOutputBindings) override;

protected:
	virtual bool InitializedAndConfigureMembers();
	bool ConfigureTensors(const bool InIsInput);

	FRuntimeConf RuntimeConf;
	FString TempDirForModelWithExternalData;

	/** ORT-related variables */
	TSharedRef<FEnvironment> Environment;
	TUniquePtr<Ort::Session> Session;
	TUniquePtr<Ort::AllocatorWithDefaultOptions> Allocator;
	TUniquePtr<Ort::SessionOptions> SessionOptions;
	TUniquePtr<Ort::MemoryInfo> MemoryInfo;

	/** IO ORT-related variables */
	TArray<ONNXTensorElementDataType> InputTensorsORTType;
	TArray<ONNXTensorElementDataType> OutputTensorsORTType;

	TArray<Ort::AllocatedStringPtr> InputTensorNameValues;
	TArray<Ort::AllocatedStringPtr> OutputTensorNameValues;
	TArray<char*> InputTensorNames;
	TArray<char*> OutputTensorNames;

	TArray<NNE::Internal::FTensor> InputTensors;
	TArray<NNE::Internal::FTensor> OutputTensors;
};

class FModelInstanceORTCpu : public FModelInstanceORTBase<NNE::IModelInstanceCPU, NNE::FTensorBindingCPU>
{
public:
	FModelInstanceORTCpu(const FRuntimeConf& InRuntimeConf, TSharedRef<FEnvironment> InEnvironment) : FModelInstanceORTBase(InRuntimeConf, InEnvironment) {}
	virtual ~FModelInstanceORTCpu() = default;

private:
	virtual bool InitializedAndConfigureMembers() override;
};

class FModelORTCpu : public NNE::IModelCPU
{
public:
	FModelORTCpu(TSharedRef<FEnvironment> InEnvironment, TSharedRef<UE::NNE::FSharedModelData> InModelData);
	virtual ~FModelORTCpu() = default;

	virtual TSharedPtr<NNE::IModelInstanceCPU> CreateModelInstanceCPU() override;

private:
	TSharedRef<FEnvironment> Environment;
	TSharedRef<UE::NNE::FSharedModelData> ModelData;
};

#if PLATFORM_WINDOWS
class FModelORTDmlGPU : public NNE::IModelGPU
{
public:
	FModelORTDmlGPU(TSharedRef<FEnvironment> InEnvironment, TSharedRef<UE::NNE::FSharedModelData> InModelData);
	virtual ~FModelORTDmlGPU() = default;

	virtual TSharedPtr<NNE::IModelInstanceGPU> CreateModelInstanceGPU() override;

private:
	TSharedRef<FEnvironment> Environment;
	TSharedRef<UE::NNE::FSharedModelData> ModelData;
};

class FModelInstanceORTDmlGPU : public FModelInstanceORTBase<NNE::IModelInstanceGPU, NNE::FTensorBindingCPU>
{
public:
	FModelInstanceORTDmlGPU(const FRuntimeConf& InRuntimeConf, TSharedRef<FEnvironment> InEnvironment) : FModelInstanceORTBase(InRuntimeConf, InEnvironment) {}
	virtual ~FModelInstanceORTDmlGPU() = default;

private:
	virtual bool InitializedAndConfigureMembers() override;
};

class FModelORTDmlRDG : public NNE::IModelRDG
{
public:
	FModelORTDmlRDG(TSharedRef<FEnvironment> InEnvironment, TSharedRef<UE::NNE::FSharedModelData> InModelData);

	virtual ~FModelORTDmlRDG() = default;

	virtual TSharedPtr<NNE::IModelInstanceRDG> CreateModelInstanceRDG() override;

private:
	TSharedRef<FEnvironment> Environment;
	TSharedRef<UE::NNE::FSharedModelData> ModelData;
};

class FModelInstanceORTDmlRDG : public NNE::Internal::FModelInstanceBase<NNE::IModelInstanceRDG>
{

public:
	using ESetInputTensorShapesStatus = NNE::IModelInstanceRDG::ESetInputTensorShapesStatus;
	using EEnqueueRDGStatus = NNE::IModelInstanceRDG::EEnqueueRDGStatus;

	FModelInstanceORTDmlRDG(TSharedRef<UE::NNE::FSharedModelData> InModelData, const FRuntimeConf& InRuntimeConf, TSharedRef<FEnvironment> InEnvironment);

	virtual ~FModelInstanceORTDmlRDG();

	virtual ESetInputTensorShapesStatus SetInputTensorShapes(TConstArrayView<NNE::FTensorShape> InInputShapes) override;

	bool Init();

	EEnqueueRDGStatus EnqueueRDG(FRDGBuilder& GraphBuilder, TConstArrayView<NNE::FTensorBindingRDG> Inputs, TConstArrayView<NNE::FTensorBindingRDG> Outputs) override;

protected:
	bool ConfigureTensors(const Ort::Session& ActiveSession);
	bool ConfigureTensors(const Ort::Session& ActiveSession, bool InIsInput);

	TSharedRef<UE::NNE::FSharedModelData> ModelData;
	FRuntimeConf RuntimeConf;
	FString TempDirForModelWithExternalData;

	/** ORT-related variables */
	TSharedRef<FEnvironment> Environment;
	TUniquePtr<Ort::Session> Session;
	TUniquePtr<Ort::SessionOptions> SessionOptions;
	TUniquePtr<Ort::AllocatorWithDefaultOptions> Allocator;

	/** IO ORT-related variables */
	TArray<ONNXTensorElementDataType> InputTensorsORTType;
	TArray<ONNXTensorElementDataType> OutputTensorsORTType;

	TArray<Ort::AllocatedStringPtr> InputTensorNameValues;
	TArray<Ort::AllocatedStringPtr> OutputTensorNameValues;
	TArray<char*> InputTensorNames;
	TArray<char*> OutputTensorNames;
	TArray<TArray<FString>> InputSymbolicDimensionNames;
	TArray<TArray<FString>> OutputSymbolicDimensionNames;

	TArray<NNE::Internal::FTensor> InputTensors;
	TArray<NNE::Internal::FTensor> OutputTensors;
};

class FModelInstanceORTNpu : public FModelInstanceORTBase<NNE::IModelInstanceNPU, NNE::FTensorBindingCPU>
{
public:
	FModelInstanceORTNpu(const FRuntimeConf& InRuntimeConf, TSharedRef<FEnvironment> InEnvironment) : FModelInstanceORTBase(InRuntimeConf, InEnvironment) {}
	virtual ~FModelInstanceORTNpu() = default;

private:
	virtual bool InitializedAndConfigureMembers() override;
};

class FModelORTNpu : public NNE::IModelNPU
{
public:
	FModelORTNpu(TSharedRef<FEnvironment> InEnvironment, TSharedRef<UE::NNE::FSharedModelData> InModelData);
	virtual ~FModelORTNpu() = default;

	virtual TSharedPtr<NNE::IModelInstanceNPU> CreateModelInstanceNPU() override;

private:
	TSharedRef<FEnvironment> Environment;
	TSharedRef<UE::NNE::FSharedModelData> ModelData;
};
#endif //PLATFORM_WINDOWS
	
} // UE::NNERuntimeORT::Private