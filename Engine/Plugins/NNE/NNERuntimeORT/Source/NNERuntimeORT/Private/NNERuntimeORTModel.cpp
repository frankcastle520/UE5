// Copyright Epic Games, Inc. All Rights Reserved.

#include "NNERuntimeORTModel.h"

#include "HAL/PlatformFileManager.h"
#include "HAL/PlatformMisc.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "NNERuntimeORT.h"
#include "NNERuntimeORTModelFormat.h"
#include "NNERuntimeORTSettings.h"
#include "NNERuntimeORTUtils.h"
#include "RenderGraphUtils.h"

#if PLATFORM_WINDOWS
#include "ID3D12DynamicRHI.h"
#endif // PLATFORM_WINDOWS

// DirectML is implemented using COM on all platforms
#ifdef IID_GRAPHICS_PPV_ARGS
#define DML_PPV_ARGS(x) __uuidof(*x), IID_PPV_ARGS_Helper(x)
#else
#define DML_PPV_ARGS(x) IID_PPV_ARGS(x)
#endif

BEGIN_SHADER_PARAMETER_STRUCT(FORTModelInstanceRDGParameters, )
	RDG_BUFFER_ACCESS_ARRAY(InputBuffers)
	RDG_BUFFER_ACCESS_ARRAY(OutputBuffers)
END_SHADER_PARAMETER_STRUCT()

DECLARE_GPU_STAT_NAMED(FNNERuntimeORTDmlRDG, TEXT("FModelInstanceORTDmlRDG::EnqueueRDG"));

namespace UE::NNERuntimeORT::Private
{

namespace Detail
{

	FRuntimeConf MakeRuntimeConfigFromSettings(const UNNERuntimeORTSettings* Settings)
	{
		FRuntimeConf Result{};
#if WITH_EDITOR
			FThreadingOptions ThreadingOptions = Settings->EditorThreadingOptions;
#else
			FThreadingOptions ThreadingOptions = Settings->GameThreadingOptions;
#endif
		Result.ExecutionMode = ThreadingOptions.ExecutionMode == EExecutionMode::SEQUENTIAL ? ExecutionMode::ORT_SEQUENTIAL : ExecutionMode::ORT_PARALLEL;

		return Result;
	}

	FString CreateTempDirPath(const FString& BasePath)
	{
		FString UniqueDirName;
		do
		{
			UniqueDirName = FPaths::Combine(BasePath, *FString::Printf(TEXT("ORTModel_%s"), *FGuid::NewGuid().ToString()));
		} while (IFileManager::Get().DirectoryExists(*UniqueDirName));

		return UniqueDirName;
	}

