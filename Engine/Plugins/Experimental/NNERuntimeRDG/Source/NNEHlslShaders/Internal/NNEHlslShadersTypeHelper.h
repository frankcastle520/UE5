// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "NNETypes.h"
#include "PixelFormat.h"

namespace UE::NNEHlslShaders::Internal
{
	enum class ENNEShaderDataType : uint8
	{
		BOOL,
		FLOAT16,
		FLOAT32,
		INT8,
		INT16,
		INT32,
		UINT8,
		UINT16,
		UINT32,

		MAX
	};

	const TCHAR	       NNEHLSLSHADERS_API *ShaderDataTypeToName(ENNEShaderDataType ShaderType);
	ENNEShaderDataType NNEHLSLSHADERS_API TensorToShaderDataType(ENNETensorDataType TensorType);
	EPixelFormat	   NNEHLSLSHADERS_API TensorDataTypeToPixelFormat(ENNETensorDataType TensorType);

} // UE::NNEHlslShaders::Internal
