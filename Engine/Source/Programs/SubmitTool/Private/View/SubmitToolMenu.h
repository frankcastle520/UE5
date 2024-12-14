// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "SubmitToolCommandList.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"

class FSubmitToolMenu
{
public:
	static void FillMainMenuEntries(FMenuBuilder& MenuBuilder);

#if !UE_BUILD_SHIPPING
	static void FillDebugMenuEntries(FMenuBuilder& MenuBuilder);
#endif
};
