// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "NNERuntimeRDGUtilsModelBuilder.h"
#include "Templates/UniquePtr.h"

namespace UE::NNERuntimeRDGUtils::Internal
{

NNERUNTIMERDGUTILS_API TUniquePtr<IModelBuilder> CreateNNEModelBuilder();

} // UE::NNERuntimeRDGUtils::Internal

