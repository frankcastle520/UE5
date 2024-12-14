// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class FAppVersion
{
public:
	static FString GetVersion();

private:
	static FString Version;
};