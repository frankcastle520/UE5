// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

namespace UE::NNERuntimeRDGUtils::Internal
{

NNERUNTIMERDGUTILS_API TOptional<uint32> GetOpVersionFromOpsetVersion(const FString& OpType, int OpsetVersion);

} // namespace UE::NNERuntimeRDGUtils::Internal