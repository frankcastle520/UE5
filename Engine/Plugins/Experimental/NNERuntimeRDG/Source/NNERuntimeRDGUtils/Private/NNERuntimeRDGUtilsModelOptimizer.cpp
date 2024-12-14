// Copyright Epic Games, Inc. All Rights Reserved.

#include "NNERuntimeRDGUtilsModelOptimizer.h"

#include "NNERuntimeRDGUtilsModelOptimizerNNE.h"
#include "NNERuntimeRDGUtilsModelOptimizerONNX.h"

namespace UE::NNERuntimeRDGUtils::Internal
{

TUniquePtr<NNE::Internal::IModelOptimizer> CreateModelOptimizer(ENNEInferenceFormat InputFormat, ENNEInferenceFormat OutputFormat)
{
	if (InputFormat == ENNEInferenceFormat::ONNX)
	{
		if (OutputFormat == ENNEInferenceFormat::NNERT)
		{
			return MakeUnique<FModelOptimizerONNXToNNERT>();
		}
		else if (OutputFormat == ENNEInferenceFormat::ONNX)
		{
			return MakeUnique<FModelOptimizerONNXToONNX>();
		}
		else
		{
			return MakeUnique<FModelOptimizerONNXToORT>();
		}
	}

	return nullptr;
}

} // namespace UE::NNERuntimeRDGUtils::Internal
