// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

class IElectraCodecFactory;

class FElectraMediaMP3Decoder
{
public:
	static void Startup();
	static void Shutdown();
	static TSharedPtr<IElectraCodecFactory, ESPMode::ThreadSafe> CreateFactory();
};
