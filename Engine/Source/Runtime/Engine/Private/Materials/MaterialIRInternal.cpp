// Copyright Epic Games, Inc. All Rights Reserved.

#include "Materials/MaterialIRInternal.h"
#include "Materials/MaterialAttributeDefinitionMap.h"
#include "Materials/MaterialIREmitter.h"
#include "Materials/Material.h"
#include "MaterialShared.h"

#if WITH_EDITOR

namespace UE::MIR::Internal {

bool IsMaterialPropertyShared(EMaterialProperty InProperty)
{
	switch (InProperty)
	{
	case MP_Normal:
	case MP_Tangent:
	case MP_EmissiveColor:
	case MP_Opacity:
	case MP_OpacityMask:
	case MP_BaseColor:
	case MP_Metallic:
	case MP_Specular:
	case MP_Roughness:
	case MP_Anisotropy:
	case MP_AmbientOcclusion:
	case MP_Refraction:
	case MP_PixelDepthOffset:
	case MP_SubsurfaceColor:
	case MP_ShadingModel:
	case MP_SurfaceThickness:
	case MP_FrontMaterial:
	case MP_Displacement:
		return true;
	default:
		return false;
	}
}

bool NextMaterialAttributeInput(UMaterial* BaseMaterial, int32& PropertyIndex, FMaterialInputDescription& Input)
{
	for (; PropertyIndex < MP_MAX; ++PropertyIndex)
	{
		EMaterialProperty Property = (EMaterialProperty)PropertyIndex;
		if (MIR::Internal::IsMaterialPropertyShared(Property)
			&& Property != MP_SubsurfaceColor
			&& Property != MP_FrontMaterial
			&& BaseMaterial->GetExpressionInputDescription(Property, Input))
		{
			return true;
		}
	}

	return false;
}

MIR::FValue* CreateMaterialAttributeDefaultValue(FEmitter& Emitter, const UMaterial* Material, EMaterialProperty Property)
{
	EMaterialValueType Type = FMaterialAttributeDefinitionMap::GetValueType(Property);
	FVector4f DefaultValue = FMaterialAttributeDefinitionMap::GetDefaultValue(Property);

	switch (Type)
	{
		case MCT_ShadingModel: return Emitter.EmitConstantInt1(Material->GetShadingModels().GetFirstShadingModel());

		case MCT_Float1: return Emitter.EmitConstantFloat1(  DefaultValue.X );
		case MCT_Float2: return Emitter.EmitConstantFloat2({ DefaultValue.X, DefaultValue.Y });
		case MCT_Float3: return Emitter.EmitConstantFloat3({ DefaultValue.X, DefaultValue.Y, DefaultValue.Z });
		case MCT_Float4: return Emitter.EmitConstantFloat4(DefaultValue);

		case MCT_UInt1: return Emitter.EmitConstantInt1(  (int32)DefaultValue.X );
		case MCT_UInt2: return Emitter.EmitConstantInt2({ (int32)DefaultValue.X, (int32)DefaultValue.Y });
		case MCT_UInt3: return Emitter.EmitConstantInt3({ (int32)DefaultValue.X, (int32)DefaultValue.Y, (int32)DefaultValue.Z });
		case MCT_UInt4: return Emitter.EmitConstantInt4({ (int32)DefaultValue.X, (int32)DefaultValue.Y, (int32)DefaultValue.Z, (int32)DefaultValue.W });

		default: UE_MIR_UNREACHABLE();
	}
}

EMaterialTextureParameterType TextureMaterialValueTypeToParameterType(EMaterialValueType Type)
{
	switch (Type)
	{
		case MCT_Texture2D: return EMaterialTextureParameterType::Standard2D;
		case MCT_Texture2DArray: return EMaterialTextureParameterType::Array2D;
		case MCT_TextureCube: return EMaterialTextureParameterType::Cube;
		case MCT_TextureCubeArray: return EMaterialTextureParameterType::ArrayCube;
		case MCT_VolumeTexture: return EMaterialTextureParameterType::Volume;
		default: UE_MIR_UNREACHABLE();
	}
}

} // namespace UE::MIR::Internal

#endif
