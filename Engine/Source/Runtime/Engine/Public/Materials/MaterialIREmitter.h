// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Materials/MaterialIRCommon.h"
#include "Materials/MaterialIR.h"
#include "Materials/MaterialIRTypes.h"

#if WITH_EDITOR

namespace UE::MIR {

enum class EVectorComponent : uint8_t
{
	X, Y, Z, W, 
};

// Return the lower case string representation of specified component (e.g. "x")
const TCHAR* VectorComponentToString(EVectorComponent);

//
struct FSwizzleMask
{
	EVectorComponent Components[4];
	int NumComponents{};

	static FSwizzleMask XYZ();

	FSwizzleMask() {}
	FSwizzleMask(EVectorComponent X);
	FSwizzleMask(EVectorComponent X, EVectorComponent Y);
	FSwizzleMask(EVectorComponent X, EVectorComponent Y, EVectorComponent Z);
	FSwizzleMask(EVectorComponent X, EVectorComponent Y, EVectorComponent Z, EVectorComponent W);

	void Append(EVectorComponent Component);
	const EVectorComponent* begin() const { return Components; }
	const EVectorComponent* end() const { return Components + NumComponents; }
};

//
class FEmitter
{
public:
	FEmitter(FMaterialIRModuleBuilderImpl* InBuilder, UMaterial* InMaterial, FMaterialIRModule* InModule);
	
	/* IO */

	// Tries to get a value from specified input. If it has no value (for instance
	// because it is not connected), it returns null.
	FValue* TryGet(const FExpressionInput* Input);

	// Gets and returns the value flowing into input with specified `InputIndex`.
	// If no value is flowing in (e.g. bacause it is not connected) it reports
	// an error and returns null.
	FValue* Get(const FExpressionInput* Input);
	
	// Flows specified `Value` out of output with specified `OutputIndex`.
	FEmitter& Put(int OutputIndex, FValue* Value);

	// Flows specified `Value` out of specified expression `Output`.
	FEmitter& Put(const FExpressionOutput* Output, FValue* Value);
	
	/* IO Helpers */

	//
	FEmitter& DefaultToFloatZero(const FExpressionInput* Input);

	//
	FEmitter& DefaultTo(const FExpressionInput* Input, TFloat Float);

	// It gets the value flowing into it and checks that its type is float scalar.
	FValue* GetFloat(const FExpressionInput* Input);

	//
	FValue* GetScalar(const FExpressionInput* Input);
	
	//
	FValue* GetPrimitive(const FExpressionInput* Input);

	// Gets the value flowing into `Input` and returns it after checking that its
	// type matches `Kind`.
	FValue* GetOfType(const FExpressionInput* Input, ETypeKind Kind);

	/* Error Checking */

	//
	bool CheckValueValid(const FValue* Value);

	//
	void CheckInputIsScalar(const FExpressionInput* Input, FValue* InputValue);

	//
	void CheckInputIsScalar(const FExpressionInput* Input, FValue* InputValue, EScalarKind Kind);

	// Checks that the type of the value `InputValue` flowing into `Input` is of
	// specified type `Kind. If it isn't it reports an error. You may check whether
	// an error occurred with `IsInvalid()`.
	void CheckInputTypeIs(const FExpressionInput* Input, FValue* InputValue, ETypeKind Kind);

	/* Constants */

	FValue* EmitConstantFromShaderValue(const UE::Shader::FValue& InValue);
	FValue* EmitConstantScalarZero(EScalarKind Kind);
	FValue* EmitConstantTrue();
	FValue* EmitConstantFalse();
	FValue* EmitConstantBool1(bool InX);
	FValue* EmitConstantFloat1(TFloat InX);
	FValue* EmitConstantFloat2(const FVector2f& InValue);
	FValue* EmitConstantFloat3(const FVector3f& InValue);
	FValue* EmitConstantFloat4(const FVector4f& InValue);
	FValue* EmitConstantInt1(TInteger InX);
	FValue* EmitConstantInt2(const FIntVector2& InValue);
	FValue* EmitConstantInt3(const FIntVector3& InValue);
	FValue* EmitConstantInt4(const FIntVector4& InValue);
	FValue* EmitVector2(FValue* InX, FValue* InY);
	FValue* EmitVector3(FValue* InX, FValue* InY, FValue* InZ);
	FValue* EmitVector4(FValue* InX, FValue* InY, FValue* InZ, FValue* InW);
	FValue* GetExternalInput(EExternalInput Id);

	/* Other Values */

	FValue* EmitSubscript(FValue* Value, int ComponentIndex);
	FValue* EmitSwizzle(FValue* Value, FSwizzleMask Mask);
	FValue* GetParameter(FName Name, const FMaterialParameterMetadata& Metadata);

	/* Instructions */

	FSetMaterialOutput* EmitSetMaterialOutput(EMaterialProperty InProperty, FValue* InArgValue);
	FValue* EmitBinaryOperator(EBinaryOperator Operator, FValue* Lhs, FValue* Rhs);
	FValue* EmitBranch(FValue* Condition, FValue* True, FValue* False);
	FValue* EmitConstruct(FTypePtr Type, FValue* Initializer);
	FValue* EmitTextureSample(UTexture* Texture, FValue* TexCoord, ESamplerSourceMode SamplerSourceMode, ETextureMipValueMode MipValueMode, EMaterialSamplerType SamplerType);

	/* Types */

	FPrimitiveTypePtr GetCommonPrimitiveType(FPrimitiveTypePtr A, FPrimitiveTypePtr B);

	/* Error reporting */

	bool IsInvalid() const { return bHasExprBuildError; }

	template <int TFormatLength, typename... TArgs>
	void Errorf(const TCHAR (&Format)[TFormatLength], TArgs&&... Args)
	{
		Error(FString::Printf(Format, Forward<TArgs>(Args)...));
	}

	void Error(FString Message);

	struct FPrivate;
	friend FMaterialIRModuleBuilderImpl;

private:
	void Initialize();

	UMaterial* Material{};
	FMaterialIRModule* Module{};
	UMaterialExpression* Expression{};
	FMaterialIRModuleBuilderImpl* Builder{};
	bool bHasExprBuildError = false;
	FValue* ConstantTrue;
	FValue* ConstantFalse;
};

} // namespace UE::MIR

#endif // #if WITH_EDITOR
