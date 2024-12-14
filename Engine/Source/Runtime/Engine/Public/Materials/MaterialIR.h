// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Materials/MaterialIRCommon.h"
#include "MaterialTypes.h"

#if WITH_EDITOR

namespace UE::MIR {

enum EValueKind
{
	/* Values */

	VK_Constant,
	VK_ExternalInput,
	VK_MaterialParameter,

	/* Instructions */

	VK_InstructionBegin,

	VK_Dimensional,
	VK_SetMaterialOutput,
	VK_BinaryOperator,
	VK_Branch,
	VK_Subscript,
	VK_Cast,
	VK_TextureSample,

	VK_InstructionEnd,
};

const TCHAR* ValueKindToString(EValueKind Kind);

/* Values */

enum EValueFlags
{
	VF_None = 0,
	VF_ValueAnalyzed = 1,
	VF_InstructionAnalyzed = 2,
};

//
struct FValue
{
	EValueKind Kind : 8{};
	EValueFlags Flags : 8{};
	FTypePtr  Type{};

	// Returns the size in bytes of this value instance.
	uint32 GetSizeInBytes() const;
	
	// Sets the specified value flags.
	void SetFlags(EValueFlags InFlags) { Flags = (EValueFlags)(Flags | InFlags); }

	// Returns whether this value is of specified kind.
	bool IsA(EValueKind InKind) const { return Kind == InKind; }

	// Tries to cast this value to an instruction and returns it (nullptr otherwise).
	FInstruction* AsInstruction();

	// Tries to cast this value to an instruction and returns it (nullptr otherwise).
	const FInstruction* AsInstruction() const;

	// Returns the array of this value's uses. An use is another value referenced by this one (e.g. the operands of a binary expression).
	TArrayView<const FValue*> GetUses() const;

	// Returns the array of this value's uses. An use is another value referenced by this one (e.g. the operands of a binary expression).
	TArrayView<FValue*> GetUses();

	// Returns whether this value exactly equals Other.
	bool Equals(const FValue* Other) const;

	// Returns whether this value is a scalar (its type is Primitive with exactly 1 component).
	bool IsScalar() const;

	// Returns whether this value is a vector (tis type is Primitive with 1-4 rows and exactly 1 column).
	bool IsVector() const;

	// Returns whether this value is a constant boolean with value true.
	bool IsTrue() const;

	// Returns whether this value is a constant boolean with value false.
	bool IsFalse() const;

	// Returns whether this value is arithmetic and exactly zero.
	bool IsExactlyZero() const;

	// Returns whether this value is arithmetic and approximately zero.
	bool IsNearlyZero() const;

	// Returns whether this value is arithmetic and exactly one.
	bool IsExactlyOne() const;

	// Returns whether this value is arithmetic and approximately one.
	bool IsNearlyOne() const;
	
	// Returns this value's textures if it has one (nullptr otherwise).
	UTexture* GetTexture();

	// Tries to cast this value to specified type T and returns the casted pointer, if possible (nullptr otherwise).
	template <typename T>
	T* As() { return this && IsA(T::TypeKind) ? static_cast<T*>(this) : nullptr; }

	// Tries to cast this value to specified type T and returns the casted pointer, if possible (nullptr otherwise).
	template <typename T>
	const T* As() const { return this && IsA(T::TypeKind) ? static_cast<const T*>(this) : nullptr; }
};

template <EValueKind TTypeKind>
struct TValue : FValue
{
	static constexpr EValueKind TypeKind = TTypeKind;
};

using TInteger = int64_t;
using TFloat = double;

struct FConstant : TValue<VK_Constant>
{
	union
	{
		bool  		Boolean;
		TInteger	Integer;
		TFloat 		Float;
	};

	bool IsBool() const;
	bool IsInteger() const;
	bool IsFloat() const;

	template <typename T>
	T Get() const
	{
		if constexpr (std::is_same_v<T, bool>)
		{
			return Boolean;
		}
		else if constexpr (std::is_integral_v<T>)
		{
			return Integer;
		}
		else if constexpr (std::is_floating_point_v<T>)
		{
			return Float;
		}
		else
		{
			check(false && "unexpected type T.");
		}
	}
};

enum class EExternalInput
{
	None,

	TexCoord0,
	TexCoord1,
	TexCoord2,
	TexCoord3,
	TexCoord4,
	TexCoord5,
	TexCoord6,
	TexCoord7,

