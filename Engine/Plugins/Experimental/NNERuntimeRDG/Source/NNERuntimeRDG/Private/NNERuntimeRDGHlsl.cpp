// Copyright Epic Games, Inc. All Rights Reserved.

#include "NNERuntimeRDGHlsl.h"

#include "Misc/SecureHash.h"
#include "NNEAttributeMap.h"
#include "NNEHlslShadersLog.h"
#include "NNEModelData.h"
#include "NNEModelOptimizerInterface.h"
#include "NNERuntimeRDG.h"
#include "NNERuntimeRDGHlslHelper.h"
#include "NNERuntimeRDGModelHlsl.h"
#ifdef NNE_UTILITIES_AVAILABLE
#include "NNERuntimeRDGUtilsModelOptimizer.h"
#endif // NNE_UTILITIES_AVAILABLE
#include "HAL/IConsoleManager.h"
#include "Hlsl/NNERuntimeRDGBatchNormalization.h"
#include "Hlsl/NNERuntimeRDGCast.h"
#include "Hlsl/NNERuntimeRDGConv.h"
#include "Hlsl/NNERuntimeRDGConcat.h"
#include "Hlsl/NNERuntimeRDGConstant.h"
#include "Hlsl/NNERuntimeRDGConvTranspose.h"
#include "Hlsl/NNERuntimeRDGCumSum.h"
#include "Hlsl/NNERuntimeRDGDepthToSpace.h"
#include "Hlsl/NNERuntimeRDGDropout.h"
#include "Hlsl/NNERuntimeRDGElementWiseBinary.h"
#include "Hlsl/NNERuntimeRDGElementWiseUnary.h"
#include "Hlsl/NNERuntimeRDGElementWiseVariadic.h"
#include "Hlsl/NNERuntimeRDGFlatten.h"
#include "Hlsl/NNERuntimeRDGGather.h"
#include "Hlsl/NNERuntimeRDGGemm.h"
#include "Hlsl/NNERuntimeRDGGlobalPool.h"
#include "Hlsl/NNERuntimeRDGIdentity.h"
#include "Hlsl/NNERuntimeRDGInstanceNormalization.h"
#include "Hlsl/NNERuntimeRDGLayerNormalization.h"
#include "Hlsl/NNERuntimeRDGGatherElements.h"
#include "Hlsl/NNERuntimeRDGPad.h"
#include "Hlsl/NNERuntimeRDGPool.h"
#include "Hlsl/NNERuntimeRDGReduce.h"
#include "Hlsl/NNERuntimeRDGResize.h"
#include "Hlsl/NNERuntimeRDGReshape.h"
#include "Hlsl/NNERuntimeRDGScatterND.h"
#include "Hlsl/NNERuntimeRDGShape.h"
#include "Hlsl/NNERuntimeRDGSize.h"
#include "Hlsl/NNERuntimeRDGSlice.h"
#include "Hlsl/NNERuntimeRDGSplit.h"
#include "Hlsl/NNERuntimeRDGSoftmax.h"
#include "Hlsl/NNERuntimeRDGSqueeze.h"
#include "Hlsl/NNERuntimeRDGTranspose.h"
#include "Hlsl/NNERuntimeRDGUnsqueeze.h"
#include "Hlsl/NNERuntimeRDGUpsample.h"
#include "Hlsl/NNERuntimeRDGMatMul.h"

using namespace UE::NNERuntimeRDG::Private::Hlsl;

FGuid UNNERuntimeRDGHlslImpl::GUID = FGuid((int32)'R', (int32)'D', (int32)'G', (int32)'H');
int32 UNNERuntimeRDGHlslImpl::Version = 0x00000007;

bool UNNERuntimeRDGHlslImpl::Init()
{
	FOperatorRegistryHlsl* Registry = FOperatorRegistryHlsl::Get();
	check(Registry != nullptr);

	RegisterBatchNormalizationOperator(*Registry);
	RegisterCastOperator(*Registry);
	RegisterConvOperator(*Registry);
	RegisterConcatOperator(*Registry);
	RegisterConstantOperator(*Registry);
	RegisterConvTransposeOperator(*Registry);
	RegisterCumSumOperator(*Registry);
	RegisterDepthToSpaceOperator(*Registry);
	RegisterDropoutOperator(*Registry);
	RegisterElementWiseBinaryOperators(*Registry);
	RegisterElementWiseUnaryOperators(*Registry);
	RegisterElementWiseVariadicOperators(*Registry);
	RegisterFlattenOperator(*Registry);
	RegisterGatherOperator(*Registry);
	RegisterGemmOperator(*Registry);
	RegisterGlobalPoolOperators(*Registry);
	RegisterIdentityOperator(*Registry);
	RegisterInstanceNormalizationOperator(*Registry);
	RegisterLayerNormalizationOperator(*Registry);
	RegisterGatherElementsOperator(*Registry);
	RegisterPadOperator(*Registry);
	RegisterPoolOperators(*Registry);
	RegisterReduceOperators(*Registry);
	RegisterReshapeOperator(*Registry);
	RegisterResizeOperator(*Registry);
	RegisterScatterNDOperator(*Registry);
	RegisterShapeOperator(*Registry);
	RegisterSizeOperator(*Registry);
	RegisterSliceOperator(*Registry);
	RegisterSplitOperator(*Registry);
	RegisterSoftmaxOperator(*Registry);
	RegisterSqueezeOperator(*Registry);
	RegisterTransposeOperator(*Registry);
	RegisterUnsqueezeOperator(*Registry);
	RegisterUpsampleOperator(*Registry);
	RegisterMatMulOperator(*Registry);

	return true;
}

