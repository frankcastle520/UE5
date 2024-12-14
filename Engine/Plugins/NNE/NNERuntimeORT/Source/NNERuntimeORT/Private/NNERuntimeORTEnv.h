// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "NNEOnnxruntime.h"

namespace UE::NNERuntimeORT::Private
{

class FEnvironment
{
public:
	struct FConfig
	{
		bool bUseGlobalThreadPool = false;
		int32 IntraOpNumThreads = 0;
		int32 InterOpNumThreads = 0;
	};

	FEnvironment() = default;
	
	~FEnvironment() = default;

	void CreateOrtEnv(const FConfig& InConfig);

	const Ort::Env& GetOrtEnv() const;

	FConfig GetConfig() const { return Config; }

private:
	FConfig Config{};
	Ort::Env OrtEnvironment{nullptr};
};

} // UE::NNERuntimeORT::Private