// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

class UNNEModelData;

namespace UE::NNEEditor::Internal::OnnxFileLoaderHelper
{
	bool NNEEDITOR_API InitUNNEModelDataFromFile(UNNEModelData& ModelData, int64& ModelFileSize, const FString& Filename);

} // namespace UE::NNEEditor::Internal::OnnxFileLoaderHelper