namespace UE::NNERuntimeRDG::Private::Hlsl
{
	namespace ConsoleCommands
	{
		static FAutoConsoleCommand GetAutomationRuntimeFilterCommand(
			TEXT("nne.hlsl.getoperatorsupportmatrix"), TEXT("Get the NNERuntimeRDGHlsl operators support matrix in term of ONNX."),
			FConsoleCommandWithArgsDelegate::CreateStatic(
				[](const TArray< FString >& Args)
				{
					FOperatorRegistryHlsl* Registry = FOperatorRegistryHlsl::Get();
					check(Registry != nullptr);
					FString SupportMatrix = Registry->ListAllRegisteredOperators();
					UE_LOG(LogNNERuntimeRDGHlsl, Display, TEXT("Operators support matrix: \n%s"), *SupportMatrix);
				}
			)
		);
	} // namespace ConsoleCommands

	namespace Details
	{
		UNNERuntimeRDGHlslImpl::ECanCreateModelDataStatus CheckCanCreateModelData(bool bShouldLog, const FString& FileType, TConstArrayView64<uint8> FileData, const TMap<FString, TConstArrayView64<uint8>>& AdditionalFileData, const FGuid& FileId, const ITargetPlatform* TargetPlatform)
		{
#ifdef NNE_UTILITIES_AVAILABLE
			if (FileType.Compare("onnx", ESearchCase::IgnoreCase) != 0)
			{
				if (bShouldLog)
				{
					UE_LOG(LogNNERuntimeRDGHlsl, Error, TEXT("Cannot create the model data with id %s (Filetype: %s), Only 'onnx' file type is supported"), *FileId.ToString(EGuidFormats::Digits).ToLower(), *FileType);
				}
				return UNNERuntimeRDGHlslImpl::ECanCreateModelDataStatus::Fail;
			}

			// Check model is not > 2GB
			if ((TArray<uint8>::SizeType)FileData.Num() != FileData.Num())
			{
				if (bShouldLog)
				{
					UE_LOG(LogNNERuntimeRDGHlsl, Error, TEXT("Cannot create the model data with id %s (Filetype: %s), models > 2GBs are not supported"), *FileId.ToString(EGuidFormats::Digits).ToLower(), *FileType);
				}
				return UNNERuntimeRDGHlslImpl::ECanCreateModelDataStatus::Fail;
			}

			if (!AdditionalFileData.IsEmpty())
			{
				if (bShouldLog)
				{
					UE_LOG(LogNNERuntimeRDGHlsl, Error, TEXT("Cannot create the model data with id %s (Filetype: %s), external data not supported at the moment, please convert the model to internal storage. See https://onnx.ai/onnx/repo-docs/ExternalData.html"), *FileId.ToString(EGuidFormats::Digits).ToLower(), *FileType);
				}
				return UNNERuntimeRDGHlslImpl::ECanCreateModelDataStatus::Fail;
			}

			return UNNERuntimeRDGHlslImpl::ECanCreateModelDataStatus::Ok;
#else
			if (bShouldLog)
			{
				UE_LOG(LogNNERuntimeRDGHlsl, Error, TEXT("Cannot create the model data with id %s (Filetype: %s), NNERuntimeRDGUtils is not available on this platform"), *FileId.ToString(EGuidFormats::Digits).ToLower(), *FileType);
			}
			return UNNERuntimeRDGHlslImpl::ECanCreateModelDataStatus::Fail;
#endif
		}
	} // namespace Details

} // namespace UE::NNERuntimeRDG::Private::Hlsl


bool UNNERuntimeRDGHlslImpl::IsCurrentPlatformSupported()
{
	bool bResult = true;

#ifndef NNE_FORCE_HARDWARE_SUPPORTS_HLSL
	if(GMaxRHIFeatureLevel < ERHIFeatureLevel::SM5)
	{
		UE_LOG(LogNNERuntimeRDGHlsl, Display, TEXT("Minimum feature level required is SM5 for current RHI platform."));
		bResult = false;
	}

	if(!GRHISupportsWaveOperations)
	{
		UE_LOG(LogNNERuntimeRDGHlsl, Display, TEXT("Current RHI platform doesn't support wave operations."));
		bResult = false;
	}

	if(!GRHIGlobals.SupportsNative16BitOps)
	{
		UE_LOG(LogNNERuntimeRDGHlsl, Display, TEXT("Current RHI platform doesn't support native 16-bit operations."));
		bResult = false;
	}
#endif

	return bResult;
}

