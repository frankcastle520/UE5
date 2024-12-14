// Copyright Epic Games, Inc. All Rights Reserved.

#include "Materials/MaterialIREmitter.h"
#include "Materials/MaterialIRModule.h"
#include "Materials/MaterialIRModuleBuilder.h"
#include "Materials/MaterialIRInternal.h"
#include "Shader/ShaderTypes.h"
#include "MaterialShared.h"
#include "MaterialExpressionIO.h"

#if WITH_EDITOR

namespace UE::MIR
{

const TCHAR* VectorComponentToString(EVectorComponent Component)
{
	static const TCHAR* Strings[] = { TEXT("x"), TEXT("y"), TEXT("z"), TEXT("w") };
	return Strings[(int)Component];
}

FSwizzleMask::FSwizzleMask(EVectorComponent X)
: NumComponents{ 1 }
{
	Components[0] = X;
}

FSwizzleMask::FSwizzleMask(EVectorComponent X, EVectorComponent Y)
: NumComponents{ 2 }
{
	Components[0] = X;
	Components[1] = Y;
}

FSwizzleMask::FSwizzleMask(EVectorComponent X, EVectorComponent Y, EVectorComponent Z)
: NumComponents{ 3 }
{
	Components[0] = X;
	Components[1] = Y;
	Components[2] = Z;
}

FSwizzleMask::FSwizzleMask(EVectorComponent X, EVectorComponent Y, EVectorComponent Z, EVectorComponent W)
: NumComponents{ 4 }
{

	Components[0] = X;
	Components[1] = Y;
	Components[2] = Z;
	Components[3] = W;
}

FSwizzleMask FSwizzleMask::XYZ()
{
	return { EVectorComponent::X, EVectorComponent::Y, EVectorComponent::Z };
}

void FSwizzleMask::Append(EVectorComponent Component)
{
	check(NumComponents < 4);
	Components[NumComponents++] = Component;
}

struct FEmitter::FPrivate
{
	// Looks for an existing value in the module that matches `Prototype` and returns it if found.
	static FValue* FindValue(FEmitter* Emitter, const FValue* Prototype)
	{
		/* todo: improve this with hashmap */
		for (FValue* CurrValue : Emitter->Module->Values)
		{
			if (CurrValue->Equals(Prototype))
			{
				return CurrValue;
			}
		}

		return nullptr;
	}

	// Allocates a block of memory using the emitter's allocator.
	static uint8* Allocate(FEmitter* Emitter, int Size, int Alignment)
	{
		uint8* Bytes = (uint8*) FMemory::Malloc(Size, Alignment); // Emitter->Module->Allocator.PushBytes(Size, Alignment);
		FMemory::Memzero(Bytes, Size);
		return Bytes;
	}

