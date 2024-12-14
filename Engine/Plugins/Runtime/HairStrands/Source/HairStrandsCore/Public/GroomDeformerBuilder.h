// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "HAL/Platform.h"

class UGroomAsset;
class USkeletalMesh;

struct FGroomDeformerBuilder
{
	// Create skeletal mesh bones+physics asset from groom asset guides
	static HAIRSTRANDSCORE_API USkeletalMesh* CreateSkeletalMesh(UGroomAsset* GroomAsset);
};
