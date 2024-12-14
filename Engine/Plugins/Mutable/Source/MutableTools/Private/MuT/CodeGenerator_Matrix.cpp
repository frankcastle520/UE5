// Copyright Epic Games, Inc. All Rights Reserved.

#include "MuT/CodeGenerator.h"

#include "MuT/ASTOpConstantMatrix.h"
#include "MuT/NodeMatrixConstant.h"
#include "MuT/NodeMatrixParameter.h"

namespace mu
{

	void CodeGenerator::GenerateMatrix(FMatrixGenerationResult& Result, const FGenericGenerationOptions& Options, const Ptr<const NodeMatrix>& Untyped)
	{
		if (!Untyped)
		{
			Result = FMatrixGenerationResult();
			return;
		}

		// See if it was already generated
		FGeneratedCacheKey Key;
		Key.Node = Untyped;
		Key.Options = Options;
		FGeneratedMatrixMap::ValueType* it = GeneratedMatrices.Find(Key);
		if (it)
		{
			Result = *it;
			return;
		}

		if (Untyped->GetType() == NodeMatrixConstant::GetStaticType())
		{
			const NodeMatrixConstant* Constant = static_cast<const NodeMatrixConstant*>(Untyped.get());
			GenerateMatrix_Constant(Result, Options, Constant);
		}
		else if (Untyped->GetType() == NodeMatrixParameter::GetStaticType())
		{
			const NodeMatrixParameter* Parameter = static_cast<const NodeMatrixParameter*>(Untyped.get());
			GenerateMatrix_Parameter(Result, Options, Parameter);
		}  
	}

	//-------------------------------------------------------------------------------------------------
	void CodeGenerator::GenerateMatrix_Constant(FMatrixGenerationResult& result, const FGenericGenerationOptions& Options, const Ptr<const NodeMatrixConstant>& Typed)
	{
		Ptr<ASTOpConstantMatrix> op = new ASTOpConstantMatrix();
		op->value = Typed->Value;
		result.op = op;
	}

	void CodeGenerator::GenerateMatrix_Parameter(FMatrixGenerationResult& result, const FGenericGenerationOptions& Options, const Ptr<const NodeMatrixParameter>& Typed)
	{
		Ptr<ASTOpParameter> op;
		Ptr<ASTOpParameter>* it = FirstPass.ParameterNodes.Find(Typed.get());
		if (!it)
		{
			FParameterDesc param;
			param.m_name = Typed->Name;
			const TCHAR* CStr = ToCStr(Typed->Uid);
			param.m_uid.ImportTextItem(CStr, 0, nullptr, nullptr);
			param.m_type = PARAMETER_TYPE::T_MATRIX;
			param.m_defaultValue.Set<ParamMatrixType>(Typed->DefaultValue);

			op = new ASTOpParameter();
			op->type = OP_TYPE::MA_PARAMETER;
			op->parameter = param;

			// Generate the code for the ranges
			for (int32 a = 0; a < Typed->Ranges.Num(); ++a)
			{
				FRangeGenerationResult rangeResult;
				GenerateRange(rangeResult, Options, Typed->Ranges[a]);
				op->ranges.Emplace(op.get(), rangeResult.sizeOp, rangeResult.rangeName, rangeResult.rangeUID);
			}

			FirstPass.ParameterNodes.Add(Typed.get(), op);
		}
		else
		{
			op = *it;
		}

		result.op = op;
	}

}
