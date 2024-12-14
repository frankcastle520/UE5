// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AssetRegistry/AssetData.h"

class UPoseSearchDatabase;

namespace UE::PoseSearch
{
	struct FPoseSearchEditorUtils
	{
		static bool IsAssetCompatibleWithDatabase(const UPoseSearchDatabase* InDatabase, const FAssetData & InAssetData);
	};
}