	bool CreateSession(
		TConstArrayView64<uint8> ModelData,
		const Ort::SessionOptions& SessionOptions,
		const FEnvironment& Environment,
		TUniquePtr<Ort::Session>& Session, FString& TempDirForModelWithExternalData)
	{
		FMemoryReaderView Reader(MakeMemoryView(ModelData), /*bIsPersitent =*/ true);
		FGuid GUID;
		int32 Version;
		Reader << GUID;
		Reader << Version;

		FOnnxDataDescriptor Descriptor;
		Reader << Descriptor;

		int64 BaseDataOffset = Reader.Tell();
		TConstArrayView64<uint8> ModelBuffer = TConstArrayView64<uint8>(&(ModelData.GetData()[BaseDataOffset]), Descriptor.OnnxModelDataSize);

		if (ModelBuffer.Num() == 0)
		{
			UE_LOG(LogNNERuntimeORT, Error, TEXT("Cannot create ORT session: Input model data is empty."));
			return false;
		}

		// Starting with ORT v18 we will get AddExternalInitializersFromFilesInMemory() via onnxruntime_c_api.h
		// however for now we use temp files when working with model with external data.
		if (Descriptor.AdditionalDataDescriptors.Num() > 0)
		{
			FString Filepath;
			if (TempDirForModelWithExternalData.IsEmpty())
			{
				FString ProjIntermediateDir = FPaths::ConvertRelativePathToFull(FPaths::ProjectIntermediateDir());
				TempDirForModelWithExternalData = Detail::CreateTempDirPath(ProjIntermediateDir);
				Filepath = FPaths::Combine(TempDirForModelWithExternalData, TEXT("OnnxModel.onnx"));

				// Note: SaveArrayToFile() will create the needed folders as needed both for the Onnx model and the additional data files.
				if (!FFileHelper::SaveArrayToFile(ModelBuffer, *Filepath))
				{
					IFileManager::Get().DeleteDirectory(*TempDirForModelWithExternalData, false, true);
					UE_LOG(LogNNERuntimeORT, Error, TEXT("Large models are an experimental feature at the moment. Could not write model to disk at %s."), *Filepath);
					return false;
				}

				for (const FOnnxAdditionalDataDescriptor& AdditionalDataDescriptor : Descriptor.AdditionalDataDescriptors)
				{
					FString AdditionalDataFilename = FPaths::Combine(TempDirForModelWithExternalData, *AdditionalDataDescriptor.Path);
					TConstArrayView64<uint8> AdditionalDataBuffer = TConstArrayView64<uint8>(&(ModelData.GetData()[BaseDataOffset + AdditionalDataDescriptor.Offset]), AdditionalDataDescriptor.Size);

					if (!FFileHelper::SaveArrayToFile(AdditionalDataBuffer, *AdditionalDataFilename))
					{
						IFileManager::Get().DeleteDirectory(*TempDirForModelWithExternalData, false, true);
						UE_LOG(LogNNERuntimeORT, Error, TEXT("Large models are an experimental feature at the moment. Could not write additional data to disk at %s."), *AdditionalDataFilename);
						return false;
					}
				}
			}
			else
			{
				Filepath = FPaths::Combine(TempDirForModelWithExternalData, TEXT("OnnxModel.onnx"));
			}

			Session = CreateOrtSession(Environment, Filepath, SessionOptions);
		}
		else
		{
			Session = CreateOrtSessionFromArray(Environment, ModelBuffer, SessionOptions);
		}

		return Session.IsValid();
	}

} // namespace Detail

template <class ModelInterface, class TensorBinding> 
FModelInstanceORTBase<ModelInterface, TensorBinding>::FModelInstanceORTBase(const FRuntimeConf& InRuntimeConf, TSharedRef<FEnvironment> InEnvironment)
	: RuntimeConf(InRuntimeConf), Environment(InEnvironment)
{

}

template <class ModelInterface, class TensorBinding>
FModelInstanceORTBase<ModelInterface, TensorBinding>::~FModelInstanceORTBase()
{
	Session.Reset();
	if (!TempDirForModelWithExternalData.IsEmpty())
	{
		if (!IFileManager::Get().DeleteDirectory(*TempDirForModelWithExternalData, false, true))
		{
			UE_LOG(LogNNERuntimeORT, Warning, TEXT("Large models are an experimental feature at the moment. Could not delete temp directy %s on model instance destruction."), *TempDirForModelWithExternalData);
		}
	}
}

template <class ModelInterface, class TensorBinding> 
bool FModelInstanceORTBase<ModelInterface, TensorBinding>::Init(TConstArrayView64<uint8> ModelData)
{
#if WITH_EDITOR
	try
#endif // WITH_EDITOR
	{
		if (!InitializedAndConfigureMembers())
		{
			UE_LOG(LogNNERuntimeORT, Error, TEXT("InitializedAndConfigureMembers failed."));
			return false;
		}

		if (!Detail::CreateSession(ModelData, *SessionOptions, *Environment, Session, TempDirForModelWithExternalData))
		{
			UE_LOG(LogNNERuntimeORT, Error, TEXT("Session creation failed."));
			return false;
		}

		if (!ConfigureTensors(true))
		{
			UE_LOG(LogNNERuntimeORT, Error, TEXT("Failed to configure Inputs tensors."));
			return false;
		}
		if (!ConfigureTensors(false))
		{
			UE_LOG(LogNNERuntimeORT, Error, TEXT("Failed to configure Outputs tensors."));
			return false;
		}
	}
#if WITH_EDITOR
	catch (const Ort::Exception& Exception)
	{
		UE_LOG(LogNNERuntimeORT, Error, TEXT("%s"), UTF8_TO_TCHAR(Exception.what()));
		return false;
	}
	catch (...)
	{
		UE_LOG(LogNNERuntimeORT, Error, TEXT("Unknown exception!"));
		return false;
	}
#endif // WITH_EDITOR

	return true;
}

template <class ModelInterface, class TensorBinding> 
bool FModelInstanceORTBase<ModelInterface, TensorBinding>::InitializedAndConfigureMembers()
{
	Allocator = MakeUnique<Ort::AllocatorWithDefaultOptions>();
	MemoryInfo = MakeUnique<Ort::MemoryInfo>(Ort::MemoryInfo::CreateCpu(OrtDeviceAllocator, OrtMemTypeCPU));

	return true;
}

template <class ModelInterface, class TensorBinding>
bool FModelInstanceORTBase<ModelInterface, TensorBinding>::ConfigureTensors(bool bAreTensorInputs)
{
	const uint32 NumberTensors							= bAreTensorInputs ? Session->GetInputCount() : Session->GetOutputCount();
	TArray<NNE::FTensorDesc>& SymbolicTensorDescs		= bAreTensorInputs ? NNE::Internal::FModelInstanceBase<ModelInterface>::InputSymbolicTensors : NNE::Internal::FModelInstanceBase<ModelInterface>::OutputSymbolicTensors;
	TArray<ONNXTensorElementDataType>& TensorsORTType	= bAreTensorInputs ? InputTensorsORTType	: OutputTensorsORTType;
	TArray<char*>& TensorNames							= bAreTensorInputs ? InputTensorNames		: OutputTensorNames;
	TArray<Ort::AllocatedStringPtr>& TensorNameValues	= bAreTensorInputs ? InputTensorNameValues	: OutputTensorNameValues;

	SymbolicTensorDescs.Reset();

	for (uint32 TensorIndex = 0; TensorIndex < NumberTensors; ++TensorIndex)
	{
		// Get Tensor name
		Ort::AllocatedStringPtr CurTensorName = bAreTensorInputs ? Session->GetInputNameAllocated(TensorIndex, *Allocator) : Session->GetOutputNameAllocated(TensorIndex, *Allocator);
		TensorNameValues.Emplace(MoveTemp(CurTensorName));
		TensorNames.Emplace(TensorNameValues.Last().get());

		// Get node type
		const Ort::TypeInfo CurrentTypeInfo = bAreTensorInputs ? Session->GetInputTypeInfo(TensorIndex) : Session->GetOutputTypeInfo(TensorIndex);
		const Ort::ConstTensorTypeAndShapeInfo CurrentTensorInfo = CurrentTypeInfo.GetTensorTypeAndShapeInfo();
		const ONNXTensorElementDataType ONNXTensorElementDataTypeEnum = CurrentTensorInfo.GetElementType();
		const TypeInfoORT TypeInfo = TranslateTensorTypeORTToNNE(ONNXTensorElementDataTypeEnum);

		TensorsORTType.Emplace(ONNXTensorElementDataTypeEnum);

		// Get shape
		TArray<int32, TInlineAllocator<NNE::FTensorShape::MaxRank>> ShapeData;
		ShapeData.Reserve(CurrentTensorInfo.GetShape().size());
		for (int64 CurrentTensorSize : CurrentTensorInfo.GetShape())
		{
			ShapeData.Add((int32)CurrentTensorSize);
		}

		const NNE::FSymbolicTensorShape Shape = NNE::FSymbolicTensorShape::Make(ShapeData);
		const NNE::FTensorDesc SymbolicTensorDesc = NNE::FTensorDesc::Make(FString(TensorNames.Last()), Shape, TypeInfo.DataType);

		check(SymbolicTensorDesc.GetElementByteSize() == TypeInfo.ElementSize);
		SymbolicTensorDescs.Emplace(SymbolicTensorDesc);
	}

	return true;
}

template <class ModelInterface, class TensorBinding> 
typename ModelInterface::ESetInputTensorShapesStatus FModelInstanceORTBase<ModelInterface, TensorBinding>::SetInputTensorShapes(TConstArrayView<NNE::FTensorShape> InInputShapes)
{
	using ModelInstanceBase = NNE::Internal::FModelInstanceBase<ModelInterface>;

	InputTensors.Reset();
	OutputTensors.Reset();
	ModelInstanceBase::OutputTensorShapes.Reset();

	// Verify input shape are valid for the model and set InputTensorShapes
	if (typename ModelInterface::ESetInputTensorShapesStatus Status = ModelInstanceBase::SetInputTensorShapes(InInputShapes); Status != ModelInterface::ESetInputTensorShapesStatus::Ok)
	{
		return Status;
	}

	// Setup concrete input tensor
	for (int32 i = 0; i < ModelInstanceBase::InputSymbolicTensors.Num(); ++i)
	{
		NNE::Internal::FTensor Tensor = NNE::Internal::FTensor::Make(ModelInstanceBase::InputSymbolicTensors[i].GetName(), InInputShapes[i], ModelInstanceBase::InputSymbolicTensors[i].GetDataType());
		InputTensors.Emplace(Tensor);
	}

	// Setup concrete output shapes only if all model output shapes are concretes, otherwise it will be set during Run()
	for (NNE::FTensorDesc SymbolicTensorDesc : ModelInstanceBase::OutputSymbolicTensors)
	{
		if (SymbolicTensorDesc.GetShape().IsConcrete())
		{
			NNE::Internal::FTensor Tensor = NNE::Internal::FTensor::MakeFromSymbolicDesc(SymbolicTensorDesc);
			OutputTensors.Emplace(Tensor);
			ModelInstanceBase::OutputTensorShapes.Emplace(Tensor.GetShape());
		}
	}
	if (OutputTensors.Num() != ModelInstanceBase::OutputSymbolicTensors.Num())
	{
		OutputTensors.Reset();
		ModelInstanceBase::OutputTensorShapes.Reset();
	}

	return ModelInterface::ESetInputTensorShapesStatus::Ok;
}

template <class TensorBinding>
Ort::Value CreateTensor(const Ort::MemoryInfo& MemoryInfo, const TensorBinding& Binding, const NNE::Internal::FTensor& Tensor, const ONNXTensorElementDataType ElementDataType)
{
	const uint64 SizeInBytes = Tensor.GetDataSize();
	const uint32 ShapeLen = (uint32)Tensor.GetShape().Rank();

	TUniquePtr<int64_t[]> Shape = MakeUnique<int64_t[]>(Tensor.GetShape().Rank());
	for (int32 DimIndex = 0; DimIndex < Tensor.GetShape().Rank(); ++DimIndex)
	{
		Shape.Get()[DimIndex] = Tensor.GetShape().GetData()[DimIndex];
	}
	
	return	Ort::Value::CreateTensor(MemoryInfo, Binding.Data, SizeInBytes, Shape.Get(), ShapeLen, ElementDataType);
}

template <class ModelInterface, class TensorBinding>
typename ModelInterface::ERunSyncStatus FModelInstanceORTBase<ModelInterface, TensorBinding>::RunSync(TConstArrayView<TensorBinding> InInputBindings, TConstArrayView<TensorBinding> InOutputBindings)
{
	SCOPED_NAMED_EVENT_TEXT("FModelInstanceORTBase::RunSync", FColor::Magenta);

	if (!Session.IsValid())
	{
		UE_LOG(LogNNERuntimeORT, Error, TEXT("Called without a Session."));
		return ModelInterface::ERunSyncStatus::Fail;
	}

	// Verify the model inputs were prepared
	if (NNE::Internal::FModelInstanceBase<ModelInterface>::InputTensorShapes.IsEmpty())
	{
		UE_LOG(LogNNERuntimeORT, Error, TEXT("Input shapes are not set, please call SetInputTensorShapes."));
		return ModelInterface::ERunSyncStatus::Fail;
	}

	check(NNE::Internal::FModelInstanceBase<ModelInterface>::InputTensorShapes.Num() == InputTensors.Num());
	check(NNE::Internal::FModelInstanceBase<ModelInterface>::InputTensorShapes.Num() == InputTensorNames.Num());
	check(NNE::Internal::FModelInstanceBase<ModelInterface>::InputSymbolicTensors.Num() == InputTensors.Num());

	if (InInputBindings.Num() != InputTensors.Num())
	{
		UE_LOG(LogNNERuntimeORT, Error, TEXT("Input bindings need to match input tensor descriptor count (got %d, expected %d)."), InInputBindings.Num(), InputTensors.Num());
		return ModelInterface::ERunSyncStatus::Fail;
	}

	check(NNE::Internal::FModelInstanceBase<ModelInterface>::OutputSymbolicTensors.Num() == OutputTensorNames.Num());

	if (!InOutputBindings.IsEmpty() && InOutputBindings.Num() != OutputTensorNames.Num())
	{
		UE_LOG(LogNNERuntimeORT, Error, TEXT("Output binding can be empty or needs to match output tensor descriptor count (got %d, expected %d)."), InOutputBindings.Num(), OutputTensorNames.Num());
		return ModelInterface::ERunSyncStatus::Fail;
	}

#if WITH_EDITOR
	try
#endif // WITH_EDITOR
	{
		TArray<Ort::Value> OrtInputTensors;
		for (int32 i = 0; i < InputTensorNames.Num(); i++)
		{
			const TensorBinding& Binding = InInputBindings[i];
			const NNE::Internal::FTensor& Tensor = InputTensors[i];

			if (!Binding.Data && Binding.SizeInBytes != 0)
			{
				UE_LOG(LogNNERuntimeORT, Error, TEXT("Binding input tensor %d is not set but given size is non-zero %d."), i, Binding.SizeInBytes);
				return ModelInterface::ERunSyncStatus::Fail;
			}

			if (Binding.SizeInBytes != Tensor.GetDataSize())
			{
				UE_LOG(LogNNERuntimeORT, Error, TEXT("Binding input tensor %d size does not match size given by tensor descriptor (got %d, expected %d)."), i, Binding.SizeInBytes, Tensor.GetDataSize());
				return ModelInterface::ERunSyncStatus::Fail;
			}

			OrtInputTensors.Add(CreateTensor(*MemoryInfo, Binding, Tensor, InputTensorsORTType[i]));
		}

		TArray<Ort::Value> OrtOutputTensors;
		for (int32 i = 0; i < OutputTensorNames.Num(); i++)
		{
			if (OutputTensors.IsEmpty() ||
				InOutputBindings.IsEmpty() ||
				!InOutputBindings[i].Data ||
				InOutputBindings[i].SizeInBytes < OutputTensors[i].GetDataSize())
			{
				OrtOutputTensors.Emplace(nullptr);
			}
			else
			{
				OrtOutputTensors.Add(CreateTensor(*MemoryInfo, InOutputBindings[i], OutputTensors[i], OutputTensorsORTType[i]));
			}
		}

		Session->Run(Ort::RunOptions{nullptr},
			InputTensorNames.GetData(), &OrtInputTensors[0], InputTensorNames.Num(),
			OutputTensorNames.GetData(), &OrtOutputTensors[0], OutputTensorNames.Num());

		// At this (latest) stage shapes are known, therefore set them if not present yet and possibly copy data to output binding
		if (OutputTensors.IsEmpty())
		{
			check(NNE::Internal::FModelInstanceBase<ModelInterface>::OutputTensorShapes.IsEmpty());

			for (int32 i = 0; i < OutputTensorNames.Num(); i++)
			{
				const NNE::FTensorDesc& TensorDesc = NNE::Internal::FModelInstanceBase<ModelInterface>::OutputSymbolicTensors[i];
				const NNE::FTensorShape Shape = NNE::FTensorShape::Make(OrtHelper::GetShape(OrtOutputTensors[i]));
				const NNE::Internal::FTensor Tensor = NNE::Internal::FTensor::Make(TensorDesc.GetName(), Shape, TensorDesc.GetDataType());

				OutputTensors.Add(Tensor);
				NNE::Internal::FModelInstanceBase<ModelInterface>::OutputTensorShapes.Add(Shape);

				if (!InOutputBindings.IsEmpty() &&
					InOutputBindings[i].Data &&
					Tensor.GetDataSize() > 0 &&
					InOutputBindings[i].SizeInBytes >= Tensor.GetDataSize())
				{
					FMemory::Memcpy(InOutputBindings[i].Data, OrtOutputTensors[i].GetTensorData<void>(), Tensor.GetDataSize());
				}
			}
		}
	}
#if WITH_EDITOR
	catch (const Ort::Exception& Exception)
	{
		UE_LOG(LogNNERuntimeORT, Error, TEXT("%s"), UTF8_TO_TCHAR(Exception.what()));
		return ModelInterface::ERunSyncStatus::Fail;
	}
	catch (...)
	{
		UE_LOG(LogNNERuntimeORT, Error, TEXT("Unknown exception!"));
		return ModelInterface::ERunSyncStatus::Fail;
	}
#endif // WITH_EDITOR

	return ModelInterface::ERunSyncStatus::Ok;
}

TSharedPtr<NNE::IModelInstanceCPU> FModelORTCpu::CreateModelInstanceCPU()
{
	const FRuntimeConf RuntimeConfig = Detail::MakeRuntimeConfigFromSettings(GetDefault<UNNERuntimeORTSettings>());

	TSharedPtr<FModelInstanceORTCpu> ModelInstance = MakeShared<FModelInstanceORTCpu>(RuntimeConfig, Environment);
	if (!ModelInstance->Init(ModelData->GetView()))
	{
		return {};
	}

	return ModelInstance;
}

FModelORTCpu::FModelORTCpu(TSharedRef<FEnvironment> InEnvironment, TSharedRef<UE::NNE::FSharedModelData> InModelData) :
	Environment(InEnvironment), ModelData(InModelData)
{
}

bool FModelInstanceORTCpu::InitializedAndConfigureMembers()
{
	if (!FModelInstanceORTBase::InitializedAndConfigureMembers())
	{
		return false;
	}

	SessionOptions = CreateSessionOptionsDefault(Environment);
	if (!SessionOptions.IsValid())
	{
		return false;
	}

	SessionOptions->SetExecutionMode(RuntimeConf.ExecutionMode);
	SessionOptions->SetGraphOptimizationLevel(GetGraphOptimizationLevelForCPU(true));
	SessionOptions->EnableCpuMemArena();

	return true;
}

#if PLATFORM_WINDOWS
TSharedPtr<NNE::IModelInstanceGPU> FModelORTDmlGPU::CreateModelInstanceGPU()
{
	const FRuntimeConf RuntimeConfig = Detail::MakeRuntimeConfigFromSettings(GetDefault<UNNERuntimeORTSettings>());

	TSharedPtr<FModelInstanceORTDmlGPU> ModelInstance = MakeShared<FModelInstanceORTDmlGPU>(RuntimeConfig, Environment);
	if (!ModelInstance->Init(ModelData->GetView()))
	{
		return {};
	}

	return ModelInstance;
}

FModelORTDmlGPU::FModelORTDmlGPU(TSharedRef<FEnvironment> InEnvironment, TSharedRef<UE::NNE::FSharedModelData> InModelData) :
	Environment(InEnvironment), ModelData(InModelData)
{
}

bool FModelInstanceORTDmlGPU::InitializedAndConfigureMembers()
{
	if (!FModelInstanceORTBase::InitializedAndConfigureMembers())
	{
		return false;
	}

	SessionOptions = CreateSessionOptionsForDirectML(Environment, false);
	if (!SessionOptions.IsValid())
	{
		return false;
	}

	SessionOptions->SetGraphOptimizationLevel(GetGraphOptimizationLevelForDML(true));

	return true;
}

FModelORTDmlRDG::FModelORTDmlRDG(TSharedRef<FEnvironment> InEnvironment, TSharedRef<UE::NNE::FSharedModelData> InModelData) :
	Environment(InEnvironment), ModelData(InModelData)
{
}

TSharedPtr<NNE::IModelInstanceRDG> FModelORTDmlRDG::CreateModelInstanceRDG()
{
	const FRuntimeConf RuntimeConfig = Detail::MakeRuntimeConfigFromSettings(GetDefault<UNNERuntimeORTSettings>());

	TSharedPtr<FModelInstanceORTDmlRDG> ModelInstance = MakeShared<FModelInstanceORTDmlRDG>(ModelData, RuntimeConfig, Environment);
	if (!ModelInstance->Init())
	{
		return {};
	}

	return ModelInstance;
}

FModelInstanceORTDmlRDG::FModelInstanceORTDmlRDG(TSharedRef<UE::NNE::FSharedModelData> InModelData, const FRuntimeConf& InRuntimeConf, TSharedRef<FEnvironment> InEnvironment)
	:  ModelData(InModelData), RuntimeConf(InRuntimeConf), Environment(InEnvironment)
{}

FModelInstanceORTDmlRDG::~FModelInstanceORTDmlRDG()
{
	Session.Reset();
	if (!TempDirForModelWithExternalData.IsEmpty())
	{
		if (!IFileManager::Get().DeleteDirectory(*TempDirForModelWithExternalData, false, true))
		{
			UE_LOG(LogNNERuntimeORT, Warning, TEXT("Large models are an experimental feature at the moment. FModelInstanceORTDmlRDG could not delete temp directy %s on model instance destruction."), *TempDirForModelWithExternalData);
		}
	}
}

bool FModelInstanceORTDmlRDG::Init()
{
#if WITH_EDITOR
	try
#endif // WITH_EDITOR
	{
		Allocator = MakeUnique<Ort::AllocatorWithDefaultOptions>();

		SessionOptions = CreateSessionOptionsForDirectML(Environment);
		if (!SessionOptions.IsValid())
		{
			UE_LOG(LogNNERuntimeORT, Error, TEXT("Failed to configure session options for DirectML Execution Provider."));
			return false;
		}
		
		SessionOptions->SetGraphOptimizationLevel(GetGraphOptimizationLevelForDML(true));

		if (!Detail::CreateSession(ModelData->GetView(), *SessionOptions, *Environment, Session, TempDirForModelWithExternalData))
		{
			UE_LOG(LogNNERuntimeORT, Error, TEXT("Session creation failed."));
			return false;
		}

		if (!ConfigureTensors(*Session))
		{
			UE_LOG(LogNNERuntimeORT, Error, TEXT("Failed to configure Inputs tensors."));
			return false;
		}
	}
#if WITH_EDITOR
	catch (const Ort::Exception& Exception)
	{
		UE_LOG(LogNNERuntimeORT, Error, TEXT("%s"), UTF8_TO_TCHAR(Exception.what()));
		return false;
	}
	catch (...)
	{
		UE_LOG(LogNNERuntimeORT, Error, TEXT("Unknown exception!"));
		return false;
	}
#endif // WITH_EDITOR

	return true;
}

bool FModelInstanceORTDmlRDG::ConfigureTensors(const Ort::Session& ActiveSession)
{
	if (!ConfigureTensors(ActiveSession, true))
	{
		UE_LOG(LogNNERuntimeORT, Error, TEXT("Failed to configure Inputs tensors."));
		return false;
	}
	if (!ConfigureTensors(ActiveSession, false))
	{
		UE_LOG(LogNNERuntimeORT, Error, TEXT("Failed to configure Outputs tensors."));
		return false;
	}

	return true;
}

bool FModelInstanceORTDmlRDG::ConfigureTensors(const Ort::Session& ActiveSession, bool bAreTensorInputs)
{
	SCOPED_NAMED_EVENT_TEXT("FModelInstanceORTDmlRDG::ConfigureTensors", FColor::Magenta);

	const uint32 NumberTensors							= bAreTensorInputs ? ActiveSession.GetInputCount() : ActiveSession.GetOutputCount();
	TArray<NNE::FTensorDesc>& SymbolicTensorDescs		= bAreTensorInputs ? InputSymbolicTensors   : OutputSymbolicTensors;
	TArray<ONNXTensorElementDataType>& TensorsORTType	= bAreTensorInputs ? InputTensorsORTType	: OutputTensorsORTType;
	TArray<char*>& TensorNames							= bAreTensorInputs ? InputTensorNames		: OutputTensorNames;
	TArray<Ort::AllocatedStringPtr>& TensorNameValues	= bAreTensorInputs ? InputTensorNameValues	: OutputTensorNameValues;
	TArray<TArray<FString>>& SymbolicDimensionNames 	= bAreTensorInputs ? InputSymbolicDimensionNames : OutputSymbolicDimensionNames;

	SymbolicTensorDescs.Reset();
	TensorsORTType.Reset();
	TensorNameValues.Reset();
	TensorNames.Reset();
	SymbolicDimensionNames.SetNum(NumberTensors);

	for (uint32 TensorIndex = 0; TensorIndex < NumberTensors; ++TensorIndex)
	{
		// Get Tensor name
		Ort::AllocatedStringPtr CurTensorName = bAreTensorInputs ? ActiveSession.GetInputNameAllocated(TensorIndex, *Allocator) : ActiveSession.GetOutputNameAllocated(TensorIndex, *Allocator);
		TensorNameValues.Emplace(MoveTemp(CurTensorName));
		TensorNames.Emplace(TensorNameValues.Last().get());

		// Get node type
		const Ort::TypeInfo CurrentTypeInfo = bAreTensorInputs ? ActiveSession.GetInputTypeInfo(TensorIndex) : ActiveSession.GetOutputTypeInfo(TensorIndex);
		const Ort::ConstTensorTypeAndShapeInfo CurrentTensorInfo = CurrentTypeInfo.GetTensorTypeAndShapeInfo();
		const ONNXTensorElementDataType ONNXTensorElementDataTypeEnum = CurrentTensorInfo.GetElementType();
		const TypeInfoORT TypeInfo = TranslateTensorTypeORTToNNE(ONNXTensorElementDataTypeEnum);

		// Get dynamic shape dimension names
		TUniquePtr<const char*[]> SymbolidDimensionNames = MakeUnique<const char*[]>(CurrentTensorInfo.GetShape().size());
		CurrentTensorInfo.GetSymbolicDimensions(SymbolidDimensionNames.Get(), CurrentTensorInfo.GetShape().size());

		TArray<FString>& CurrentSymbolicDimensionNames = SymbolicDimensionNames[TensorIndex];
		CurrentSymbolicDimensionNames.SetNum(CurrentTensorInfo.GetShape().size());

		for (int32 i = 0; i < CurrentTensorInfo.GetShape().size(); i++)
		{
			CurrentSymbolicDimensionNames[i] = FString((SymbolidDimensionNames.Get())[i]);
		}

		TensorsORTType.Emplace(ONNXTensorElementDataTypeEnum);

		// Get shape
		TArray<int32, TInlineAllocator<NNE::FTensorShape::MaxRank>> ShapeData;
		ShapeData.Reserve(CurrentTensorInfo.GetShape().size());
		for (int64 CurrentTensorSize : CurrentTensorInfo.GetShape())
		{
			ShapeData.Add((int32)CurrentTensorSize);
		}

		const NNE::FSymbolicTensorShape Shape = NNE::FSymbolicTensorShape::Make(ShapeData);
		const NNE::FTensorDesc SymbolicTensorDesc = NNE::FTensorDesc::Make(FString(TensorNames.Last()), Shape, TypeInfo.DataType);

		check(SymbolicTensorDesc.GetElementByteSize() == TypeInfo.ElementSize);
		SymbolicTensorDescs.Emplace(SymbolicTensorDesc);
	}

	return true;
}

FModelInstanceORTDmlRDG::ESetInputTensorShapesStatus FModelInstanceORTDmlRDG::SetInputTensorShapes(TConstArrayView<NNE::FTensorShape> InInputShapes)
{
	SCOPED_NAMED_EVENT_TEXT("FModelInstanceORTDmlRDG::SetInputTensorShapes", FColor::Magenta);

	InputTensors.Reset();
	OutputTensors.Reset();
	OutputTensorShapes.Reset();

	// Verify input shape are valid for the model and set InputTensorShapes
	if (ESetInputTensorShapesStatus Status = NNE::Internal::FModelInstanceBase<NNE::IModelInstanceRDG>::SetInputTensorShapes(InInputShapes); Status != ESetInputTensorShapesStatus::Ok)
	{
		return Status;
	}

	// Check whether all input tensor shapes are concrete
	bool bHasSymbolicInputShapes = false;
	for (int32 i = 0; i < InputSymbolicTensors.Num(); i++)
	{
		if (!InputSymbolicTensors[i].GetShape().IsConcrete())
		{
			bHasSymbolicInputShapes = true;

			break;
		}
	}

	if (!bHasSymbolicInputShapes)
	{
		for (int32 i = 0; i < InputSymbolicTensors.Num(); i++)
		{
			NNE::Internal::FTensor Tensor = NNE::Internal::FTensor::Make(InputSymbolicTensors[i].GetName(), InInputShapes[i], InputSymbolicTensors[i].GetDataType());
			InputTensors.Emplace(Tensor);
		}

		// All output shapes need to be concrete now
		for (int32 i = 0; i < OutputSymbolicTensors.Num(); i++)
		{
			const NNE::FTensorDesc SymbolicTensorDesc = OutputSymbolicTensors[i];

			if (SymbolicTensorDesc.GetShape().IsConcrete())
			{
				NNE::Internal::FTensor Tensor = NNE::Internal::FTensor::MakeFromSymbolicDesc(SymbolicTensorDesc);
				OutputTensors.Emplace(Tensor);
				OutputTensorShapes.Emplace(Tensor.GetShape());
			}
			else
			{
				UE_LOG(LogNNERuntimeORT, Warning, TEXT("One or more output tensors contain free dimensions, but input tensors are all concrete!"));
				return ESetInputTensorShapesStatus::Fail;
			}
		}

		return ESetInputTensorShapesStatus::Ok;
	}

	// Recreate session options because potentially we add new free dimension overrides
	SessionOptions = CreateSessionOptionsForDirectML(Environment);
	if (!SessionOptions.IsValid())
	{
		UE_LOG(LogNNERuntimeORT, Error, TEXT("Failed to recreate session options!"));
		return ESetInputTensorShapesStatus::Fail;
	}

	SessionOptions->SetGraphOptimizationLevel(GetGraphOptimizationLevelForDML(true));

	// Setup concrete input tensors
	for (int32 i = 0; i < InputSymbolicTensors.Num(); i++)
	{
		NNE::Internal::FTensor Tensor = NNE::Internal::FTensor::Make(InputSymbolicTensors[i].GetName(), InInputShapes[i], InputSymbolicTensors[i].GetDataType());
		InputTensors.Emplace(Tensor);

		const NNE::FSymbolicTensorShape& SymbolicInputShape = InputSymbolicTensors[i].GetShape();

		// Override free dimensions of input tensors
		if (!SymbolicInputShape.IsConcrete())
		{
			check(InputTensorShapes[i].IsCompatibleWith(SymbolicInputShape));

			TConstArrayView<int32> InputSymbolicShapeData = SymbolicInputShape.GetData();
			TConstArrayView<uint32> InputShapeData = InputTensorShapes[i].GetData();

			for (int32 j = 0; j < InputShapeData.Num(); j++)
			{
				if (InputSymbolicShapeData[j] < 0)
				{
					Ort::GetApi().AddFreeDimensionOverrideByName(*SessionOptions, TCHAR_TO_ANSI(*InputSymbolicDimensionNames[i][j]), InputShapeData[j]);
				}
			}
		}
	}

	if (!Detail::CreateSession(ModelData->GetView(), *SessionOptions, *Environment, Session, TempDirForModelWithExternalData))
	{
		UE_LOG(LogNNERuntimeORT, Error, TEXT("Failed to recreate session!"));
		return ESetInputTensorShapesStatus::Fail;
	}

	// Need to configure output tensors with new session (to apply free dimension overrides)
	if (!ConfigureTensors(*Session, false))
	{
		UE_LOG(LogNNERuntimeORT, Error, TEXT("Failed to configure tensors!"));
		return ESetInputTensorShapesStatus::Fail;
	}

	// All output shapes need to be concrete now
	for (int32 i = 0; i < OutputSymbolicTensors.Num(); i++)
	{
		const NNE::FTensorDesc SymbolicTensorDesc = OutputSymbolicTensors[i];

		if (SymbolicTensorDesc.GetShape().IsConcrete())
		{
			NNE::Internal::FTensor Tensor = NNE::Internal::FTensor::MakeFromSymbolicDesc(SymbolicTensorDesc);
			OutputTensors.Emplace(Tensor);
			OutputTensorShapes.Emplace(Tensor.GetShape());
		}
		else
		{
			for (int32 j = 0; j < SymbolicTensorDesc.GetShape().Rank(); j++)
			{
				if (SymbolicTensorDesc.GetShape().GetData()[j] < 0)
				{
					UE_LOG(LogNNERuntimeORT, Warning, TEXT("Tensor '%hs' has free dimension '%s'."), OutputTensorNames[i], *OutputSymbolicDimensionNames[i][j]);
				}
			}

			UE_LOG(LogNNERuntimeORT, Error, TEXT("One or more output tensors contain free dimensions!"));
			return ESetInputTensorShapesStatus::Fail;
		}
	}

	return ESetInputTensorShapesStatus::Ok;
}

Ort::Value CreateTensor(const OrtDmlApi& DmlApi, const Ort::MemoryInfo& MemoryInfo, FRHIBuffer* Buffer, const NNE::Internal::FTensor& Tensor, ONNXTensorElementDataType ElementDataType,
	TArray<std::unique_ptr<void, void (*)(void*)>>& DmlAllocatorResources)
{
	checkf(Buffer, TEXT("CreateTensor needs Buffer to be set"));

	ID3D12Resource* NativeD3D12Resource = GetID3D12DynamicRHI()->RHIGetResource(Buffer);

	void* DmlAllocatorResourcePtr;
	Ort::ThrowOnError(DmlApi.CreateGPUAllocationFromD3DResource(NativeD3D12Resource, &DmlAllocatorResourcePtr));

	std::unique_ptr<void, void (*)(void*)> DmlAllocatorResource(DmlAllocatorResourcePtr,
		[] (void* Ptr)
	{
		const OrtDmlApi* DmlApi;
		Ort::ThrowOnError(Ort::GetApi().GetExecutionProviderApi("DML", ORT_API_VERSION, reinterpret_cast<const void**>(&DmlApi)));

		DmlApi->FreeGPUAllocation(Ptr);
	});

	uint64 SizeInBytes = static_cast<uint64>(NativeD3D12Resource->GetDesc().Width);

	TUniquePtr<int64_t[]> Shape = MakeUnique<int64_t[]>(Tensor.GetShape().Rank());
	for (int32 i = 0; i < Tensor.GetShape().Rank(); ++i)
	{
		Shape.Get()[i] = Tensor.GetShape().GetData()[i];
	}
	const uint32 ShapeLen{ (uint32)Tensor.GetShape().Rank() };

	Ort::Value Result = Ort::Value::CreateTensor(MemoryInfo, DmlAllocatorResource.get(), SizeInBytes, Shape.Get(), ShapeLen, ElementDataType);

	DmlAllocatorResources.Add(MoveTemp(DmlAllocatorResource));

	return Result;
}

FModelInstanceORTDmlRDG::EEnqueueRDGStatus FModelInstanceORTDmlRDG::EnqueueRDG(FRDGBuilder& GraphBuilder, TConstArrayView<NNE::FTensorBindingRDG> Inputs, TConstArrayView<NNE::FTensorBindingRDG> Outputs)
{
	SCOPED_NAMED_EVENT_TEXT("FModelInstanceORTDmlRDG::EnqueueRDG", FColor::Magenta);

	if (!Session.IsValid())
	{
		UE_LOG(LogNNERuntimeORT, Error, TEXT("Invalid Session, Init() should have been called."));
		return EEnqueueRDGStatus::Fail;
	}

	if (InputTensorShapes.IsEmpty())
	{
		UE_LOG(LogNNERuntimeORT, Error, TEXT("Input shapes are not set, please call SetInputTensorShapes."));
		return EEnqueueRDGStatus::Fail;
	}

	check(InputTensorShapes.Num() == InputTensors.Num());
	check(InputTensorShapes.Num() == InputTensorNames.Num());
	check(InputSymbolicTensors.Num() == InputTensors.Num());

	if (Inputs.Num() != InputTensors.Num())
	{
		UE_LOG(LogNNERuntimeORT, Error, TEXT("Input bindings need to match input tensor descriptor count (got %d, expected %d)."), Inputs.Num(), InputTensors.Num());
		return EEnqueueRDGStatus::Fail;
	}

	check(OutputSymbolicTensors.Num() == OutputTensorNames.Num());

	if (!Outputs.IsEmpty() && Outputs.Num() != OutputTensorNames.Num())
	{
		UE_LOG(LogNNERuntimeORT, Error, TEXT("Output binding can be empty or needs to match output tensor descriptor count (got %d, expected %d)."), Outputs.Num(), OutputTensorNames.Num());
		return EEnqueueRDGStatus::Fail;
	}

	FORTModelInstanceRDGParameters* PassParameters = GraphBuilder.AllocParameters<FORTModelInstanceRDGParameters>();
	for (int32 i = 0; i < Inputs.Num(); i++)
	{
		const NNE::FTensorBindingRDG& Binding = Inputs[i];
		if (!Binding.Buffer && InputTensors[i].GetDataSize() != 0)
		{
			UE_LOG(LogNNERuntimeORT, Error, TEXT("Binding input tensor %d is not set but given size by tensor descriptor is non-zero %d."), i, InputTensors[i].GetDataSize());
			return EEnqueueRDGStatus::Fail;
		}

		const uint64 DmlImpliedSizeBytes = CalcRDGBufferSizeForDirectML(InputTensors[i].GetDataSize());
		if (Binding.Buffer && Binding.Buffer->Desc.GetSize() != DmlImpliedSizeBytes)
		{
			UE_LOG(LogNNERuntimeORT, Error, TEXT("Binding input tensor %d size does not match tensor buffer size required by DirectML (got %d, expected %d, data size was %d)."), i, Binding.Buffer->Desc.GetSize(), DmlImpliedSizeBytes, InputTensors[i].GetDataSize());
			return EEnqueueRDGStatus::Fail;
		}

		PassParameters->InputBuffers.Emplace(Binding.Buffer, ERHIAccess::CopySrc);
	}

	TArray<int32> ValidOutputs;
	for (int32 i = 0; i < Outputs.Num(); i++)
	{
		const NNE::FTensorBindingRDG& Binding = Outputs[i];

		const uint64 DmlImpliedSizeBytes = CalcRDGBufferSizeForDirectML(OutputTensors[i].GetDataSize());
		if (Binding.Buffer && Binding.Buffer->Desc.GetSize() != DmlImpliedSizeBytes)
		{
			UE_LOG(LogNNERuntimeORT, Error, TEXT("Binding output tensor %d size does not match tensor buffer size required by DirectML (got %d, expected %d, data size was %d)."), i, Binding.Buffer->Desc.GetSize(), DmlImpliedSizeBytes, OutputTensors[i].GetDataSize());
			return EEnqueueRDGStatus::Fail;
		}

		PassParameters->OutputBuffers.Emplace(Binding.Buffer, ERHIAccess::CopyDest);
		ValidOutputs.Add(i);
	}

	RDG_EVENT_SCOPE_STAT(GraphBuilder, FNNERuntimeORTDmlRDG, "FModelInstanceORTDmlRDG::EnqueueRDG");
	RDG_GPU_STAT_SCOPE(GraphBuilder, FNNERuntimeORTDmlRDG);

	GraphBuilder.AddPass(RDG_EVENT_NAME("FModelInstanceORTDmlRDG::EnqueueRDG.AddPass"), PassParameters, ERDGPassFlags::Readback,
	[this, ValidOutputsCopy = ValidOutputs, PassParameters](FRHICommandListImmediate& RHICmdList)
	{
		SCOPED_NAMED_EVENT_TEXT("FModelInstanceORTDmlRDG::EnqueueRDG.AddPass", FColor::Magenta);

		TArray<FRHIBuffer*> InputBuffers;
		InputBuffers.SetNumUninitialized(PassParameters->InputBuffers.Num());
		for (int32 i = 0; i < PassParameters->InputBuffers.Num(); i++)
		{
			InputBuffers[i] = PassParameters->InputBuffers[i]->GetRHI();
		}

		check(ValidOutputsCopy.Num() == PassParameters->OutputBuffers.Num());

		TArray<FRHIBuffer*> OutputBuffers;
		OutputBuffers.SetNumZeroed(OutputSymbolicTensors.Num());
		for (int32 i = 0; i < ValidOutputsCopy.Num(); i++)
		{
			OutputBuffers[ValidOutputsCopy[i]] = PassParameters->OutputBuffers[i]->GetRHI();
		}

		// Submit previous work here to the GPU to avoid ORT Session Run() dispatching its work first
		RHICmdList.ImmediateFlush(EImmediateFlushType::DispatchToRHIThread);

		RHICmdList.EnqueueLambda([this, InputBuffersCopy = InputBuffers, OutputBuffersCopy = OutputBuffers](FRHICommandListImmediate& RHICmdList)
		{
			GetID3D12PlatformDynamicRHI()->RHIRunOnQueue(ED3D12RHIRunOnQueueType::Graphics, [this, InputBuffersCopyCopy = InputBuffersCopy, OutputBuffersCopyCopy = OutputBuffersCopy](ID3D12CommandQueue* D3D12CommandQueue)
			{
#if WITH_EDITOR
				try
#endif // WITH_EDITOR
				{
					const OrtDmlApi* DmlApi;
					Ort::ThrowOnError(Ort::GetApi().GetExecutionProviderApi("DML", ORT_API_VERSION, reinterpret_cast<const void**>(&DmlApi)));
					
					Ort::MemoryInfo MemoryInfo("DML", OrtAllocatorType::OrtDeviceAllocator, 0, OrtMemType::OrtMemTypeDefault);
					
					TArray<std::unique_ptr<void, void (*)(void*)>> DmlAllocatorResources;
					TArray<Ort::Value> OrtInputTensors;
					TArray<Ort::Value> OrtOutputTensors;

					for (int32 i = 0; i < InputBuffersCopyCopy.Num(); i++)
					{
						OrtInputTensors.Add(CreateTensor(*DmlApi, MemoryInfo, InputBuffersCopyCopy[i], InputTensors[i], InputTensorsORTType[i], DmlAllocatorResources));
					}
					for (int32 i = 0; i < OutputBuffersCopyCopy.Num(); i++)
					{
						if (OutputBuffersCopyCopy[i])
						{
							OrtOutputTensors.Add(CreateTensor(*DmlApi, MemoryInfo, OutputBuffersCopyCopy[i], OutputTensors[i], OutputTensorsORTType[i], DmlAllocatorResources));
						}
						else
						{
							OrtOutputTensors.Emplace(nullptr);
						}
					}

					Session->Run(Ort::RunOptions{ nullptr },
						InputTensorNames.GetData(), &OrtInputTensors[0], InputTensorNames.Num(),
						OutputTensorNames.GetData(), &OrtOutputTensors[0], OutputTensorNames.Num());
				}
#if WITH_EDITOR
				catch (const Ort::Exception& Exception)
				{
					UE_LOG(LogNNERuntimeORT, Error, TEXT("ORT Exception: %s"), UTF8_TO_TCHAR(Exception.what()));
				}
				catch (...)
				{
					UE_LOG(LogNNERuntimeORT, Error, TEXT("ORT Exception: Unknown!"));
				}
#endif // WITH_EDITOR
			}, false);
		});
	});

	return EEnqueueRDGStatus::Ok;
}

TSharedPtr<NNE::IModelInstanceNPU> FModelORTNpu::CreateModelInstanceNPU()
{
	const FRuntimeConf RuntimeConfig = Detail::MakeRuntimeConfigFromSettings(GetDefault<UNNERuntimeORTSettings>());

	TSharedPtr<FModelInstanceORTNpu> ModelInstance = MakeShared<FModelInstanceORTNpu>(RuntimeConfig, Environment);
	if (!ModelInstance->Init(ModelData->GetView()))
	{
		return {};
	}

	return ModelInstance;
}

FModelORTNpu::FModelORTNpu(TSharedRef<FEnvironment> InEnvironment, TSharedRef<UE::NNE::FSharedModelData> InModelData) :
	Environment(InEnvironment), ModelData(InModelData)
{
}

bool FModelInstanceORTNpu::InitializedAndConfigureMembers()
{
	if (!FModelInstanceORTBase::InitializedAndConfigureMembers())
	{
		return false;
	}

	SessionOptions = CreateSessionOptionsForDirectMLNpu(Environment);
	if (!SessionOptions.IsValid())
	{
		return false;
	}

	SessionOptions->SetExecutionMode(RuntimeConf.ExecutionMode);
	SessionOptions->SetGraphOptimizationLevel(GetGraphOptimizationLevelForDML(true));

	return true;
}
#endif //PLATFORM_WINDOWS
	
} // namespace UE::NNERuntimeORT::Private