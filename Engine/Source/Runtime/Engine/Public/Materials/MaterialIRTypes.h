// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Materials/MaterialIRCommon.h"
#include "Shader/ShaderTypes.h"
#include "MaterialValueType.h"

#if WITH_EDITOR

namespace UE::MIR
{

enum ETypeKind
{
	TK_Void,
	TK_Primitive,
	TK_Texture,
};

const TCHAR* TypeKindToString(ETypeKind Kind);

struct FType
{
	// Identifies what derived type this is.
	ETypeKind Kind;

	// Returns the type matching specified UE::Shader::FType.
	static FTypePtr FromShaderType(const UE::Shader::FType& InShaderType);
	
	// Returns the type matching specified EMaterialValueType.
	static FTypePtr FromMaterialValueType(EMaterialValueType Type);

	// Returns the `void` type.
	static FTypePtr GetVoid();

	// Returns whether this type is a `bool` scalar.
	bool IsBoolScalar() const;

	// Returns this type upcast this type to ArithmeticType if it is one. Otherwise it returns nullptr.
	FPrimitiveTypePtr AsPrimitive() const;

	// Returns this type upcast this type to ArithmeticType if it's a scalar. Otherwise it returns nullptr.
	FPrimitiveTypePtr AsScalar() const;

	// Returns this type upcast this type to ArithmeticType if it's a vector. Otherwise it returns nullptr.
	FPrimitiveTypePtr AsVector() const;

	// Returns this type upcast this type to ArithmeticType if it's a matrix. Otherwise it returns nullptr.
	FPrimitiveTypePtr AsMatrix() const;

	// Returns the this type name spelling (e.g. float4x4).
	FStringView GetSpelling() const;

	// Converts this type to a UE::Shader::EValueType.
	UE::Shader::EValueType ToValueType() const;
};

// Primitive types of a single scalar.
// Note: These are listed in precision order. Converting one to the other is then simply performed taking the max EScalarKind.
enum EScalarKind
{
	SK_Bool, SK_Int, SK_Float,
};

const TCHAR* ScalarKindToString(EScalarKind Kind);

struct FPrimitiveType : FType
{
	FStringView Spelling;
	EScalarKind ScalarKind;
	int NumRows;
	int NumColumns;

	static FPrimitiveTypePtr GetBool1();
	static FPrimitiveTypePtr GetInt1();
	static FPrimitiveTypePtr GetFloat1();
	static FPrimitiveTypePtr GetFloat2();
	static FPrimitiveTypePtr GetFloat3();
	static FPrimitiveTypePtr GetFloat4();

	static FPrimitiveTypePtr GetScalar(EScalarKind InScalarKind);
	static FPrimitiveTypePtr GetVector(EScalarKind InScalarKind, int NumRows);
	static FPrimitiveTypePtr GetMatrix(EScalarKind InScalarKind, int NumColumns, int NumRows);
	static FPrimitiveTypePtr Get(EScalarKind InScalarKind, int NumRows, int NumColumns);

	int  GetNumComponents() const { return NumRows * NumColumns; }
	bool IsScalar() const { return GetNumComponents() == 1; }
	bool IsVector() const { return NumRows > 1 && NumColumns == 1; }
	bool IsMatrix() const { return NumRows > 1 && NumColumns > 1; }
	FPrimitiveTypePtr ToScalar() const;
};

struct FTextureType : FType
{
	static FTypePtr Get();
};

} // namespace UE::MIR

#endif // #if WITH_EDITOR
