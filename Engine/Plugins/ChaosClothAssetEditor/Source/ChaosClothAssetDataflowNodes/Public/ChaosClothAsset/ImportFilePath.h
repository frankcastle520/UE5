// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Delegates/DelegateCombinations.h"
#include "ImportFilePath.generated.h"

namespace UE::Dataflow
{
	class FContext;
}

PRAGMA_DISABLE_DEPRECATION_WARNINGS  // For bForceReimport
USTRUCT()
struct FChaosClothAssetImportFilePath
{
PRAGMA_ENABLE_DEPRECATION_WARNINGS
	GENERATED_USTRUCT_BODY()

public:
	DECLARE_DELEGATE_OneParam(FDelegate, UE::Dataflow::FContext&);

	UPROPERTY(EditAnywhere, Category = "Import File Path")
	FString FilePath;

	UE_DEPRECATED(5.5, "Use delegate instead.")
	UPROPERTY()
	bool bForceReimport = false;

	FChaosClothAssetImportFilePath() = default;

	explicit FChaosClothAssetImportFilePath(FDelegate&& InDelegate) : Delegate(MoveTemp(InDelegate)) {}

	void Execute(UE::Dataflow::FContext& Context) const { Delegate.ExecuteIfBound(Context); }

private:
	FDelegate Delegate;
};