	TexCoord0_Ddx,
	TexCoord1_Ddx,
	TexCoord2_Ddx,
	TexCoord3_Ddx,
	TexCoord4_Ddx,
	TexCoord5_Ddx,
	TexCoord6_Ddx,
	TexCoord7_Ddx,

	TexCoord0_Ddy,
	TexCoord1_Ddy,
	TexCoord2_Ddy,
	TexCoord3_Ddy,
	TexCoord4_Ddy,
	TexCoord5_Ddy,
	TexCoord6_Ddy,
	TexCoord7_Ddy,

	Count,
};

static constexpr int TexCoordMaxNum = 8;

const TCHAR* ExternalInputToString(EExternalInput Input);
MIR::EExternalInput TexCoordIndexToExternalInput(int TexCoordIndex);
FTypePtr GetExternalInputType(EExternalInput Id);
bool IsExternalInputTexCoord(EExternalInput Id);
bool IsExternalInputTexCoordDdx(EExternalInput Id);
bool IsExternalInputTexCoordDdy(EExternalInput Id);

struct FExternalInput : TValue<VK_ExternalInput>
{
	EExternalInput Id;
};

struct FMaterialParameter : TValue<VK_MaterialParameter>
{	
	FMaterialParameterInfo Info;
	FMaterialParameterMetadata Metadata;
};

/* Instructions */

struct FBlock
{
	FBlock* Parent{};
	FInstruction* Instructions{};
	int32 Level{};

	FBlock* FindCommonParentWith(MIR::FBlock* Other);
};

struct FInstruction : FValue
{
	FInstruction* Next{};
	FBlock* Block{};
	uint32 NumUsers{};
	uint32 NumProcessedUsers{};

	FBlock* GetDesiredBlockForUse(int32 UseIndex); 
};

template <EValueKind TTypeKind>
struct TInstruction : FInstruction
{
	static constexpr EValueKind TypeKind = TTypeKind;
};

struct FDimensional : TInstruction<VK_Dimensional>
{
	static constexpr int MaxNumComponents = 16;

	// Returns the constant array of component values. 
	TArrayView<FValue* const> GetComponents() const;

	// Returns the mutable array of component values. 
	TArrayView<FValue*> GetComponents();

	// Returns whether all components are constant.
	bool AreComponentsConstant() const;
};

template <int TDimension>
struct TDimensional : FDimensional
{
	FValue* Components[TDimension];
};

struct FSetMaterialOutput : TInstruction<VK_SetMaterialOutput>
{
	EMaterialProperty Property;
	FValue* Arg;
};

enum EBinaryOperator
{
	BO_Invalid,

	/* Arithmetic */
	BO_Add,
	BO_Subtract,
	BO_Multiply,
	BO_Divide,

	/* Comparison */
	BO_GreaterThan,
	BO_GreaterThanOrEquals,
	BO_LowerThan,
	BO_LowerThanOrEquals,
	BO_Equals,
	BO_NotEquals,
};

bool IsArithmeticOperator(EBinaryOperator Op);
bool IsComparisonOperator(EBinaryOperator Op);
const TCHAR* BinaryOperatorToString(EBinaryOperator Op);

struct FBinaryOperator : TInstruction<VK_BinaryOperator>
{
	EBinaryOperator Operator = BO_Invalid;
	FValue* LhsArg{};
	FValue* RhsArg{};
};

struct FBranch : TInstruction<VK_Branch>
{
	FValue* ConditionArg{};
	FValue* TrueArg{};
	FValue* FalseArg{};
	FBlock TrueBlock{};
	FBlock FalseBlock{};
};

struct FSubscript : TInstruction<VK_Subscript>
{
	FValue* Arg;
	int Index;
};

struct FCast : TInstruction<VK_Cast>
{
	FValue* Arg{};
};

struct FTextureSample : TInstruction<VK_TextureSample>
{
	FValue* TexCoordArg;
	// TexCoordDerivatives;
	FValue* MipValueArg;
	FValue* AutomaticMipBiasArg;
	UTexture* Texture;
	ESamplerSourceMode SamplerSourceMode;
	ETextureMipValueMode MipValueMode;
	EMaterialSamplerType SamplerType;
	
	/* Analysis Values */
	int TextureParameterIndex;
};

UTexture* GetTextureFromValue(FValue* TextureValue);

} // namespace UE::MIR
#endif