	// Pushes a new value to the list of values.
	static void PushNewValue(FEmitter* Emitter, FValue* Value)
	{
		Emitter->Module->Values.Add(Value);
	}
};

// Creates a new `FDimensional` value of specified `Type` and returns it.
static FDimensional* NewDimensionalValue(FEmitter* Emitter, FPrimitiveTypePtr Type)
{
	check(!Type->IsScalar());

	int Dimensions = Type->NumRows * Type->NumColumns;
	int SizeInBytes = sizeof(FDimensional) + sizeof(FValue*) * Dimensions;

	uint8* Bytes = FEmitter::FPrivate::Allocate(Emitter, SizeInBytes, alignof(FDimensional));

	FDimensional* Value = new (Bytes) FDimensional{};
	Value->Kind = VK_Dimensional;
	Value->Type = Type;

	return Value;
}

// Emits specified newly created `Value`. If the exact value already exists,
// specified one is *destroyed* and existing one is returned instead.
static FValue* EmitNew(FEmitter* Emitter, FValue* Value)
{
	if (FValue* Existing = FEmitter::FPrivate::FindValue(Emitter, Value))
	{
		delete Value;
		return Existing;
	}

	FEmitter::FPrivate::PushNewValue(Emitter, Value);
	return Value;
}

template <typename T>
static T MakePrototype(FTypePtr InType)
{
	T Value;
	FMemory::Memzero(Value);
	Value.Kind = T::TypeKind;
	Value.Type = InType;
	return Value;
}

// Searches for an existing value in module that matches specified `Prototype`.
// If none found, it creates a new value as a copy of the prototype, adds it to
// the module then returns it.
template <typename TValueType>
static FValue* EmitPrototype(FEmitter* Emitter, const TValueType& Prototype)
{
	if (FValue* Existing = FEmitter::FPrivate::FindValue(Emitter, &Prototype))
	{
		return Existing;
	}

	uint8* Bytes = FEmitter::FPrivate::Allocate(Emitter, sizeof(TValueType), alignof(TValueType));
	TValueType* Value = new (Bytes) TValueType{ Prototype };

	FEmitter::FPrivate::PushNewValue(Emitter, Value);

	return Value;
}

// Finds the expression input index. Although the implementation has O(n) complexity, it is only used for error reporting.
static int SlowFindExpressionInputIndex(UMaterialExpression* Expression, const FExpressionInput* Input)
{
	for (FExpressionInputIterator It{ Expression }; It; ++It)
	{
		if (It.Input == Input)
		{
			return It.Index;
		}
	}
	check(false && "No input found.");
	return -1;
}

// Finds the expression input name. Although the implementation has O(n) complexity, it is only used for error reporting.
static FName SlowFindInputName(UMaterialExpression* Expression, const FExpressionInput* Input)
{
	int InputIndex = SlowFindExpressionInputIndex(Expression, Input);
	return Expression->GetInputName(InputIndex);
}

// FEmitter API

FEmitter::FEmitter(FMaterialIRModuleBuilderImpl* InBuilder, UMaterial* InMaterial, FMaterialIRModule* InModule)
{
	Builder = InBuilder;
	Material = InMaterial;
	Module = InModule;
}

FValue*  FEmitter::TryGet(const FExpressionInput* Input)
{
	FValue* Value = MIR::Internal::GetInputValue(Builder, Input);
	return Value ? Value : nullptr;
}

FValue* FEmitter::Get(const FExpressionInput* Input)
{
	FValue* Value = TryGet(Input);
	if (!Value)
	{
		Errorf(TEXT("Input '%s' has no input value."), *SlowFindInputName(Expression, Input).ToString());
		return nullptr;
	}
	return Value;
}
	
FEmitter& FEmitter::Put(int OutputIndex, FValue* Value)
{
	Put(Expression->GetOutput(OutputIndex), Value);
	return *this;
}

FEmitter& FEmitter::Put(const FExpressionOutput* Output, FValue* Value)
{
	check(Output);
	MIR::Internal::SetOutputValue(Builder, Output, Value);
	return *this;
}

FEmitter& FEmitter::DefaultToFloatZero(const FExpressionInput* Input)
{
	return DefaultTo(Input, 0.0f);
}

FEmitter& FEmitter::DefaultTo(const FExpressionInput* Input, TFloat Float)
{
	if (!Input->IsConnected())
	{
		Internal::SetInputValue(Builder, Input, EmitConstantFloat1(Float));
	}

	return *this;
}

FValue* FEmitter::GetFloat(const FExpressionInput* Input)
{
	FValue* Value = Get(Input);
	CheckInputIsScalar(Input, Value, SK_Float);
	return Value;
}

FValue* FEmitter::GetScalar(const FExpressionInput* Input)
{
	FValue* Value = GetPrimitive(Input);
	if (!Value)
	{
		return nullptr;
	}

	FPrimitiveTypePtr ScalarType = Value->Type->AsPrimitive()->ToScalar();
	return EmitConstruct(ScalarType, Value);
}

FValue* FEmitter::GetPrimitive(const FExpressionInput* Input)
{
	FValue* Value = Get(Input);
	CheckInputTypeIs(Input, Value, TK_Primitive);
	return Value;
}

FValue* FEmitter::GetOfType(const FExpressionInput* Input, ETypeKind Kind)
{
	FValue* Value = Get(Input);
	if (!Value)
	{
		return nullptr;
	}

	CheckInputTypeIs(Input, Value, Kind);
	return Value;
}

bool FEmitter::CheckValueValid(const FValue* Value)
{
	if (!Value)
	{
		Error(TEXT("Unspecified value"));
		return false;
	}
	return true;
}

void FEmitter::CheckInputIsScalar(const FExpressionInput* Input, FValue* InputValue)
{
	FPrimitiveTypePtr PrimitiveType = InputValue->Type->AsPrimitive();
	if (!PrimitiveType || !PrimitiveType->IsScalar())
	{
		Errorf(TEXT("Input '%s' expected to be a scalar. It is %s instead."), *SlowFindInputName(Expression, Input).ToString(), InputValue->Type->GetSpelling().GetData());
	}
}

void FEmitter::CheckInputIsScalar(const FExpressionInput* Input, FValue* InputValue, EScalarKind Kind)
{
	FPrimitiveTypePtr PrimitiveType = InputValue->Type->AsPrimitive();
	if (!PrimitiveType || !PrimitiveType->IsScalar() || PrimitiveType->ScalarKind != Kind)
	{
		Errorf(TEXT("Input '%s' expected to be a %s scalar. It is %s instead."), *SlowFindInputName(Expression, Input).ToString(), ScalarKindToString(Kind), InputValue->Type->GetSpelling().GetData());
	}
}

void FEmitter::CheckInputTypeIs(const FExpressionInput* Input, FValue* InputValue, ETypeKind Kind)
{
	if (InputValue && InputValue->Type->Kind != Kind)
	{
		Errorf(TEXT("Input '%s' expected to be have type %s. It is %s instead."), *SlowFindInputName(Expression, Input).ToString(), TypeKindToString(Kind), InputValue->Type->GetSpelling().GetData());
	}
}

FValue* FEmitter::EmitConstantFromShaderValue(const UE::Shader::FValue& InValue)
{
	using namespace UE::Shader;

	switch (InValue.Type.ValueType)
	{
		case UE::Shader::EValueType::Float1: return EmitConstantFloat1(InValue.AsFloatScalar());
		case UE::Shader::EValueType::Float2: return EmitConstantFloat2(FVector2f{ InValue.Component[0].Float, InValue.Component[1].Float });
		case UE::Shader::EValueType::Float3: return EmitConstantFloat3(FVector3f{ InValue.Component[0].Float, InValue.Component[1].Float, InValue.Component[2].Float });
		case UE::Shader::EValueType::Float4: return EmitConstantFloat4(FVector4f{ InValue.Component[0].Float, InValue.Component[1].Float, InValue.Component[2].Float, InValue.Component[3].Float });

		case UE::Shader::EValueType::Int1: return EmitConstantInt1(InValue.AsFloatScalar());
		case UE::Shader::EValueType::Int2: return EmitConstantInt2(FIntVector2{ InValue.Component[0].Int, InValue.Component[1].Int });
		case UE::Shader::EValueType::Int3: return EmitConstantInt3(FIntVector3{ InValue.Component[0].Int, InValue.Component[1].Int, InValue.Component[2].Int });
		case UE::Shader::EValueType::Int4: return EmitConstantInt4(FIntVector4{ InValue.Component[0].Int, InValue.Component[1].Int, InValue.Component[2].Int, InValue.Component[3].Int });
	}

	UE_MIR_UNREACHABLE();
} 

FValue* FEmitter::EmitConstantScalarZero(EScalarKind Kind)
{
	switch (Kind)
	{
		case SK_Bool: return EmitConstantFalse();
		case SK_Int: return EmitConstantInt1(0);
		case SK_Float: return EmitConstantFloat1(0.0f);
		default: UE_MIR_UNREACHABLE();
	}
}

FValue* FEmitter::EmitConstantTrue()
{
	return ConstantTrue;
}

FValue* FEmitter::EmitConstantFalse()
{
	return ConstantFalse;
}

FValue* FEmitter::EmitConstantBool1(bool InX)
{
	return InX ? EmitConstantTrue() : EmitConstantFalse();
}

FValue* FEmitter::EmitConstantFloat1(TFloat InX)
{
	FConstant Scalar = MakePrototype<FConstant>(FPrimitiveType::GetScalar(SK_Float));
	Scalar.Float = InX;
	return EmitPrototype(this, Scalar);
}

FValue* FEmitter::EmitConstantFloat2(const FVector2f& InValue)
{
	FValue* X = EmitConstantFloat1(InValue.X);
	FValue* Y = EmitConstantFloat1(InValue.Y);
	return EmitVector2(X, Y);
}

FValue* FEmitter::EmitConstantFloat3(const FVector3f& InValue)
{
	FValue* X = EmitConstantFloat1(InValue.X);
	FValue* Y = EmitConstantFloat1(InValue.Y);
	FValue* Z = EmitConstantFloat1(InValue.Z);
	return EmitVector3(X, Y, Z);
}

FValue* FEmitter::EmitConstantFloat4(const FVector4f& InValue)
{
	FValue* X = EmitConstantFloat1(InValue.X);
	FValue* Y = EmitConstantFloat1(InValue.Y);
	FValue* Z = EmitConstantFloat1(InValue.Z);
	FValue* W = EmitConstantFloat1(InValue.W);
	return EmitVector4(X, Y, Z, W);
}

FValue* FEmitter::EmitConstantInt1(TInteger InX)
{
	FConstant Scalar = MakePrototype<FConstant>(FPrimitiveType::GetScalar(SK_Int));
	Scalar.Integer = InX;
	return EmitPrototype(this, Scalar);
}

FValue* FEmitter::EmitConstantInt2(const FIntVector2& InValue)
{
	FValue* X = EmitConstantInt1(InValue.X);
	FValue* Y = EmitConstantInt1(InValue.Y);
	return EmitVector2(X, Y);
}

FValue* FEmitter::EmitConstantInt3(const FIntVector3& InValue)
{
	FValue* X = EmitConstantInt1(InValue.X);
	FValue* Y = EmitConstantInt1(InValue.Y);
	FValue* Z = EmitConstantInt1(InValue.Z);
	return EmitVector3(X, Y, Z);
}

FValue* FEmitter::EmitConstantInt4(const FIntVector4& InValue)
{
	FValue* X = EmitConstantInt1(InValue.X);
	FValue* Y = EmitConstantInt1(InValue.Y);
	FValue* Z = EmitConstantInt1(InValue.Z);
	FValue* W = EmitConstantInt1(InValue.W);
	return EmitVector4(X, Y, Z, W);
}

FValue* FEmitter::EmitVector2(FValue* InX, FValue* InY)
{
	check(InX->Type->AsScalar());
	check(InX->Type == InY->Type);

	TDimensional<2> Vector = MakePrototype<TDimensional<2>>(FPrimitiveType::GetVector(InX->Type->AsPrimitive()->ScalarKind, 2));
	TArrayView<FValue*> Components = Vector.GetComponents();
	Components[0] = InX;
	Components[1] = InY;

	return EmitPrototype(this, Vector);
}

FValue* FEmitter::EmitVector3(FValue* InX, FValue* InY, FValue* InZ)
{
	check(InX->Type->AsScalar());
	check(InX->Type == InY->Type);
	check(InY->Type == InZ->Type);

	TDimensional<3> Vector = MakePrototype<TDimensional<3>>(FPrimitiveType::GetVector(InX->Type->AsPrimitive()->ScalarKind, 3));
	TArrayView<FValue*> Components = Vector.GetComponents();
	Components[0] = InX;
	Components[1] = InY;
	Components[2] = InZ;

	return EmitPrototype(this, Vector);
}

FValue* FEmitter::EmitVector4(FValue* InX, FValue* InY, FValue* InZ, FValue* InW)
{
	check(InX->Type->AsScalar());
	check(InX->Type == InY->Type);
	check(InY->Type == InZ->Type);
	check(InZ->Type == InW->Type);

	TDimensional<4> Vector = MakePrototype<TDimensional<4>>(FPrimitiveType::GetVector(InX->Type->AsPrimitive()->ScalarKind, 4));
	TArrayView<FValue*> Components = Vector.GetComponents();
	Components[0] = InX;
	Components[1] = InY;
	Components[2] = InZ;
	Components[3] = InW;

	return EmitPrototype(this, Vector);
}

FValue* FEmitter::GetExternalInput(EExternalInput Id)
{
	MIR::FExternalInput Prototype = MakePrototype<MIR::FExternalInput>(GetExternalInputType(Id));
	Prototype.Id = Id;
	return EmitPrototype(this, Prototype);
}

FValue* FEmitter::EmitSubscript(FValue* Value, int Index)
{
	FPrimitiveTypePtr PrimitiveType = Value->Type->AsPrimitive();
	if (!PrimitiveType)
	{
		Errorf(TEXT("Value of type `%s` cannot be subscripted."), Value->Type->GetSpelling().GetData());
		return nullptr;
	}

	// Getting first component and Value is already a scalar, just return itself.
	if (Index == 0 && Value->Type->AsScalar())
	{
		return Value;
	}

	// Getting first component and Value is already a scalar, just return itself.
	if (FDimensional* DimensionalValue = Value->As<FDimensional>())
	{
		check(Index < DimensionalValue->GetComponents().Num());
		return DimensionalValue->GetComponents()[Index];
	}
	
	// Avoid subscripting a subscript (e.g. no value.xy.x)
	if (FSubscript* Subscript = Value->As<FSubscript>())
	{
		Value = Subscript->Arg;
	}

	// We can't resolve it at compile time: emit subscript value.
	FSubscript Prototype = MakePrototype<FSubscript>(PrimitiveType->ToScalar());
	Prototype.Arg = Value;
	Prototype.Index = Index;

	return EmitPrototype(this, Prototype);
}

FValue* FEmitter::EmitSwizzle(FValue* Value, FSwizzleMask Mask)
{
	// At least one component must have been specified.
	check(Mask.NumComponents > 0);

	// We can only swizzle on non-matrix primitive types.
	FPrimitiveTypePtr PrimitiveType = Value->Type->AsVector();
	if (!PrimitiveType || PrimitiveType->IsMatrix())
	{
		Errorf(TEXT("Cannot swizzle a `%s` value."), Value->Type->GetSpelling().GetData());
		return nullptr;
	}

	// Make sure each component in the mask fits the number of components in Value.
	for (EVectorComponent Component : Mask)
	{
		if ((int)Component >= PrimitiveType->NumRows)
		{
			Errorf(TEXT("Value of type `%s` has no component `%s`."), PrimitiveType->Spelling.GetData(), VectorComponentToString(Component));
			return nullptr;
		}
	}

	// If only one component is requested, we can use EmitSubscript() to return the single component.
	if (Mask.NumComponents == 1)
	{
		return EmitSubscript(Value, (int)Mask.Components[0]);
	}

	// If the requested number of components is the same as Value and the order in which the components
	// are specified in the mask is sequential (e.g. x, y, z) then this is a no op, simply return Value as is.
	if (Mask.NumComponents == PrimitiveType->GetNumComponents())
	{
		bool InOrder = true;
		for (int i = 0; i < Mask.NumComponents; ++i)
		{
			if (Mask.Components[i] != (EVectorComponent)i)
			{
				InOrder = false;
				break;
			}
		}

		if (InOrder)
		{
			return Value;
		}
	}
	
	// Make the result vector type.
	FPrimitiveTypePtr ResultType = FPrimitiveType::GetVector(PrimitiveType->ScalarKind, Mask.NumComponents);
	FDimensional* Result = NewDimensionalValue(this, ResultType);

	for (int i = 0; i < Mask.NumComponents; ++i)
	{
		Result->GetComponents()[i] = EmitSubscript(Value, (int)Mask.Components[i]);
	}

	return Result;
}

FValue* FEmitter::GetParameter(FName Name, const FMaterialParameterMetadata& Metadata)
{
	FMaterialParameterInfo Info{ MoveTemp(Name), EMaterialParameterAssociation::GlobalParameter, INDEX_NONE };
	
	MIR::FMaterialParameter Proto = MakePrototype<MIR::FMaterialParameter>(MIR::FTextureType::Get());
	Proto.Info = Info;
	Proto.Metadata = Metadata;

	return EmitPrototype(this, Proto);
}

FSetMaterialOutput* FEmitter::EmitSetMaterialOutput(EMaterialProperty InProperty, FValue* InArgValue)
{
	FSetMaterialOutput* Instr = new FSetMaterialOutput;
	Instr->Kind 	= VK_SetMaterialOutput;
	Instr->Block 	= Module->RootBlock;
	Instr->Property = InProperty;
	Instr->Arg 		= InArgValue;

	Module->Values.Add(Instr);
	Module->Outputs.Add(Instr);
	return Instr;
}

template <typename T>
static T FoldScalarArithmeticOperator(EBinaryOperator Operator, T Lhs, T Rhs)
{
	switch (Operator)
	{
		case BO_Add: return Lhs + Rhs;
		case BO_Subtract: return Lhs - Rhs;
		case BO_Multiply: return Lhs * Rhs;
		case BO_Divide: return Lhs / Rhs;
		default: UE_MIR_UNREACHABLE();
	}
}

template <typename T>
static bool FoldComparisonOperatorScalar(EBinaryOperator Operator, T Lhs, T Rhs)
{
	switch (Operator)
	{
		case BO_GreaterThan: return Lhs > Rhs;
		case BO_GreaterThanOrEquals: return Lhs >= Rhs;
		case BO_LowerThan: return Lhs < Rhs;
		case BO_LowerThanOrEquals: return Lhs <= Rhs;
		case BO_Equals: return Lhs == Rhs;
		case BO_NotEquals: return Lhs != Rhs;
		default: UE_MIR_UNREACHABLE();
	}
}

static FValue* TryFoldBinaryOperatorScalar(FEmitter* Emitter, EBinaryOperator Operator, FValue* Lhs, FValue* Rhs)
{
	FPrimitiveTypePtr PrimitiveType = Lhs->Type->AsPrimitive();

	// Check whether we can fold the operation based on identies (e.g. "x + 0 = x ∀ x ∈ R").
	// These simplifications don't requre that both lhs and rhs are constant.
	switch (Operator)
	{
		case BO_Add:
			if (Lhs->IsNearlyZero())
			{
				return Rhs;
			}
			else if (Rhs->IsNearlyZero())
			{
				return Lhs;
			}
			break;

		case BO_Subtract:
			if (Rhs->IsNearlyZero())
			{
				return Lhs;
			}
			else if (Lhs->IsNearlyZero())
			{
				// return Emitter->EmitUnaryOperator(UO_Minus, Rhs);
				UE_MIR_TODO();
			}
			break;

		case BO_Multiply:
			if (Lhs->IsNearlyZero() || Rhs->IsNearlyOne())
			{
				return Lhs;
			}
			else if (Lhs->IsNearlyOne() || Rhs->IsNearlyZero())
			{
				return Rhs;
			}
			break;

		case BO_Divide:
			if (Lhs->IsNearlyZero() || Rhs->IsNearlyOne())
			{
				return Lhs;
			}
			break;

		case BO_GreaterThan:
		case BO_LowerThan:
		case BO_NotEquals:
			if (Lhs->Equals(Rhs))
			{
				return Emitter->EmitConstantFalse();
			}
			break;

		case BO_GreaterThanOrEquals:
		case BO_LowerThanOrEquals:
		case BO_Equals:
			if (Lhs->Equals(Rhs))
			{
				return Emitter->EmitConstantTrue();
			}
			break;
	}

	// Verify that both lhs and rhs are constants, otherwise we cannot fold the operation.
	const FConstant* LhsConstant = Lhs->As<FConstant>();
	const FConstant* RhsConstant = Rhs->As<FConstant>();
	if (!LhsConstant || !RhsConstant)
	{
		return nullptr;
	}

	if (IsArithmeticOperator(Operator))
	{
		switch (PrimitiveType->ScalarKind)
		{
			case SK_Int:
			{
				int Result = FoldScalarArithmeticOperator<TInteger>(Operator, LhsConstant->Integer, RhsConstant->Integer);
				return Emitter->EmitConstantInt1(Result);
			}

			case SK_Float:
			{
				TFloat Result = FoldScalarArithmeticOperator<TFloat>(Operator, LhsConstant->Float, RhsConstant->Float);
				return Emitter->EmitConstantFloat1(Result);
			}

			default:
				UE_MIR_UNREACHABLE();
		}
	}
	else if (IsComparisonOperator(Operator))
	{
		bool Result;
		switch (PrimitiveType->ScalarKind)
		{
			case SK_Int:
				Result = FoldComparisonOperatorScalar<TInteger>(Operator, LhsConstant->Integer, RhsConstant->Integer);
				break;

			case SK_Float:
				Result = FoldComparisonOperatorScalar<TFloat>(Operator, LhsConstant->Float, RhsConstant->Float);
				break;

			default:
				UE_MIR_UNREACHABLE();
		}
		return Emitter->EmitConstantBool1(Result);
	}
	else
	{
		UE_MIR_UNREACHABLE();
	}
}

FValue* FEmitter::EmitBinaryOperator(EBinaryOperator Operator, FValue* Lhs, FValue* Rhs)
{
	// Argument value types must always match.
	if (Lhs->Type != Rhs->Type)
	{
		Errorf(TEXT("operands of binary operator `%s` do not have the same type."), BinaryOperatorToString(Operator));
		return nullptr;
	}

	// Get the operands primitive type.
	FPrimitiveTypePtr PrimitiveType = Lhs->Type->AsPrimitive();
	if (!PrimitiveType)
	{
		Errorf(TEXT("invalid binary operator on non primitive types."));
		return nullptr;
	}

	// Determine the result type. If the operator is arithmetic, the result type will be the same
	// as the operands type. Otherwise it will have the same number of components but bool.
	FPrimitiveTypePtr ResultType = IsArithmeticOperator(Operator)
		? PrimitiveType
		: FPrimitiveType::Get(SK_Bool, PrimitiveType->NumRows, PrimitiveType->NumColumns);

	// Check that at least one component of the resulting dimensional value would folded.
	// If all components of resulting dimensional value are not folded, then instead of emitting
	// an individual operator instruction for each component, simply emit a single binary operator
	// instruction applied between lhs and rhs as a whole. (v1 + v2 rather than float2(v1.x + v2.x, v1.y + v2.y)
	bool bSomeResultComponentWasFolded = false;
	bool bResultIsIdenticalToLhs = true;
	bool bResultIsIdenticalToRhs = true;

	// Allocate the temporary array to store the folded component results
	FMemMark Mark(FMemStack::Get());
	TArrayView<FValue*> TempResultComponents = MakeTemporaryArray<FValue*>(Mark, ResultType->GetNumComponents());
	
	for (int i = 0; i < ResultType->GetNumComponents(); ++i)
	{
		// Extract the arguments individual components
		FValue* LhsComponent = EmitSubscript(Lhs, i);
		FValue* RhsComponent = EmitSubscript(Rhs, i);

		// Try folding the operation, it may return null
		FValue* ResultComponent = TryFoldBinaryOperatorScalar(this, Operator, LhsComponent, RhsComponent);

		// Update the flags
		bSomeResultComponentWasFolded |= (bool)ResultComponent;
		bResultIsIdenticalToLhs &= ResultComponent && ResultComponent->Equals(LhsComponent);
		bResultIsIdenticalToRhs &= ResultComponent && ResultComponent->Equals(RhsComponent);

		// Cache the results
		TempResultComponents[i] = ResultComponent;
	}

	// If result is identical to either lhs or rhs, simply return it
	if (bResultIsIdenticalToLhs)
	{
		return Lhs;
	}
	else if (bResultIsIdenticalToRhs)
	{
		return Rhs;
	}

	// If some component was folded (it is either constant or the operation was a NOP), it is worth
	// build the binary operation as a separate operation for each component, that is like
	//    float2(lhs.x + rhs.x, lhs.y + rhs.y)
	// rather than
	//    lhs + rhs
	// so that we retain as much compile-time information as possible.
	if (bSomeResultComponentWasFolded)
	{
		// If result type is scalar, simply return the single folded result (instead of creating a dimensional value)
		if (ResultType->IsScalar())
		{
			check(TempResultComponents[0]);
			return TempResultComponents[0];
		}

		// Make the new dimensional value
		FDimensional* Result = NewDimensionalValue(this, ResultType);
		TArrayView<FValue*> ResultComponents = Result->GetComponents();

		// Create the operator instruction for each component pair
		for (int i = 0; i < ResultType->GetNumComponents(); ++i)
		{
			// Reuse cached result if possible
			ResultComponents[i] = TempResultComponents[i];

			// Otherwise emit the binary operation between the two components (this will create a new instruction)
			if (!ResultComponents[i])
			{
				FValue* LhsComponent = EmitSubscript(Lhs, i);
				FValue* RhsComponent = EmitSubscript(Rhs, i);
				ResultComponents[i] = EmitBinaryOperator(Operator, LhsComponent, RhsComponent);
			}
		}

		return Result;
	}

	// Finally, simply emit the binary operator instruction between lhs and rhs (i.e. "lhs + rhs")
	FBinaryOperator Proto = MakePrototype<FBinaryOperator>(ResultType);
	Proto.Operator = Operator;
	Proto.LhsArg= Lhs;
	Proto.RhsArg = Rhs;

	return EmitPrototype(this, Proto);
}

FValue* FEmitter::EmitBranch(FValue* Condition, FValue* True, FValue* False)
{
	// Condition must be of type bool
	check(Condition->Type->IsBoolScalar());

	// If the condition is a scalar constant, then simply evaluate the result now.
	if (const FConstant* ConstCondition = Condition->As<FConstant>())
	{
		return ConstCondition->Boolean ? True : False;
	}

	// If the condition is not static, the types of the true and false operands must match.
	// The resulting type will be that of the branch instruction created.
	check(True->Type == False->Type);

	// Create the branch instruction.
	FBranch Proto = MakePrototype<FBranch>(True->Type);
	Proto.ConditionArg = Condition;
	Proto.TrueArg = True;
	Proto.FalseArg = False;

	return EmitPrototype(this, Proto);
}

static FValue* CastConstant(FEmitter* Emitter, FConstant* Constant, EScalarKind ConstantScalarKind, EScalarKind TargetKind)
{
	if (ConstantScalarKind == TargetKind)
	{
		return Constant;
	}

	switch (ConstantScalarKind)
	{
		case SK_Bool:
		case SK_Int:
		{
			switch (TargetKind)
			{
				case SK_Bool: UE_MIR_TODO();
				case SK_Int: return Emitter->EmitConstantInt1(Constant->Integer);
				case SK_Float: return Emitter->EmitConstantFloat1((TFloat)Constant->Integer);
				default: UE_MIR_UNREACHABLE();
			}
		}

		case SK_Float:
		{
			switch (TargetKind)
			{
				case SK_Bool: UE_MIR_TODO();
				case SK_Int: return Emitter->EmitConstantInt1((int)Constant->Float);
				default: UE_MIR_UNREACHABLE();
			}
		}

		default: break;
	}

	UE_MIR_UNREACHABLE();
}

static FValue* ConstructPrimitiveValue(FEmitter* Emitter, FPrimitiveTypePtr TargetPrimitiveType, FValue* Initializer)
{
	FPrimitiveTypePtr InitializerPrimitiveType = Initializer->Type->AsPrimitive();
	if (!InitializerPrimitiveType)
	{
		Emitter->Errorf(TEXT("Cannot construct a '%s' from non primitive type '%s'."), TargetPrimitiveType->Spelling.GetData(), Initializer->Type->GetSpelling().GetData());
		return nullptr;
	}

	// Construct a scalar from another scalar.
	if (TargetPrimitiveType->IsScalar())
	{
		//
		Initializer = Emitter->EmitSubscript(Initializer, 0);
		InitializerPrimitiveType = Initializer->Type->AsPrimitive();
		
		//
		if (InitializerPrimitiveType == TargetPrimitiveType)
		{
			return Initializer;
		}

		// Construct the scalar from a constant.
		if (FConstant* ConstantInitializer = Initializer->As<FConstant>())
		{
			return CastConstant(Emitter, ConstantInitializer, InitializerPrimitiveType->ScalarKind, TargetPrimitiveType->ScalarKind);
		}
		else
		{
			/* todo: emit conversion unary operator */
			UE_MIR_TODO();
		}
	}

	// Construct a vector or matrix from a scalar. E.g. float4(3.14f)
	if (!TargetPrimitiveType->IsScalar() && InitializerPrimitiveType->IsScalar())
	{
		// Create the result dimensional value.
		FDimensional* Result = NewDimensionalValue(Emitter, TargetPrimitiveType);

		// Create a dimensional and initialize each of its components to the conversion
		// of initializer value to the single component type.
		FValue* Component = Emitter->EmitConstruct(TargetPrimitiveType->ToScalar(), Initializer);

		// Initialize all result components to the same scalar.
		for (int i = 0; i < TargetPrimitiveType->GetNumComponents(); ++i)
		{
			Result->GetComponents()[i] = Component;
		}
		
		return EmitNew(Emitter, Result);
	}

	// Construct a vector from another vector. If constructed vector is larger, initialize
	// remaining components to zero. If it's smaller, truncate initializer vector and only use
	// the necessary components.
	if (TargetPrimitiveType->IsVector() && InitializerPrimitiveType->IsVector())
	{
		int TargetNumComponents = TargetPrimitiveType->GetNumComponents();
		int InitializerNumComponents = InitializerPrimitiveType->GetNumComponents();

		// Create the result dimensional value.
		FDimensional* Result = NewDimensionalValue(Emitter, TargetPrimitiveType);

		// Determine the result component type (scalar).
		FPrimitiveTypePtr ResultComponentType = TargetPrimitiveType->ToScalar();

		// For iterating over the components of the result dimensional value.
		int Index = 0;

		// Convert components from the initializer vector.
		int MinNumComponents = FMath::Min(TargetNumComponents, InitializerNumComponents);
		for (; Index < MinNumComponents; ++Index)
		{
			Result->GetComponents()[Index] = Emitter->EmitConstruct(ResultComponentType, Emitter->EmitSubscript(Initializer, Index));
		}

		// Initialize remaining result dimensional components to zero.
		for (; Index < TargetNumComponents; ++Index)
		{
			Result->GetComponents()[Index] = Emitter->EmitConstantScalarZero(ResultComponentType->ScalarKind);
		}

		return EmitNew(Emitter, Result);
	}
	
	// The two primitive types are identical matrices that differ only by their scalar type.
	if (TargetPrimitiveType->NumRows 	 == InitializerPrimitiveType->NumRows &&
		TargetPrimitiveType->NumColumns == InitializerPrimitiveType->NumColumns)
	{
		check(TargetPrimitiveType->IsMatrix());

		// 
		if (FDimensional* DimensionalInitializer = Initializer->As<FDimensional>())
		{
			// Create the result dimensional value.
			FDimensional* Result = NewDimensionalValue(Emitter, TargetPrimitiveType);
			
			// Determine the result component type (scalar).
			FPrimitiveTypePtr ResultComponentType = TargetPrimitiveType->ToScalar();

			// Convert components from the initializer vector.
			for (int Index = 0, Num = TargetPrimitiveType->GetNumComponents(); Index < Num; ++Index)
			{
				Result->GetComponents()[Index] = Emitter->EmitConstruct(ResultComponentType, DimensionalInitializer->GetComponents()[Index]);
			}

			return EmitNew(Emitter, Result);
		}
		else
		{
			// Initializer is an unknown value, construct target value casting initializer.
			FCast Prototype = MakePrototype<FCast>(TargetPrimitiveType);
			Prototype.Arg = Initializer;
			return EmitPrototype(Emitter, Prototype);
		}
	}

	// Initializer value cannot be used to construct this primitive type.
	return nullptr;
}

FValue* FEmitter::EmitConstruct(FTypePtr Type, FValue* Initializer)
{
	// If target type matches initializer's, simply return the same value.
	FTypePtr InitializerType = Initializer->Type;
	if (InitializerType == Type)
	{
		return Initializer;
	}
	
	FValue* Result{};
	if (FPrimitiveTypePtr PrimitiveType = Type->AsPrimitive())
	{
		Result = ConstructPrimitiveValue(this, PrimitiveType, Initializer);
	}

	// No other legal conversions applicable. Report error if we haven't converted the value.
	if (!Result)
	{
		Errorf(TEXT("Cannot construct a '%s' from a '%s'."), TEXT(""), TEXT(""));
	}
	
	return Result;
}

FValue* FEmitter::EmitTextureSample(UTexture* Texture, FValue* TexCoord, ESamplerSourceMode SamplerSourceMode, ETextureMipValueMode MipValueMode, EMaterialSamplerType SamplerType)
{
	MIR::FTextureSample Prototype = MakePrototype<MIR::FTextureSample>(FPrimitiveType::GetFloat4());
	Prototype.Texture = Texture;
	Prototype.TexCoordArg = TexCoord;
	Prototype.MipValueArg = nullptr;
	Prototype.AutomaticMipBiasArg = nullptr;
	Prototype.SamplerSourceMode = SamplerSourceMode;
	Prototype.MipValueMode = MipValueMode;
	Prototype.SamplerType = SamplerType;

	return EmitPrototype(this, Prototype);
}

FPrimitiveTypePtr FEmitter::GetCommonPrimitiveType(FPrimitiveTypePtr A, FPrimitiveTypePtr B)
{
	// Trivial case: types are equal
	if (A == B)
	{
		return A;
	}
	
	// If both A and B are matrices, their dimensions must match (equality check above didn't trigger).
	if (A->IsMatrix() || B->IsMatrix())
	{
		Errorf(TEXT("No common primitive type between `%s` and `%s`."), A->Spelling.GetData(), B->Spelling.GetData());
		return nullptr;
	}

	// Neither A nor B are matrices, but single scalar or vector. Return the largest.
	check(A->NumColumns == 1 && B->NumColumns == 1);
	EScalarKind ScalarKind = FMath::Max(A->ScalarKind, B->ScalarKind);
	int NumRows = FMath::Max(A->NumRows, B->NumRows);
	return FPrimitiveType::Get(ScalarKind, NumRows, 1);
}

void FEmitter::Error(FString Message)
{
	FMaterialIRModule::FError Error;
	Error.Expression = Expression;
	Error.Message = MoveTemp(Message);
	Module->Errors.Push(Error);
	bHasExprBuildError = true;
}

void FEmitter::Initialize()
{
	// Create and reference the true/false constants.
	FConstant Temp = MakePrototype<FConstant>(FPrimitiveType::GetBool1());

	Temp.Boolean = true;
	ConstantTrue = EmitPrototype(this, Temp);

	Temp.Boolean = false;
	ConstantFalse = EmitPrototype(this, Temp);
}

} // namespace UE::MIR

#endif // #if WITH_EDITOR