UNNERuntimeRDGHlslImpl::ECanCreateModelDataStatus UNNERuntimeRDGHlslImpl::CanCreateModelData(const FString& FileType, TConstArrayView64<uint8> FileData, const TMap<FString, TConstArrayView64<uint8>>& AdditionalFileData, const FGuid& FileId, const ITargetPlatform* TargetPlatform) const
{
	return Details::CheckCanCreateModelData(/*bShouldLog*/ false , FileType, FileData, AdditionalFileData, FileId, TargetPlatform);
}

UNNERuntimeRDGHlslImpl::ECanCreateModelRDGStatus UNNERuntimeRDGHlslImpl::CanCreateModelRDG(const TObjectPtr<UNNEModelData> ModelData) const
{
	int32 GuidSize = sizeof(GUID);
	int32 VersionSize = sizeof(Version);
	TSharedPtr<UE::NNE::FSharedModelData> SharedData = ModelData->GetModelData(GetRuntimeName());

	if (!SharedData.IsValid())
	{
		return ECanCreateModelRDGStatus::Fail;
	}

	TConstArrayView64<uint8> Data = SharedData->GetView();

	if (Data.Num() <= GuidSize + VersionSize)
	{
		return ECanCreateModelRDGStatus::Fail;
	}
	bool bResult = FGenericPlatformMemory::Memcmp(&(Data[0]), &(GUID), GuidSize) == 0;
	bResult &= FGenericPlatformMemory::Memcmp(&(Data[GuidSize]), &(Version), VersionSize) == 0;

	bResult &= IsCurrentPlatformSupported();

	return bResult ? ECanCreateModelRDGStatus::Ok : ECanCreateModelRDGStatus::Fail;
};

TSharedPtr<UE::NNE::FSharedModelData> UNNERuntimeRDGHlslImpl::CreateModelData(const FString& FileType, TConstArrayView64<uint8> FileData, const TMap<FString, TConstArrayView64<uint8>>& AdditionalFileData, const FGuid& FileId, const ITargetPlatform* TargetPlatform)
{
	if (Details::CheckCanCreateModelData(/*bShouldLog*/ true, FileType, FileData, AdditionalFileData, FileId, TargetPlatform) != ECanCreateModelDataStatus::Ok)
	{
		return {};
	}

#ifdef NNE_UTILITIES_AVAILABLE
	TUniquePtr<UE::NNE::Internal::IModelOptimizer> Optimizer = UE::NNERuntimeRDGUtils::Internal::CreateONNXToNNEModelOptimizer();
	Optimizer->AddValidator(MakeShared<FModelValidatorHlsl>());

	FNNEModelRaw InputModel;
	InputModel.Data = FileData;
	InputModel.Format = ENNEInferenceFormat::ONNX;

	FNNEModelRaw OutputModel;
	if (!Optimizer->Optimize(InputModel, OutputModel, {}))
	{
		return {};
	}

	TArray64<uint8> Result;
	FMemoryWriter64 Writer(Result);
	
	Writer << GUID;
	Writer << Version;
	Writer.Serialize(OutputModel.Data.GetData(), OutputModel.Data.Num());

	return MakeShared<UE::NNE::FSharedModelData>(MakeSharedBufferFromArray(MoveTemp(Result)), 0);
#else //NNE_UTILITIES_AVAILABLE
	return {};
#endif //NNE_UTILITIES_AVAILABLE
};

FString UNNERuntimeRDGHlslImpl::GetModelDataIdentifier(const FString& FileType, TConstArrayView64<uint8> FileData, const TMap<FString, TConstArrayView64<uint8>>& AdditionalFileData, const FGuid& FileId, const ITargetPlatform* TargetPlatform) const
{
	return FileId.ToString(EGuidFormats::Digits) + "-" + UNNERuntimeRDGHlslImpl::GUID.ToString(EGuidFormats::Digits) + "-" + FString::FromInt(UNNERuntimeRDGHlslImpl::Version);
}

TSharedPtr<UE::NNE::IModelRDG> UNNERuntimeRDGHlslImpl::CreateModelRDG(const TObjectPtr<UNNEModelData> ModelData)
{
	if (CanCreateModelRDG(ModelData) != ECanCreateModelRDGStatus::Ok)
	{
		UE_LOG(LogNNERuntimeRDGHlsl, Warning, TEXT("Cannot create a model from the model data with id %s"), *ModelData->GetFileId().ToString(EGuidFormats::Digits));
		return TSharedPtr<UE::NNE::IModelRDG>();
	}

	TSharedPtr<UE::NNE::FSharedModelData> Data = ModelData->GetModelData(GetRuntimeName());
	check(Data.IsValid());
	UE::NNERuntimeRDG::Private::Hlsl::FModel* Model = new UE::NNERuntimeRDG::Private::Hlsl::FModel(Data);

	return TSharedPtr<UE::NNE::IModelRDG>(Model);
}
