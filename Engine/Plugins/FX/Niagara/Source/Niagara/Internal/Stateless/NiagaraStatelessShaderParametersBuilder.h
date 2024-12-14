// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "NiagaraStatelessCommon.h"
#include "ShaderParameterStruct.h"

class FNiagaraStatelessShaderParametersBuilder
{
public:
	// Find the current size of the shader parameter structure
	int32 GetParametersStructSize() const
	{
		return ParameterOffset;
	}

	// Adds a shader parameters structure that is scoped to the data interface
	// i.e. if the structured contained "MyFloat" the shader variable would be "UniqueDataInterfaceName_MyFloat"
	template<typename T> void AddParameterNestedStruct()
	{
		const int32 StructOffset = Align(ParameterOffset, TShaderParameterStructTypeInfo<T>::Alignment);
		ParameterOffset = StructOffset + TShaderParameterStructTypeInfo<T>::GetStructMetadata()->GetSize();
	}

private:
	int32	ParameterOffset = 0;
};
