// Copyright Epic Games, Inc. All Rights Reserved.

#include "Materials/MaterialIRTypes.h"

#if WITH_EDITOR

namespace UE::MIR
{

const TCHAR* TypeKindToString(ETypeKind Kind)
{
	switch (Kind)
	{
		case TK_Void: return TEXT("void");
		case TK_Primitive: return TEXT("primitive");
		default: UE_MIR_UNREACHABLE();
	}
}

FTypePtr FType::FromShaderType(const UE::Shader::FType& InShaderType)
{
	check(!InShaderType.IsStruct());
	check(!InShaderType.IsObject());

	switch (InShaderType.ValueType)
	{
		case UE::Shader::EValueType::Void:
			return FType::GetVoid();

		case UE::Shader::EValueType::Float1:
		case UE::Shader::EValueType::Float2:
		case UE::Shader::EValueType::Float3:
		case UE::Shader::EValueType::Float4:
			return FPrimitiveType::GetVector(SK_Float, (int)InShaderType.ValueType - (int)UE::Shader::EValueType::Float1 + 1);

		case UE::Shader::EValueType::Int1:
		case UE::Shader::EValueType::Int2:
		case UE::Shader::EValueType::Int3:
		case UE::Shader::EValueType::Int4:
			return FPrimitiveType::GetVector(SK_Int, (int)InShaderType.ValueType - (int)UE::Shader::EValueType::Int1 + 1);

		case UE::Shader::EValueType::Bool1:
		case UE::Shader::EValueType::Bool2:
		case UE::Shader::EValueType::Bool3:
		case UE::Shader::EValueType::Bool4:
			return FPrimitiveType::GetVector(SK_Bool, (int)InShaderType.ValueType - (int)UE::Shader::EValueType::Int1 + 1);

		default:
			UE_MIR_UNREACHABLE();
	}
}

FTypePtr FType::FromMaterialValueType(EMaterialValueType Type)
{
	switch (Type)
	{
		case MCT_VoidStatement:
			return FType::GetVoid();

		case MCT_Float1:
		case MCT_Float2:
		case MCT_Float3:
		case MCT_Float4:
			return FPrimitiveType::GetVector(SK_Float, (int)Type - (int)MCT_Float1 + 1);

		case MCT_Float:
			return FPrimitiveType::GetVector(SK_Float, 4);

		case MCT_UInt1:
		case MCT_UInt2:
		case MCT_UInt3:
		case MCT_UInt4:
			return FPrimitiveType::GetVector(SK_Int, (int)Type - (int)MCT_UInt1 + 1);

		case MCT_Bool:
			return FPrimitiveType::GetVector(SK_Bool, (int)Type - (int)UE::Shader::EValueType::Int1 + 1);

		default:
			UE_MIR_UNREACHABLE();
	}
}

FTypePtr FType::GetVoid()
{
	static FType Type{ TK_Void };
	return &Type;
}

FStringView FType::GetSpelling() const
{
	if (auto PrimitiveType = AsPrimitive())
	{
		return PrimitiveType->Spelling;
	}
	
	UE_MIR_UNREACHABLE();
}

UE::Shader::EValueType FType::ToValueType() const
{
	using namespace UE::Shader;

	if (FPrimitiveTypePtr PrimitiveType = AsPrimitive())
	{
		if (PrimitiveType->IsMatrix())
		{
			if (PrimitiveType->NumRows == 4 && PrimitiveType->NumColumns == 4)
			{
				if (PrimitiveType->ScalarKind == SK_Float)
				{
					return EValueType::Float4x4;
				}
				else
				{
					return EValueType::Numeric4x4;
				}
			}

			return EValueType::Any;
		}

		check(PrimitiveType->NumColumns == 1 && PrimitiveType->NumRows <= 4);

		switch (PrimitiveType->ScalarKind)
		{
			case SK_Bool: 	return (EValueType)((int)EValueType::Bool1 + PrimitiveType->NumRows - 1); break;
			case SK_Int: 	return (EValueType)((int)EValueType::Int1 + PrimitiveType->NumRows - 1); break;
			case SK_Float: 	return (EValueType)((int)EValueType::Float1 + PrimitiveType->NumRows - 1); break;
			default: UE_MIR_UNREACHABLE();
		}
	}
	
	UE_MIR_UNREACHABLE();
}

bool FType::IsBoolScalar() const
{
	return this == FPrimitiveType::GetBool1();
}

FPrimitiveTypePtr FType::AsPrimitive() const
{
	return Kind == TK_Primitive ? static_cast<FPrimitiveTypePtr>(this) : nullptr; 
}

FPrimitiveTypePtr FType::AsScalar() const
{
	FPrimitiveTypePtr Type = AsPrimitive();
	return Type->IsScalar() ? Type : nullptr;
}

FPrimitiveTypePtr FType::AsVector() const
{
	FPrimitiveTypePtr Type = AsPrimitive();
	return Type->IsVector() ? Type : nullptr;
}

FPrimitiveTypePtr FType::AsMatrix() const
{
	FPrimitiveTypePtr Type = AsPrimitive();
	return Type->IsMatrix() ? Type : nullptr;
}

const TCHAR* ScalarKindToString(EScalarKind Kind)
{
	switch (Kind)
	{
		case SK_Bool: return TEXT("bool"); break;
		case SK_Int: return TEXT("int"); break;
		case SK_Float: return TEXT("float"); break;
		default: UE_MIR_UNREACHABLE();
	}
}

FPrimitiveTypePtr FPrimitiveType::GetBool1()
{
	return GetScalar(SK_Bool);
}

FPrimitiveTypePtr FPrimitiveType::GetInt1()
{
	return GetScalar(SK_Int);
}

FPrimitiveTypePtr FPrimitiveType::GetFloat1()
{
	return GetScalar(SK_Float);
}

FPrimitiveTypePtr FPrimitiveType::GetFloat2()
{
	return GetVector(SK_Float, 2);
}

FPrimitiveTypePtr FPrimitiveType::GetFloat3()
{
	return GetVector(SK_Float, 3);
}

FPrimitiveTypePtr FPrimitiveType::GetFloat4()
{
	return GetVector(SK_Float, 4);
}

const FPrimitiveType* FPrimitiveType::GetScalar(EScalarKind InScalarKind)
{
	return Get(InScalarKind, 1, 1);
}

const FPrimitiveType* FPrimitiveType::GetVector(EScalarKind InScalarKind, int NumComponents)
{
	check(NumComponents >= 1 && NumComponents <= 4);
	return Get(InScalarKind, NumComponents, 1);
}

const FPrimitiveType* FPrimitiveType::GetMatrix(EScalarKind InScalarKind, int NumRows, int NumColumns)
{
	check(NumColumns > 1 && NumColumns <= 4);
	check(NumRows > 1 && NumRows <= 4);
	return Get(InScalarKind, NumRows, NumColumns);
}

FPrimitiveTypePtr FPrimitiveType::Get(EScalarKind InScalarKind, int NumRows, int NumColumns)
{
	check(InScalarKind >= 0 && InScalarKind <= SK_Float);
	
	static const FStringView Invalid = TEXT("invalid");

	static const FPrimitiveType Types[] {
		{ { TK_Primitive }, { TEXT("bool") }, 		SK_Bool, 1, 1 },
		{ { TK_Primitive }, Invalid, 				SK_Bool, 1, 2 }, 
		{ { TK_Primitive }, Invalid, 				SK_Bool, 1, 3 },
		{ { TK_Primitive }, Invalid, 				SK_Bool, 1, 4 },
		{ { TK_Primitive }, { TEXT("bool2") },   	SK_Bool, 2, 1 },
		{ { TK_Primitive }, { TEXT("bool2x2") }, 	SK_Bool, 2, 2 },
		{ { TK_Primitive }, { TEXT("bool2x3") }, 	SK_Bool, 2, 3 },
		{ { TK_Primitive }, { TEXT("bool2x4") }, 	SK_Bool, 2, 4 },
		{ { TK_Primitive }, { TEXT("bool3") },   	SK_Bool, 3, 1 },
		{ { TK_Primitive }, { TEXT("bool3x2") }, 	SK_Bool, 3, 2 },
		{ { TK_Primitive }, { TEXT("bool3x3") }, 	SK_Bool, 3, 3 },
		{ { TK_Primitive }, { TEXT("bool3x4") }, 	SK_Bool, 3, 4 },
		{ { TK_Primitive }, { TEXT("bool4") },   	SK_Bool, 4, 1 },
		{ { TK_Primitive }, { TEXT("bool4x2") }, 	SK_Bool, 4, 2 },
		{ { TK_Primitive }, { TEXT("bool4x3") }, 	SK_Bool, 4, 3 },
		{ { TK_Primitive }, { TEXT("bool4x4") }, 	SK_Bool, 4, 4 },
		{ { TK_Primitive }, { TEXT("int") }, 		SK_Int, 1, 1 },
		{ { TK_Primitive }, Invalid, 				SK_Int, 1, 2 },
		{ { TK_Primitive }, Invalid, 				SK_Int, 1, 3 },
		{ { TK_Primitive }, Invalid, 				SK_Int, 1, 4 },
		{ { TK_Primitive }, { TEXT("int2") },   	SK_Int, 2, 1 },
		{ { TK_Primitive }, { TEXT("int2x2") }, 	SK_Int, 2, 2 },
		{ { TK_Primitive }, { TEXT("int2x3") }, 	SK_Int, 2, 3 },
		{ { TK_Primitive }, { TEXT("int2x4") }, 	SK_Int, 2, 4 },
		{ { TK_Primitive }, { TEXT("int3") },   	SK_Int, 3, 1 },
		{ { TK_Primitive }, { TEXT("int3x2") }, 	SK_Int, 3, 2 },
		{ { TK_Primitive }, { TEXT("int3x3") }, 	SK_Int, 3, 3 },
		{ { TK_Primitive }, { TEXT("int3x4") }, 	SK_Int, 3, 4 },
		{ { TK_Primitive }, { TEXT("int4") },   	SK_Int, 4, 1 },
		{ { TK_Primitive }, { TEXT("int4x2") }, 	SK_Int, 4, 2 },
		{ { TK_Primitive }, { TEXT("int4x3") }, 	SK_Int, 4, 3 },
		{ { TK_Primitive }, { TEXT("int4x4") }, 	SK_Int, 4, 4 },
		{ { TK_Primitive }, { TEXT("float") }, 	SK_Float, 1, 1 },
		{ { TK_Primitive }, Invalid, 				SK_Float, 1, 2 },
		{ { TK_Primitive }, Invalid, 				SK_Float, 1, 3 },
		{ { TK_Primitive }, Invalid, 				SK_Float, 1, 4 },
		{ { TK_Primitive }, { TEXT("float2") },   	SK_Float, 2, 1 },
		{ { TK_Primitive }, { TEXT("float2x2") }, 	SK_Float, 2, 2 },
		{ { TK_Primitive }, { TEXT("float2x3") }, 	SK_Float, 2, 3 },
		{ { TK_Primitive }, { TEXT("float2x4") }, 	SK_Float, 2, 4 },
		{ { TK_Primitive }, { TEXT("float3") },   	SK_Float, 3, 1 },
		{ { TK_Primitive }, { TEXT("float3x2") }, 	SK_Float, 3, 2 },
		{ { TK_Primitive }, { TEXT("float3x3") }, 	SK_Float, 3, 3 },
		{ { TK_Primitive }, { TEXT("float3x4") }, 	SK_Float, 3, 4 },
		{ { TK_Primitive }, { TEXT("float4") },   	SK_Float, 4, 1 },
		{ { TK_Primitive }, { TEXT("float4x2") }, 	SK_Float, 4, 2 },
		{ { TK_Primitive }, { TEXT("float4x3") }, 	SK_Float, 4, 3 },
		{ { TK_Primitive }, { TEXT("float4x4") }, 	SK_Float, 4, 4 },
	};

	int Index = InScalarKind * 4 * 4 + (NumRows - 1) * 4 + (NumColumns - 1);
	check(Index < UE_ARRAY_COUNT(Types));
	return &Types[Index];
}

FPrimitiveTypePtr FPrimitiveType::ToScalar() const
{
	return FPrimitiveType::GetScalar(ScalarKind);
}

FTypePtr FTextureType::Get()
{
	static FTextureType Instance { TK_Texture };
	return &Instance;
}


} // namespace UE::MIR

#endif // #if WITH_EDITOR
