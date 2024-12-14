// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MuR/Operations.h"
#include "MuR/ParametersPrivate.h"
#include "MuR/Ptr.h"
#include "MuT/AST.h"
#include "Curves/RichCurve.h"
#include "HAL/Platform.h"


namespace mu
{
struct FProgram;


	//---------------------------------------------------------------------------------------------
	//!
	//---------------------------------------------------------------------------------------------
	class ASTOpScalarCurve final : public ASTOp
	{
	public:

		ASTChild time;

		//!
		FRichCurve Curve;

	public:

		ASTOpScalarCurve();
		ASTOpScalarCurve(const ASTOpScalarCurve&) = delete;
		~ASTOpScalarCurve() override;

		OP_TYPE GetOpType() const override { return OP_TYPE::SC_CURVE; }
		uint64 Hash() const override;
		void ForEachChild(const TFunctionRef<void(ASTChild&)>) override;
		bool IsEqual(const ASTOp& otherUntyped) const override;
		Ptr<ASTOp> Clone(MapChildFuncRef mapChild) const override;
		void Link(FProgram& program, FLinkerOptions* Options) override;
	};


}

