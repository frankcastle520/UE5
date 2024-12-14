// Copyright Epic Games, Inc. All Rights Reserved.

#include "Materials/MaterialIR.h"
#include "Materials/MaterialIRTypes.h"
#include "Engine/Texture.h"

#if WITH_EDITOR

namespace UE::MIR
{

const TCHAR* ValueKindToString(EValueKind Kind)
{
	switch (Kind)
	{
		case VK_Constant: return TEXT("Constant");
		case VK_ExternalInput: return TEXT("ExternalInput");
		case VK_MaterialParameter: return TEXT("MaterialParameter");
		case VK_Dimensional: return TEXT("Dimensional");
		case VK_SetMaterialOutput: return TEXT("SetMaterialOutput");
		case VK_BinaryOperator: return TEXT("BinaryOperator");
		case VK_Branch: return TEXT("Branch");
		case VK_Subscript: return TEXT("Subscript");
		case VK_Cast: return TEXT("Cast");
		case VK_TextureSample: return TEXT("TextureSample");
		default: UE_MIR_UNREACHABLE();
	};
}

uint32 FValue::GetSizeInBytes() const
{
	switch (Kind)
	{
		case VK_Constant: return sizeof(FConstant);
		case VK_ExternalInput: return sizeof(FExternalInput);
		case VK_MaterialParameter: return sizeof(FMaterialParameter);
		case VK_SetMaterialOutput: return sizeof(FSetMaterialOutput);
		case VK_BinaryOperator: return sizeof(FBinaryOperator);
		case VK_Dimensional: return sizeof(FDimensional) + sizeof(FValue*) * static_cast<const FDimensional*>(this)->GetComponents().Num();
		case VK_Branch: return sizeof(FBranch);
		case VK_Subscript: return sizeof(FSubscript);
		case VK_Cast: return sizeof(FCast);
		case VK_TextureSample: return sizeof(FTextureSample);
		default: UE_MIR_UNREACHABLE();
	}
}

FInstruction* FValue::AsInstruction()
{
	return this && (Kind > VK_InstructionBegin && Kind < VK_InstructionEnd) ? static_cast<FInstruction*>(this) : nullptr;
}

const FInstruction* FValue::AsInstruction() const
{
	return const_cast<FValue*>(this)->AsInstruction();
}

bool FValue::Equals(const FValue* Other) const
{
	// Trivial case, pointers match.
	if (this == Other)
	{
		return true;
	}

	// Todo: we are guaranteeing that the IR values are unique. Are we sure we need this?

	// If kinds are different the two values are surely different.
	if (Kind != Other->Kind)
	{
		return false;
	}

	// Get the size of this value in bytes. It should match that of Other, since the value kinds are the same.
	uint32 SizeInBytes = GetSizeInBytes();
	if (SizeInBytes != Other->GetSizeInBytes())
	{
		return false;
	}

	// Values are PODs by design, therefore simply comparing bytes is sufficient.
	return FMemory::Memcmp(this, Other, SizeInBytes) == 0;
}

TArrayView<const FValue*> FValue::GetUses() const
{
	TArrayView<FValue*> Uses = const_cast<FValue*>(this)->GetUses();
	return { Uses.Num() ? (const FValue**)&Uses[0] : nullptr , Uses.Num() };
}

TArrayView<FValue*> FValue::GetUses()
{
	// Values have no uses by definition.
	if (Kind < VK_InstructionBegin)
	{
		return {};
	}

	switch (Kind)
	{
		case VK_Dimensional:
		{
			auto This = static_cast<FDimensional*>(this);
			return This->GetComponents();
		}

		case VK_SetMaterialOutput:
		{
			auto This = static_cast<FSetMaterialOutput*>(this);
			return { &This->Arg, 1 };
		}

		case VK_BinaryOperator:
		{
			auto This = static_cast<FBinaryOperator*>(this);
			return { &This->LhsArg, 2 };
		}

		case VK_Branch:
		{
			auto This = static_cast<FBranch*>(this);
			return { &This->ConditionArg, 3 };
		}

		case VK_Subscript:
		{
			auto This = static_cast<FSubscript*>(this);
			return { &This->Arg, 1 };
		}

		case VK_TextureSample:
		{
			auto This = static_cast<FTextureSample*>(this);
			return { &This->TexCoordArg, 3 };
		}

		default: UE_MIR_UNREACHABLE();
	}
}

bool FValue::IsScalar() const
{
	return Type->AsScalar() != nullptr;
}

bool FValue::IsVector() const
{
	return Type->AsVector() != nullptr;
}

bool FValue::IsTrue() const
{
	const MIR::FConstant* Constant = As<MIR::FConstant>();
	return Constant && Constant->IsBool() && Constant->Boolean == true;
}

bool FValue::IsFalse() const
{
	const MIR::FConstant* Constant = As<MIR::FConstant>();
	return Constant && Constant->IsBool() && Constant->Boolean == false;
}

bool FValue::IsExactlyZero() const
{
	if (const MIR::FConstant* Constant = As<MIR::FConstant>())
	{
		return (Constant->IsInteger() && Constant->Integer == 0)
			|| (Constant->IsFloat() && Constant->Float == 0.0f);
	}
	return false;
}

bool FValue::IsNearlyZero() const
{
	if (const MIR::FConstant* Constant = As<MIR::FConstant>())
	{
		return (Constant->IsInteger() && Constant->Integer == 0)
			|| (Constant->IsFloat() && FMath::IsNearlyZero(Constant->Float));
	}
	return false;
}

bool FValue::IsExactlyOne() const
{
	if (const MIR::FConstant* Constant = As<MIR::FConstant>())
	{
		return (Constant->IsInteger() && Constant->Integer == 1)
			|| (Constant->IsFloat() && Constant->Float == 1.0f);
	}
	return false;
}

bool FValue::IsNearlyOne() const
{
	if (const MIR::FConstant* Constant = As<MIR::FConstant>())
	{
		return (Constant->IsInteger() && Constant->Integer == 1)
			|| (Constant->IsFloat() && FMath::IsNearlyEqual(Constant->Float, 1.0f));
	}
	return false;
}

UTexture* FValue::GetTexture()
{
	if (auto Parameter = As<FMaterialParameter>())
	{
		UObject* TextureObject = Parameter->Metadata.Value.AsTextureObject();
		if (TextureObject)
		{
			return static_cast<UTexture*>(TextureObject);
		}
	}

	return nullptr;
}

bool FConstant::IsBool() const
{
	return Type->IsBoolScalar();
}

bool FConstant::IsInteger() const
{
	return Type == FPrimitiveType::GetInt1();
}

bool FConstant::IsFloat() const
{
	return Type == FPrimitiveType::GetFloat1();
}

const TCHAR* ExternalInputToString(EExternalInput Input)
{
	switch (Input)
	{
		case EExternalInput::TexCoord0: return TEXT("TexCoord0");
		case EExternalInput::TexCoord1: return TEXT("TexCoord1");
		case EExternalInput::TexCoord2: return TEXT("TexCoord2");
		case EExternalInput::TexCoord3: return TEXT("TexCoord3");
		case EExternalInput::TexCoord4: return TEXT("TexCoord4");
		case EExternalInput::TexCoord5: return TEXT("TexCoord5");
		case EExternalInput::TexCoord6: return TEXT("TexCoord6");
		case EExternalInput::TexCoord7: return TEXT("TexCoord7");
		case EExternalInput::TexCoord0_Ddx: return TEXT("TexCoord0_Ddx");
		case EExternalInput::TexCoord1_Ddx: return TEXT("TexCoord1_Ddx");
		case EExternalInput::TexCoord2_Ddx: return TEXT("TexCoord2_Ddx");
		case EExternalInput::TexCoord3_Ddx: return TEXT("TexCoord3_Ddx");
		case EExternalInput::TexCoord4_Ddx: return TEXT("TexCoord4_Ddx");
		case EExternalInput::TexCoord5_Ddx: return TEXT("TexCoord5_Ddx");
		case EExternalInput::TexCoord6_Ddx: return TEXT("TexCoord6_Ddx");
		case EExternalInput::TexCoord7_Ddx: return TEXT("TexCoord7_Ddx");
		case EExternalInput::TexCoord0_Ddy: return TEXT("TexCoord0_Ddy");
		case EExternalInput::TexCoord1_Ddy: return TEXT("TexCoord1_Ddy");
		case EExternalInput::TexCoord2_Ddy: return TEXT("TexCoord2_Ddy");
		case EExternalInput::TexCoord3_Ddy: return TEXT("TexCoord3_Ddy");
		case EExternalInput::TexCoord4_Ddy: return TEXT("TexCoord4_Ddy");
		case EExternalInput::TexCoord5_Ddy: return TEXT("TexCoord5_Ddy");
		case EExternalInput::TexCoord6_Ddy: return TEXT("TexCoord6_Ddy");
		case EExternalInput::TexCoord7_Ddy: return TEXT("TexCoord7_Ddy");
		default: UE_MIR_UNREACHABLE();
	}
}

MIR::EExternalInput TexCoordIndexToExternalInput(int TexCoordIndex)
{
	check(TexCoordIndex < TexCoordMaxNum);
	return (MIR::EExternalInput)((int)MIR::EExternalInput::TexCoord0 + TexCoordIndex);
}

FTypePtr GetExternalInputType(EExternalInput Id)
{
	int IdIndex = (int)Id;
	if (IdIndex >= (int)EExternalInput::TexCoord0 && IdIndex <= (int)EExternalInput::TexCoord7_Ddy)
	{
		return FPrimitiveType::GetFloat2();
	}

	UE_MIR_UNREACHABLE();
}

bool IsExternalInputTexCoord(EExternalInput Id)
{
	return Id >= EExternalInput::TexCoord0 && Id <= EExternalInput::TexCoord7;
}

bool IsExternalInputTexCoordDdx(EExternalInput Id)
{
	return Id >= EExternalInput::TexCoord0_Ddx && Id <= EExternalInput::TexCoord7_Ddx;
}

bool IsExternalInputTexCoordDdy(EExternalInput Id)
{
	return Id >= EExternalInput::TexCoord0_Ddy && Id <= EExternalInput::TexCoord7_Ddy;
}

FBlock* FBlock::FindCommonParentWith(MIR::FBlock* Other)
{
	FBlock* A = this;
	FBlock* B = Other;

	if (A == B)
	{
		return A;
	}

	while (A->Level > B->Level)
	{
		A = A->Parent;
	}

	while (B->Level > A->Level)
	{
		B = B->Parent;
	}

	while (A != B)
	{
		A = A->Parent;
		B = B->Parent;
	}

	return A;
}

TArrayView<FValue* const> FDimensional::GetComponents() const
{
	TArrayView<FValue*> Components = const_cast<FDimensional*>(this)->GetComponents();
	return { Components.GetData(), Components.Num() };
}

TArrayView<FValue*> FDimensional::GetComponents()
{
	FPrimitiveTypePtr PrimitiveType = Type->AsPrimitive();
	check(PrimitiveType);

	FValue** Ptr = (FValue**)static_cast<TDimensional<1>*>(this)->Components;
	return { Ptr, PrimitiveType->NumRows };
}

bool FDimensional::AreComponentsConstant() const
{
	for (FValue const* Component : GetComponents())
	{
		if (!Component->As<FConstant>())
		{
			return false;
		}
	}
	return true;
}

FBlock* FInstruction::GetDesiredBlockForUse(int32 UseIndex)
{
	if (auto Branch = As<FBranch>())
	{
		switch (UseIndex)
		{
			case 0: return Block; // ConditionArg goes into the same block as this instruction's
			case 1: return &Branch->TrueBlock; // TrueArg
			case 2: return &Branch->FalseBlock; // FalseArg
			default: UE_MIR_UNREACHABLE();
		}
	}

	// By default, dependencies can go in the same block as this instruction
	return Block;
}

bool IsArithmeticOperator(EBinaryOperator Op)
{
	return Op >= BO_Add && Op <= BO_Divide;
}

bool IsComparisonOperator(EBinaryOperator Op)
{
	return Op >= BO_GreaterThan && Op <= BO_NotEquals;
}

const TCHAR* BinaryOperatorToString(EBinaryOperator Op)
{
	switch (Op)
	{
		case BO_Add: return TEXT("Add");
		case BO_Subtract: return TEXT("Subtract");
		case BO_Multiply: return TEXT("Multiply");
		case BO_Divide: return TEXT("Divide");
		case BO_GreaterThan: return TEXT("GreaterThan");
		case BO_GreaterThanOrEquals: return TEXT("GreaterThanOrEquals");
		case BO_LowerThan: return TEXT("LowerThan");
		case BO_LowerThanOrEquals: return TEXT("LowerThanOrEquals");
		case BO_Equals: return TEXT("Equals");
		case BO_NotEquals: return TEXT("NotEquals");
		default: UE_MIR_UNREACHABLE();
	};
}

} // namespace UE::MIR

#endif // #if WITH_EDITOR
