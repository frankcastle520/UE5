// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "HAL/Platform.h"
#include "MuR/Operations.h"
#include "MuR/Ptr.h"
#include "MuT/AST.h"


namespace mu
{
struct FProgram;


//---------------------------------------------------------------------------------------------
//!
//---------------------------------------------------------------------------------------------
class ASTOpConstantMatrix final : public ASTOp
{
public:

	//!
	FMatrix44f value;

public:

	ASTOpConstantMatrix(FMatrix44f value = FMatrix44f::Identity);

	OP_TYPE GetOpType() const override { return OP_TYPE::MA_CONSTANT; }
	uint64 Hash() const override;
	void ForEachChild(const TFunctionRef<void(ASTChild&)>) override {}
	bool IsEqual(const ASTOp& otherUntyped) const override;
	Ptr<ASTOp> Clone(MapChildFuncRef mapChild) const override;
	void Link(FProgram& program, FLinkerOptions* Options) override;
};

}
