// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Materials/MaterialIRCommon.h"
#include "MaterialShared.h"

#if WITH_EDITOR

template <typename T>
TArrayView<T> MakeTemporaryArray(FMemMark&, int Count)
{
	auto Ptr = (T*)FMemStack::Get().Alloc(sizeof(T) * Count, alignof(T));
	return { Ptr, Count };
}

namespace UE::MIR::Internal {

//
bool IsMaterialPropertyShared(EMaterialProperty InProperty);

//
bool NextMaterialAttributeInput(UMaterial* BaseMaterial, int32& PropertyIndex, FMaterialInputDescription& Input);

//
FValue* CreateMaterialAttributeDefaultValue(FEmitter& Emitter, const UMaterial* Material, EMaterialProperty Property);
 
//
EMaterialTextureParameterType TextureMaterialValueTypeToParameterType(EMaterialValueType Type);

//
FValue* GetInputValue(FMaterialIRModuleBuilderImpl* Builder, const FExpressionInput* Input);

//
void SetInputValue(FMaterialIRModuleBuilderImpl* Builder, const FExpressionInput* Input, FValue* Value);

//
void SetOutputValue(FMaterialIRModuleBuilderImpl* Builder, const FExpressionOutput* Output, FValue* Value);

} // namespace UE::MIR::Internal

#endif // #if WITH_EDITOR

