// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "NNERuntimeRDGUtilsModelOptimizerBase.h"

namespace UE::NNERuntimeRDGUtils::Internal
{

class FModelOptimizerONNXToNNERT : public FModelOptimizerBase
{
public:
	virtual FString GetName() const override 
	{
		return TEXT("NNEModelOptimizerONNXToNNE");
	}

	virtual bool Optimize(const FNNEModelRaw& InputModel, FNNEModelRaw& OptimizedModel, const FOptimizerOptionsMap& Options) override;
};

} // namespace UE::NNERuntimeRDGUtils::Internal
